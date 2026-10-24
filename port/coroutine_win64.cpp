/**
 * coroutine_win64.cpp — Windows x86-64 coroutine backend on a hand-rolled
 * context switch (port/coroutine_win64.asm).
 *
 * Replaces the Win32 fiber backend (coroutine_win32.cpp) on x64 MSVC builds.
 * Fibers are opaque handles whose stack and registers cannot be copied, and
 * rollback (sys/netstate.c) has to snapshot suspended GObj-thread coroutines.
 * This backend owns the stack memory and the saved register set, so a
 * coroutine is a plain blob: same shape as coroutine_android.cpp.
 *
 * The CoroCtx struct MUST stay in lockstep with the offsets in the .asm; the
 * static_asserts below catch drift at compile time.
 */

#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_X64)

#include "coroutine.h"
#include "port_watchdog.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define MIN_STACK_SIZE 32768

/* MSVC CRT defaults; no game code touches the control registers. */
#define WIN64_DEFAULT_MXCSR 0x1F80u
#define WIN64_DEFAULT_FPUCW 0x027Fu

extern "C" {

struct alignas(16) CoroCtx {
	uint64_t rsp;                 /*   0 */
	uint64_t rbx, rbp, rdi, rsi;  /*   8..32 */
	uint64_t r12, r13, r14, r15;  /*  40..64 */
	uint64_t stack_base;          /*  72  NT_TIB.StackBase */
	uint64_t stack_limit;         /*  80  NT_TIB.StackLimit */
	uint64_t dealloc_stack;       /*  88  TEB.DeallocationStack */
	uint32_t mxcsr;               /*  96 */
	uint16_t fpucw;               /* 100 */
	uint16_t pad0;                /* 102 */
	uint64_t pad1;                /* 104 */
	uint8_t  xmm[10][16];         /* 112..256: xmm6..xmm15 */
};
static_assert(sizeof(CoroCtx) == 272, "CoroCtx size must match coroutine_win64.asm");
static_assert(offsetof(CoroCtx, rsp) == 0, "rsp offset");
static_assert(offsetof(CoroCtx, rbx) == 8, "rbx offset");
static_assert(offsetof(CoroCtx, r12) == 40, "r12 offset");
static_assert(offsetof(CoroCtx, r15) == 64, "r15 offset");
static_assert(offsetof(CoroCtx, stack_base) == 72, "stack_base offset");
static_assert(offsetof(CoroCtx, stack_limit) == 80, "stack_limit offset");
static_assert(offsetof(CoroCtx, dealloc_stack) == 88, "dealloc_stack offset");
static_assert(offsetof(CoroCtx, mxcsr) == 96, "mxcsr offset");
static_assert(offsetof(CoroCtx, fpucw) == 100, "fpucw offset");
static_assert(offsetof(CoroCtx, xmm) == 112, "xmm6 offset");

struct PortCoroutine {
	CoroCtx ctx;             /* this coroutine's saved state */
	CoroCtx caller_ctx;      /* whoever last resumed us */
	void  (*entry)(void *);
	void   *arg;
	int     finished;
	void   *stack_mem;       /* VirtualAlloc base (guard page at the bottom) */
	size_t  stack_total;     /* full reservation incl. guard page */
};

/* port/coroutine_win64.asm */
void port_coroutine_swap(CoroCtx *from, CoroCtx *to);
void port_coroutine_trampoline_win64(void);

static thread_local PortCoroutine *sCurrentCoroutine = nullptr;

static size_t port_page_size(void)
{
	static size_t ps = 0;
	if (ps == 0) {
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		ps = si.dwPageSize ? si.dwPageSize : 4096;
	}
	return ps;
}

/* Called from the asm trampoline with the PortCoroutine* in rcx. */
__declspec(noreturn) void port_coroutine_trampoline_c(PortCoroutine *co)
{
	co->entry(co->arg);
	co->finished = 1;
	sCurrentCoroutine = nullptr;
	/* Permanent yield: nothing on this stack to return to, so swap back to
	 * whoever last resumed us. They see `finished` and never resume again. */
	port_coroutine_swap(&co->ctx, &co->caller_ctx);
	fprintf(stderr, "SSB64: coroutine resumed after finish\n");
	abort();
}

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */

void port_coroutine_init_main(void)
{
	/* No-op: caller_ctx is filled by the first swap out of the main thread. */
}

PortCoroutine *port_coroutine_create(void (*entry)(void *), void *arg, size_t stack_size)
{
	if (stack_size < MIN_STACK_SIZE) {
		stack_size = MIN_STACK_SIZE;
	}
	PortCoroutine *co = (PortCoroutine *)calloc(1, sizeof(PortCoroutine));
	if (co == nullptr) {
		return nullptr;
	}

	/* Own the stack so rollback can copy it. A PAGE_NOACCESS guard page at
	 * the low end turns an overflow into an immediate fault instead of a
	 * silent heap scribble. VirtualAlloc returns page-aligned memory, which
	 * satisfies the ABI's 16-byte rsp alignment. */
	size_t ps = port_page_size();
	stack_size = (stack_size + ps - 1) & ~(ps - 1);
	co->stack_total = stack_size + ps;
	void *stk = VirtualAlloc(nullptr, co->stack_total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (stk == nullptr) {
		fprintf(stderr, "SSB64: VirtualAlloc for coroutine stack failed (error %lu, size=%zu)\n",
		        GetLastError(), co->stack_total);
		free(co);
		return nullptr;
	}
	DWORD old_protect = 0;
	VirtualProtect(stk, ps, PAGE_NOACCESS, &old_protect);

	co->stack_mem = stk;
	co->entry = entry;
	co->arg = arg;
	co->finished = 0;

	/* Initial context. The first swap into this coroutine `ret`s through
	 * ctx.rsp, so the trampoline address sits on the stack and r12 carries
	 * the PortCoroutine* (callee-saved: it survives the swap). After the
	 * ret, rsp == stack_top - 8, i.e. 8 mod 16, like any function entry.
	 * The TIB bounds describe this stack so SEH and __chkstk see a sane
	 * thread. */
	char *stack_top = (char *)co->stack_mem + co->stack_total;
	uint64_t *slot = (uint64_t *)(stack_top - 16);
	slot[0] = (uint64_t)&port_coroutine_trampoline_win64;
	slot[1] = 0;
	co->ctx.rsp = (uint64_t)slot;
	co->ctx.r12 = (uint64_t)co;
	co->ctx.stack_base = (uint64_t)stack_top;
	co->ctx.stack_limit = (uint64_t)co->stack_mem + ps;
	co->ctx.dealloc_stack = (uint64_t)co->stack_mem;
	co->ctx.mxcsr = WIN64_DEFAULT_MXCSR;
	co->ctx.fpucw = WIN64_DEFAULT_FPUCW;

	return co;
}

void port_coroutine_destroy(PortCoroutine *co)
{
	if (co == nullptr) {
		return;
	}
	if (co == sCurrentCoroutine) {
		fprintf(stderr, "SSB64: port_coroutine_destroy on current coroutine\n");
		abort();
	}
	if (co->stack_mem != nullptr) {
		VirtualFree(co->stack_mem, 0, MEM_RELEASE);
	}
	free(co);
}

void port_coroutine_resume(PortCoroutine *co)
{
	if (co == nullptr || co->finished) {
		return;
	}
	/* Save the previous current so nested resumes restore correctly (main
	 * resumes Thread5, Thread5 resumes a GObj coroutine; when the GObj
	 * yields, Thread5 must be current again so it can yield later). */
	PortCoroutine *prev = sCurrentCoroutine;
	sCurrentCoroutine = co;

	port_coroutine_swap(&co->caller_ctx, &co->ctx);

	sCurrentCoroutine = prev;
}

void port_coroutine_yield(void)
{
	PortCoroutine *co = sCurrentCoroutine;
	if (co == nullptr) {
		fprintf(stderr, "SSB64: port_coroutine_yield called outside coroutine\n");
		return;
	}
	port_watchdog_note_yield();
	sCurrentCoroutine = nullptr;
	port_coroutine_swap(&co->ctx, &co->caller_ctx);
	/* Returns here when resumed. */
}

int port_coroutine_is_finished(PortCoroutine *co)
{
	return co ? co->finished : 1;
}

int port_coroutine_in_coroutine(void)
{
	return sCurrentCoroutine != nullptr;
}

/* ---- Rollback snapshot (see coroutine.h) ---------------------------------
 * Blob layout: [CoroCtx][finished][usable stack bytes], the usable stack
 * being everything above the guard page. Same scheme as the POSIX and
 * Android backends. */
struct PortCoroutineBlobHeader {
	CoroCtx ctx;
	int finished;
};

static size_t port_coroutine_usable_stack(PortCoroutine *co)
{
	size_t ps = port_page_size();
	return (co->stack_total > ps) ? (co->stack_total - ps) : 0;
}

int port_coroutine_snapshot_supported(void)
{
	return 1;
}

size_t port_coroutine_snapshot_size(PortCoroutine *co)
{
	if (co == nullptr || co->stack_mem == nullptr) {
		return 0;
	}
	return sizeof(PortCoroutineBlobHeader) + port_coroutine_usable_stack(co);
}

void port_coroutine_snapshot(PortCoroutine *co, void *out_blob)
{
	auto *hdr = static_cast<PortCoroutineBlobHeader *>(out_blob);
	if (co == nullptr || out_blob == nullptr || co->stack_mem == nullptr) {
		return;
	}
	if (co == sCurrentCoroutine) {
		fprintf(stderr, "SSB64: port_coroutine_snapshot on current coroutine\n");
		abort();
	}
	memcpy(&hdr->ctx, &co->ctx, sizeof(hdr->ctx));
	hdr->finished = co->finished;
	memcpy(hdr + 1, static_cast<char *>(co->stack_mem) + port_page_size(), port_coroutine_usable_stack(co));
}

void port_coroutine_restore(PortCoroutine *co, const void *blob)
{
	auto *hdr = static_cast<const PortCoroutineBlobHeader *>(blob);
	if (co == nullptr || blob == nullptr || co->stack_mem == nullptr) {
		return;
	}
	if (co == sCurrentCoroutine) {
		fprintf(stderr, "SSB64: port_coroutine_restore on current coroutine\n");
		abort();
	}
	memcpy(&co->ctx, &hdr->ctx, sizeof(co->ctx));
	co->finished = hdr->finished;
	memcpy(static_cast<char *>(co->stack_mem) + port_page_size(), hdr + 1, port_coroutine_usable_stack(co));
}

} /* extern "C" */

#endif /* _WIN32 && _MSC_VER && _M_X64 */
