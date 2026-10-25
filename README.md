<p align="center">
  <img src="assets/BattleShip.svg" alt="BattleShip" width="640">
</p>

# BattleShip Online

**Super Smash Bros. 64 on PC, online with your friends.** Type your friend's code (like `ADO#1148`), they press Accept, and you are on the character select screen together, with rollback netcode in the style of Slippi.

BattleShip Online is built on top of [**BattleShip**](https://github.com/JRickey/BattleShip) by JRickey, the PC port of Super Smash Bros. (N64) made from the [ssb-decomp-re](https://github.com/VetriTheRetri/ssb-decomp-re) decompilation. Everything BattleShip does still works here; this fork adds online play.

- **Connect by friend code.** Nobody sees or types an IP address, and there is no port forwarding and no server to host.
- **Rollback netcode.** Your own fighter responds after the input delay only (2 frames by default); the network delay is hidden by rollback.
- **Your name in the game.** The name part of your code is drawn over your fighter and on the results screen instead of P1 / P2.
- **Lobbies of up to 4.** Two players fight 1v1 while the others spectate, and the winner stays on.
- **The game's own menus.** Rules, character select, stage select, results and rematch all run in sync for everyone.

> **You need your own Super Smash Bros. (USA) v1.0 ROM.** No Nintendo data (ROM, game code, textures, audio) is in this repository or in the download. If you do not own the game, you cannot play this.

## Quick start (Windows)

1. Download **`BattleShip-Online-win64.zip`** from the [latest release](https://github.com/susicadnan/Battleship-Online/releases/latest).
2. Extract the whole zip somewhere you can write to, such as your Desktop (not Program Files). Keep the files together.
3. Double-click **`BattleShip.exe`**. On the first launch it asks for your ROM: browse to it (or drag it onto the window) and click **Extract**. This takes about a minute and only happens once. You can instead copy your ROM into the folder as `baserom.us.z64` before launching.
4. When Windows Firewall asks, click **Allow** (tick Private and Public). If you clicked Cancel, online play cannot connect: allow `BattleShip.exe` under Windows Security > Firewall > "Allow an app through firewall".

Your ROM must be the USA v1.0 release (`.z64`, `.n64` or `.v64`):

| Version | Game code | SHA-1 | MD5 |
|---------|-----------|-------|-----|
| US (NTSC-U v1.0) | `NALE` | `e2929e10fccc0aa84e5776227e798abc07cedabf` | `f7c52568a31aadf26e14dc2b6416b2ed` |

## Play with a friend

Both of you need **the same build** of the game, so both download the same release zip.

1. Both players open **VS Mode**. The **Online** panel appears at the bottom of the screen.
2. Set **Your name**. It is shown over your fighter. Your code, like `NAME#1234`, appears next to "your code"; the **Copy** button copies it. Send it to your friend (Discord, text, anything).
3. One player types the other's code into **Friend's code** and presses **Play**.
4. The other player sees "*code* wants to play" and presses **Accept**. If you both type each other's code and press Play, it connects without Accept.
5. You land on character select together. Pick fighters and play. Rematch from the results screen.

More friends can join the same lobby the same way (up to 4). Two players fight and the rest spectate; the winner stays on and the next player in line rotates in.

Tips:

- Wired Ethernet beats Wi-Fi. The ping is shown in the match HUD.
- Input delay 2 (the default) hides most of the network delay. Change it under Esc > Settings > Netplay.
- If it says your router blocks direct connections, one of you is on a network that cannot accept direct connections (some mobile hotspots and ISPs). Try a different network.

## Controls (gamepad)

Plug controllers in before launching. Keyboard works too. Press **Esc** for the settings menu.

| Button | Action | Button | Action |
|--------|--------|--------|--------|
| A | Attack | B | Special |
| X / Y | Jump (or push the stick up) | Start | Pause |
| LB | Taunt | LT | Shield |
| RT | Grab | Left stick | Move |
| Right stick | Smash attacks (aerials in the air) | D-pad | Same as the right stick in matches |

Rebind under Esc > Settings > Input Mappings > Controller Configuration. Right-stick attacks and tap jump are per-player toggles under Settings > Control Enhancements. The download starts with everything unlocked and master volume at 10% (Settings > Audio).

## Known limits

- **Everyone needs the same build.** A different build is refused with "has a different version of the game". When a new release comes out, everyone updates.
- **No relay server.** Connections are direct between players. If both players are behind strict NATs (some mobile hotspots, carrier-grade NAT), they may not be able to connect at all.
- **Friend codes use free public services** (public STUN servers and public MQTT brokers, with messages encrypted per code). If all of those are down, codes cannot find each other.
- **Windows download only.** Linux works when built from source (below); macOS and Android builds of the online version are untested.
- **US ROM only** for online play.
- **Ignore the "Updates" section in Settings.** It checks the upstream BattleShip project, and its download would replace this online build with one that has no online play.
- The game log is `%APPDATA%\BattleShip\ssb64.log` and is overwritten on every launch, so copy it right after a session if you want to report a problem.

## Building from source

Clone with submodules (the `decomp` submodule comes from [susicadnan/ssb-decomp-re](https://github.com/susicadnan/ssb-decomp-re)):

```bash
git clone --recursive https://github.com/susicadnan/Battleship-Online.git
cd Battleship-Online
# To play online with people using the release zip, build the same commits:
git checkout online-1.0 && git submodule update --init --recursive
```

Online play only connects games built from **exactly the same commits** (the build id is the port and decomp commit hashes). Building `main` or your own changes gives a different id, which is fine as long as everyone you play with runs the same build.

Put your ROM at the repo root as `baserom.us.z64` (or pick it on first launch).

### Windows (MSVC)

Needs Visual Studio 2022 with the C++ workload, the Windows SDK, CMake and **Python 3.12 with Pillow** (`py -3.12 -m pip install pillow`). Newer Pythons may lack a Pillow build, so point CMake at 3.12 explicitly. From a Developer PowerShell:

```powershell
cmake -S . -B build\us -A x64 -DSSB64_VERSION=us -DPython3_EXECUTABLE="$(py -3.12 -c 'import sys; print(sys.executable)')"
cmake --build build\us --config Release
.\build\us\Release\BattleShip.exe
```

### Linux (clang)

On Ubuntu/Debian:

```bash
sudo apt-get install -y cmake ninja-build zip clang libsdl2-dev libzip-dev libglew-dev \
  nlohmann-json3-dev libtinyxml2-dev libspdlog-dev libgl1-mesa-dev libegl1-mesa-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev python3 python3-pil
# libzip-dev's CMake config expects these tools; stub them if your distro does not ship them
for t in zipcmp zipmerge ziptool; do command -v $t >/dev/null || { sudo touch /usr/bin/$t; sudo chmod +x /usr/bin/$t; }; done

CC=clang CXX=clang++ cmake -S . -B build-us -GNinja -DSSB64_VERSION=us \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-DFMT_CONSTEVAL=
cmake --build build-us -j4
cd build-us && ./BattleShip
```

`-DCMAKE_CXX_FLAGS=-DFMT_CONSTEVAL=` works around a `fmt` consteval error with newer clang. On clang++ 20 (Ubuntu 26.04) the system rapidjson `document.h` also needed a small local fix to compile.

[BUILDING.md](BUILDING.md) has the upstream build notes (JP builds, macOS, GameCube adapter setup, packaging).

## Everything else from BattleShip

Everything below is toggleable in-game from the ESC menu.

### Input enhancements (per-player)

- **Disable Tap Jump**
- **C-Stick Smash**
- **D-Pad to Jump**
- **NRage analog-stick remap** with customizable per-axis ranges

### Gameplay & match options

- **Classic Co-op** — play the originally single-player Classic mode with 2-player local co-op, with an optional friendly-fire toggle
- **Z-Cancel assists** — Auto Z-Cancel and Flash on Failed Z-Cancel (L-cancel practice aids)
- **Competitive ruleset** — one-click tournament rules, plus neutral spawns on Dream Land
- **Disable Stage Hazards**
- **Hitbox view** — visualize hitboxes / hurtboxes / collision
- **Boot straight to the VS character-select screen**, **Skip Results Screen**, **Force CPU Level 9**
- **Unlocks** — unlock everything, or individual characters (Captain Falcon, Jigglypuff, Luigi, Ness) and features (Mushroom Kingdom, Item Switch menu, Sound Test)

### Audio

- **Music shuffler** — randomize stage BGM
- **Music selection screen** — pick the track per stage
- Independent Master / Music / Sound / Voice volume sliders

### Rendering (powered by libultraship / Fast3D)

- Internal resolution scaling, MSAA anti-aliasing, three-point texture filtering
- Full widescreen support, including ultrawide resolutions.
- Renderer backend selection (OpenGL / Metal / Direct3D), VSync, windowed-fullscreen, multi-window
- **Post-process shader support** with an in-app shader-pack downloader

### Textures & mods

- **Hi-res texture packs** on desktop *and* Android (opt-in, read in place straight out of a `.zip`). Download GhostlyDark's pack from [evilgames.eu](https://evilgames.eu/texture-packs/ssb-reloaded.htm#pc).
- **Native C mod support** — write mods in C, compiled at runtime (TinyCC), with hot-reload, function detours, and a documented engine/fighter event catalog. See [`docs/modding.md`](docs/modding.md). *(Desktop only; scripting is disabled on Android.)*

### Controls

- Controller and rumble support powered by **SDL2**, with plug-and-play routing for up to 4 pads
- Native **Raphnet** adapter support up to 4 channels through **hidapi**
- Native **GameCube adapter** support (WUP-028 and compatible clones) over **libusb**, driven directly rather than through SDL — all 4 adapter ports, hotplug, and fully remappable like any other device. Each adapter port feeds the matching player by default and can be reassigned to any player. *(Desktop only. On Windows the adapter must be bound to the WinUSB driver.)*
- Per-controller configuration UI and a bundled `gamecontrollerdb.txt` mapping database

### Platform & quality-of-life

- Runs natively on **macOS (Apple Silicon), Linux, Windows, and Android**
- First-run ROM-extraction wizard and Discord Rich Presence

## Under the hood

The sections below are from the upstream BattleShip README and still describe the port as a whole. The online layer adds `port/netplay/` (friend codes, lobby, transport) and rollback / net-input code in the decomp submodule; see [`docs/rollback_design.md`](docs/rollback_design.md), [`docs/netplay_connect_codes.md`](docs/netplay_connect_codes.md) and [`docs/netplay_groups.md`](docs/netplay_groups.md).

### Architecture

The port has three layers and they are kept deliberately separate:

```
┌──────────────────────────────────────────────────────────────┐
│  decompiled game code  (decomp/src/)                         │
│  Unmodified C produced by the decomp project. Talks to the   │
│  N64 the same way the original ROM did: GBI display lists,   │
│  ALSeqPlayer audio, OS threads, OSContPad input.             │
├──────────────────────────────────────────────────────────────┤
│  port layer            (port/)                               │
│  Modern C++ glue. Translates N64-shaped APIs into LUS calls, │
│  fixes endianness on freshly-loaded data, owns Ship::Context │
│  and the resource factories, and quarantines every change    │
│  the decomp doesn't need to know about.                      │
├──────────────────────────────────────────────────────────────┤
│  libultraship         (libultraship/)                        │
│  PC-native runtime: Fast3D renderer (OpenGL / Metal / D3D),  │
│  SDL2 input, miniaudio output, OTR/O2R resource manager,     │
│  ImGui overlay.                                              │
└──────────────────────────────────────────────────────────────┘
```

### Asset pipeline

`baserom.us.z64` is never read at runtime. At build time, **Torch** walks the ROM with the YAMLs under `yamls/us/` and emits `BattleShip.o2r` — a zip-format archive of typed resources (textures, sequences, sample banks, animations, reloc files). At launch, libultraship's resource manager mounts `BattleShip.o2r` + `f3d.o2r` and the port code requests resources by path. This is the same pipeline used by Ship of Harkinian, Starship, SpaghettiKart, etc.

The relocatable-data files (fighter tables, item tables, effects, sprites) are SSB64-specific and required custom factories on the Torch side and a custom loader (`port/resource/RelocFileFactory.cpp`) on the runtime side.

### Build-time codegen

A small amount of generated code lives outside the source tree (gitignored) and is regenerated on every build from `tools/reloc_data_symbols.us.txt`:

- `include/reloc_data.h` — extern declarations for every relocatable symbol
- `yamls/us/reloc_*.yml` — Torch extraction configs
- `port/resource/RelocFileTable.cpp` — the runtime symbol table

If you ever see "undefined reference to `dFooBarReloc`" you regenerated the table without rebuilding, or vice versa.

---

## Code conventions

### `#ifdef PORT` — what it is and what it isn't

Every meaningful change to a decomp source file is wrapped in `#ifdef PORT` / `#else` / `#endif`. The discipline this enforces:

- **The original decomp code path stays intact and compilable** under the IDO toolchain on a real N64 build. This is non-negotiable — if it ever stops being true, upstreaming improvements back to the decomp project becomes impossible.
- **The PORT branch is allowed to be ugly** — an explicit endian conversion, a struct rewrite, a function shim — as long as the contract it presents to the rest of the file is the same as the N64 branch.
- **Reloc tokens vs. raw pointers**: a field declared `u32` under `#ifdef PORT` where the N64 branch declared `T*` is a *reloc token*, not a raw pointer. Resolving it requires `PORT_RESOLVE(token)`. Assigning a real pointer with `(uintptr_t)ptr` will silently truncate on LP64 — wrap post-load writes in `PORT_REGISTER`.

### Decomp preservation: behavior, not bytes

The repo follows a single principle for changes to `decomp/src/`:

> **Accuracy to game behavior > accuracy to ROM bytes.**

That means IDO idioms that encode original N64 semantics — odd casts, `goto` flow, deliberate temporaries — are load-bearing and stay. But **compiler-compat shims** (warning suppressions, permissive flags, header shortcuts) that mask real bugs on modern LP64 toolchains do *not* survive. The most expensive lesson of the project was that `-Wno-implicit-function-declaration` was silently truncating 64-bit pointer returns to 32-bit `int` in dozens of places — see `docs/bugs/item_arrow_gobj_implicit_int_2026-04-20.md`. The shim is gone; the real declarations are in.

### Naming prefixes

The decomp uses two-letter module prefixes throughout. Knowing them makes the source tree navigable:

| Prefix | Meaning |
|--------|---------|
| `ft`   | Fighter (`ftMario`, `ftKirby`, `ftFox`, …) |
| `it`   | Item (`itAttribute`, `itManager`) |
| `wp`   | Weapon |
| `ef`   | Effect / particle |
| `gm`   | Game mode |
| `gr`   | Stage (ground) |
| `mp`   | Map / collision |
| `mn`   | Menu |
| `sc`   | Scene |
| `sy`   | System (engine internals) |
| `sf`   | Saved-state / save file |
| `db`   | Debug |
| `cm`   | Camera |
| `lb`   | Library (low-level utilities) |
| `obj`  | GObj / DObj / OMObj — game-object wrappers |

Full reference: [`docs/c_conventions.md`](docs/c_conventions.md).

### Endianness handling

The N64 is big-endian; PC targets are little-endian. The port handles this in three layers:

1. **Gross byteswap at load** — `lbRelocLoadAndRelocFile` byteswaps relocatable files word-by-word during load.
2. **Per-struct fixups** — small `portFixupStructU16` / `portFixupStructU8` helpers fix sub-word fields that the gross swap got wrong (e.g., `{u16, u8, u8}` patterns where the two `u8`s end up swapped).
3. **Per-stream walkers** — animation events, spline interpolators, and other variable-length streams are halfswapped at file-bake time and need a per-stream un-halfswap on first access. These live in `port/port_aobj_fixup.{h,cpp}` and friends.

If you find a new struct that reads garbage, the playbook in `docs/n64_reference.md` will tell you which layer it belongs in.

### Bitfield layout

The IDO compiler packs small bitfields into preceding `u16` pad gaps, MSB-first. Modern compilers (Clang, GCC) on LE targets pack LSB-first into the next storage unit. Bitfield structs that travel through file data must be **rewritten under `#ifdef PORT`** to match the IDO physical layout — see `docs/debug_ido_bitfield_layout.md` for the workflow (compile + rabbitizer disasm to verify bit positions before porting).

Patching the *reads* in game code instead of the *layout* is forbidden by team policy; the bug always comes back. See `feedback_struct_rewrite_over_overrides.md` in the project's memory for the long version.

---

## Why the submodules are forks

Both `libultraship` and `torch` are pinned to **personal forks** rather than the upstream Harbour Masters repos, because each needs SSB64-specific changes that don't exist upstream.

### [`libultraship`](https://github.com/JRickey/libultraship/tree/ssb64) — fork of [Kenix3/libultraship](https://github.com/Kenix3/libultraship)

SSB64 drives the RDP differently than the Zelda / Mario 64 / Star Fox 64 titles libultraship was originally built for — in particular around tile masks, `SetTileSize` extents, IA/I4 texture uploads, `gDPSetPrimDepth` for 2D layering, and Metal sampler binding for matanim CCs. Upstreaming the fixes is on the list, but until then the fork carries:

- Clamping `ImportTexture*` upload width/height to the active `SetTileSize` extent (fixes the fighter "black squares" bug and the IA8/I4 stretch bug)
- Honouring `SetTile` mask/maskt in the Import* path
- A real implementation of `gDPSetPrimDepth` (was a `TODO Implement` stub upstream — broke every `G_ZS_PRIM` 2D sprite)
- A 1×1 black fallback texture for sampler slots that would otherwise alias the screen drawable on Metal (fixed the Whispy canopy stripe)
- A no-logging path in `IResource`'s destructor to prevent a shutdown-time crash

### [`torch`](https://github.com/JRickey/Torch/tree/ssb64) — fork of [HarbourMasters/Torch](https://github.com/HarbourMasters/Torch)

Torch is the tool that reads the ROM and emits `BattleShip.o2r`. Upstream supports OoT, MM, SF64, MK64, PM64, etc., but has no knowledge of SSB64's file formats. The fork adds:

- An `SSB64` build flag and game target
- A reloc-file factory for SSB64's relocatable data blobs (fighters, items, effects, sprites)
- `libvpk0` integration for VPK0-compressed segments

Both forks live as submodules so their history stays their own and so upstream changes can be merged in cleanly when/if the fixes are accepted.

---

## Repo layout

```
port/         modern C++ port layer — Ship::Context, resource factories,
              endian fixups, bridges between decomp code and libultraship
                port/hooks/     engine/fighter event system (all platforms)
                port/mods/      native C mod loader + hook backend (funchook)
workspace/    worked example mods (hooktest, playertint, template)
include/      headers (some generated: reloc_data.h)
decomp/       submodule — decompiled SSB64 C source (largely unchanged
              game logic). Major subdirs inside the submodule:
                src/sys/        main loop, DMA, scheduling, audio,
                                controllers, threading
                src/ft/         fighters (ftmario/, ftkirby/, ftfox/, …)
                src/sc/ gm/ gr/ scene / game modes / stage rendering
                src/mn/ it/ ef/ menus / items / effects
                src/relocData/  reloc data sources
libultraship/ submodule — PC-native render / audio / input / resource mgr
torch/        submodule — asset extractor (ROM → BattleShip.o2r)
yamls/us/     Torch YAML extraction configs (some generated)
tools/        Python helpers: reloc stubs, YAML gen, credits encoder
docs/         architecture notes, bug write-ups, debugging guides
debug_tools/  optional disasm / diff utilities (not required for a build)
scripts/      packaging (.dmg / .AppImage / .zip), worktree helper
```

### Further reading

- [`docs/modding.md`](docs/modding.md) — writing native C mods: the event catalog, function detours, and fighter override points
- [`docs/architecture.md`](docs/architecture.md) — project status, ROM info, dependency map, source-tree layout
- [`docs/c_conventions.md`](docs/c_conventions.md) — decomp naming prefixes, IDO idioms to preserve, code style, macros
- [`docs/n64_reference.md`](docs/n64_reference.md) — RDRAM, RSP/RDP, GBI, audio, threading, endianness primer
- [`docs/build_and_tooling.md`](docs/build_and_tooling.md) — CMake details, reloc stub regen, runtime logs, LP64 compat notes
- [`docs/debug_gbi_trace.md`](docs/debug_gbi_trace.md) — capturing GBI traces from the port and a M64P plugin, diffing with `gbi_diff.py`
- [`docs/debug_ido_bitfield_layout.md`](docs/debug_ido_bitfield_layout.md) — verifying ported struct bit positions against IDO output via rabbitizer
- [`docs/bugs/README.md`](docs/bugs/README.md) — index of resolved bugs with per-bug root-cause and fix write-ups (~45 entries)

---

## Contributing

PRs are welcome from anyone. If you're opening a bug report, the most useful things to include are:

- SHA-1 of your `baserom.us.z64`
- OS + architecture (especially macOS ARM64 vs x86_64, since LP64 bitfield layout has bitten us several times — see `docs/bugs/`)
- The contents of `ssb64.log` from the run that reproduces the issue
- A GBI trace if the issue is rendering-related (see `docs/debug_gbi_trace.md`)

## Credits & licensing

- Game code, data, sound, textures, models, and trademarks: **© Nintendo / HAL Laboratory.** Not included in this repository, not redistributed, and not endorsed by them.
- Decompilation: [VetriTheRetri/ssb-decomp-re](https://github.com/VetriTheRetri/ssb-decomp-re) and its contributors. Vendored as the `decomp/` submodule. At the time of writing the upstream project does not publish an explicit license; this repository makes no copyright claim over the decompiled source and refers to the upstream project for any rights, terms, or restrictions on reuse.
- Runtime framework: [libultraship](https://github.com/Kenix3/libultraship) — Copyright (c) 2022 kenix3, MIT-licensed. Originated by the Harbour Masters team (Ship of Harkinian) and now maintained at Kenix3/libultraship. Vendored as the `libultraship/` submodule.
- Asset pipeline: [Torch](https://github.com/HarbourMasters/Torch) — Copyright (c) 2023 Lywx (Harbour Masters), MIT-licensed. Vendored as the `torch/` submodule.
- Menu fonts: [Montserrat](https://github.com/JulietaUla/Montserrat) and [Inconsolata](https://github.com/cyrealtype/Inconsolata), both bundled under the [SIL Open Font License 1.1](https://openfontlicense.org). License texts ship alongside the font files in [`assets/custom/fonts/`](assets/custom/fonts/).
- Controller mappings: [`gamecontrollerdb.txt`](gamecontrollerdb.txt) from the [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) project — bundled with every build and loaded at startup so more controllers work out of the box. Distributed under the zlib license; full text in [`LICENSE`](LICENSE).
- High-resolution texture pack: *SSB Reloaded* by [GhostlyDark](https://github.com/GhostlyDark), distributed separately at [evilgames.eu](https://evilgames.eu/texture-packs/ssb-reloaded.htm#pc). Not bundled with this repository or its builds — the port loads it only if you download it yourself.
- Reference ports the original port learned from and adapted code from: [Starship](https://github.com/HarbourMasters/Starship) (SF64) and [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) (OoT) — both MIT-licensed by The Harbour Masters; see [`LICENSE`](LICENSE) for the per-file attribution.
- Reference ports the original port learned from but did not borrow code from: [SM64 PC Port](https://github.com/sm64-port/sm64-port) (SM64), [SpaghettiKart](https://github.com/HarbourMasters/SpaghettiKart) (MK64).
- Original BattleShip PC port: [JRickey](https://github.com/JRickey) ([JRickey/BattleShip](https://github.com/JRickey/BattleShip)), with help, debugging, and feature suggestions from the contributors in its Discord server. Everything that is not online play in this repository is their work.
- Online play (rollback netcode, friend codes, 4-player lobbies, in-game names): [ado](https://github.com/susicadnan), built on top of BattleShip. Please send online-play bugs here, not to the upstream BattleShip project.

This project is **not affiliated with, endorsed by, or authorized by Nintendo.** It is a personal, non-commercial research and preservation effort. Do not upload ROMs, extracted `.o2r` archives, or any other Nintendo-owned data to issues or pull requests.

This project is **not affiliated with, endorsed by, or authorized by Harbour Masters** either. It uses libultraship (originated by the Harbour Masters team and now maintained at [Kenix3/libultraship](https://github.com/Kenix3/libultraship)) and Torch (the [HarbourMasters/Torch](https://github.com/HarbourMasters/Torch) asset extractor) as upstream dependencies via personal forks, but it is an independent fan effort. Issues, bugs, and support questions about this port should not be directed to the Harbour Masters team.

## License

Source code in this repository (everything outside the `decomp/`, `libultraship/`, and `torch/` submodules — each of which carries its own attribution) is released under the [MIT License](LICENSE) — free to use, modify, and redistribute, with no warranty and no liability. See [`LICENSE`](LICENSE) for the full text.

The MIT grant covers only the port-specific code (the `port/` layer, build scripts, tools, docs). It does **not** extend to:
- Game assets, code, audio, textures, models, or any other content owned by Nintendo / HAL Laboratory — none of which is in this repository.
- The decompilation vendored as the `decomp/` submodule, sourced from [VetriTheRetri/ssb-decomp-re](https://github.com/VetriTheRetri/ssb-decomp-re). The upstream project does not currently publish an explicit license; this repository makes no copyright claim over the decompiled source and incorporates it by reference. Defer to the upstream project and its contributors for any rights or restrictions on reuse.
- Decomp-derived content elsewhere in the tree (vendored or auto-generated from the decomp's symbol/file-name tables): `tools/reloc_data_symbols.us.txt`, `tools/relocFileDescriptions.us.txt`, `include/reloc_data.h`, `port/resource/RelocFileTable.cpp`, and `yamls/us/reloc_*.yml`. These follow the same upstream-deference as the `decomp/` submodule. The generator scripts that produce them (`tools/generate_*.py`) are port-authored and remain MIT.
- The `libultraship` submodule — Copyright (c) 2022 kenix3, MIT-licensed (originated by the Harbour Masters team).
- The `torch` submodule — Copyright (c) 2023 Lywx (Harbour Masters), MIT-licensed.
- The bundled menu fonts under `assets/custom/fonts/`, which are licensed under the SIL Open Font License 1.1 (per-font license files in that directory).
- The bundled controller-mapping database `gamecontrollerdb.txt`, from the [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) project — distributed under the zlib license. Full text and attribution in [`LICENSE`](LICENSE).
