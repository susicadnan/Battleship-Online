/**
 * lobby.cpp — see lobby.h. Rendezvous protocol: tools/netplay/rendezvous_server.py.
 */
#include "lobby.h"

#include "netsock.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

extern "C" void port_log(const char *fmt, ...);
/* decomp/src/sys/netpeer.c (C) */
extern "C" void syNetPeerLobbyConfigure(intptr_t sock, const PortNetAddr *peer, int is_host, uint32_t delay,
                                        uint32_t session_id, uint32_t seed, uint8_t host_fighter,
                                        uint8_t client_fighter, uint32_t stage_kind, uint32_t stocks,
                                        uint32_t time_minutes, int menu_mode);
extern "C" int syNetPeerLobbyStep(void);
extern "C" void syNetPeerLobbyAbort(void);
extern "C" void syNetPeerAbortMatch(const char *reason);
extern "C" int syNetPeerCheckMenuMode(void);
extern "C" void syNetPeerRematchRequest(uint8_t my_fighter);
extern "C" int syNetPeerRematchStep(void);
extern "C" int syNetPeerCheckPeerWantsRematch(void);
extern "C" void syNetPeerHostConfigure(intptr_t sock, uint32_t delay, uint32_t session_id, uint32_t seed);
extern "C" int syNetPeerHostAddMember(const PortNetAddr *addr, const char *code);
extern "C" void syNetPeerHostStartSession(void);

