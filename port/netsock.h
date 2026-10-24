/**
 * netsock.h — the one UDP socket the netcode needs, on POSIX and WinSock.
 *
 * sys/netpeer.c (decomp game code) talks to the peer through this instead of
 * <sys/socket.h> directly so the same code runs on Linux, macOS and Windows.
 * Addresses are plain host-order IPv4 + port; the shim does the sockaddr and
 * byte-order work. Sockets are non-blocking: receive returns 0 when nothing
 * is queued. Implemented in port/stubs/netsock.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortNetAddr
{
	uint32_t ip;    /* host order; 0 = any (for bind) */
	uint16_t port;  /* host order */
} PortNetAddr;

/* "host:port" -> address. "*" and "0.0.0.0" mean any. 1 on success. */
int port_netsock_parse_addr(const char *text, PortNetAddr *out);

/* Dotted-quad "a.b.c.d:port" of an address into buf (at least 24 bytes). */
void port_netsock_format_addr(const PortNetAddr *addr, char *buf, size_t cap);

/* Bound, non-blocking UDP socket; -1 on failure (reason logged). Initialises
 * the platform socket layer on first use. */
intptr_t port_netsock_open_udp(const PortNetAddr *bind_addr);
void port_netsock_close(intptr_t sock);

/* Bytes sent, or -1. */
int port_netsock_sendto(intptr_t sock, const void *buf, int len, const PortNetAddr *to);

/* Bytes received (> 0), 0 if nothing is queued, -1 on a real error. The
 * sender's address is written to *from when non-NULL. */
int port_netsock_recvfrom(intptr_t sock, void *buf, int cap, PortNetAddr *from);

void port_netsock_sleep_us(uint32_t usecs);

/* Monotonic milliseconds, for round-trip timing. */
uint32_t port_netsock_now_ms(void);

#ifdef __cplusplus
}
#endif
