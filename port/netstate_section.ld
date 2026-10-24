/*
 * netstate_section.ld — GCC/ELF side of the rollback state section.
 *
 * clang and MSVC redirect every writable global of the decomp game code into
 * one named section from a force-included header (port/netstate_section.h).
 * GCC has no such pragma, so CMake compiles ssb64_game with -fdata-sections
 * (one .data.<sym> / .bss.<sym> input section per variable) and this script
 * gathers those input sections, object file by object file, into two output
 * sections the runtime can find: ssb64_state_data and ssb64_state_bss, each
 * bracketed by the same __start_/__stop_ symbols ld would give an orphan
 * section of that name (read by port/stubs/netstate_section.c,
 * syNetStateSectionGetRanges, shared with the clang/ELF build).
 *
 * EXCLUDE_FILE mirrors SSB64_STATE_EXCLUDE_REGEX in CMakeLists.txt: the TUs
 * whose state belongs to the host (netcode bookkeeping and transport, the
 * scheduler, audio, video, hardware input, OS shims) and must not be rewound.
 * Keep the two lists in sync.
 *
 * Ordering: this script is inserted BEFORE the default .data / .bss output
 * sections, so its statements see the input sections first. Everything the
 * default script places earlier than .data (notably .data.rel.ro*, which is
 * read-only after relocation and must never be memcpy'd over) is already
 * claimed by then. SYNET_HOST_VAR variables of included TUs live in
 * .data.ssb64_host and are claimed here first so the wildcard cannot take
 * them; SYNET_STATE_VAR variables of excluded TUs live in
 * .data.ssb64_state_optin and are pulled in explicitly.
 */

SECTIONS
{
	.ssb64_host_data :
	{
		*(.data.ssb64_host)
	}

	ssb64_state_data :
	{
		/* ld only synthesises __start_/__stop_ for orphan sections; a section
		 * the script defines must provide them itself. */
		PROVIDE_HIDDEN(__start_ssb64_state_data = .);
		*(.data.ssb64_state_optin)
		EXCLUDE_FILE(*decomp/src/sys/net*.c.o
		             *decomp/src/sys/scheduler.c.o
		             *decomp/src/sys/audio.c.o
		             *decomp/src/sys/main.c.o
		             *decomp/src/sys/video.c.o
		             *decomp/src/sys/framebuffer.c.o
		             *decomp/src/sys/rdp.c.o
		             *decomp/src/sys/dma.c.o
		             *decomp/src/sys/debug.c.o
		             *decomp/src/sys/controller.c.o
		             *decomp/src/sys/interp.c.o
		             *port/stubs/*.o
		             *decomp/src/libultra/*.o)
			*ssb64_game.dir/*.o(.data .data.*)
		PROVIDE_HIDDEN(__stop_ssb64_state_data = .);
	}
}
INSERT BEFORE .data;

SECTIONS
{
	ssb64_state_bss :
	{
		PROVIDE_HIDDEN(__start_ssb64_state_bss = .);
		EXCLUDE_FILE(*decomp/src/sys/net*.c.o
		             *decomp/src/sys/scheduler.c.o
		             *decomp/src/sys/audio.c.o
		             *decomp/src/sys/main.c.o
		             *decomp/src/sys/video.c.o
		             *decomp/src/sys/framebuffer.c.o
		             *decomp/src/sys/rdp.c.o
		             *decomp/src/sys/dma.c.o
		             *decomp/src/sys/debug.c.o
		             *decomp/src/sys/controller.c.o
		             *decomp/src/sys/interp.c.o
		             *port/stubs/*.o
		             *decomp/src/libultra/*.o)
			*ssb64_game.dir/*.o(.bss .bss.* COMMON)
		PROVIDE_HIDDEN(__stop_ssb64_state_bss = .);
	}
}
INSERT BEFORE .bss;
