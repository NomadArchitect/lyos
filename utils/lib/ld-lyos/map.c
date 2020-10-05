#include <elf.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "ldso.h"
#include "env.h"

struct so_info* ldso_map_object(const char* pathname, int fd)
{
    struct so_info* si = ldso_alloc_info(pathname);
    if (!si) return NULL;

    si->ehdr = mmap(NULL, pagesz, PROT_READ, MAP_SHARED, fd, 0);

    if (si->ehdr == MAP_FAILED) {
        goto failed;
    }

    if (si->ehdr->e_ident[0] != 0x7f) {
        xprintf("%s: bad elf magic number\n", pathname);
        goto failed;
    }

    if (si->ehdr->e_type != ET_EXEC && si->ehdr->e_type != ET_DYN) {
        xprintf("%s: bad elf type %x\n", pathname, si->ehdr->e_type);
        goto failed;
    }
    si->is_dynamic = (si->ehdr->e_type == ET_DYN);

    Elf32_Ehdr* ehdr = si->ehdr;
    Elf32_Phdr* phdr = (Elf32_Phdr*)((char*)ehdr + ehdr->e_phoff);
    Elf32_Phdr* phdr_tls = NULL;
    Elf32_Addr tls_vaddr = 0;
    size_t phsize = ehdr->e_phnum * sizeof(Elf32_Phdr);
    char* phend = (char*)phdr + phsize;

    Elf32_Phdr* segs[2];
    int nsegs = 0;
    for (; (char*)phdr < phend; phdr++) {
        switch (phdr->p_type) {
        case PT_LOAD:
            if (nsegs < 2) segs[nsegs] = phdr;
            nsegs++;
            break;
        case PT_DYNAMIC:
            si->dynamic = (Elf32_Dyn*)phdr->p_vaddr;
            break;
        case PT_TLS:
            phdr_tls = phdr;
            break;
        }
    }

    if (!si->dynamic) {
        xprintf("%s: not dynamically linked\n", pathname);
        goto failed;
    }

    si->entry = (char*)ehdr->e_entry;

    if (nsegs != 2) {
        xprintf("%s: wrong number of segments\n", pathname);
        goto failed;
    }

    Elf32_Addr base_vaddr = rounddown(segs[0]->p_vaddr);
    Elf32_Addr base_addr = (Elf32_Addr)(si->is_dynamic ? 0 : base_vaddr);
    size_t text_size = roundup(segs[0]->p_memsz);
    size_t map_size = roundup(segs[1]->p_vaddr + segs[1]->p_memsz) - base_vaddr;
    off_t base_offset = rounddown(segs[0]->p_offset);

    /* Map data segment */
    Elf32_Addr data_vaddr = rounddown(segs[1]->p_vaddr);
    size_t data_size = roundup(segs[1]->p_memsz);
    off_t data_offset = rounddown(segs[1]->p_offset);
    Elf32_Addr clear_vaddr = segs[1]->p_vaddr + segs[1]->p_filesz;
    size_t clear_size = segs[1]->p_memsz - segs[1]->p_filesz;

    if (phdr_tls) {
        ++ldso_tls_dtv_generation;
        si->tls_index = ++ldso_tls_max_index;
        si->tls_size = phdr_tls->p_memsz;
        si->tls_align = phdr_tls->p_align;
        si->tls_init_size = phdr_tls->p_filesz;
        tls_vaddr = phdr_tls->p_vaddr;
    }

    if (base_offset < pagesz) {
        munmap(ehdr, pagesz);
        ehdr = MAP_FAILED;
    }

    /* Map text segment */
    char* mapbase =
        mmap((void*)base_addr, map_size, PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | (si->is_dynamic ? 0 : MAP_FIXED), fd, base_offset);
    if (mapbase == MAP_FAILED) {
        xprintf("%s: failed to map text segment\n", pathname);
        goto failed;
    }

    Elf32_Addr clear_addr = (Elf32_Addr)(mapbase + (clear_vaddr - base_addr));
    Elf32_Addr data_addr = (Elf32_Addr)(mapbase + (data_vaddr - base_addr));

    if (mmap((void*)data_addr, data_size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_FIXED, fd, data_offset) == MAP_FAILED) {
        xprintf("%s: failed to map data segment\n", pathname);
        goto failed;
    }
    memset((void*)clear_addr, 0, clear_size);

    si->mapbase = mapbase;
    si->mapsize = map_size;
    si->relocbase = mapbase - base_addr;

    if (si->dynamic)
        si->dynamic = (Elf32_Dyn*)(si->relocbase + (Elf32_Addr)si->dynamic);
    if (si->entry) si->entry = (char*)(si->relocbase + (Elf32_Addr)si->entry);

    if (phdr_tls) {
        si->tls_init = mapbase + tls_vaddr;
    }

    return si;

failed:
    if (ehdr != MAP_FAILED) munmap(ehdr, pagesz);
    si->flags = 0;
    return NULL;
}
