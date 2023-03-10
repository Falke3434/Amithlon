/* Linker script for the Midas labs RTE-NB85E-CB evaluation board
   (CONFIG_RTE_CB_NB85E), with the Multi debugger ROM monitor .  */

MEMORY {
	/* 1MB of SRAM; we can't use the last 96KB, because it's used by
	   the monitor scratch-RAM.  This memory is mirrored 4 times.  */
	SRAM  : ORIGIN = 0x03C00000, LENGTH = 0x000E8000
	/* Monitor scratch RAM; only the interrupt vectors should go here.  */
	MRAM  : ORIGIN = 0x03CE8000, LENGTH = 0x00018000
	/* 16MB of SDRAM.  */
	SDRAM : ORIGIN = 0x01000000, LENGTH = 0x01000000
}

#ifdef CONFIG_RTE_CB_NB85E_KSRAM
# define KRAM SRAM
#else
# define KRAM SDRAM
#endif

SECTIONS {
	/* We can't use RAMK_KRAM_CONTENTS because that puts the whole
	   kernel in a single ELF segment, and the Multi debugger (which
	   we use to load the kernel) appears to have bizarre problems
	   dealing with it.  */

	.text : {
		__kram_start = . ;
		TEXT_CONTENTS
	} > KRAM

	.data : {
		DATA_CONTENTS
		BSS_CONTENTS
		RAMK_INIT_CONTENTS
		__kram_end = . ;
		BOOTMAP_CONTENTS

		/* The address at which the interrupt vectors are initially
		   loaded by the loader.  We can't load the interrupt vectors
		   directly into their target location, because the monitor
		   ROM for the GHS Multi debugger barfs if we try.
		   Unfortunately, Multi also doesn't deal correctly with ELF
		   sections where the LMA and VMA differ (it just ignores the
		   LMA), so we can't use that feature to work around the
		   problem!  What we do instead is just put the interrupt
		   vectors into a normal section, and have the
		   `mach_early_init' function for Midas boards do the
		   necessary copying and relocation at runtime (this section
		   basically only contains `jr' instructions, so it's not
		   that hard).  */
		. = ALIGN (0x10) ;
		__intv_load_start = . ;
		INTV_CONTENTS
	} > KRAM

	.root ALIGN (4096) : { ROOT_FS_CONTENTS } > SDRAM
}
