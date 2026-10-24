/**
 * netstate_section.h — force every writable global of the decomp game code
 * into one named section so rollback can snapshot/restore it with a memcpy.
 *
 * Force-included (-include / /FI) into the decomp TUs listed by CMake
 * (SSB64_STATE_SOURCES). It is NOT included into the netcode machinery
 * (sys/net*.c), the OS/service shims (port/stubs, sys/scheduler.c, sys/audio.c,
 * sys/main.c, ...) or anything whose state belongs to the host, because
 * restoring those would rewind the transport, the audio pipeline or the
 * coroutine scheduler along with the simulation. The exclusion list is
 * SSB64_STATE_EXCLUDE_REGEX in CMakeLists.txt (mirrored in netstate_section.ld).
 *
 * Mechanism per toolchain (see docs/rollback_design.md):
 *   clang (macOS, Linux, Android): #pragma clang section redirects .data/.bss
 *       of everything declared after this header, function-scope statics
 *       included. Boundaries come from the linker (section$start$/section$end$
 *       on Mach-O, __start_/__stop_ on ELF).
 *   MSVC: #pragma data_seg/bss_seg; .ssbst$a / .ssbst$z sentinels bracket the
 *       grouped section (port/stubs/netstate_section.c).
 *   GCC (ELF): nothing here; CMake adds -fdata-sections and the linker script
 *       port/netstate_section.ld collects the ssb64_game objects' .data/.bss.
 *
 * Opt-in for a single variable in an excluded TU: netstate_optin.h.
 */
#pragma once

#include "netstate_optin.h"

#if defined(__clang__)
#if defined(__APPLE__)
#pragma clang section bss = "__DATA,__ssb64stbss" data = "__DATA,__ssb64stdat"
#else
#pragma clang section bss = "ssb64_state_bss" data = "ssb64_state_data"
#endif
#elif defined(_MSC_VER)
#pragma bss_seg(".ssbst$m")
#pragma data_seg(".ssbst$m")
#endif
