/**
 * netsock.c — see port/netsock.h. One UDP socket, POSIX and WinSock.
 *
 * Lives in port/stubs so it is compiled into ssb64_game without the
 * rollback state-section include (sockets are host state).
 */
#include "netsock.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET port_sock_t;
#define PORT_SOCK_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
typedef int port_sock_t;
#define PORT_SOCK_INVALID (-1)
#endif

extern void port_log(const char *fmt, ...);

/* port/stubs is compiled with the decomp's libc shadow headers on the include
 * path, and they leave these out. */
extern long strtol(const char *s, char **end, int base);
extern int strcmp(const char *a, const char *b);

static char *port_netsock_put_u32(char *p, char *end, uint32_t v)
{
	char tmp[11];
	int n = 0;
	do {
		tmp[n++] = (char)('0' + (v % 10));
		v /= 10;
	} while (v != 0);
	while ((n > 0) && (p + 1 < end)) {
		*p++ = tmp[--n];
	}
	return p;
}

static int port_netsock_last_error(void)
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

static int port_netsock_would_block(int err)
{
#ifdef _WIN32
	return err == WSAEWOULDBLOCK;
#else
	return (err == EAGAIN) || (err == EWOULDBLOCK);
#endif
}

static int port_netsock_init(void)
{
#ifdef _WIN32
	static int inited = 0;
	if (!inited) {
		WSADATA wsa;
		int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (rc != 0) {
			port_log("SSB64 NetSock: WSAStartup failed (%d)\n", rc);
			return 0;
		}
		inited = 1;
	}
#endif
	return 1;
}

static void port_netsock_to_sockaddr(const PortNetAddr *addr, struct sockaddr_in *out)
{
	memset(out, 0, sizeof(*out));
	out->sin_family = AF_INET;
	out->sin_port = htons(addr->port);
	out->sin_addr.s_addr = htonl(addr->ip);
}

static void port_netsock_from_sockaddr(const struct sockaddr_in *in, PortNetAddr *out)
{
	out->ip = ntohl(in->sin_addr.s_addr);
	out->port = ntohs(in->sin_port);
}

int port_netsock_parse_addr(const char *text, PortNetAddr *out)
{
	const char *colon = NULL;
	const char *p;
	char host[64];
	size_t host_len;
	long port;

	if ((text == NULL) || (out == NULL)) {
		return 0;
	}
	for (p = text; *p != '\0'; p++) {
		if (*p == ':') colon = p;
	}
	if ((colon == NULL) || (colon == text) || (colon[1] == '\0')) {
		return 0;
	}
	host_len = (size_t)(colon - text);
	if (host_len >= sizeof(host)) {
		return 0;
	}
	memcpy(host, text, host_len);
	host[host_len] = '\0';
	port = strtol(colon + 1, NULL, 10);
	if ((port <= 0) || (port > 65535)) {
		return 0;
	}
	out->port = (uint16_t)port;
	if ((strcmp(host, "*") == 0) || (strcmp(host, "0.0.0.0") == 0)) {
		out->ip = 0;
		return 1;
	}
	{
		struct in_addr a;
		if (inet_pton(AF_INET, host, &a) != 1) {
			return 0;
		}
		out->ip = ntohl(a.s_addr);
	}
	return 1;
}

void port_netsock_format_addr(const PortNetAddr *addr, char *buf, size_t cap)
{
	char *p = buf;
	char *end = buf + cap;
	int i;

	if (cap == 0) {
		return;
	}
	for (i = 3; i >= 0; i--) {
		p = port_netsock_put_u32(p, end, (addr->ip >> (i * 8)) & 0xFF);
		if (p + 1 < end) {
			*p++ = (i > 0) ? '.' : ':';
		}
	}
	p = port_netsock_put_u32(p, end, addr->port);
	*p = '\0';
}

intptr_t port_netsock_open_udp(const PortNetAddr *bind_addr)
{
	struct sockaddr_in sa;
	port_sock_t s;
	int reuse = 1;

	if (!port_netsock_init()) {
		return -1;
	}
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == PORT_SOCK_INVALID) {
		port_log("SSB64 NetSock: socket failed (%d)\n", port_netsock_last_error());
		return -1;
	}
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
	port_netsock_to_sockaddr(bind_addr, &sa);
	if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		port_log("SSB64 NetSock: bind failed (%d)\n", port_netsock_last_error());
		port_netsock_close((intptr_t)s);
		return -1;
	}
#ifdef _WIN32
	{
		u_long nonblock = 1;
		if (ioctlsocket(s, FIONBIO, &nonblock) != 0) {
			port_log("SSB64 NetSock: nonblocking setup failed (%d)\n", port_netsock_last_error());
			port_netsock_close((intptr_t)s);
			return -1;
		}
	}
#else
	{
		int flags = fcntl(s, F_GETFL, 0);
		if ((flags < 0) || (fcntl(s, F_SETFL, flags | O_NONBLOCK) != 0)) {
			port_log("SSB64 NetSock: nonblocking setup failed (%d)\n", port_netsock_last_error());
			port_netsock_close((intptr_t)s);
			return -1;
		}
	}
#endif
	return (intptr_t)s;
}

void port_netsock_close(intptr_t sock)
{
	if (sock < 0) {
		return;
	}
#ifdef _WIN32
	closesocket((SOCKET)sock);
#else
	close((int)sock);
#endif
}

int port_netsock_sendto(intptr_t sock, const void *buf, int len, const PortNetAddr *to)
{
	struct sockaddr_in sa;
	int sent;

	if (sock < 0) {
		return -1;
	}
	port_netsock_to_sockaddr(to, &sa);
	sent = (int)sendto((port_sock_t)sock, (const char *)buf, len, 0, (struct sockaddr *)&sa, sizeof(sa));
	return (sent < 0) ? -1 : sent;
}

int port_netsock_recvfrom(intptr_t sock, void *buf, int cap, PortNetAddr *from)
{
	struct sockaddr_in sa;
#ifdef _WIN32
	int sa_len = sizeof(sa);
#else
	socklen_t sa_len = sizeof(sa);
#endif
	int got;

	if (sock < 0) {
		return -1;
	}
	got = (int)recvfrom((port_sock_t)sock, (char *)buf, cap, 0, (struct sockaddr *)&sa, &sa_len);
	if (got < 0) {
		return port_netsock_would_block(port_netsock_last_error()) ? 0 : -1;
	}
	if (from != NULL) {
		port_netsock_from_sockaddr(&sa, from);
	}
	return got;
}

void port_netsock_sleep_us(uint32_t usecs)
{
#ifdef _WIN32
	Sleep((usecs + 999) / 1000);
#else
	usleep(usecs);
#endif
}

uint32_t port_netsock_now_ms(void)
{
#ifdef _WIN32
	/* Not GetTickCount64: it only moves every 15.6 ms, which is about a
	 * frame of error in every ping sample. */
	static LARGE_INTEGER freq;
	LARGE_INTEGER now;

	if (freq.QuadPart == 0)
	{
		QueryPerformanceFrequency(&freq);
	}
	QueryPerformanceCounter(&now);
	return (uint32_t)((now.QuadPart / freq.QuadPart) * 1000u + ((now.QuadPart % freq.QuadPart) * 1000u) / freq.QuadPart);
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
#endif
}
