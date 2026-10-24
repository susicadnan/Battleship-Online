/**
 * lobby.h — connect-code netplay: find a friend through the rendezvous server
 * (tools/netplay/rendezvous_server.py), punch through both NATs, then hand
 * the socket to sys/netpeer.c for the match.
 *
 * Driven once per frame by port_lobby_tick() from the game loop and by the
 * Netplay page of the in-game menu (port/netplay/lobby_ui.cpp). State lives
 * on the host side; the game simulation knows nothing about it until
 * syNetPeerLobbyConfigure().
 *
 * Settings (CVars, saved with the rest): gNetplay.Name, gNetplay.Tag (the
 * four digits after '#', generated once), gNetplay.Server ("host:port" of
 * the rendezvous server), gNetplay.Port (local UDP port), gNetplay.Fighter,
 * gNetplay.Stage, gNetplay.Delay.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PortLobbyState
{
	PORT_LOBBY_IDLE = 0,
	PORT_LOBBY_REGISTERING,   /* HELLO sent, no WELCOME yet */
	PORT_LOBBY_REGISTERED,    /* server knows our code and public endpoint */
	PORT_LOBBY_WAITING,       /* we named a friend; waiting for them to name us */
	PORT_LOBBY_PUNCHING,      /* both endpoints known, exchanging PUNCH datagrams */
	PORT_LOBBY_BOOTSTRAP,     /* netpeer is exchanging the match config */
	PORT_LOBBY_IN_MATCH,
	PORT_LOBBY_POST_MATCH,    /* game over, connection kept; Rematch or Leave */
	PORT_LOBBY_FAILED,
} PortLobbyState;

/* Once per frame, from the game loop. Cheap when idle. */
void port_lobby_tick(void);

/* "NAME#1234"; the tag is generated and saved on first use. */
const char *port_lobby_code(void);
void port_lobby_set_name(const char *name);

/* Register with the server (if not already) and ask to play friend_code. */
void port_lobby_connect(const char *friend_code);
/* LAN / port-forwarded play without a server: both sides enter the other's
 * ip:port, one as host and one as client. */
void port_lobby_connect_direct(const char *host_port, int is_host);
void port_lobby_cancel(void);

int port_lobby_state(void);
const char *port_lobby_status(void);   /* one line for the UI */

/* From sys/netpeer.c when a match ends abnormally (peer gone, quit). */
void port_lobby_notify_match_over(const char *reason);
/* From sys/netpeer.c when a game ends normally and the connection is kept. */
void port_lobby_notify_post_match(void);

/* Post-match: ask for another game with the same opponent / hang up. */
void port_lobby_request_rematch(void);
int port_lobby_peer_wants_rematch(void);
const char *port_lobby_public_addr(void); /* "" until registered */

#ifdef __cplusplus
}
#endif