namespace {

constexpr const char *kMagic = "BSR1";
constexpr int kHelloInterval = 60;      /* frames between HELLO/JOIN keepalives */
constexpr int kPunchInterval = 3;
constexpr int kPunchSettleFrames = 30;  /* keep punching this long after both sides see each other */
constexpr int kServerTimeoutFrames = 600;
constexpr int kPunchRelayFrames = 300;  /* 5 s without a direct path: ask the server to relay */
constexpr int kPunchTimeoutFrames = 900;
constexpr int kMaxGuests = 3;
constexpr int kGuestKeepaliveFrames = 30; /* host -> each waiting guest */
constexpr int kGuestTimeoutFrames = 600;

struct Guest
{
	PortNetAddr addr{};
	std::string code;
	int silent = 0;
};

struct Lobby
{
	PortLobbyState state = PORT_LOBBY_IDLE;
	std::string status = "Not connected";
	std::string code;          /* NAME#1234 */
	std::string friend_code;
	std::string public_addr;
	std::string match_id;
	PortNetAddr server{};
	PortNetAddr peer{};
	bool is_host = false;
	bool direct = false;
	bool relayed = false;      /* game packets go through the server */
	bool peer_saw_us = false;  /* the peer's PUNCH reported that it has seen ours */
	bool force_relay = false;  /* SSB64_LOBBY_FORCE_RELAY=1: never punch, to test the relay */
	bool auto_rematch = false; /* SSB64_LOBBY_AUTO_REMATCH=1: ask for a rematch a second after each game */
	int peer_fighter = 0;
	int auto_start = 0;        /* SSB64_LOBBY_AUTOSTART=N: the host starts once N people are in */
	bool host_punch = false;   /* hosting with an address to punch towards */
	std::vector<Guest> guests; /* HOSTING */
	intptr_t sock = -1;
	int frames = 0;            /* frames in the current state */
	int since_welcome = 0;
	int punch_replies = 0;
	int settle = 0;
};

Lobby g;

std::string upper_alnum(const char *s, size_t max_len)
{
	std::string out;
	for (; *s != '\0' && out.size() < max_len; s++) {
		unsigned char c = (unsigned char)*s;
		if (std::isalnum(c)) out.push_back((char)std::toupper(c));
	}
	return out;
}

void refresh_code()
{
	std::string name = upper_alnum(CVarGetString("gNetplay.Name", "PLAYER"), 12);
	if (name.empty()) name = "PLAYER";
	const char *tag = CVarGetString("gNetplay.Tag", "");
	if (std::strlen(tag) != 4) {
		std::random_device rd;
		char buf[8];
		std::snprintf(buf, sizeof(buf), "%04u", (unsigned)(rd() % 10000u));
		CVarSetString("gNetplay.Tag", buf);
		CVarSave();
		tag = CVarGetString("gNetplay.Tag", buf);
	}
	g.code = name + "#" + tag;
}

void set_state(PortLobbyState s, const std::string &status)
{
	if (g.state != s) g.frames = 0;
	g.state = s;
	g.status = status;
	port_log("SSB64 Lobby: %s\n", status.c_str());
}

bool open_socket()
{
	if (g.sock >= 0) return true;
	int base = CVarGetInteger("gNetplay.Port", 7000);
	for (int i = 0; i < 10; i++) {
		PortNetAddr bind{ 0, (uint16_t)(base + i) };
		g.sock = port_netsock_open_udp(&bind);
		if (g.sock >= 0) {
			port_log("SSB64 Lobby: bound UDP port %d\n", base + i);
			return true;
		}
	}
	set_state(PORT_LOBBY_FAILED, "Could not open a UDP port");
	return false;
}

void close_socket()
{
	if (g.sock >= 0) {
		port_netsock_close(g.sock);
		g.sock = -1;
	}
}

void send_text(const PortNetAddr *to, const std::string &line)
{
	port_netsock_sendto(g.sock, line.data(), (int)line.size(), to);
}

int my_fighter() { return CVarGetInteger("gNetplay.Fighter", 0); }

void send_hello()
{
	send_text(&g.server, std::string(kMagic) + " HELLO " + g.code + " " + std::to_string(my_fighter()));
	if (!g.friend_code.empty())
		send_text(&g.server, std::string(kMagic) + " JOIN " + g.code + " " + g.friend_code);
}

/* PUNCH <match> <code> <fkind> <seen>: seen=1 once the peer's punches reach
 * us, so each side knows the path works in both directions before handing
 * the socket to netpeer (which no longer reads lobby messages). */
void send_punch()
{
	send_text(&g.peer, std::string(kMagic) + " PUNCH " + g.match_id + " " + g.code + " " + std::to_string(my_fighter()) +
	                       (g.punch_replies > 0 ? " 1" : " 0"));
}

/* Host -> a guest: we hear you (seen = 1). */
void send_punch_to(intptr_t sock, const PortNetAddr &to)
{
	std::string line = std::string(kMagic) + " PUNCH direct " + g.code + " " + std::to_string(my_fighter()) + " 1";
	port_netsock_sendto(sock, line.data(), (int)line.size(), &to);
}

Guest *find_guest(const PortNetAddr &addr)
{
	for (auto &guest : g.guests)
		if (guest.addr.ip == addr.ip && guest.addr.port == addr.port) return &guest;
	return nullptr;
}

void hosting_status()
{
	std::string s = "Hosting on UDP port " + std::to_string(CVarGetInteger("gNetplay.Port", 7000));
	if (g.guests.empty()) s += ", waiting for players to join";
	else s += ", " + std::to_string(g.guests.size() + 1) + " people in. Press Start when everyone is here";
	g.status = s;
}

void send_relay_request()
{
	send_text(&g.server, std::string(kMagic) + " RELAY " + g.match_id);
}

uint32_t session_from_match()
{
	if (g.direct) return 1;
	uint32_t v = (uint32_t)std::strtoul(g.match_id.c_str(), nullptr, 16);
	return v ? v : 1;
}

void start_bootstrap()
{
	uint32_t delay = (uint32_t)CVarGetInteger("gNetplay.Delay", 2);
	uint32_t stage = (uint32_t)CVarGetInteger("gNetplay.Stage", 0);
	uint8_t host_f = (uint8_t)(g.is_host ? my_fighter() : g.peer_fighter);
	uint8_t client_f = (uint8_t)(g.is_host ? g.peer_fighter : my_fighter());
	uint32_t seed = (uint32_t)std::time(nullptr) | 1u;
	uint32_t stocks = (uint32_t)CVarGetInteger("gNetplay.Stocks", 3);
	uint32_t minutes = (uint32_t)CVarGetInteger("gNetplay.TimeLimit", 0);
	/* Default: both players pick everything in the game's own VS menus,
	 * run in lockstep. gNetplay.QuickMatch=1 is the old path (picks on the
	 * Netplay page, straight into the battle). */
	int menu_mode = CVarGetInteger("gNetplay.QuickMatch", 0) == 0 ? 1 : 0;
	/* netpeer owns the socket from here on (it keeps it between games). */
	syNetPeerLobbyConfigure(g.sock, &g.peer, g.is_host ? 1 : 0, delay, session_from_match(), seed, host_f, client_f, stage,
	                        stocks, minutes, menu_mode);
	g.sock = -1;
	set_state(PORT_LOBBY_BOOTSTRAP, std::string(g.relayed ? "Relayed. " : "Connected. ") +
	                                    (g.is_host ? "Sending match settings"
	                                     : menu_mode ? "Waiting for the host to start"
	                                                 : "Waiting for match settings"));
}

/* Split a datagram into fields. */
int split(char *text, char **fields, int max_fields)
{
	int n = 0;
	char *p = text;
	while (*p != '\0' && n < max_fields) {
		while (*p == ' ') p++;
		if (*p == '\0') break;
		fields[n++] = p;
		while (*p != '\0' && *p != ' ') p++;
		if (*p == ' ') *p++ = '\0';
	}
	return n;
}

void handle_datagram(char *text, int len, const PortNetAddr &from)
{
	text[len] = '\0';
	char *f[8];
	int n = split(text, f, 8);
	if (n < 2 || std::strcmp(f[0], kMagic) != 0) return;

	if (std::strcmp(f[1], "WELCOME") == 0 && n >= 3) {
		g.public_addr = f[2];
		g.since_welcome = 0;
		if (g.state == PORT_LOBBY_REGISTERING)
			set_state(PORT_LOBBY_REGISTERED, "Online as " + g.code + (g.friend_code.empty() ? "" : ", waiting for " + g.friend_code));
	} else if (std::strcmp(f[1], "WAITING") == 0 && n >= 3) {
		if (g.state == PORT_LOBBY_REGISTERED || g.state == PORT_LOBBY_REGISTERING)
			set_state(PORT_LOBBY_WAITING, std::string("Waiting for ") + f[2] + " to enter your code");
	} else if (std::strcmp(f[1], "PEER") == 0 && n >= 6) {
		if (g.state != PORT_LOBBY_REGISTERED && g.state != PORT_LOBBY_WAITING) return;
		PortNetAddr peer;
		if (!port_netsock_parse_addr(f[3], &peer)) return;
		g.is_host = (std::strcmp(f[2], "host") == 0);
		g.peer = peer;
		g.match_id = f[4];
		g.peer_fighter = std::atoi(f[5]);
		g.punch_replies = 0;
		g.settle = 0;
		g.relayed = false;
		g.peer_saw_us = false;
		set_state(PORT_LOBBY_PUNCHING, std::string("Found ") + (n >= 7 ? f[6] : "peer") + ", connecting...");
	} else if (std::strcmp(f[1], "PUNCH") == 0 && n >= 4 && g.state == PORT_LOBBY_HOSTING) {
		Guest *guest = find_guest(from);
		if (guest == nullptr) {
			if ((int)g.guests.size() >= kMaxGuests) {
				send_text(&from, std::string(kMagic) + " FULL");
				return;
			}
			g.guests.push_back(Guest{ from, f[3], 0 });
			guest = &g.guests.back();
			port_log("SSB64 Lobby: %s joined (%d in)\n", f[3], (int)g.guests.size() + 1);
			hosting_status();
		}
		guest->silent = 0;
		send_punch_to(g.sock, from);
	} else if (std::strcmp(f[1], "FULL") == 0) {
		if (g.state == PORT_LOBBY_PUNCHING) {
			set_state(PORT_LOBBY_FAILED, "That session is full (four people)");
			close_socket();
		}
	} else if (std::strcmp(f[1], "PUNCH") == 0 && n >= 3) {
		if (g.state != PORT_LOBBY_PUNCHING || g.relayed) return;
		if (!g.direct && g.match_id != f[2]) return;
		/* The NAT may have rewritten the port: trust where it actually came from. */
		g.peer = from;
		if (n >= 5) g.peer_fighter = std::atoi(f[4]);
		if (n >= 6 && std::atoi(f[5]) != 0) g.peer_saw_us = true;
		if (g.punch_replies++ == 0) {
			char buf[32];
			port_netsock_format_addr(&from, buf, sizeof(buf));
			set_state(PORT_LOBBY_PUNCHING, std::string("Peer reached at ") + buf);
		}
	} else if (std::strcmp(f[1], "RELAYED") == 0 && n >= 3) {
		if (g.state != PORT_LOBBY_PUNCHING || g.direct || g.match_id != f[2]) return;
		/* Either side gave up on a direct path; from now on the server
		 * forwards the game packets between us. */
		g.relayed = true;
		g.peer = g.server;
		set_state(PORT_LOBBY_PUNCHING, "No direct path, relaying through the server");
		start_bootstrap();
	} else if (std::strcmp(f[1], "ERROR") == 0) {
		std::string msg;
		for (int i = 2; i < n; i++) msg += std::string(i > 2 ? " " : "") + f[i];
		set_state(PORT_LOBBY_FAILED, "Server: " + msg);
	}
}

void pump()
{
	char buf[512];
	for (;;) {
		PortNetAddr from;
		int got = port_netsock_recvfrom(g.sock, buf, sizeof(buf) - 1, &from);
		if (got <= 0) break;
		/* A guest that is already waiting for the start pings us (binary):
		 * that keeps it in, nothing else to do with it here. */
		if (g.state == PORT_LOBBY_HOSTING) {
			if (Guest *guest = find_guest(from)) guest->silent = 0;
		}
		handle_datagram(buf, got, from);
	}
}

void host_start()
{
	uint32_t delay = (uint32_t)CVarGetInteger("gNetplay.Delay", 2);
	uint32_t seed = (uint32_t)std::time(nullptr) | 1u;
	syNetPeerHostConfigure(g.sock, delay, 1, seed);
	for (auto &guest : g.guests) syNetPeerHostAddMember(&guest.addr, guest.code.c_str());
	g.sock = -1; /* netpeer's from here on */
	syNetPeerHostStartSession();
	set_state(PORT_LOBBY_BOOTSTRAP, "Starting: " + std::to_string(g.guests.size() + 1) + " people");
}

} // namespace

