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

#include <lyos/type.h>
#include <lyos/ipc.h>
#include "sys/types.h"
#include "lyos/config.h"
#include "stdio.h"
#include <stdint.h>
#include "unistd.h"
#include "errno.h"
#include "lyos/const.h"
#include "string.h"
#include "lyos/proc.h"
#include "lyos/global.h"
#include "lyos/proto.h"
#include <lyos/vm.h>
#include "region.h"
#include "proto.h"
#include <lyos/cpufeature.h>
#include "global.h"
#include "const.h"

#define MAX_KERN_MAPPINGS   10
PRIVATE struct kern_mapping {
    phys_bytes phys_addr;
    void* vir_addr;
    size_t len;
    int flags;
} kern_mappings[MAX_KERN_MAPPINGS];
PRIVATE int nr_kern_mappings = 0;

#if defined(__i386__)
PRIVATE int global_bit = 0;
#endif

PRIVATE struct mmproc * mmprocess = &mmproc_table[TASK_MM];
PRIVATE struct mm_struct self_mm;
//#define PAGETABLE_DEBUG    1

/* before MM has set up page table for its own, we use these pages in page allocation */
PRIVATE char static_bootstrap_pages[ARCH_PG_SIZE * STATIC_BOOTSTRAP_PAGES]
        __attribute__((aligned(ARCH_PG_SIZE)));

PUBLIC void mm_init(struct mm_struct* mm);

PUBLIC void pt_init()
{
    int i;

    void* bootstrap_pages_mem = static_bootstrap_pages;
    for (i = 0; i < STATIC_BOOTSTRAP_PAGES; i++) {
        void * v = (void *)(bootstrap_pages_mem + i * ARCH_PG_SIZE);
        phys_bytes bootstrap_phys_addr;
        if (umap(SELF, v, &bootstrap_phys_addr)) panic("MM: can't get phys addr for bootstrap page");
        bootstrap_pages[i].phys_addr = bootstrap_phys_addr;
        bootstrap_pages[i].vir_addr = __va(bootstrap_phys_addr);
        bootstrap_pages[i].used = 0;
    }

#if defined(__i386__)
    if (_cpufeature(_CPUF_I386_PGE))
        global_bit = ARCH_PG_GLOBAL;
#endif

    /* init mm structure */
    mmprocess->mm = &self_mm;
    mm_init(mmprocess->mm);
    mmprocess->mm->slot = TASK_MM;
    mmprocess->active_mm = mmprocess->mm;

    pt_kern_mapping_init();

    /* prepare page directory for MM */
    pgdir_t * mypgd = &mmprocess->mm->pgd;

    if (pgd_new(mypgd)) panic("MM: pgd_new for self failed");

    /* map kernel for MM */
    int kernel_pde = kernel_info.kernel_start_pde;
    phys_bytes paddr = kernel_info.kernel_start_phys;
    int user_flag = 0;

    while (kernel_pde < ARCH_PDE(KERNEL_VMA + LOWMEM_END)) {
        if (paddr >= kernel_info.kernel_end_phys) {
            user_flag = ARCH_PG_USER;
        }
#if defined(__i386__)
        mypgd->vir_addr[kernel_pde] = __pde(paddr | ARCH_PG_PRESENT | ARCH_PG_BIGPAGE | ARCH_PG_RW | user_flag);
#elif defined(__arm__)
        mypgd->vir_addr[kernel_pde] = __pde((paddr & ARM_VM_SECTION_MASK)
            | ARM_VM_SECTION
            | ARM_VM_SECTION_DOMAIN
            | ARM_VM_SECTION_CACHED
            | ARM_VM_SECTION_SUPER);
#endif

        paddr += ARCH_BIG_PAGE_SIZE;
        kernel_pde++;
    }

    unsigned int mypdbr = 0;
    static pde_t currentpagedir[ARCH_VM_DIR_ENTRIES];
    if (vmctl_getpdbr(SELF, &mypdbr)) panic("MM: failed to get page directory base register");
    /* kernel has done identity mapping for the bootstrap page dir we are using, so this is ok */
    data_copy(SELF, &currentpagedir, NO_TASK, (void *)mypdbr, sizeof(pde_t) * ARCH_VM_DIR_ENTRIES);

    for(i = 0; i < ARCH_VM_DIR_ENTRIES; i++) {
        pde_t entry = currentpagedir[i];

        if (!(pde_val(entry) & ARCH_PG_PRESENT)) continue;
        if (pde_val(entry) & ARCH_PG_BIGPAGE) continue;

        if (pt_create((pmd_t*)&mypgd->vir_addr[i]) != 0) {
            panic("MM: failed to allocate page table for MM");
        }

        phys_bytes ptphys_kern = pde_val(entry) & ARCH_PG_MASK;
        phys_bytes ptphys_mm = pde_val(mypgd->vir_addr[i]) & ARCH_PG_MASK;
        data_copy(NO_TASK, (void*)ptphys_mm, NO_TASK, (void*)ptphys_kern, ARCH_PG_SIZE);
    }

    /* using the new page dir */
    pgd_bind(mmprocess, mypgd);

    pt_init_done = 1;
}

