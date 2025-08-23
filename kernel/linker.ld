/* The bootloader will look at this image and start execution at the symbol
   designated as the entry point. */
ENTRY(_start)

/* Tell where the various sections of the object files will be put in the final
   kernel image. */
SECTIONS
{
	. = 1M;

	.identity : {
		*(.multiboot)
		*(.identity)
		*(.bootstrap_stack)
	}

	.identity_data : ALIGN(4096) {
		*(.identity_data)
	}

	. += 0xC0000000;

	__kernel_virtual_base = .;
	__kernel_load_base = 0x00200000;
	__kernel_start = .;

	/* First put the multiboot header, as it is required to be put very early
	   in the image or the bootloader won't recognize the file format.
	   Next we'll put the .text section. */
	.text ALIGN (4K) : AT (ADDR (.text) - 0xC0000000 + 0x00200000)
	{
		__kernel_text_start = .;
		*(.text)
		__kernel_text_end = .;
	}

	/* Read-only data. */
	.rodata BLOCK(4K) : ALIGN(4K)
	{
		__kernel_rodata_start = .;
		*(.rodata)
		__kernel_rodata_end = .;
	}

	/* Read-write data (initialized) */
	.data BLOCK(4K) : ALIGN(4K)
	{
		__kernel_data_start = .;
		*(.data)
		__kernel_data_end = .;
	}

	/* Read-write data (uninitialized) and stack */
	.bss BLOCK(4K) : ALIGN(4K)
	{
		__kernel_bss_start = .;
		*(COMMON)
		*(.bss)
		__kernel_bss_end = .;
	}

	__kernel_end = .;

	/* The compiler may produce other sections, by default it will put them in
	   a segment with the same name. Simply add stuff here as needed. */
}