/* Headless testing: SSB64_LOBBY_CONNECT=<friend code> with SSB64_LOBBY_SERVER
 * and SSB64_LOBBY_NAME, or SSB64_LOBBY_DIRECT=<ip:port> with SSB64_LOBBY_HOST=1
 * on one side, start the lobby at boot exactly as the menu buttons would. */
static void lobby_autostart_from_env()
{
	static bool done = false;
	if (done) return;
	done = true;
	if (const char *name = std::getenv("SSB64_LOBBY_NAME")) CVarSetString("gNetplay.Name", name);
	if (const char *server = std::getenv("SSB64_LOBBY_SERVER")) CVarSetString("gNetplay.Server", server);
	if (const char *port = std::getenv("SSB64_LOBBY_PORT")) CVarSetInteger("gNetplay.Port", std::atoi(port));
	if (const char *fighter = std::getenv("SSB64_LOBBY_FIGHTER")) CVarSetInteger("gNetplay.Fighter", std::atoi(fighter));
	if (const char *delay = std::getenv("SSB64_LOBBY_DELAY")) CVarSetInteger("gNetplay.Delay", std::atoi(delay));
	if (const char *stocks = std::getenv("SSB64_LOBBY_STOCKS")) CVarSetInteger("gNetplay.Stocks", std::atoi(stocks));
	if (const char *minutes = std::getenv("SSB64_LOBBY_TIME")) CVarSetInteger("gNetplay.TimeLimit", std::atoi(minutes));
	if (const char *tag = std::getenv("SSB64_LOBBY_TAG")) CVarSetString("gNetplay.Tag", tag);
	if (const char *relay = std::getenv("SSB64_LOBBY_FORCE_RELAY")) g.force_relay = std::atoi(relay) != 0;
	if (const char *rematch = std::getenv("SSB64_LOBBY_AUTO_REMATCH")) g.auto_rematch = std::atoi(rematch) != 0;
	if (const char *quick = std::getenv("SSB64_LOBBY_QUICK")) CVarSetInteger("gNetplay.QuickMatch", std::atoi(quick));
	if (const char *stage = std::getenv("SSB64_LOBBY_STAGE")) CVarSetInteger("gNetplay.Stage", std::atoi(stage));
	if (const char *mode = std::getenv("SSB64_LOBBY_GROUP_MODE")) CVarSetInteger("gNetplay.GroupMode", std::atoi(mode));
	if (const char *preset = std::getenv("SSB64_LOBBY_PRESET")) CVarSetInteger("gNetplay.Preset", std::atoi(preset));
	if (const char *autostart = std::getenv("SSB64_LOBBY_AUTOSTART")) g.auto_start = std::atoi(autostart);
	if (const char *code = std::getenv("SSB64_LOBBY_CONNECT")) {
		port_lobby_connect(code);
	} else if (const char *direct = std::getenv("SSB64_LOBBY_DIRECT")) {
		const char *host = std::getenv("SSB64_LOBBY_HOST");
		port_lobby_connect_direct(direct, (host != nullptr && std::atoi(host) != 0) ? 1 : 0);
	}
}