PUBLIC struct mm_struct* mm_allocate()
{
    struct mm_struct* mm;

    int len = roundup(sizeof(struct mm_struct), ARCH_PG_SIZE);
    mm = (struct mm_struct*) alloc_vmem(NULL, len, 0);
    return mm;
}

PUBLIC void mm_init(struct mm_struct* mm)
{
    if (!mm) return;

    INIT_LIST_HEAD(&mm->mem_regions);
    region_init_avl(mm);
    INIT_ATOMIC(&mm->refcnt, 1);
}

PUBLIC void mm_free(struct mm_struct* mm)
{
    if (atomic_dec_and_test(&mm->refcnt)) {
        int len = roundup(sizeof(struct mm_struct), ARCH_PG_SIZE);
        free_vmem(mm, len);
    }
}

PUBLIC pmd_t* pmd_create(pde_t* pde, unsigned addr)
{
    return pmd_offset(pde, addr);
}

PUBLIC int pt_create(pmd_t* pmde)
{
    if (!pmde_none(*pmde)) {
        /* page table already created */
        return 0;
    }

    phys_bytes pt_phys;
    pte_t * pt = (pte_t *)alloc_vmem(&pt_phys, sizeof(pte_t) * ARCH_VM_PT_ENTRIES, PGT_PAGETABLE);
    if (pt == NULL) {
        printl("MM: pt_create: failed to allocate memory for new page table\n");
        return ENOMEM;
    }

#if PAGETABLE_DEBUG
        printl("MM: pt_create: allocated new page table\n");
#endif

    int i;
    for (i = 0; i < ARCH_VM_PT_ENTRIES; i++) {
        pt[i] = __pte(0);
    }

#ifdef __i386__
    pmde_populate(pmde, pt);
#elif defined(__arm__)
    pgd->vir_addr[pde] = __pde((pt_phys & ARM_VM_PDE_MASK) | ARM_VM_PDE_PRESENT | ARM_VM_PDE_DOMAIN);
#endif

    return 0;
}

PUBLIC pte_t* pt_create_map(pmd_t* pmde, unsigned long addr) {
    pt_create(pmde);
    return pte_offset(pmde, addr);
}

/**
 * <Ring 1> Map a physical page, create page table if necessary.
 * @param  phys_addr Physical address.
 * @param  vir_addr  Virtual address.
 * @return           Zero on success.
 */
PUBLIC int pt_mappage(pgdir_t * pgd, phys_bytes phys_addr, void* vir_addr, unsigned int flags)
{
    pde_t* pde;
    pmd_t* pmde;
    pte_t* pte;

    pde = pgd_offset(pgd->vir_addr, (unsigned long)vir_addr);
    pmde = pmd_create(pde, (unsigned long)vir_addr);
    pte = pt_create_map(pmde, (unsigned long)vir_addr);

    *pte = __pte((phys_addr & ARCH_PG_MASK) | flags);

    return 0;
}

