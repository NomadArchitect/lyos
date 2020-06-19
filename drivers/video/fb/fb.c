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
#include <sys/types.h>
#include <lyos/config.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <assert.h>
#include <unistd.h>
#include <lyos/const.h>
#include <string.h>
#include <lyos/fs.h>
#include <lyos/proc.h>
#include <lyos/global.h>
#include <lyos/proto.h>
#include <lyos/list.h>
#include <lyos/sysutils.h>
#include <lyos/service.h>
#include <lyos/vm.h>
#include <libchardriver/libchardriver.h>
#include <libdevman/libdevman.h>
#include <sys/mman.h>

#include "fb.h"

static int init_fb();

static int fb_open(dev_t minor, int access);
static int fb_close(dev_t minor);
static ssize_t fb_read(dev_t minor, u64 pos, endpoint_t endpoint, char* buf,
                       unsigned int count, cdev_id_t id);
static ssize_t fb_write(dev_t minor, u64 pos, endpoint_t endpoint, char* buf,
                        unsigned int count, cdev_id_t id);
static int fb_ioctl(dev_t minor, int request, endpoint_t endpoint, char* buf,
                    cdev_id_t id);
static int fb_mmap(dev_t minor, endpoint_t endpoint, char* addr, off_t offset,
                   size_t length, char** retaddr);

static int open_counter[NR_FB_DEVS];

static bus_type_id_t fb_subsys_id;

static struct chardriver fbdriver = {
    .cdr_open = fb_open,
    .cdr_close = fb_close,
    .cdr_read = fb_read,
    .cdr_write = fb_write,
    .cdr_ioctl = fb_ioctl,
    .cdr_mmap = fb_mmap,
};

/*****************************************************************************
 *                              main
 *****************************************************************************/
/**
 * <Ring 3> The main loop of framebuffer driver.
 *
 *****************************************************************************/
int main()
{
    serv_register_init_fresh_callback(init_fb);
    serv_init();

    return chardriver_task(&fbdriver);
}

static int init_fb()
{
    int i;
    struct device_info devinf;
    device_id_t device_id;
    dev_t devt;

    printl("fb: framebuffer driver is running\n");

    fb_subsys_id = dm_bus_register("fb");
    if (fb_subsys_id == BUS_TYPE_ERROR)
        panic("tty: cannot register tty subsystem");

    for (i = 0; i < NR_FB_DEVS; i++) {
        open_counter[i] = 0;
        devt = MAKE_DEV(DEV_CHAR_FB, i);
        dm_cdev_add(devt);

        memset(&devinf, 0, sizeof(devinf));
        snprintf(devinf.name, sizeof(devinf.name), "fb%d", i);
        devinf.bus = fb_subsys_id;
        devinf.parent = NO_DEVICE_ID;
        devinf.devt = devt;
        devinf.type = DT_CHARDEV;

        device_id = dm_device_register(&devinf);
        if (device_id == NO_DEVICE_ID)
            panic("tty: cannot register console device");
    }
    return 0;
}

static int fb_open(dev_t minor, int access)
{
    if (minor < 0 || minor >= NR_FB_DEVS) return ENXIO;

    int retval = arch_init_fb(minor);
    if (retval) {
        return ENXIO;
    }

    open_counter[minor]++;
    return OK;
}

static int fb_close(dev_t minor)
{
    if (minor < 0 || minor >= NR_FB_DEVS) return ENXIO;

    open_counter[minor]--;
    return OK;
}

static ssize_t fb_read(dev_t minor, u64 pos, endpoint_t endpoint, char* buf,
                       unsigned int count, cdev_id_t id)
{
    return 0;
}

static ssize_t fb_write(dev_t minor, u64 pos, endpoint_t endpoint, char* buf,
                        unsigned int count, cdev_id_t id)
{
    int retval = OK;
    void* base;
    size_t size;
    if (minor < 0 || minor >= NR_FB_DEVS) return -ENXIO;

    retval = arch_get_device(minor, &base, &size);
    if (retval != OK) return retval;

    if (count == 0 || pos >= size) return OK;
    if (pos + count > size) {
        count = size - pos;
    }

    data_copy(SELF, (void*)(base + (size_t)pos), endpoint, buf, count);

    return count;
}

static int fb_ioctl(dev_t minor, int request, endpoint_t endpoint, char* buf,
                    cdev_id_t id)
{
    return 0;
}

static int fb_mmap(dev_t minor, endpoint_t endpoint, char* addr, off_t offset,
                   size_t length, char** retaddr)
{
    int retval = OK;
    phys_bytes base, size;
    if (minor < 0 || minor >= NR_FB_DEVS) return ENXIO;

    retval = arch_get_device_phys(minor, &base, &size);
    if (retval != OK) return retval;

    if (length == 0 || offset >= size) return OK;
    if (offset + length > size) {
        length = size - offset;
    }

    char* mapped =
        mm_map_phys(endpoint, (void*)(base + (size_t)offset), length);
    if (mapped == MAP_FAILED) {
        return ENOMEM;
    }

    *retaddr = mapped;
    return 0;
}