extern "C" void port_lobby_tick(void)
{
	lobby_autostart_from_env();
	g.frames++;
	switch (g.state) {
	case PORT_LOBBY_IDLE:
	case PORT_LOBBY_FAILED:
	case PORT_LOBBY_IN_MATCH:
		return;

	case PORT_LOBBY_REGISTERING:
	case PORT_LOBBY_REGISTERED:
	case PORT_LOBBY_WAITING:
		if (g.frames % kHelloInterval == 1) send_hello();
		g.since_welcome++;
		pump();
		if (g.since_welcome > kServerTimeoutFrames) {
			set_state(PORT_LOBBY_FAILED, "No answer from the rendezvous server");
			close_socket();
		}
		return;

	case PORT_LOBBY_HOSTING:
		if (g.host_punch && g.frames % kPunchInterval == 0) send_punch();
		if (g.frames % kGuestKeepaliveFrames == 0) {
			for (auto &guest : g.guests) send_punch_to(g.sock, guest.addr);
		}
		for (auto &guest : g.guests) guest.silent++;
		pump();
		for (size_t i = 0; i < g.guests.size();) {
			if (g.guests[i].silent > kGuestTimeoutFrames) {
				port_log("SSB64 Lobby: %s went quiet, removed\n", g.guests[i].code.c_str());
				g.guests.erase(g.guests.begin() + (long)i);
				hosting_status();
			} else i++;
		}
		if (g.auto_start > 0 && (int)g.guests.size() + 1 >= g.auto_start && g.frames > 60) host_start();
		return;

	case PORT_LOBBY_PUNCHING:
		if (g.frames % kPunchInterval == 0 && !g.force_relay) send_punch();
		if (!g.direct && g.frames > kPunchRelayFrames && g.frames % 60 == 0 && !(g.punch_replies > 0 && g.peer_saw_us)) {
			send_relay_request();
		}
		pump();
		if (g.state != PORT_LOBBY_PUNCHING) return;  /* RELAYED moved us on */
		if (g.punch_replies > 0 && g.peer_saw_us && ++g.settle >= kPunchSettleFrames) {
			start_bootstrap();
		} else if (g.frames > kPunchTimeoutFrames) {
			set_state(PORT_LOBBY_FAILED, "Could not reach the other player (NAT?)");
			close_socket();
		}
		return;

	case PORT_LOBBY_BOOTSTRAP: {
		int rc = syNetPeerLobbyStep();
		if (rc > 0)
			set_state(PORT_LOBBY_IN_MATCH, syNetPeerCheckMenuMode() ? "Connected. Pick your fighters in the game's VS menu"
			                                                        : "Match starting");
		else if (rc < 0 && g.state == PORT_LOBBY_BOOTSTRAP) set_state(PORT_LOBBY_FAILED, "Match setup timed out");
		return;
	}

	case PORT_LOBBY_POST_MATCH: {
		int rc = syNetPeerRematchStep();
		if (rc > 0) set_state(PORT_LOBBY_BOOTSTRAP, "Rematch! Exchanging match settings");
		else if (rc < 0) set_state(PORT_LOBBY_IDLE, "Not connected");
		else if (g.auto_rematch && g.frames == 60) port_lobby_request_rematch();
		return;
	}
	}
}