/**
 * <Ring 1> Make a physical page write-protected.
 * @param  vir_addr  Virtual address.
 * @return           Zero on success.
 */
PUBLIC int pt_wppage(pgdir_t * pgd, void * vir_addr)
{
    pde_t* pde;
    pmd_t* pmde;
    pte_t* pte;

    pde = pgd_offset(pgd->vir_addr, (unsigned long)vir_addr);
    if (pde_none(*pde)) {
        return EINVAL;
    }

    pmde = pmd_offset(pde, (unsigned long)vir_addr);
    if (pmde_none(*pmde)) {
        return EINVAL;
    }

    pte = pte_offset(pmde, (unsigned long)vir_addr);
    if (pte_none(*pte)) {
        return EINVAL;
    }

    *pte = __pte(pte_val(*pte) & (~ARCH_PG_RW));

    return 0;
}

/**
 * <Ring 1> Make a physical page read-write.
 * @param  vir_addr  Virtual address.
 * @return           Zero on success.
 */
PUBLIC int pt_unwppage(pgdir_t * pgd, void * vir_addr)
{
    pde_t* pde;
    pmd_t* pmde;
    pte_t* pte;

    pde = pgd_offset(pgd->vir_addr, (unsigned long)vir_addr);
    if (pde_none(*pde)) {
        return EINVAL;
    }

    pmde = pmd_offset(pde, (unsigned long)vir_addr);
    if (pmde_none(*pmde)) {
        return EINVAL;
    }

    pte = pte_offset(pmde, (unsigned long)vir_addr);
    if (pte_none(*pte)) {
        return EINVAL;
    }

    *pte = __pte(pte_val(*pte) | ARCH_PG_RW);

    return 0;
}

PUBLIC int pt_writemap(pgdir_t * pgd, phys_bytes phys_addr, void* vir_addr, size_t length, int flags)
{
    /* sanity check */
    if (phys_addr % ARCH_PG_SIZE != 0) printl("MM: pt_writemap: phys_addr is not page-aligned!\n");
    if ((uintptr_t) vir_addr % ARCH_PG_SIZE != 0) printl("MM: pt_writemap: vir_addr is not page-aligned!\n");
    if (length % ARCH_PG_SIZE != 0) printl("MM: pt_writemap: length is not page-aligned!\n");

    while (1) {
        pt_mappage(pgd, phys_addr, vir_addr, flags);

        length -= ARCH_PG_SIZE;
        phys_addr = phys_addr + ARCH_PG_SIZE;
        vir_addr = vir_addr + ARCH_PG_SIZE;
        if (length <= 0) break;
    }

    return 0;
}

PUBLIC int pt_wp_memory(pgdir_t * pgd, void * vir_addr, size_t length)
{
    /* sanity check */
    if ((uintptr_t) vir_addr % ARCH_PG_SIZE != 0) printl("MM: pt_wp_memory: vir_addr is not page-aligned!\n");
    if (length % ARCH_PG_SIZE != 0) printl("MM: pt_wp_memory: length is not page-aligned!\n");

    while (1) {
        pt_wppage(pgd, vir_addr);

        length -= ARCH_PG_SIZE;
        vir_addr = (void *)((void*)vir_addr + ARCH_PG_SIZE);
        if (length <= 0) break;
    }

    return 0;
}

PUBLIC int pt_unwp_memory(pgdir_t * pgd, void * vir_addr, size_t length)
{
    /* sanity check */
    if ((uintptr_t)vir_addr % ARCH_PG_SIZE != 0) printl("MM: pt_wp_memory: vir_addr is not page-aligned!\n");
    if (length % ARCH_PG_SIZE != 0) printl("MM: pt_wp_memory: length is not page-aligned!\n");

    while (1) {
        pt_unwppage(pgd, vir_addr);

        length -= ARCH_PG_SIZE;
        vir_addr = (void *)((char*)vir_addr + ARCH_PG_SIZE);
        if (length <= 0) break;
    }

    return 0;
}

