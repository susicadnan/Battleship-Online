/**
 * signal.cpp — see signal.h. A minimal MQTT 3.1.1 client (CONNECT,
 * SUBSCRIBE, PUBLISH at QoS 0, PINGREQ) over non-blocking TCP, a STUN
 * Binding client on the caller's UDP socket, and SHA-256 for the mailbox
 * names and the payload cipher. POSIX and WinSock.
 */
#include "signal.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET tcp_sock_t;
#define TCP_SOCK_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int tcp_sock_t;
#define TCP_SOCK_INVALID (-1)
#endif

extern "C" void port_log(const char *fmt, ...);

namespace {

constexpr const char *kDefaultBrokers = "broker.emqx.io:1883,broker.hivemq.com:1883,test.mosquitto.org:1883";
constexpr const char *kDefaultStun = "stun.l.google.com:19302,stun.cloudflare.com:3478,stun1.l.google.com:19302";
constexpr const char *kTopicPrefix = "battleship-smash/v1/";
constexpr uint16_t kKeepaliveSeconds = 60;
constexpr uint32_t kPingEveryMs = 20000;
constexpr uint32_t kRetryMs = 5000;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr size_t kMaxPacket = 4096;

/* ---------------------------------------------------------------- SHA-256 */

struct Sha256
{
	uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	uint8_t block[64];
	size_t fill = 0;
	uint64_t bits = 0;

	static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