extern "C" void port_lobby_notify_post_match(void)
{
	/* netpeer still owns the socket; it only comes back to us on Leave. */
	g.sock = -1;
	set_state(PORT_LOBBY_POST_MATCH, "Game over. Rematch?");
}

extern "C" void port_lobby_request_rematch(void)
{
	if (g.state != PORT_LOBBY_POST_MATCH) return;
	syNetPeerRematchRequest((uint8_t)my_fighter());
	set_state(PORT_LOBBY_POST_MATCH, "Rematch requested, waiting for the other player");
}

extern "C" int port_lobby_peer_wants_rematch(void)
{
	return g.state == PORT_LOBBY_POST_MATCH && syNetPeerCheckPeerWantsRematch();
}

extern "C" const char *port_lobby_code(void)
{
	if (g.code.empty()) refresh_code();
	return g.code.c_str();
}

extern "C" void port_lobby_set_name(const char *name)
{
	CVarSetString("gNetplay.Name", name);
	CVarSave();
	refresh_code();
}

extern "C" void port_lobby_connect(const char *friend_code)
{
	if (g.state == PORT_LOBBY_PUNCHING || g.state == PORT_LOBBY_BOOTSTRAP || g.state == PORT_LOBBY_IN_MATCH ||
	    g.state == PORT_LOBBY_POST_MATCH || g.state == PORT_LOBBY_HOSTING)
		return;
	refresh_code();
	if (!port_netsock_parse_addr(CVarGetString("gNetplay.Server", ""), &g.server)) {
		set_state(PORT_LOBBY_FAILED, "Set the rendezvous server as host:port first");
		return;
	}
	g.direct = false;
	g.friend_code.clear();
	if (friend_code != nullptr && friend_code[0] != '\0') {
		/* NAME#1234: letters/digits before the '#', four digits after. */
		const char *hash = std::strchr(friend_code, '#');
		std::string name = upper_alnum(hash ? std::string(friend_code, hash - friend_code).c_str() : friend_code, 12);
		std::string tag = hash ? upper_alnum(hash + 1, 4) : "";
		if (name.empty() || tag.size() != 4) {
			set_state(PORT_LOBBY_FAILED, "Friend code must look like NAME#1234");
			return;
		}
		g.friend_code = name + "#" + tag;
	}
	if (!open_socket()) return;
	g.since_welcome = 0;
	set_state(PORT_LOBBY_REGISTERING, "Contacting rendezvous server...");
	g.frames = 0;
	send_hello();
}

