/**
 * netstate_optin.h — per-variable overrides of the rollback state section
 * (see netstate_section.h). Safe to include anywhere; no TU-wide pragma.
 *
 *   SYNET_STATE_VAR  place one variable of an otherwise excluded TU into
 *                    the state section (it is simulation state).
 *   SYNET_HOST_VAR   keep one variable of an included TU OUT of the state
 *                    section (it is host state: pacing counters, timing).
 *                    Example: taskman.c must be in the section for its bump
 *                    allocator, but dSYTaskmanUpdateCount is what the game's
 *                    own hang detector (debug thread 8) and the framebuffer
 *                    pacing watch; rewinding it froze the counter under
 *                    SyncTest and the hang screen stole the controller.
 *
 * Variables given an explicit section must be defined with an initializer
 * on every toolchain (zero is fine).
 */
#pragma once

#if !defined(PORT_ROLLBACK_STATE_SECTION)
#define SYNET_STATE_VAR
#define SYNET_HOST_VAR
#elif defined(__clang__) && defined(__APPLE__)
#define SYNET_STATE_VAR __attribute__((section("__DATA,__ssb64stdat")))
#define SYNET_HOST_VAR __attribute__((section("__DATA,__ssb64host")))
#elif defined(__clang__)
#define SYNET_STATE_VAR __attribute__((section("ssb64_state_data")))
#define SYNET_HOST_VAR __attribute__((section("ssb64_host_data")))
#elif defined(_MSC_VER)
#pragma section(".ssbst$m", read, write)
#pragma section(".ssbhost", read, write)
#define SYNET_STATE_VAR __declspec(allocate(".ssbst$m"))
#define SYNET_HOST_VAR __declspec(allocate(".ssbhost"))
#elif defined(__GNUC__)
#define SYNET_STATE_VAR __attribute__((section(".data.ssb64_state_optin")))
#define SYNET_HOST_VAR __attribute__((section(".data.ssb64_host")))
#else
#define SYNET_STATE_VAR
#define SYNET_HOST_VAR
#endif
