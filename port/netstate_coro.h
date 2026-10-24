/**
 * netstate_coro.h — registry of GObj-process coroutines for rollback.
 *
 * GObj "thread" processes (nGCProcessKindThread) run as coroutines created
 * lazily by osStartThread (port/stubs/n64_stubs.c). Their OSThread lives in
 * the scene arena and is rewound with it, but the coroutine object, its
 * register context and its stack live outside the arena. sys/netstate.c uses
 * this registry to snapshot/restore every live GObj coroutine alongside the
 * arena, and to keep destroyed coroutines around (a rolled-back frame may
 * resurrect a process that has since finished).
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortCoroutine PortCoroutine;

typedef struct PortGObjCoroutineRecord
{
	void *thread;          /* OSThread* in the arena that owns it */
	PortCoroutine *co;     /* heap object, stable address while registered */
	unsigned serial;       /* unique per registration: a freed coroutine's address
	                        * is reused by the next calloc, and a snapshot must not
	                        * mistake the newcomer for the one it saved */
	int alive;             /* 0 once osDestroyThread ran (kept while rollback is active) */
	unsigned death_tick;   /* netstate tick at which it died, for collection */

} PortGObjCoroutineRecord;

#define PORT_GOBJ_COROUTINE_MAX 64

/* While active, osDestroyThread parks GObj coroutines instead of freeing
 * them, stamping death_tick from tick_fn(). Turning it off frees every parked
 * coroutine. */
void port_gobj_coroutine_set_rollback_active(int active, unsigned (*tick_fn)(void));

/* Current registry contents (alive and parked). */
int port_gobj_coroutine_count(void);
const PortGObjCoroutineRecord *port_gobj_coroutine_get(int index);

/* Free parked coroutines that died before oldest_tick (no snapshot can still
 * refer to them). */
void port_gobj_coroutine_collect(unsigned oldest_tick);

/* Make the registry match a saved set: coroutines created after the snapshot
 * are destroyed for real; saved ones get their alive flag back. The caller
 * then restores each saved coroutine's blob. */
void port_gobj_coroutine_restore_set(const PortGObjCoroutineRecord *saved, int count);

/* 1 if the registry holds exactly this coroutine (address and serial). */
int port_gobj_coroutine_check(PortCoroutine *co, unsigned serial);

#ifdef __cplusplus
}
#endif
