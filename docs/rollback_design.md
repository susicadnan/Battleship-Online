# Rollback netcode — design and Phase 1 (snapshot/restore + SyncTest)

Goal: Slippi-style online play — connect to anyone, inputs applied locally on
the frame they are pressed, the remote player's inputs predicted and corrected
by rolling the simulation back and re-running it when the real inputs arrive.

This document covers the state model (what a "frame of game state" is in this
port and how it is saved and restored), the driver that re-simulates, the
SyncTest that proves it, and the phases that remain. Companion notes:
`docs/netplay_architecture.md` (input layer), `docs/state_trace.md` (the
per-tick gameplay hash that acts as the oracle), `docs/netcode_agent_rules.md`.

## Where the simulation lives

Dolphin/Slippi save-state the whole emulated RAM. This port has no RAM image,
but the game was written for a fixed N64 memory map and that structure
survives in the decomp:

| State | Where it is on PC | How rollback captures it |
|---|---|---|
| Every GObj, fighter, item, weapon, stage object, effect, GObj thread stack (`GObjThreadStack`), graphics heap | `gPortSceneHeap`, one 16 MB `malloc` block, carved by the bump allocator `syMallocSet` (`gSYTaskmanGeneralHeap`) | `memcpy` of the used prefix (`gSYTaskmanGeneralHeap.ptr - gPortSceneHeap`); on restore the abandoned tail is re-zeroed |
| Decomp globals (`gSCManagerBattleState`, RNG seed, `gGCCommonLinks`, fighter manager counters, map collision, menu/interface state, function-scope `static`s…) | `.data`/`.bss` of ~1 000 translation units | Build-time grouping into one named section (below); `memcpy` of the range |
| Suspended GObj *thread* processes (`nGCProcessKindThread`: announcer, countdown, entry focus…) | `PortCoroutine` heap object + `mmap`ed 64 KB stack (`port/coroutine_*.cpp`) | Registry in `port/stubs/n64_stubs.c`; blob = saved register context + stack, restored in place |
| netinput tick / last-published / checksums | `sys/netinput.c` | `SYNetInputState`, saved explicitly (the TU itself is *not* in the section) |
| Port bookkeeping *about* arena bytes: which sprites/bitmaps/vertices/texels are already in native byte order (`lbreloc_byteswap.cpp`), the live reloc-file range registry and extern-file bump cursor (`lbreloc_bridge.cpp`), how far the token table was handed out (`RelocPointerTable.cpp`) | C++ statics in `port/` (outside the state section by construction) | `port/netstate_host.cpp`: one blob per slot. The byte-order sets are journaled while a session is active (a snapshot is a journal mark, restore undoes past it); the others are copied. |

Pointers inside the state keep pointing at the same addresses after a restore
because every object is restored to the address it was saved from; nothing is
relocated.

The last row was found by the first SyncTest run: the port converts reloc file
data to native byte order lazily and in place, remembering per address that it
has done so. Restoring an arena image from before such a conversion while the
"already done" set still said yes made `lbCommonMakeSObjForGObj` read a
big-endian sprite as if it were native and crash on a garbage token. Any new
host-side cache keyed by arena address that the simulation *reads through* has
to be added to `port/netstate_host.cpp`; caches that only affect rendering
(texture uploads, widened display lists) do not, because a re-simulated load
recreates identical bytes at the same address.

### The state section

`port/netstate_section.h` is force-included into every decomp TU except the
ones listed in `SSB64_STATE_EXCLUDE_REGEX` (CMakeLists.txt):

- clang (macOS, Linux, Android): `#pragma clang section bss=… data=…` moves
  every variable declared after the pragma, function-scope statics included.
  Bounds: `section$start$`/`section$end$` on Mach-O, `__start_`/`__stop_` on ELF.
- MSVC: `#pragma bss_seg/data_seg(".ssbst$m")`, bracketed by `.ssbst$a`/`$z`
  sentinels in `port/stubs/netstate_section.c`.
- GCC: `-fdata-sections` on `ssb64_game` plus `port/netstate_section.ld`
  (`INSERT AFTER .bss`) collecting the `ssb64_game.dir` objects.

`syNetStateSectionGetRanges()` returns the resulting range(s) at runtime.

**Excluded TUs** — their state belongs to the host and must not be rewound:
`sys/net*.c` (rollback bookkeeping, transport buffers, replay/trace I/O),
`sys/scheduler.c`, `sys/audio.c`, `sys/main.c`, `sys/video.c`,
`sys/framebuffer.c`, `sys/rdp.c`, `sys/dma.c`, `sys/debug.c`,
`sys/controller.c` (hardware polling), `sys/interp.c` (frame interpolation),
`port/stubs/*` (OS shims: service threads, message queues), `libultra/*`.
A single variable in an excluded TU can opt in with `SYNET_STATE_VAR`
(`port/netstate_optin.h`); `sSYSchedulerTicCount` does, because the VS match
timer reads it (`ifcommon.c`).

`sys/taskman.c` **is** included: `gSYTaskmanGeneralHeap` (the bump pointer)
must rewind with the arena. Its message queues are consistent at the hook
point (drained before `task_update`, nothing posted until the next
`PortPushFrame`), so restoring them is a no-op in practice.

### Why coroutines matter

Fighters, items, weapons, stages and effects all use `nGCProcessKindFunc`
processes — plain function calls, no coroutine. Only a handful of interface
and scene processes are `nGCProcessKindThread` (37 call sites, none in
`ft/`, `it/`, `wp/`, `gr/`, `ef/`). In VS battle that is the announcer,
countdown and entry-focus threads. They still decide simulation-visible
things (when `game_status` flips to GO), so their stacks are snapshotted.