	void compress()
	{
		static const uint32_t k[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};
		uint32_t w[64];
		for (int i = 0; i < 16; i++)
			w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) |
			       (uint32_t)block[i * 4 + 3];
		for (int i = 16; i < 64; i++) {
			uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for (int i = 0; i < 64; i++) {
			uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			hh = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}

	void update(const void *data, size_t len)
	{
		const uint8_t *p = (const uint8_t *)data;
		bits += (uint64_t)len * 8;
		while (len-- > 0) {
			block[fill++] = *p++;
			if (fill == 64) {
				compress();
				fill = 0;
			}
		}
	}

	void final(uint8_t out[32])
	{
		uint64_t total = bits;
		uint8_t pad = 0x80;
		update(&pad, 1);
		pad = 0;
		while (fill != 56) update(&pad, 1);
		for (int i = 7; i >= 0; i--) {
			block[fill++] = (uint8_t)(total >> (i * 8));
		}
		compress();
		for (int i = 0; i < 8; i++) {
			out[i * 4] = (uint8_t)(h[i] >> 24);
			out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
			out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
			out[i * 4 + 3] = (uint8_t)h[i];
		}
	}
};

void sha256(const std::string &a, const void *b, size_t b_len, uint8_t out[32])
{
	Sha256 s;
	s.update(a.data(), a.size());
	if (b_len != 0) s.update(b, b_len);
	s.final(out);
}

std::string topic_for(const std::string &code)
{
	uint8_t d[32];
	sha256("battleship-smash topic " + code, nullptr, 0, d);
	static const char *hex = "0123456789abcdef";
	std::string t = kTopicPrefix;
	for (int i = 0; i < 16; i++) {
		t.push_back(hex[d[i] >> 4]);
		t.push_back(hex[d[i] & 15]);
	}
	return t;
}

/* Payload: nonce[8] | text XOR keystream | tag[8]. Keystream block i is
 * SHA-256(key | nonce | i); the tag is SHA-256(key | nonce | text)[0..8). The
 * key is SHA-256 of the recipient's code: anyone who knows the code can read
 * its mail, which is everyone it was given to, and nobody else. */
void derive_key(const std::string &code, uint8_t key[32])
{
	sha256("battleship-smash key " + code, nullptr, 0, key);
}

void keystream_xor(const uint8_t key[32], const uint8_t nonce[8], uint8_t *data, size_t len)
{
	for (size_t off = 0, blk = 0; off < len; off += 32, blk++) {
		uint8_t in[44];
		std::memcpy(in, key, 32);
		std::memcpy(in + 32, nonce, 8);
		in[40] = (uint8_t)(blk >> 24);
		in[41] = (uint8_t)(blk >> 16);
		in[42] = (uint8_t)(blk >> 8);
		in[43] = (uint8_t)blk;
		Sha256 s;
		s.update(in, sizeof(in));
		uint8_t ks[32];
		s.final(ks);
		for (size_t i = 0; i < 32 && off + i < len; i++) data[off + i] ^= ks[i];
	}
}

void make_tag(const uint8_t key[32], const uint8_t nonce[8], const uint8_t *text, size_t len, uint8_t tag[8])
{
	Sha256 s;
	s.update(key, 32);
	s.update(nonce, 8);
	s.update(text, len);
	uint8_t d[32];
	s.final(d);
	std::memcpy(tag, d, 8);
}

std::mt19937_64 &rng()
{
	static std::mt19937_64 r(((uint64_t)std::random_device{}() << 32) ^ std::random_device{}());
	return r;
}

std::vector<uint8_t> seal(const std::string &to_code, const std::string &text)
{
	uint8_t key[32];
	derive_key(to_code, key);
	std::vector<uint8_t> out(8 + text.size() + 8);
	uint64_t n = rng()();
	std::memcpy(out.data(), &n, 8);
	std::memcpy(out.data() + 8, text.data(), text.size());
	make_tag(key, out.data(), (const uint8_t *)text.data(), text.size(), out.data() + 8 + text.size());
	keystream_xor(key, out.data(), out.data() + 8, text.size());
	return out;
}

bool open_sealed(const std::string &my_code, const uint8_t *data, size_t len, std::string *text, uint64_t *nonce)
{
	if (len < 16 || len > kMaxPacket) return false;
	uint8_t key[32];
	derive_key(my_code, key);
	std::vector<uint8_t> plain(data + 8, data + len - 8);
	keystream_xor(key, data, plain.data(), plain.size());
	uint8_t tag[8];
	make_tag(key, data, plain.data(), plain.size(), tag);
	if (std::memcmp(tag, data + len - 8, 8) != 0) return false;
	std::memcpy(nonce, data, 8);
	text->assign((const char *)plain.data(), plain.size());
	return true;
}

/* ---------------------------------------------------------------- sockets */

uint32_t now_ms() { return port_netsock_now_ms(); }

bool sockets_ready()
{
#ifdef _WIN32
	static bool inited = false;
	if (!inited) {
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
		inited = true;
	}
#endif
	return true;
}

void close_tcp(tcp_sock_t s)
{
	if (s == TCP_SOCK_INVALID) return;
#ifdef _WIN32
	closesocket(s);
#else
	close(s);
#endif
}

bool set_nonblocking(tcp_sock_t s)
{
#ifdef _WIN32
	u_long nb = 1;
	return ioctlsocket(s, FIONBIO, &nb) == 0;
#else
	int flags = fcntl(s, F_GETFL, 0);
	return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool last_error_would_block()
{
#ifdef _WIN32
	int e = WSAGetLastError();
	return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

/* host:port list -> entries. */
std::vector<std::pair<std::string, uint16_t>> split_hosts(const char *list)
{
	std::vector<std::pair<std::string, uint16_t>> out;
	std::string s = list ? list : "";
	size_t pos = 0;
	while (pos <= s.size()) {
		size_t comma = s.find(',', pos);
		std::string item = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
		item.erase(std::remove(item.begin(), item.end(), ' '), item.end());
		size_t colon = item.rfind(':');
		if (colon != std::string::npos && colon > 0) {
			int port = std::atoi(item.c_str() + colon + 1);
			if (port > 0 && port < 65536) out.emplace_back(item.substr(0, colon), (uint16_t)port);
		}
		if (comma == std::string::npos) break;
		pos = comma + 1;
	}
	return out;
}

/* getaddrinfo blocks, sometimes for seconds: every lookup runs on its own
 * detached thread and the game polls the result. */
struct Lookup
{
	std::atomic<int> state{ 0 }; /* 0 running, 1 done, -1 failed */
	uint32_t ip = 0;
};

std::shared_ptr<Lookup> resolve_async(const std::string &host)
{
	auto lookup = std::make_shared<Lookup>();
	std::thread([lookup, host]() {
		struct addrinfo hints;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		struct addrinfo *res = nullptr;
		if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res != nullptr) {
			lookup->ip = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
			freeaddrinfo(res);
			lookup->state = 1;
		} else {
			lookup->state = -1;
		}
	}).detach();
	return lookup;
}

/* ---------------------------------------------------------------- MQTT */

enum class BrokerState { Resolving, Waiting, Connecting, Handshake, Ready };

struct Broker
{
	std::string host;
	uint16_t port = 0;
	BrokerState state = BrokerState::Resolving;
	std::shared_ptr<Lookup> lookup;
	uint32_t ip = 0;
	tcp_sock_t sock = TCP_SOCK_INVALID;
	uint32_t since_ms = 0;  /* entered the current state */
	uint32_t last_send_ms = 0;
	std::vector<uint8_t> in;
	std::vector<uint8_t> out; /* not yet accepted by the socket */
	std::string subscribed;
	bool announced_down = false;
};

struct Signal
{
	bool open = false;
	std::string code;
	std::string topic;
	std::vector<Broker> brokers;
	std::deque<std::string> inbox;
	std::deque<uint64_t> seen; /* nonces, to drop the copies other brokers deliver */
	uint16_t packet_id = 1;
};

Signal s;

void put_u16(std::vector<uint8_t> &v, uint16_t x)
{
	v.push_back((uint8_t)(x >> 8));
	v.push_back((uint8_t)x);
}

void put_str(std::vector<uint8_t> &v, const std::string &str)
{
	put_u16(v, (uint16_t)str.size());
	v.insert(v.end(), str.begin(), str.end());
}

std::vector<uint8_t> frame(uint8_t type_flags, const std::vector<uint8_t> &body)
{
	std::vector<uint8_t> p;
	p.push_back(type_flags);
	size_t len = body.size();
	do {
		uint8_t byte = (uint8_t)(len % 128);
		len /= 128;
		if (len > 0) byte |= 0x80;
		p.push_back(byte);
	} while (len > 0);
	p.insert(p.end(), body.begin(), body.end());
	return p;
}

void broker_reset(Broker &b, const char *why)
{
	if (b.sock != TCP_SOCK_INVALID) {
		close_tcp(b.sock);
		b.sock = TCP_SOCK_INVALID;
	}
	if (why != nullptr && !b.announced_down) {
		port_log("SSB64 Signal: %s:%u %s\n", b.host.c_str(), b.port, why);
		b.announced_down = true;
	}
	b.in.clear();
	b.out.clear();
	b.subscribed.clear();
	b.state = BrokerState::Waiting;
	b.since_ms = now_ms();
}

void broker_queue(Broker &b, const std::vector<uint8_t> &p)
{
	b.out.insert(b.out.end(), p.begin(), p.end());
	b.last_send_ms = now_ms();
}

void broker_flush(Broker &b)
{
	while (!b.out.empty() && b.sock != TCP_SOCK_INVALID) {
		int n = (int)send(b.sock, (const char *)b.out.data(), (int)b.out.size(), 0);
		if (n > 0) {
			b.out.erase(b.out.begin(), b.out.begin() + n);
		} else {
			if (n < 0 && !last_error_would_block()) broker_reset(b, "connection lost");
			return;
		}
	}
}

void broker_subscribe(Broker &b)
{
	std::vector<uint8_t> body;
	put_u16(body, s.packet_id++);
	put_str(body, s.topic);
	body.push_back(0); /* QoS 0 */
	broker_queue(b, frame(0x82, body));
	b.subscribed = s.topic;
}

void broker_start_connect(Broker &b)
{
	if (!sockets_ready()) return;
	b.sock = socket(AF_INET, SOCK_STREAM, 0);
	if (b.sock == TCP_SOCK_INVALID || !set_nonblocking(b.sock)) {
		broker_reset(b, "socket failed");
		return;
	}
	int one = 1;
	setsockopt(b.sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
	struct sockaddr_in sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(b.port);
	sa.sin_addr.s_addr = htonl(b.ip);
	int rc = connect(b.sock, (struct sockaddr *)&sa, sizeof(sa));
	if (rc != 0 && !last_error_would_block()) {
		broker_reset(b, "unreachable");
		return;
	}
	b.state = BrokerState::Connecting;
	b.since_ms = now_ms();
}

bool socket_writable(tcp_sock_t sock, bool *failed)
{
	fd_set wr, ex;
	FD_ZERO(&wr);
	FD_ZERO(&ex);
	FD_SET(sock, &wr);
	FD_SET(sock, &ex);
	struct timeval tv = { 0, 0 };
	int rc = select((int)sock + 1, nullptr, &wr, &ex, &tv);
	*failed = false;
	if (rc <= 0) return false;
	if (FD_ISSET(sock, &ex)) {
		*failed = true;
		return false;
	}
	int err = 0;
#ifdef _WIN32
	int len = sizeof(err);
#else
	socklen_t len = sizeof(err);
#endif
	getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
	if (err != 0) {
		*failed = true;
		return false;
	}
	return FD_ISSET(sock, &wr) != 0;
}

void broker_send_connect(Broker &b)
{
	char client_id[32];
	std::snprintf(client_id, sizeof(client_id), "bship-%016llx", (unsigned long long)rng()());
	std::vector<uint8_t> body;
	put_str(body, "MQTT");
	body.push_back(4);    /* 3.1.1 */
	body.push_back(0x02); /* clean session */
	put_u16(body, kKeepaliveSeconds);
	put_str(body, client_id);
	broker_queue(b, frame(0x10, body));
	b.state = BrokerState::Handshake;
	b.since_ms = now_ms();
}

void handle_publish(const uint8_t *p, size_t len, uint8_t flags)
{
	if (len < 2) return;
	size_t tlen = ((size_t)p[0] << 8) | p[1];
	size_t off = 2 + tlen;
	if (((flags >> 1) & 3) != 0) off += 2; /* packet id at QoS 1/2 (we only subscribe at 0) */
	if (off > len) return;
	std::string topic((const char *)p + 2, tlen);
	if (topic != s.topic) return;
	std::string text;
	uint64_t nonce = 0;
	if (!open_sealed(s.code, p + off, len - off, &text, &nonce)) return;
	if (std::find(s.seen.begin(), s.seen.end(), nonce) != s.seen.end()) return;
	s.seen.push_back(nonce);
	if (s.seen.size() > 128) s.seen.pop_front();
	s.inbox.push_back(text);
}

void broker_read(Broker &b)
{
	uint8_t buf[2048];
	for (;;) {
		int n = (int)recv(b.sock, (char *)buf, sizeof(buf), 0);
		if (n > 0) {
			b.in.insert(b.in.end(), buf, buf + n);
			if (b.in.size() > 64 * 1024) {
				broker_reset(b, "flooded");
				return;
			}
			continue;
		}
		if (n == 0 || !last_error_would_block()) {
			broker_reset(b, "connection closed");
			return;
		}
		break;
	}
	/* Complete packets off the front. */
	for (;;) {
		if (b.in.size() < 2) return;
		size_t len = 0, mul = 1, i = 1;
		bool complete = false;
		for (; i < b.in.size() && i <= 4; i++) {
			len += (b.in[i] & 0x7F) * mul;
			mul *= 128;
			if ((b.in[i] & 0x80) == 0) {
				complete = true;
				break;
			}
		}
		if (!complete) {
			if (b.in.size() > 5) broker_reset(b, "bad packet");
			return;
		}
		size_t header = i + 1;
		if (b.in.size() < header + len) return;
		uint8_t type = b.in[0] >> 4;
		uint8_t flags = b.in[0] & 0x0F;
		const uint8_t *body = b.in.data() + header;
		if (type == 2) { /* CONNACK */
			if (len >= 2 && body[1] == 0) {
				b.state = BrokerState::Ready;
				b.since_ms = now_ms();
				port_log("SSB64 Signal: %s:%u connected\n", b.host.c_str(), b.port);
				b.announced_down = false;
				broker_subscribe(b);
			} else {
				broker_reset(b, "refused the connection");
				return;
			}
		} else if (type == 3) {
			handle_publish(body, len, flags);
		}
		b.in.erase(b.in.begin(), b.in.begin() + (long)(header + len));
	}
}

void broker_tick(Broker &b)
{
	uint32_t t = now_ms();
	switch (b.state) {
	case BrokerState::Resolving:
		if (!b.lookup) b.lookup = resolve_async(b.host);
		if (b.lookup->state == 0) return;
		if (b.lookup->state < 0) {
			b.lookup.reset();
			broker_reset(b, "name lookup failed");
			return;
		}
		b.ip = b.lookup->ip;
		b.lookup.reset();
		broker_start_connect(b);
		return;
	case BrokerState::Waiting:
		if (t - b.since_ms >= kRetryMs) {
			if (b.ip != 0) broker_start_connect(b);
			else b.state = BrokerState::Resolving;
		}
		return;
	case BrokerState::Connecting: {
		bool failed = false;
		if (socket_writable(b.sock, &failed)) broker_send_connect(b);
		else if (failed) broker_reset(b, "unreachable");
		else if (t - b.since_ms > kConnectTimeoutMs) broker_reset(b, "timed out");
		break;
	}
	case BrokerState::Handshake:
		if (t - b.since_ms > kConnectTimeoutMs) {
			broker_reset(b, "no answer to CONNECT");
			return;
		}
		broker_read(b);
		break;
	case BrokerState::Ready:
		if (b.subscribed != s.topic) broker_subscribe(b);
		if (t - b.last_send_ms > kPingEveryMs) broker_queue(b, { 0xC0, 0x00 });
		broker_read(b);
		break;
	}
	if (b.sock != TCP_SOCK_INVALID) broker_flush(b);
}

/* ---------------------------------------------------------------- STUN */

struct StunServer
{
	std::string host;
	uint16_t port;
	std::shared_ptr<Lookup> lookup;
	uint32_t ip = 0;
};

std::vector<StunServer> stun_servers;
uint8_t stun_txid[12];
bool stun_txid_set = false;

} // namespace

void port_signal_open(const std::string &my_code)
{
	if (!s.open) {
		s.brokers.clear();
		for (auto &hp : split_hosts(CVarGetString("gNetplay.Brokers", kDefaultBrokers))) {
			Broker b;
			b.host = hp.first;
			b.port = hp.second;
			b.since_ms = now_ms();
			s.brokers.push_back(std::move(b));
		}
		s.open = true;
		port_log("SSB64 Signal: opening %d broker(s)\n", (int)s.brokers.size());
	}
	if (s.code != my_code) {
		s.code = my_code;
		s.topic = topic_for(my_code);
		s.inbox.clear();
	}
}

void port_signal_close()
{
	if (!s.open) return;
	for (auto &b : s.brokers) {
		if (b.sock != TCP_SOCK_INVALID) {
			if (b.state == BrokerState::Ready) {
				std::vector<uint8_t> bye = { 0xE0, 0x00 }; /* DISCONNECT */
				send(b.sock, (const char *)bye.data(), (int)bye.size(), 0);
			}
			close_tcp(b.sock);
		}
	}
	s.brokers.clear();
	s.inbox.clear();
	s.open = false;
	s.code.clear();
	s.topic.clear();
}

bool port_signal_is_open() { return s.open; }

void port_signal_tick()
{
	if (!s.open) return;
	for (auto &b : s.brokers) broker_tick(b);
}

int port_signal_brokers_up()
{
	int n = 0;
	for (auto &b : s.brokers)
		if (b.state == BrokerState::Ready) n++;
	return n;
}

int port_signal_brokers_total() { return (int)s.brokers.size(); }

void port_signal_send(const std::string &to_code, const std::string &text)
{
	if (!s.open) return;
	std::vector<uint8_t> payload = seal(to_code, text);
	std::vector<uint8_t> body;
	put_str(body, topic_for(to_code));
	body.insert(body.end(), payload.begin(), payload.end());
	std::vector<uint8_t> packet = frame(0x30, body);
	for (auto &b : s.brokers) {
		if (b.state != BrokerState::Ready) continue;
		broker_queue(b, packet);
		broker_flush(b);
	}
}

bool port_signal_receive(std::string *text)
{
	if (s.inbox.empty()) return false;
	*text = s.inbox.front();
	s.inbox.pop_front();
	return true;
}

void port_stun_send(intptr_t sock)
{
	if (stun_servers.empty()) {
		for (auto &hp : split_hosts(CVarGetString("gNetplay.Stun", kDefaultStun))) {
			StunServer srv;
			srv.host = hp.first;
			srv.port = hp.second;
			srv.lookup = resolve_async(hp.first);
			stun_servers.push_back(std::move(srv));
		}
	}
	if (!stun_txid_set) {
		for (auto &byte : stun_txid) byte = (uint8_t)rng()();
		stun_txid_set = true;
	}
	/* Binding Request: type 0x0001, length 0, magic cookie, transaction id. */
	uint8_t req[20] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42 };
	std::memcpy(req + 8, stun_txid, 12);
	for (auto &srv : stun_servers) {
		if (srv.lookup && srv.lookup->state != 0) {
			if (srv.lookup->state > 0) srv.ip = srv.lookup->ip;
			srv.lookup.reset();
		}
		if (srv.ip == 0) continue;
		PortNetAddr to{ srv.ip, srv.port };
		port_netsock_sendto(sock, req, sizeof(req), &to);
	}
}

bool port_stun_parse(const uint8_t *buf, int len, PortNetAddr *mapped)
{
	if (len < 20 || buf[0] != 0x01 || buf[1] != 0x01) return false;
	if (buf[4] != 0x21 || buf[5] != 0x12 || buf[6] != 0xA4 || buf[7] != 0x42) return false;
	if (!stun_txid_set || std::memcmp(buf + 8, stun_txid, 12) != 0) return false;
	int body = (buf[2] << 8) | buf[3];
	if (20 + body > len) return false;
	for (int off = 20; off + 4 <= 20 + body;) {
		int type = (buf[off] << 8) | buf[off + 1];
		int alen = (buf[off + 2] << 8) | buf[off + 3];
		const uint8_t *v = buf + off + 4;
		if (off + 4 + alen > len) break;
		/* XOR-MAPPED-ADDRESS (0x0020), or plain MAPPED-ADDRESS (0x0001). */
		if ((type == 0x0020 || type == 0x0001) && alen >= 8 && v[1] == 0x01) {
			uint16_t port = (uint16_t)((v[2] << 8) | v[3]);
			uint32_t ip = ((uint32_t)v[4] << 24) | ((uint32_t)v[5] << 16) | ((uint32_t)v[6] << 8) | v[7];
			if (type == 0x0020) {
				port ^= 0x2112;
				ip ^= 0x2112A442;
			}
			mapped->ip = ip;
			mapped->port = port;
			return true;
		}
		off += 4 + ((alen + 3) & ~3);
	}
	return true; /* ours, but nothing usable in it */
}

std::vector<PortNetAddr> port_signal_local_addrs(uint16_t port)
{
	std::vector<PortNetAddr> out;
	if (sockets_ready()) {
		/* A connected UDP socket sends nothing but learns the source address
		 * the OS would use for the internet, i.e. the LAN address. */
		tcp_sock_t u = socket(AF_INET, SOCK_DGRAM, 0);
		if (u != TCP_SOCK_INVALID) {
			struct sockaddr_in sa;
			std::memset(&sa, 0, sizeof(sa));
			sa.sin_family = AF_INET;
			sa.sin_port = htons(53);
			sa.sin_addr.s_addr = htonl(0x08080808);
			if (connect(u, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
				struct sockaddr_in me;
#ifdef _WIN32
				int len = sizeof(me);
#else
				socklen_t len = sizeof(me);
#endif
				if (getsockname(u, (struct sockaddr *)&me, &len) == 0 && me.sin_addr.s_addr != 0)
					out.push_back(PortNetAddr{ ntohl(me.sin_addr.s_addr), port });
			}
			close_tcp(u);
		}
	}
	out.push_back(PortNetAddr{ 0x7F000001u, port });
	return out;
}
