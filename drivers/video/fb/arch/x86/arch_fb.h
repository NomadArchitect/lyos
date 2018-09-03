#ifndef _VIDEO_ARCH_FB_H_
#define _VIDEO_ARCH_FB_H_

extern void* fb_mem_vir;
extern phys_bytes fb_mem_phys;
extern size_t fb_mem_size;

typedef int (*fb_dev_init_func)(int devind);

PUBLIC int fb_init_bochs(int devind);

#endif