/**
 * <Ring 1> Initial kernel mappings.
 */
PUBLIC void pt_kern_mapping_init()
{
    int rindex = 0;
    caddr_t addr;
    int len, flags;
    struct kern_mapping * kmapping = kern_mappings;
    void* pkmap_start = (void*)PKMAP_START;

    while (!vmctl_get_kern_mapping(rindex, &addr, &len, &flags)) {
        if (rindex > MAX_KERN_MAPPINGS) panic("MM: too many kernel mappings");

        /* fill in mapping information */
        kmapping->phys_addr = (phys_bytes) addr;
        kmapping->len = len;

        kmapping->flags = ARCH_PG_PRESENT;
        if (flags & KMF_USER) kmapping->flags |= ARCH_PG_USER;
#if defined(__arm__)
        else kmapping->flags |= ARM_PG_SUPER;
#endif
        if (flags & KMF_WRITE) kmapping->flags |= ARCH_PG_RW;
        else kmapping->flags |= ARCH_PG_RO;

#if defined(__arm__)
        kmapping->flags |= ARM_PG_CACHED;
#endif

        /* where this region will be mapped */
        kmapping->vir_addr = pkmap_start;
        if (!kmapping->vir_addr) panic("MM: cannot allocate memory for kernel mappings");

        if (vmctl_reply_kern_mapping(rindex, (void*) kmapping->vir_addr)) panic("MM: cannot reply kernel mapping");

        printl("MM: kernel mapping index %d: 0x%08x - 0x%08x  (%dkB)\n",
                rindex, kmapping->vir_addr, (int)kmapping->vir_addr + kmapping->len, kmapping->len / 1024);

        pkmap_start += kmapping->len;
        nr_kern_mappings++;
        kmapping++;

        rindex++;
    }
}

/* <Ring 1> */
PUBLIC int pgd_new(pgdir_t * pgd)
{
    phys_bytes pgd_phys;
    /* map the directory so that we can write it */
    pde_t * pg_dir = (pde_t *)alloc_vmem(&pgd_phys, sizeof(pde_t) * ARCH_VM_DIR_ENTRIES, PGT_PAGEDIR);

    pgd->phys_addr = (void *)pgd_phys;
    pgd->vir_addr = pg_dir;

    int i;

    /* zero it */
    for (i = 0; i < ARCH_VM_DIR_ENTRIES; i++) {
        pg_dir[i] = __pde(0);
    }

    for (i = 0; i < ARCH_VM_DIR_ENTRIES; i++) {
        pgd->vir_pts[i] = NULL;
    }

    pgd_mapkernel(pgd);
    return 0;
}

/**
 * <Ring 1> Map the kernel.
 * @param  pgd The page directory.
 * @return     Zero on success.
 */
PUBLIC int pgd_mapkernel(pgdir_t * pgd)
{
    int i;
    int kernel_pde = kernel_info.kernel_start_pde;
    phys_bytes addr = kernel_info.kernel_start_phys;

    /* map low memory */
    while (kernel_pde < ARCH_PDE(KERNEL_VMA + LOWMEM_END)) {
#if defined(__i386__)
        pgd->vir_addr[kernel_pde] = __pde(addr | ARCH_PG_PRESENT | ARCH_PG_BIGPAGE | ARCH_PG_RW);
#elif defined(__arm__)
        pgd->vir_addr[kernel_pde] = __pde((addr & ARM_VM_SECTION_MASK)
            | ARM_VM_SECTION
            | ARM_VM_SECTION_DOMAIN
            | ARM_VM_SECTION_CACHED
            | ARM_VM_SECTION_SUPER);
#endif

        addr += ARCH_BIG_PAGE_SIZE;
        kernel_pde++;
    }

    for (i = 0; i < nr_kern_mappings; i++) {
        pt_writemap(pgd, kern_mappings[i].phys_addr, kern_mappings[i].vir_addr, kern_mappings[i].len, kern_mappings[i].flags);
    }

    return 0;
}

