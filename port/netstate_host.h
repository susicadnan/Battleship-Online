/**
 * netstate_host.h — host-side (port) state that rollback rewinds with the
 * simulation.
 *
 * The scene arena and the decomp globals are snapshotted by sys/netstate.c
 * as raw bytes. A few port subsystems keep bookkeeping *about* those bytes
 * outside them, and it has to travel with them or a restored frame is read
 * through stale eyes:
 *
 *   - lbreloc_byteswap.cpp: which arena addresses have already been
 *     converted to native byte order (sprites, bitmaps, vertices, texels).
 *     Journaled while a session is active; a snapshot is a journal mark.
 *   - lbreloc_bridge.cpp: the live reloc-file range registry and the
 *     extern-file bump cursor (a rolled-back tick may have loaded a file).
 *   - resource/RelocPointerTable.cpp: how far the token table was handed
 *     out, so re-simulated loads mint the same tokens instead of leaking.
 *
 * sys/netstate.c owns one blob per snapshot slot and calls these in the
 * same places it copies the arena. Everything here is a no-op cost outside
 * a rollback session.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Bracket a rollback session: begin starts the journals, end drops them. */
void port_netstate_host_begin(void);
void port_netstate_host_end(void);

/* Capture the present into a new heap blob (never NULL). */
void *port_netstate_host_save(void);

/* Rewind to a blob. Returns 0 if the journal no longer reaches that far
 * (the snapshot is older than the retained window). */
int port_netstate_host_restore(const void *blob);

/* Free a blob; NULL is fine. */
void port_netstate_host_free(void *blob);

/* Forget history older than the oldest snapshot still in the ring. */
void port_netstate_host_trim(const void *oldest_blob);

#ifdef __cplusplus
}
#endif
