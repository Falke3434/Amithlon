/* Linker script for the NEC AS85EP1 V850E evaluation board
   (CONFIG_V850E_AS85EP1), with kernel in ROM (CONFIG_ROM_KERNEL).  */

MEMORY {
	/* 4MB of flash ROM.  */
	ROM   : ORIGIN = 0,          LENGTH = 0x00400000

	/* 1MB of static RAM.  */
	SRAM  : ORIGIN = 0x00400000, LENGTH = 0x00100000

	/* About 58MB of DRAM.  This can actually be at one of two
	   positions, determined by jumper JP3; we have to use the first
	   position because the second is partially out of processor
	   instruction addressing range (though in the second position
	   there's actually 64MB available).  */
	SDRAM : ORIGIN = 0x00600000, LENGTH = 0x039F8000
}

SECTIONS {
	ROMK_SECTIONS(ROM, SRAM)
}