`osStartThread` registers GObj coroutines (thread id ≥ 100); while a rollback
session is active `osDestroyThread` parks instead of freeing (a rolled-back
frame can resurrect a process that has since finished), and
`port_gobj_coroutine_collect()` frees parked ones once no snapshot in the ring
can reference them. `port_gobj_coroutine_restore_set()` destroys coroutines
created after the snapshot being restored.

Win32 fibers cannot be copied, so x64 MSVC builds use `coroutine_win64.asm` +
`coroutine_win64.cpp` (hand-rolled context switch, own VirtualAlloc'd stacks,
TIB stack bounds swapped like fibers do). The fiber backend remains for other
Windows targets, where rollback refuses to start.

## The driver

`syTaskmanRunTask()` calls `syNetStateRunTick()` instead of
`tfunc->task_update(tfunc)`. With a session active it does, for the tick
`cur = syNetInputGetTick()`:

1. `syNetStateSave(cur)` — S(cur), the present before the tick runs.
2. If netinput requested a rollback to `R < cur` (a confirmed remote input
   contradicted a published prediction): `syNetStateRestore(R)`, then
   `task_update` for `R..cur-1` with `syNetInputSetResimulating(TRUE)`,
   saving S(t+1) after each, which rewrites S(cur).
3. SyncTest (if enabled): hash, roll back `N`, resimulate `N`, hash, compare.
4. `task_update(tfunc)` — the real tick.

During resimulation: local slots replay the frame recorded in
`sSYNetInputHistory` for that tick instead of polling hardware; remote slots
re-resolve (confirmed if arrived, otherwise predicted);
`scVSBattleFuncUpdate()` runs only `ifCommonBattleUpdateInterfaceAll()` (no
packets, trace line or replay I/O). `syNetInputSetRemoteInput()` promotes a
correctly-predicted history entry to confirmed without a rollback.

Snapshots are keyed `tick % slots`; `SSB64_ROLLBACK_SLOTS` (default 10) is
therefore the maximum rollback depth. A request older than the oldest slot is
logged (`too_old`) and dropped — Phase 2 must stall the sender instead.

## SyncTest — the test you can run without a second machine

```
cd build
SSB64_ROLLBACK_SYNCTEST=4 ./BattleShip
```

Every tick the driver hashes the live state (`syNetSyncHashTick`), rolls back
four ticks, re-simulates four ticks and hashes again. Any column that differs
is logged once per tick:

```
SSB64 NetState: SYNCTEST MISMATCH tick=812 depth=4
SSB64 NetState:   column fighters live=9DDCA10B resim=41B3C8F1
```

A mismatch means some state the simulation read is **not** in the snapshot
(or something outside the snapshot is written during the tick): the column
names the subsystem, and `SSB64_SYNC_DUMP_TICK=<n>` narrows it to a fighter
and group. Fix by moving the offending TU/variable into the section (or out of
it), never by widening the hash. The match keeps running from the resimulated
state, so a bug also shows up as visible glitching.

Combine with the existing trace: `SSB64_SYNC_TRACE=a.ssb64h` without SyncTest,
then `SSB64_SYNC_VERIFY=a.ssb64h SSB64_ROLLBACK_SYNCTEST=4` with the same
replay (`SSB64_REPLAY_PLAY`) — a PASS proves rollback is invisible to the
simulation for the whole match. Session totals (`saves`, `rollbacks`,
`resim_ticks`, `max_arena_used`, `synctest_mismatches`) are logged at the end.

## Costs

Per snapshot: arena used (a few MB in VS) + state section (a few MB) + ~64 KB
per live GObj thread. Ten slots ≈ 50–100 MB. A `memcpy` of that size is well
under a millisecond; the ring reuses buffers (`realloc` in 256 KB steps).
Resimulating `N` ticks costs `N` game updates, which at N64 scale is cheap on
any desktop. Dirty-page tracking is a later optimisation if Android needs it.

## Phases

1. **Done:** state section, snapshot/restore, coroutine snapshots,
   resimulation driver, netinput resim mode, SyncTest, host-side bookkeeping
   journal. Verified on Linux (clang): two local instances over the debug P2P
   bootstrap, `SSB64_ROLLBACK_SYNCTEST=4`, 6000 frames, no mismatch.
2. **Transport correctness:** send local inputs tagged with their *own* tick
   (today `netpeer.c` adds `sSYNetPeerInputDelay`, which only works at delay
   0 under rollback); implement input delay as a local queue in netinput so
   both peers apply the same input on the same tick; stall when the remote is
   more than `slots` ticks behind; ack/redundancy already exist. Suppress SFX,
   rumble and Discord presence while `syNetStateCheckResimulating()`.
3. **Windows:** context switch done (`coroutine_win64.asm`); still needed:
   WinSock in `netpeer.c` (currently `!_WIN32`) and a runtime SyncTest pass
   on an MSVC build.
4. **Connectivity ("connect to anyone"):** STUN for reflexive address, UDP
   hole punching, a tiny signaling/matchmaking service exchanging connect
   codes (Slippi-style `ADO#123`), relay fallback for symmetric NATs; lobby
   that runs `MATCH_CONFIG` over the signaling channel instead of env vars.
5. **UX:** Netplay entry in the VS menu, connect-code UI, ping/rollback-frame
   HUD, spectator via input stream, replays reuse `netreplay.c`.
