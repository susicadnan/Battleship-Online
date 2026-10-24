# Netplay: connect codes, rendezvous server, lobby

How two players anywhere on the internet end up in the same VS battle with
rollback. Companion to `docs/rollback_design.md` (the simulation side) and
`docs/netplay_architecture.md` (input layer, debug transport).

## Pieces

| Piece | Where | Runs |
|---|---|---|
| Rendezvous server | `tools/netplay/rendezvous_server.py` | On a machine with a public UDP port (a cheap VPS). `python3 rendezvous_server.py --port 7777`. No dependencies. |
| Lobby | `port/netplay/lobby.cpp` | In the game, ticked once per frame from `port/gameloop.cpp`. |
| Netplay menu page | `port/netplay/lobby_ui.cpp` | Settings → Netplay in the ImGui menu. |
| Socket shim | `port/netsock.h`, `port/stubs/netsock.c` | POSIX and WinSock. One non-blocking UDP socket. |
| Match bootstrap | `decomp/src/sys/netpeer.c` (`syNetPeerLobbyConfigure` / `syNetPeerLobbyStep`) | Takes the punched socket, exchanges `MATCH_CONFIG` / `READY` / `START`, then `portSCManagerRequestResetTo(nSCKindVSBattle, nSCKindVSMode)`. |

## Flow

1. Both players open Settings → Netplay. Each has a connect code `NAME#1234`
   (`gNetplay.Name`, plus a tag generated once and saved in `gNetplay.Tag`).
2. Both type the other's code and press Connect. The lobby opens a UDP socket
   (`gNetplay.Port`, first free of 10 consecutive ports), sends `HELLO` to the
   server every second (keepalive, also carries the fighter pick) and `JOIN`
   with the friend's code.
3. When the server has both `JOIN`s it sends each side `PEER <role> <public
   ip:port> <match id> <peer fighter> <peer code>`. Host is the lexically
   smaller code. Both leave the server's table; a rematch registers again.
4. Both send `PUNCH <match id> <code> <fkind> <seen>` to the other's public
   endpoint every 3 frames. The first `PUNCH` that arrives tells us the
   peer's real endpoint (the NAT may have rewritten the port); `seen=1`
   tells the peer its punches reach us. Once both directions are confirmed
   we keep punching 30 more frames, then hand the socket to netpeer.
   If either side has no confirmed path after 5 s it sends `RELAY <match
   id>`; the server answers both with `RELAYED` and from then on forwards
   every game datagram between the two addresses (idle 20 s ends it). Both
   sides point netpeer at the server instead of each other; nothing else
   changes. `SSB64_LOBBY_FORCE_RELAY=1` makes one side never punch, to test
   it.
5. `syNetPeerLobbyConfigure` runs the existing bootstrap per frame: host
   sends `MATCH_CONFIG` (stage, both fighters, seed) until `READY`, then
   `START` 30 times; client answers `READY` until `START`. Then both reset
   into the battle. From there it is the rollback path of
   `docs/rollback_design.md`; the battle's `syNetPeerStartVSSession` finds
   the socket already open.

The same socket is used from the first `HELLO` to the last input packet:
hole punching only works for the port the NAT already has a mapping for.

Direct connect (LAN, port forwarding, or testing) skips the server: both
sides enter the other's `ip:port`, one presses Host and the other Join, and
the flow starts at step 4 with match id `direct`.

## Protocol (server)

Text datagrams, fields separated by single spaces, every line starts with
`BSR1`. Client → server: `HELLO <code> <fkind>`, `JOIN <code> <friend>`,
`LEAVE <code>`. Server → client: `WELCOME <public ip:port>`, `WAITING
<friend>`, `PEER <host|client> <ip:port> <match id> <peer fkind> <peer
code>`, `ERROR <text>`. Codes are case-insensitive, `[A-Z0-9]{1,12}#[0-9]{4}`.
A registration expires 10 s after its last `HELLO`; a code already online
from another address gets `ERROR code in use`.

Client → server when punching fails: `RELAY <match id>`; server → both:
`RELAYED <match id>`, after which non-`BSR1` datagrams from either address
are forwarded to the other.

Peer ↔ peer, lobby phase: `BSR1 PUNCH <match id> <code> <fkind> <seen>`. Anything
that is not a `BSR1` line is ignored by the lobby; anything that is not a
netpeer packet is dropped by netpeer, so the two phases can overlap by a few
frames without harm.

## Headless test

```
python3 tools/netplay/rendezvous_server.py --bind 127.0.0.1 --port 7777 -v &
SSB64_NETINPUT_FAKE=1 SSB64_LOBBY_SERVER=127.0.0.1:7777 SSB64_LOBBY_DELAY=0 \
  SSB64_LOBBY_NAME=ADO SSB64_LOBBY_TAG=1111 SSB64_LOBBY_CONNECT='BOB#2222' SSB64_LOBBY_PORT=7101 SSB64_LOBBY_FIGHTER=1 ./BattleShip &
SSB64_NETINPUT_FAKE=1 SSB64_LOBBY_SERVER=127.0.0.1:7777 SSB64_LOBBY_DELAY=0 \
  SSB64_LOBBY_NAME=BOB SSB64_LOBBY_TAG=2222 SSB64_LOBBY_CONNECT='ADO#1111' SSB64_LOBBY_PORT=7102 SSB64_LOBBY_FIGHTER=9 ./BattleShip
```

(Each instance needs its own `HOME`/profile dir or the logs collide.) Expect
`SSB64 Lobby: Match starting` and `SSB64 NetState: session start` in both
logs, and identical `figh=` values in the `NetSync` lines at the same ticks.
`SSB64_LOBBY_DIRECT=<ip:port>` with `SSB64_LOBBY_HOST=1` on one side tests
direct connect.

## Not there yet

- The relay costs the server the match's bandwidth (~10 KB/s per player)
  and adds a hop of latency; there is no limit on concurrent relays yet.
- Fighters and stage are picked on the Netplay page, not in the CSS. Only
  the host's stage pick counts.
- The socket is closed when the battle ends; a rematch goes through the
  lobby again.
- No ping / rollback-frame display during the match.
