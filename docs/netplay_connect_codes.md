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
- In quick-match mode (`gNetplay.QuickMatch=1`) fighters and stage are
  picked on the Netplay page and only the host's stage pick counts; stocks
  and time limit are the only rules, items are off. The default mode uses
  the game's own menus instead (see below).

## Rematch

When a game of a lobby match ends normally, `syNetPeerStopVSSession` keeps
the punched socket and marks the session post-match; the Netplay page
shows Rematch / Leave. Each side that presses Rematch sends a `REMATCH`
control packet (ping-sized, carrying its fighter pick) every half second;
when both have, the host re-runs the match config exchange with a fresh
seed and both drop back into the battle. Leave closes the socket; a new
opponent goes through the server again. `SSB64_LOBBY_AUTO_REMATCH=1` asks
for one automatically a second after each game; with `SSB64_LOBBY_TIME=1`
(a one-minute time match) that is the headless end-to-end test, and it
passes: two games, identical fighter hashes in both.

## Disconnects

`sys/netstate.c` stalls when it would have to predict deeper than the
snapshot ring. Ten seconds of consecutive stall (no input from the peer at
all) means they are gone: `syNetPeerAbortMatch` stops the session, the
lobby shows "Match ended: connection lost", and `portSCManagerRequestResetTo`
drops the player back into the VS menu.

## Hosting the rendezvous server

See `docs/netplay_hosting.md`.

## Playing through the game's own menus (default)

Unless `gNetplay.QuickMatch` is on, connecting does not start a battle: it
puts both players into the game's VS menu with **both controllers live**,
and from there they pick rules, fighters and the stage exactly as on a
couch. Pieces:

- `decomp/src/sys/netmenu.c` runs the menus in **lockstep**. While a session
  is active `sys/taskman.c` calls `syNetMenuFuncRead` instead of the scene's
  controller function. Each tick it polls the hardware once, queues the
  local frame for tick + delay, sends it, and only when the peer's frame for
  this tick has arrived publishes both into `gSYControllerDevices` (host =
  port 1, client = port 2, ports 3 and 4 empty) and lets the scene update
  run; otherwise the same frame is drawn again. Ten seconds without the
  peer's input ends the session (`syNetPeerAbortMatch`). Input packets are
  the battle's (`netpeer.c`), so the delay and the HUD ping work the same.
- The lobby's `MATCH_CONFIG` carries the host's VS rules
  (`gSCManagerTransferBattleState`) and its unlock/fighter masks, so both
  sides enter the VS menu from the same state and see the same roster (the
  client's masks are put back when the session ends). Both seed the RNG
  with the match seed, then `portSCManagerRequestResetTo(nSCKindVSMode)`;
  lockstep ticks start with the first controller read in that scene.
- When the menus reach `nSCKindVSBattle`, `syNetPeerStartVSSession` stops
  the lockstep, moves to a new session id (base + phase, so late menu
  packets can never be taken for battle input) and runs the blocking
  `MATCH_CONFIG`/`READY`/`START` exchange before any fighter is built: the
  host sends the battle state its menus produced, the client overwrites its
  own with it. From there it is the rollback battle of
  `docs/rollback_design.md`.
- When the battle ends, `syNetPeerStopVSSession` repeats the last input
  packet a few times (nothing retransmits once the battle is gone), moves to
  the next phase and restarts the lockstep, so the results screen and the
  next character select are shared too. There is no separate rematch flow
  in this mode: the players just play again.
- `port/netplay/ingame_ui.cpp` draws the **Online** panel over the mode
  select and VS menu: your code and UDP port, a friend's `ip:port` with
  Host/Join (and a connect-code field when a server is set), the input
  delay, the status line, and Leave while connected.

Why lockstep and not rollback here: menus are not in the state section and
run GObj threads with frame-timed waits; a rollback could not restore them.
The cost is one network round trip of latency on menu input, which the
input delay mostly hides.

Headless check: `SSB64_NETMENU_BOT=1` makes the local player press a fixed
A / stick up / A / START pattern that walks the VS menu and the CSS into a
battle; with `SSB64_LOBBY_DIRECT` on two instances, both logs should show
the same `entering scene` sequence and identical `figh=` hashes during the
battle.

Things that had to be made deterministic for this to hold, and are worth
knowing when touching menu code: `syUtilsRandTime*` (wall clock on the
console) draws from the seeded RNG while a session is active, and the RNG
is re-seeded on the first shared tick of every menu phase; the wait for the
peer's input is a coroutine yield inside the controller read, because
skipping the update while still drawing ran the menus' display callbacks
(which move the CSS tokens) a different number of times on each side; the
battle state the host sends keeps `is_reset_players` TRUE only at the start
of a session. A match does not end on exactly the same tick on both
machines (the GAME SET sequence), which is why sudden death restarts the
tick space and handshake like a new battle.

Known gaps: port enhancements that change menu behaviour (music select,
CPU level 9, boot-to-CSS) must be set the same on both sides or the menus
can diverge; the battle still uses the host's state, but one side could end
up on a different screen. A divergence that keeps one side out of the
battle shows as "the other player did not start the match".

## In-match HUD

`port/netplay/overlay.cpp` draws a one-line window in the top left while a
netpeer session is active: ping (PING/PONG control packets every 30 frames,
smoothed), input delay, rollbacks per second and the deepest rollback so
far, how many ticks are currently running on prediction, and the frame
advantage when one side is ahead. `gNetplay.Overlay` (Settings → Netplay)
turns it off.
