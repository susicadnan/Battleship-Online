/**
 * netstate_section.c — resolve the [start, end) range of the rollback state
 * section that port/netstate_section.h groups the decomp globals into.
 *
 * Lives in port/stubs so it is compiled into ssb64_game WITHOUT the forced
 * include (the sentinels must not themselves land inside the range on MSVC).
 */
#include <stddef.h>

#if defined(__clang__) && defined(__APPLE__)

extern char sSYNetStateBssStart[] __asm("section$start$__DATA$__ssb64stbss");
extern char sSYNetStateBssEnd[] __asm("section$end$__DATA$__ssb64stbss");
extern char sSYNetStateDataStart[] __asm("section$start$__DATA$__ssb64stdat");
extern char sSYNetStateDataEnd[] __asm("section$end$__DATA$__ssb64stdat");

int syNetStateSectionGetRanges(void **starts, size_t *sizes, int max_ranges)
{
	int n = 0;
	if (n < max_ranges)
	{
		starts[n] = sSYNetStateDataStart;
		sizes[n] = (size_t)(sSYNetStateDataEnd - sSYNetStateDataStart);
		n++;
	}
	if (n < max_ranges)
	{
		starts[n] = sSYNetStateBssStart;
		sizes[n] = (size_t)(sSYNetStateBssEnd - sSYNetStateBssStart);
		n++;
	}
	return n;
}

#elif defined(__clang__)

/* ELF: the linker synthesises __start_/__stop_ for C-identifier section names
 * when something references them. Weak so a build that ends up with an empty
 * section still links. */
extern char __start_ssb64_state_data[] __attribute__((weak));
extern char __stop_ssb64_state_data[] __attribute__((weak));
extern char __start_ssb64_state_bss[] __attribute__((weak));
extern char __stop_ssb64_state_bss[] __attribute__((weak));

int syNetStateSectionGetRanges(void **starts, size_t *sizes, int max_ranges)
{
	int n = 0;
	if ((n < max_ranges) && (__start_ssb64_state_data != NULL))
	{
		starts[n] = __start_ssb64_state_data;
		sizes[n] = (size_t)(__stop_ssb64_state_data - __start_ssb64_state_data);
		n++;
	}
	if ((n < max_ranges) && (__start_ssb64_state_bss != NULL))
	{
		starts[n] = __start_ssb64_state_bss;
		sizes[n] = (size_t)(__stop_ssb64_state_bss - __start_ssb64_state_bss);
		n++;
	}
	return n;
}

#elif defined(_MSC_VER)

/* PE groups .ssbst$a < .ssbst$m < .ssbst$z into one .ssbst section, so the
 * sentinels bracket every variable the pragmas placed in $m. */
#pragma section(".ssbst$a", read, write)
#pragma section(".ssbst$z", read, write)
__declspec(allocate(".ssbst$a")) static char sSYNetStateSectionStart[16] = {1};
__declspec(allocate(".ssbst$z")) static char sSYNetStateSectionEnd[16] = {1};

int syNetStateSectionGetRanges(void **starts, size_t *sizes, int max_ranges)
{
	if (max_ranges < 1)
	{
		return 0;
	}
	starts[0] = sSYNetStateSectionStart + sizeof(sSYNetStateSectionStart);
	sizes[0] = (size_t)((char *)sSYNetStateSectionEnd - (char *)starts[0]);
	return 1;
}

#elif defined(__GNUC__)

/* GCC/ELF: symbols defined by port/netstate_section.ld. */
extern char __ssb64_state_start[] __attribute__((weak));
extern char __ssb64_state_end[] __attribute__((weak));

int syNetStateSectionGetRanges(void **starts, size_t *sizes, int max_ranges)
{
	if ((max_ranges < 1) || (__ssb64_state_start == NULL))
	{
		return 0;
	}
	starts[0] = __ssb64_state_start;
	sizes[0] = (size_t)(__ssb64_state_end - __ssb64_state_start);
	return 1;
}

#else

int syNetStateSectionGetRanges(void **starts, size_t *sizes, int max_ranges)
{
	(void)starts;
	(void)sizes;
	(void)max_ranges;
	return 0;
}

#endif
