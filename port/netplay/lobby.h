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
 * gNetplay.Stage, gNetplay.Delay, gNetplay.GroupMode, gNetplay.Preset.
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
	PORT_LOBBY_HOSTING,       /* direct host: taking guests (up to 3) until Start */
	PORT_LOBBY_SEEKING,       /* we entered a friend's code; asking them until they accept */
} PortLobbyState;

/* Once per frame, from the game loop. Cheap when idle. */
void port_lobby_tick(void);

/* "NAME#1234"; the tag is generated and saved on first use. */
const char *port_lobby_code(void);
void port_lobby_set_name(const char *name);

/* Register with the server (if not already) and ask to play friend_code. */
void port_lobby_connect(const char *friend_code);
/* LAN / port-forwarded play without a server. The host opens a session
 * on its UDP port (host_port optional: an address to punch towards, for a
 * host behind a NAT); up to three guests join with the host's ip:port. The
 * host presses Start (port_lobby_host_start) when everyone is in; anyone
 * joining later gets in at the next character select. */
void port_lobby_connect_direct(const char *host_port, int is_host);
void port_lobby_host_start(void);

/* Connect by code, nothing to host and no addresses anyone sees (signal.h).
 * The Online panel calls port_lobby_listen() every frame it is up, which
 * makes us reachable by our code. port_lobby_play_code() asks a friend; they
 * see the request and Accept (or have typed our code too, which counts as
 * both saying yes). Whoever accepts hosts; the endpoints are swapped and the
 * NATs punched behind the scenes, then it is the usual lobby session. */
void port_lobby_listen(void);
void port_lobby_play_code(const char *friend_code);
int port_lobby_request_count(void);              /* people asking to play with us */
const char *port_lobby_request_code(int index);
/* 1 when that request's game is a different build (it can't be accepted). */
int port_lobby_request_other_version(int index);
/* This build's id, as compared on connect (commits of the port and decomp). */
const char *port_lobby_build_id(void);
void port_lobby_request_answer(int index, int accept);
const char *port_lobby_friend_code(void);        /* SEEKING/PUNCHING: who we asked */
int port_lobby_online_service_up(void);
int port_lobby_host_guest_count(void);              /* HOSTING: guests in so far */
const char *port_lobby_host_guest_code(int index);  /* HOSTING: their codes */
void port_lobby_cancel(void);

int port_lobby_state(void);
const char *port_lobby_status(void);   /* one line for the UI */

/* Group sessions (sys/netpeer.c): the host's settings for each round. */
int port_lobby_group_mode(void);  /* 0 = 1v1 winner stays on, 1 = everyone plays */
int port_lobby_preset(void);      /* 1 = Dream Land, no items, no hazards */
void port_lobby_notify_round(void);

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