extern "C" void port_lobby_connect_direct(const char *host_port, int is_host)
{
	if (g.state == PORT_LOBBY_PUNCHING || g.state == PORT_LOBBY_BOOTSTRAP || g.state == PORT_LOBBY_IN_MATCH ||
	    g.state == PORT_LOBBY_POST_MATCH || g.state == PORT_LOBBY_HOSTING)
		return;
	refresh_code();
	PortNetAddr peer{};
	bool have_peer = host_port != nullptr && host_port[0] != '\0' && port_netsock_parse_addr(host_port, &peer);
	if (!have_peer && !is_host) {
		set_state(PORT_LOBBY_FAILED, "Direct address must look like 1.2.3.4:7000");
		return;
	}
	if (!open_socket()) return;
	g.direct = true;
	g.relayed = false;
	g.peer_saw_us = false;
	g.is_host = (is_host != 0);
	g.peer = peer;
	g.match_id = "direct";
	g.punch_replies = 0;
	g.settle = 0;
	if (g.is_host) {
		/* The host does not need anyone's address: guests come to it. One
		 * typed in is still punched towards, which opens the host's own
		 * NAT for that guest. */
		g.host_punch = have_peer;
		g.guests.clear();
		set_state(PORT_LOBBY_HOSTING, "Hosting");
		hosting_status();
		g.frames = 0;
		return;
	}
	set_state(PORT_LOBBY_PUNCHING, std::string("Connecting directly to ") + host_port + "...");
	g.frames = 0;
}

