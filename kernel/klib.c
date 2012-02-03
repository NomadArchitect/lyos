/*  This file is part of Lyos.

    Lyos is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Lyos is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Lyos.  If not, see <http://www.gnu.org/licenses/>. */

#include "lyos/type.h"
#include "sys/types.h"
#include "lyos/config.h"
#include "stdio.h"
#include "stdarg.h"
#include "unistd.h"
#include "assert.h"
#include "lyos/const.h"
#include "lyos/protect.h"
#include "string.h"
#include "lyos/fs.h"
#include "lyos/proc.h"
#include "lyos/tty.h"
#include "lyos/console.h"
#include "lyos/global.h"
#include "lyos/proto.h"


#include <elf.h>

PUBLIC u16 in_word(u16 port)
{
       u16 val;
       asm volatile("inw %1,%0" : "=a" (val) : "dN" (port));
       return val;
}

PUBLIC void out_word(u16 port, u16 val)
{
       asm volatile("outw %0,%1" : "=a" (val) : "dN" (port));
}

PUBLIC u32 in_long(u16 port)
{
       u32 val;
       asm volatile("inl %1,%0" : "=a" (val) : "dN" (port));
       return val;
}

PUBLIC void out_long(u16 port, u32 val)
{
       asm volatile("outl %0,%1" : "=a" (val) : "dN" (port));
}


/*****************************************************************************
 *                                get_boot_params
 *****************************************************************************/
/**
 * <Ring 0~1> The boot parameters have been saved by LOADER.
 *            We just read them out.
 * 
 * @param pbp  Ptr to the boot params structure
 *****************************************************************************/
PUBLIC void get_boot_params(struct boot_params * pbp)
{
	/**
	 * Boot params should have been saved at BOOT_PARAM_ADDR.
	 * @see include/load.inc boot/loader.asm boot/hdloader.asm
	 */
	int * p = (int*)BOOT_PARAM_ADDR;
	assert(p[BI_MAG] == BOOT_PARAM_MAGIC);

	pbp->mem_size = p[BI_MEM_SIZE];
	pbp->kernel_file = (unsigned char *)(p[BI_KERNEL_FILE]);

	/**
	 * the kernel file should be a ELF executable,
	 * check it's magic number
	 */
	assert(memcmp(pbp->kernel_file, ELFMAG, SELFMAG) == 0);
}


/*****************************************************************************
 *                                get_kernel_map
 *****************************************************************************/
/**
 * <Ring 0~1> Parse the kernel file, get the memory range of the kernel image.
 *
 * - The meaning of `base':	base => first_valid_byte
 * - The meaning of `limit':	base + limit => last_valid_byte
 * 
 * @param b   Memory base of kernel.
 * @param l   Memory limit of kernel.
 *****************************************************************************/
PUBLIC int get_kernel_map(unsigned int * b, unsigned int * l)
{
	struct boot_params bp;
	get_boot_params(&bp);

	Elf32_Ehdr* elf_header = (Elf32_Ehdr*)(bp.kernel_file);

	/* the kernel file should be in ELF format */
	if (memcmp(elf_header->e_ident, ELFMAG, SELFMAG) != 0)
		return -1;

	*b = ~0;
	unsigned int t = 0;
	int i;
	for (i = 0; i < elf_header->e_shnum; i++) {
		Elf32_Shdr* section_header =
			(Elf32_Shdr*)(bp.kernel_file +
				      elf_header->e_shoff +
				      i * elf_header->e_shentsize);
		if (section_header->sh_flags & SHF_ALLOC) {
			int bottom = section_header->sh_addr;
			int top = section_header->sh_addr +
				section_header->sh_size;

			if (*b > bottom)
				*b = bottom;
			if (t < top)
				t = top;
		}
	}
	assert(*b < t);
	*l = t - *b - 1;

	return 0;
}

/*****************************************************************************
 *                                get_kernel_sections
 *****************************************************************************/
/**
 * <Ring 0~1> Parse the kernel file, get the memory range of the kernel sections.
 *
 *****************************************************************************/
PUBLIC int get_kernel_sections(unsigned int * text_base, unsigned int * text_len, 
				unsigned int * data_base, unsigned int * data_len)
{
	struct boot_params bp;
	get_boot_params(&bp);

	Elf32_Ehdr* elf_header = (Elf32_Ehdr*)(bp.kernel_file);

	/* the kernel file should be in ELF format */
	if (memcmp(elf_header->e_ident, ELFMAG, SELFMAG) != 0)
		return -1;

	unsigned int t = 0;
	int i;

	*data_len = 0;

	char * strtbl;
	Elf32_Shdr * section_strtbl = (Elf32_Shdr *)(bp.kernel_file + elf_header->e_shoff) + elf_header->e_shstrndx;
	strtbl = (char *)(bp.kernel_file + section_strtbl->sh_offset);

	for (i = 0; i < elf_header->e_shnum; i++) {
		Elf32_Shdr* section_header =
			(Elf32_Shdr*)(bp.kernel_file +
				      elf_header->e_shoff +
				      i * elf_header->e_shentsize);
		char * section_name = strtbl + section_header->sh_name;
		if (strcmp(section_name, ".text") == 0) { /* .text section */
			*text_base = section_header->sh_addr;
			*text_len = section_header->sh_size;
		}
		if (strcmp(section_name, ".data") == 0) { /* .data section */
			*data_base = section_header->sh_addr;
			*data_len += section_header->sh_size;
		}
		if (strcmp(section_name, ".bss") == 0) { /* bss section */
			*data_len += section_header->sh_size;
		}
	}

	return 0;
}

/*======================================================================*
                               itoa
 *======================================================================*/
PUBLIC char * itoa(char * str, int num)/* 数字前面的 0 不被显示出来, 比如 0000B800 被显示成 B800 */
{
	char *	p = str;
	char	ch;
	int	i;
	int	flag = 0;

	*p++ = '0';
	*p++ = 'x';

	if(num == 0){
		*p++ = '0';
	}
	else{	
		for(i=28;i>=0;i-=4){
			ch = (num >> i) & 0xF;
			if(flag || (ch > 0)){
				flag = 1;
				ch += '0';
				if(ch > '9'){
					ch += 7;
				}
				*p++ = ch;
			}
		}
	}

	*p = 0;

	return str;
}


/*======================================================================*
                               disp_int
 *======================================================================*/
PUBLIC void disp_int(int input)
{
	char output[16];
	itoa(output, input);
	disp_str(output);
}

/*======================================================================*
                               printk
 *======================================================================*/
PUBLIC int printk(const char *fmt, ...)
{
	int i;
	char buf[STR_DEFAULT_LEN];

	va_list arg = (va_list)((char*)(&fmt) + 4);
	i = vsprintf(buf, fmt, arg);
	disp_str(buf);
	return i;
}

/*======================================================================*
                               delay
 *======================================================================*/
PUBLIC void delay(int time)
{
	int i, j, k;
	for(k=0;k<time;k++){
		/*for(i=0;i<10000;i++){	for Virtual PC	*/
		for(i=0;i<10;i++){/*	for Bochs	*/
			for(j=0;j<10000;j++){}
		}
	}
}
