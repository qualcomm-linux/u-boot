ENTRY(CONFIG_PLATFORM_ELFENTRY)
SECTIONS
{
	. = CONFIG_PLATFORM_ELFENTRY;

	.data : {
		*(.data*)
		/*
		 * Pad the LOAD segment out to a 4K boundary. TME's IMGAUTH
		 * checks not just p_align/p_offset but that p_filesz itself
		 * is a multiple of 4K (it hashes/verifies in fixed 4K
		 * blocks) - the raw u-boot.bin blob wrapped in here is not
		 * page-sized on its own.
		 */
		. = ALIGN(0x1000);
	}
}