/* <Ring 1> */
PUBLIC int pgd_free(pgdir_t * pgd)
{
    pgd_clear(pgd);
    free_vmem(pgd->vir_addr, sizeof(pde_t) * ARCH_VM_DIR_ENTRIES);

    return 0;
}

PUBLIC int pgd_clear(pgdir_t * pgd)
{
    /* TODO: clean up the page directory recursively */
    int i;

    for (i = 0; i < ARCH_VM_DIR_ENTRIES; i++) {
        if (pgd->vir_pts[i]) {
            free_vmem(pgd->vir_pts[i], sizeof(pte_t) * ARCH_VM_PT_ENTRIES);
        }
        pgd->vir_pts[i] = NULL;
    }

    return 0;
}

PUBLIC int pgd_bind(struct mmproc * who, pgdir_t * pgd)
{
    /* make sure that the page directory is in low memory */
    return vmctl_set_address_space(who->endpoint, pgd->phys_addr, __va(pgd->phys_addr));
}

PUBLIC void* pgd_find_free_pages(pgdir_t * pgd, int nr_pages, void* minv, void* maxv)
{
    unsigned int start_pde = ARCH_PDE(minv);
    unsigned int end_pde = ARCH_PDE(maxv);

    pde_t* pde;
    pmd_t* pmde;
    pte_t* pte;

    int i, j;
    int allocated_pages = 0;
    void* retaddr = 0;

    for (i = start_pde; i < end_pde; i++) {
        unsigned long pde_addr = ARCH_VM_ADDRESS(i, 0, 0);
        pde = pgd_offset(pgd->vir_addr, (unsigned long)pde_addr);
        pmde = pmd_offset(pde, (unsigned long)pde_addr);
        pte = pte_offset(pmde, (unsigned long)pde_addr);

        /* the pde is empty, we have I386_VM_DIR_ENTRIES free pages */
        if (pmde_none(*pmde)) {
            nr_pages -= ARCH_VM_PT_ENTRIES;
            allocated_pages += ARCH_VM_PT_ENTRIES;
            if (retaddr == 0) retaddr = (void*) ARCH_VM_ADDRESS(i, 0, 0);
            if (nr_pages <= 0) {
                return retaddr;
            }
            continue;
        }

        for (j = 0; j < ARCH_VM_PT_ENTRIES; j++) {
            if (pte_val(pte[j]) != 0) {
                nr_pages += allocated_pages;
                retaddr = 0;
                allocated_pages = 0;
            } else {
                nr_pages--;
                allocated_pages++;
                if (!retaddr) retaddr = (void*) ARCH_VM_ADDRESS(i, j, 0);
            }

            if (nr_pages <= 0) return retaddr;
        }
    }

    return 0;
}

PUBLIC phys_bytes pgd_va2pa(pgdir_t* pgd, void* vir_addr)
{
    if (vir_addr >= (void*)KERNEL_VMA) {
        return __pa(vir_addr);
    }

    pde_t* pde;
    pmd_t* pmde;
    pte_t* pte;

    pde = pgd_offset(pgd->vir_addr, (unsigned long)vir_addr);
    pmde = pmd_offset(pde, (unsigned long)vir_addr);
    pte = pte_offset(pmde, (unsigned long)vir_addr);

    phys_bytes phys = pte_val(*pte) & ARCH_PG_MASK;

    return phys + ((unsigned long)vir_addr % ARCH_PG_SIZE);
}

PUBLIC int unmap_memory(pgdir_t * pgd, void* vir_addr, size_t length)
{
    /* sanity check */
    if ((uintptr_t)vir_addr % ARCH_PG_SIZE != 0) printl("MM: map_memory: vir_addr is not page-aligned!\n");
    if (length % ARCH_PG_SIZE != 0) printl("MM: map_memory: length is not page-aligned!\n");

    while (1) {
        pt_mappage(pgd, 0, vir_addr, 0);

        length -= ARCH_PG_SIZE;
        vir_addr += ARCH_PG_SIZE;
        if (length <= 0) break;
    }

    return 0;
}
