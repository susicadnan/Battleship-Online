# Netplay: remote inputs waited a tick in the socket (2026-10-25)

**Symptom.** Two players 15 minutes apart saw 90 ms ping in the match HUD
and the game felt less responsive than the ping suggested it should.

**Cause.** Within a real tick the order was: `syNetStateRunTick` (stall
check, rollback if one is pending) → controller read → whole scene update →
the VS battle's own update, which is where `syNetPeerUpdateBattleGate`
drained the socket. Packets that arrived while the previous frame was being
drawn and paced (most of the 16.7 ms) sat in the socket until *after* the
next tick had already simulated with a prediction, and the rollback that
corrected it ran one tick later still. Every remote input therefore counted
as about one frame later than it arrived: more predicted frames, more and
deeper rollbacks.

**Fix (decomp `netstate.c` / `netpeer.c`).** `syNetPeerReceiveBeforeTick()`
drains the socket at the top of `syNetStateRunTick`, before the stall check,
so whatever arrived during the last frame counts for this one. The battle
gate still receives as before. Inputs are only recorded on receive; rounds
are still applied by `syNetPeerServiceRound` in the gate, so ordering of
round changes is unchanged.

Measured headless (two Linux instances, fake input, a UDP delay proxy,
input delay 2, 2280 ticks, NetSync hashes identical on both sides):

| one-way delay | before: late inputs / resimulated ticks | after |
|---|---|---|
| 35 ms (+0-4 ms jitter) | 2247 + 2279 / 169 + 225 | 1732 + 822 / 87 + 47 |
| 20 ms (+0-4 ms jitter) | 1154 + 1457 / 58 + 77 | 7 + 575 / 1 + 27 |

**Also in this change.**

- Windows `port_netsock_now_ms` used `GetTickCount64`, which only moves
  every 15.6 ms (about a frame of error in each ping sample). Now
  `QueryPerformanceCounter`.
- The periodic `SSB64 NetPeer:` log line carries `ping`, `delay`,
  `rollbacks`, `resim`, `max_depth`, `stalls`, `skips`, and the lobby logs
  whether the peer was reached over the `internet`, `home network`,
  `carrier NAT` or `loopback` (never the address), so a player's log is
  enough to tell a bad route from a bad game loop.

**What the HUD ping is.** Round trip of a PING/PONG pair at frame
granularity: the PONG is sent when the peer next polls its socket and read
when we next poll ours, so the number is the network round trip plus about
one frame (measured: 70 ms line → 82-86 ms HUD). It is not the input lag you
feel: your own fighter always responds after exactly the input delay
(2 frames = 33 ms); ping beyond that is hidden by rollback.
