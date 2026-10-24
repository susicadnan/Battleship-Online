# Tap-jump setting read from the local config for every fighter (2026-10-24)

**Status:** FIXED (decomp `netinput.c`, `ftcommonkneebend.c`, `ftcommonjumpaerial.c`; port `TapJump.cpp`)

## Symptom

Latent netplay desync: "Disable Tap Jump (Pn)" is a per-port CVar, and the
fighter code asked `port_enhancement_tap_jump_disabled(fp->player)` for every
fighter. Each machine therefore applied *its own* config to *every* slot. Two
peers with different settings (or the new default ON vs an old saved OFF) would
disagree on whether stick-up starts a jump for the same input, and diverge.

It also applied to CPU fighters, which jump by pushing the stick up, so
turning the option on for a port a CPU occupied stopped that CPU from jumping.

## Fix

- The local player's preference rides in an unused N64 pad bit (`0x0080`,
  `SYNETINPUT_FLAG_TAP_JUMP_OFF`) of the frames built by
  `syNetInputMakeLocalFrame()`, so it travels with the input to the peer, into
  rollback history and into replays.
- `syNetInputPublishFrame()` strips the bit before the buttons reach
  `gSYControllerDevices` (no game code ever sees it) and stores it in
  `sSYNetInputTapJumpOff[player]`, rewritten every tick including re-simulated
  ones.
- `syNetInputCheckTapJumpOff(player)` returns that flag in VS battles (which
  always read through `syNetInputFuncRead()`, online or not) and the local CVar
  elsewhere (1P modes, training).
- The three call sites only apply it to `nFTPlayerKindMan`.
- `SSB64_NETINPUT_FAKE=1` sets the bit on odd slots, so SyncTest / headless
  netplay covers the trip through the peer.

## Audit hook

Anything the simulation reads from local config (CVars, saved settings) is a
desync unless every peer is guaranteed the same value. Per-player preferences
belong in that player's input stream; match-wide ones in the synced match
settings. Input remaps that run before the frame is built (C-stick / D-pad
smash, D-pad jump, analog remap, the libultraship button mapping) are safe:
peers only ever see the result.
