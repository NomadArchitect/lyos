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

#ifndef _ARCH_PAGE_H_
#define _ARCH_PAGE_H_

#define LOWMEM_END    0x30000000
#define KERNEL_VMA    0xc0000000
#define VMALLOC_START 0x80000000
#define VMALLOC_END                                                          \
    0xb0000000 /* region where MM map physical memory into its address space \
                */
#define PKMAP_START  (KERNEL_VMA + LOWMEM_END)
#define PKMAP_END    (PKMAP_START + 0x400000) /* 4 MB */
#define VM_STACK_TOP KERNEL_VMA

typedef struct {
    unsigned long pde;
} pde_t;

typedef struct {
    unsigned long pte;
} pte_t;

typedef struct {
    unsigned long pgprot;
} pgprot_t;

#define pde_val(x) ((x).pde)
#define __pde(x)   ((pde_t){(x)})

#define pte_val(x) ((x).pte)
#define __pte(x)   ((pte_t){(x)})

#define pgprot_val(x) ((x).pgprot)
#define __pgprot(x)   ((pgprot_t){(x)})

#define I386_VM_DIR_ENTRIES 1024
#define I386_VM_PT_ENTRIES  1024

#define I386_VM_ADDR_MASK_4MB   0xffc00000
#define I386_VM_OFFSET_MASK_4MB 0x003fffff

/* struct page_directory */
typedef struct {
    /* physical address of page dir */
    phys_bytes phys_addr;
    /* virtual address of page dir */
    pde_t* vir_addr;

    /* virtual address of all page tables */
    pte_t* vir_pts[I386_VM_DIR_ENTRIES];
} pgdir_t;

/* size of page directory */
#define I386_PGD_SHIFT (22)
#define I386_PGD_SIZE  (1UL << I386_PGD_SHIFT)
#define I386_PGD_MASK  (~(I386_PGD_SIZE - 1))

/* size of a virtual page */
#define I386_PG_SHIFT (12)
#define I386_PG_SIZE  (1UL << I386_PG_SHIFT)
#define I386_PG_MASK  (~(I386_PG_SIZE - 1))

/* size of a big page */
#define PG_BIG_SIZE I386_PGD_SIZE

#define I386_PG_PRESENT 0x001
#define I386_PG_RO      0x000
#define I386_PG_RW      0x002
#define I386_PG_USER    0x004
#define I386_PG_BIGPAGE 0x080
#define I386_PG_GLOBAL  (1L << 8)

#define I386_CR0_WP 0x00010000 /* Enable paging */
#define I386_CR0_PG 0x80000000 /* Enable paging */

#define I386_CR4_PSE 0x00000010 /* Page size extensions */
#define I386_CR4_PAE 0x00000020 /* Physical addr extens. */
#define I386_CR4_MCE 0x00000040 /* Machine check enable */
#define I386_CR4_PGE 0x00000080 /* Global page flag enable */

#define I386_PF_PROT(x)   ((x)&I386_PG_PRESENT)
#define I386_PF_NOPAGE(x) (!I386_PF_PROT(x))
#define I386_PF_WRITE(x)  ((x)&I386_PG_RW)
#define I386_PF_USER(x)   ((x)&I386_PG_USER)
#define I386_PF_INST(x)   ((x) & (1 << 4))

#define ARCH_PG_PRESENT I386_PG_PRESENT
#define ARCH_PG_RW      I386_PG_RW
#define ARCH_PG_RO      I386_PG_RO
#define ARCH_PG_BIGPAGE I386_PG_BIGPAGE
#define ARCH_PG_USER    I386_PG_USER
#define ARCH_PG_GLOBAL  I386_PG_GLOBAL

/*         xwr */
#define __P000 __pgprot(0)
#define __P001 __pgprot(I386_PG_PRESENT | I386_PG_USER | I386_PG_RO)
#define __P010 __pgprot(I386_PG_PRESENT | I386_PG_USER | I386_PG_RW)
#define __P011 __pgprot(I386_PG_PRESENT | I386_PG_USER | I386_PG_RW)
#define __P100 __pgprot(I386_PG_PRESENT | I386_PG_USER | I386_PG_RO)
#define __P101 __pgprot(I386_PG_PRESENT | I386_PG_USER | I386_PG_RO)
#define __P110 __pgprot(I386_PG_PRESENT | I386_PG_USER | I386_PG_RW)
#define __P111 __pgprot(I386_PG_PRESENT | I386_PG_USER | I386_PG_RW)

#define ARCH_PDE_PRESENT I386_PG_PRESENT
#define ARCH_PDE_RW      I386_PG_RW
#define ARCH_PDE_USER    I386_PG_USER

#define ARCH_VM_DIR_ENTRIES I386_VM_DIR_ENTRIES
#define ARCH_VM_PT_ENTRIES  I386_VM_PT_ENTRIES
#define ARCH_VM_OFFSET_MASK I386_VM_OFFSET_MASK

#define ARCH_PDE(x) ((unsigned long)(x) >> I386_PGD_SHIFT & 0x03FF)
#define ARCH_PTE(x) ((unsigned long)(x) >> I386_PG_SHIFT & 0x03FF)

#define ARCH_VM_ADDRESS(pde, pte, offset) \
    ((pde << I386_PGD_SHIFT) | (pte << I386_PG_SHIFT) | offset)

#define ARCH_PGD_SIZE  I386_PGD_SIZE
#define ARCH_PGD_SHIFT I386_PGD_SHIFT
#define ARCH_PGD_MASK  I386_PGD_MASK

#define ARCH_PG_SIZE       I386_PG_SIZE
#define ARCH_PG_SHIFT      I386_PG_SHIFT
#define ARCH_PG_MASK       I386_PG_MASK
#define ARCH_BIG_PAGE_SIZE PG_BIG_SIZE

#ifdef __kernel__
#define _PAGE_OFFSET KERNEL_VMA
#else
#define _PAGE_OFFSET 0
#endif

#ifndef __va
#define __va(x) ((void*)((unsigned long)(x) + _PAGE_OFFSET))
#endif

#ifndef __pa
#define __pa(x) ((phys_bytes)(x)-_PAGE_OFFSET)
#endif

static inline unsigned long virt_to_phys(void* x) { return __pa(x); }

#ifdef __kernel__
static inline void* phys_to_virt(unsigned long x) { return __va(x); }
#else
extern void* phys_to_virt(unsigned long x);
#endif

#endif
