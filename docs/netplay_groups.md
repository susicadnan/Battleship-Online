# Netplay groups: up to four people, spectators, winner stays on

A direct-connect session holds up to four people. Two of them play a 1v1
while the others watch the same game live (no controller needed), and
after every match the winner stays on and the next person in line takes
the loser's port, like Smash Ultimate's arena. With "Everyone plays" all
of them play one free-for-all instead.

## Using it

On the mode select or VS Mode screen, the **Online** panel:

- **Host** opens a session on your UDP port (`gNetplay.Port`, 7000 by
  default; it must be reachable: same LAN or a port forward). The panel
  lists who has joined. Pick **1v1, winner stays on** or **Everyone
  plays**, and leave **Dream Land, no items, no hazards** ticked for the
  standard 1v1 set. Press **Start** when everyone is in.
- **Join** with the host's `ip:port`. You wait on the panel until the host
  presses Start.
- Someone who joins after Start gets in at the next character select (as a
  spectator at the back of the line in 1v1 mode).
- In the menus the panel shows who is P1/P2 and who is watching, with "next
  up" for the front of the line. In battle a spectator's HUD says WATCHING.

The host's mode and preset can be changed during the session; they apply
from the next character select.

## How it works

Everything is in `decomp/src/sys/netpeer.c` (plus the lockstep menus in
`netmenu.c` and the spectator rule in `netstate.c`).

**Star through the host.** Each person is a *member*; the host is member 0
and keeps an address per guest. A guest only ever talks to the host. The
host sends its own packets to every guest and forwards each guest's input
packets, unchanged, to everyone else in the round. So guests never need to
reach each other, only the host (which is what direct connect already
required). Input packets name their port; the host only accepts a port's
input from the member who plays it.

**Rounds.** Who plays which port is decided per *round*. The host builds
the roster, then sends each member a `ROUND_CONFIG` (roster with
names, the rules as battle metadata, the host's unlock masks, a seed, the
mode/preset, and which scene to reset to), collects `READY` from everyone,
and sends `ROUND_START`. Every member then applies the round: takes its
port (or none), seeds the RNG, and `portSCManagerRequestResetTo` the
character select (the VS menu for the first round), with the lockstep
menus armed for that scene's next start. A round begins:

- when the host presses Start (first round, into the VS menu);
- when the results screen gives way to the character select (every member
  sees that on the same lockstep tick: the host rotates and lines up the
  next round, guests wait for it);
- when a player drops (see below).

Rounds are also where late joiners get in and quiet members are dropped.
The session id moves on with every phase (round, battle, results), so
packets from an old phase are never taken for the current one; input that
arrives one phase early (someone already on the results screen while we
finish the battle) is kept and fed in when we get there.

**Winner stays on.** The host keeps the two players and a queue. After a
1v1 the results screen's places (`portMNVSResultsGetPlace`) name the
winner; the loser goes to the back of the queue and the front of the queue
takes the loser's port. A tie keeps whoever was already on. New members go
to the back; members who left are removed and their port refilled.

**Spectators.** A spectator has no port, so it sends no input and nobody
waits on it. It receives every player's input through the host and runs
the same battle, but never predicts: `syNetStateRunTick` holds each tick
until every player's confirmed input for it is in (and the first tick
until a few more are, as a jitter buffer). It never rolls back and its
game is the players' game, slightly behind. In the lockstep menus it waits
for the players' inputs like everyone else, but sends none. Its pings keep
it alive at the host.

**Several remote players.** The per-port bookkeeping (newest tick, frame
advantage) is kept for each remote port. Time sync gives way to the player
furthest behind. netinput already handled any number of remote slots;
empty ports read neutral.

**Drops.** A guest leaving sends `BYE`. If a player of the round goes quiet
(ten seconds of stalling in the menus or the battle), the host drops them
and starts a new round with whoever is left, back at the character
select; a spectator who goes quiet is just dropped at the next round. A
guest only gives up if the host itself has gone quiet. When fewer than two
people are left, the session ends.

**Stuck with everyone connected.** If the host's game has not moved for ten
seconds yet every member still answers pings, the members no longer agree
on where they are (say one of them left the battle the others are still in).
The host then starts a new round with the same roster, back at the character
select, so the session recovers instead of hanging.

**Spectators and the start barrier.** The frame that releases the battle's
start barrier runs one battle step on tick 0's input before the tick
advances, and a spectator has to take that same step. So spectators keep the
barrier too. They release it on their own, without sending anything, once
every player's tick 0 has arrived.

**Preset.** With the preset on, the host's battle metadata forces Dream
Land, no stage select and no items, and every member turns stage hazards
off through `port_netplay_set_hazards_off`. Hazards are switched off from
inside the VS battle tick (`scVSBattleFuncUpdate` →
`port_enhancement_stage_hazards_sim_tick`), so live and re-simulated ticks
see the same thing; the usual between-frames path is skipped during a
session.

**Lobby.** `port/netplay/lobby.cpp`: the host's `HOSTING` state answers
each guest's PUNCH and keeps them alive until Start, then hands the socket
and the guest list to netpeer (`syNetPeerHostConfigure`,
`syNetPeerHostAddMember`, `syNetPeerHostStartSession`). After that netpeer
passes any non-game datagram (a late joiner's PUNCH) back to
`port_lobby_host_foreign_datagram`. A guest hands its socket over as soon
as the host answers (`syNetPeerGuestConfigure`) and waits for the first
round. A connect-code match through the rendezvous server is a group of
two.

## Headless test

```
SSB64_LOBBY_HOST=1 SSB64_LOBBY_DIRECT= SSB64_LOBBY_PORT=7100 SSB64_LOBBY_AUTOSTART=4   # host
SSB64_LOBBY_HOST=0 SSB64_LOBBY_DIRECT=127.0.0.1:7100 SSB64_LOBBY_PORT=72xx             # each guest
```

plus `SSB64_NETMENU_BOT=1 SSB64_NETINPUT_FAKE=1 SSB64_NETPLAY_TEST_MINUTES=1`
on all of them (a one-minute timed match), and a separate `XDG_DATA_HOME`
each so the logs (`ssb64.log`) do not collide. `SSB64_LOBBY_GROUP_MODE=1`
for everyone-plays. Check that every member's `NetSync ... figh=` line
agrees at each tick (spectators included), and the `winner stays on` and
`round applied` lines on the host.

## Known limits

- The host relays everything, so guest-to-guest latency in a 4-player
  match is two hops, and the host's upload carries everyone.
- Only the host's address needs to be reachable; the rendezvous server
  path is still two people.
- A player dropping mid-battle ends that battle for everyone (the next
  round starts at the character select without them).
