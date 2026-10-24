/**
 * netstate_optin.h — SYNET_STATE_VAR: place one variable of an otherwise
 * excluded TU into the rollback state section (see netstate_section.h).
 * Safe to include anywhere; it carries no TU-wide pragma.
 */
#pragma once

#if !defined(PORT_ROLLBACK_STATE_SECTION)
#define SYNET_STATE_VAR
#elif defined(__clang__) && defined(__APPLE__)
#define SYNET_STATE_VAR __attribute__((section("__DATA,__ssb64stdat")))
#elif defined(__clang__)
#define SYNET_STATE_VAR __attribute__((section("ssb64_state_data")))
#elif defined(_MSC_VER)
#pragma section(".ssbst$m", read, write)
#define SYNET_STATE_VAR __declspec(allocate(".ssbst$m"))
#elif defined(__GNUC__)
#define SYNET_STATE_VAR __attribute__((section(".data.ssb64_state_optin")))
#else
#define SYNET_STATE_VAR
#endif
