# Netplay: timed matches ended on a different tick on each machine

**Status:** FIXED (decomp `src/if/ifcommon.c`, commit 9fe384743 on `lobby-4p`)

## Symptom

In a rollback netplay session with a time limit, one machine went to the
results screen while the other stayed in the battle. The one still in the
battle stalled ("no input from the peer for 600 frames") because its peer
had stopped sending battle input. With spectators it was clearer still: a
spectator stalls more often than the players, and it ran out of time
hundreds of ticks earlier than they did.

## Cause

`ifCommonTimerFuncRun` advances the match clock by
`sySchedulerGetTicCount()`, the vertical-retrace count. That is wall time,
not simulation time. On the N64 that is how a lag frame still costs a
sixtieth of a second. Under netplay, frames where netstate holds a tick
(waiting for input, or time sync) still bump the retrace count, so the next
simulated tick takes off two or more tics. Each machine stalls at
different moments, so each reaches 0:00 on a different tick. The fighters
stay identical (the per-fighter checksum never flagged it); only
`time_remain` drifts.

`sSYSchedulerTicCount` is in the rollback state section, so rollback
restores it. That only made the drift consistent within a machine, not
across machines.

## Fix

During a netstate session the timer takes one tic per run
(`time_update = sIFCommonTimerStamp + 1`). The timer runs as a GObj
`func_run` inside the scene update, so it advances on every live and
re-simulated tick and never on a stalled or time-sync frame. Outside
netplay nothing changes.

Verified: four headless instances (2 players + 2 spectators) ran four
one-minute matches back to back. Every machine ended each one at tick 4085,
and all 152 sync checkpoints agreed.

Audit hook: anything in the simulation that reads the retrace counter or a
wall clock diverges under netplay even when inputs and snapshots are exact.