extern "C" void port_lobby_host_start(void)
{
	if (g.state != PORT_LOBBY_HOSTING || g.guests.empty()) return;
	host_start();
}

extern "C" int port_lobby_host_guest_count(void)
{
	return g.state == PORT_LOBBY_HOSTING ? (int)g.guests.size() : 0;
}

extern "C" const char *port_lobby_host_guest_code(int index)
{
	if (g.state != PORT_LOBBY_HOSTING || index < 0 || index >= (int)g.guests.size()) return "";
	return g.guests[(size_t)index].code.c_str();
}

/* From sys/netpeer.c on the host once the session is running: a datagram
 * that is not game traffic, i.e. someone's lobby PUNCH. Late joiners get in
 * at the next character select. */
extern "C" void port_lobby_host_foreign_datagram(intptr_t sock, const uint8_t *buffer, int size, const PortNetAddr *from)
{
	char text[512];
	char *f[8];
	if (size <= 0 || size >= (int)sizeof(text)) return;
	std::memcpy(text, buffer, (size_t)size);
	text[size] = '\0';
	int n = split(text, f, 8);
	if (n < 4 || std::strcmp(f[0], kMagic) != 0 || std::strcmp(f[1], "PUNCH") != 0) return;
	if (syNetPeerHostAddMember(from, f[3]) < 0) {
		std::string full = std::string(kMagic) + " FULL";
		port_netsock_sendto(sock, full.data(), (int)full.size(), from);
		return;
	}
	send_punch_to(sock, *from);
}

extern "C" int port_lobby_group_mode(void) { return CVarGetInteger("gNetplay.GroupMode", 0); }
extern "C" int port_lobby_preset(void) { return CVarGetInteger("gNetplay.Preset", 1); }

extern "C" void port_lobby_notify_round(void)
{
	if (g.state == PORT_LOBBY_BOOTSTRAP) set_state(PORT_LOBBY_IN_MATCH, "Connected");
}

extern "C" void port_lobby_cancel(void)
{
	if (g.state == PORT_LOBBY_IN_MATCH) {
		/* Ends the session on both sides (the peer sees "connection lost")
		 * and sends us back to the VS menu; netpeer closes its socket. */
		syNetPeerAbortMatch("you left the match");
		g.sock = -1;
		g.friend_code.clear();
		set_state(PORT_LOBBY_IDLE, "Not connected");
		return;
	}
	if (g.state == PORT_LOBBY_BOOTSTRAP || g.state == PORT_LOBBY_POST_MATCH) syNetPeerLobbyAbort();
	else if (g.sock >= 0 && !g.direct && g.state != PORT_LOBBY_IDLE)
		send_text(&g.server, std::string(kMagic) + " LEAVE " + g.code);
	close_socket();
	g.friend_code.clear();
	g.guests.clear();
	set_state(PORT_LOBBY_IDLE, "Not connected");
}

extern "C" void port_lobby_notify_match_over(const char *reason)
{
	/* netpeer already closed the socket it owned. */
	g.sock = -1;
	g.friend_code.clear();
	set_state(PORT_LOBBY_FAILED, std::string("Match ended: ") + (reason ? reason : "unknown"));
}

extern "C" int port_lobby_state(void) { return g.state; }
extern "C" const char *port_lobby_status(void) { return g.status.c_str(); }
extern "C" const char *port_lobby_public_addr(void) { return g.public_addr.c_str(); }
