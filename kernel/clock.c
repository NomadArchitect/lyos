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
#include "stdio.h"
#include "unistd.h"
#include "lyos/const.h"
#include "string.h"
#include "lyos/proc.h"
#include "lyos/global.h"
#include "lyos/proto.h"
#include "arch_const.h"
#include "arch_proto.h"
#ifdef CONFIG_SMP
#include "arch_smp.h"
#endif
#include "lyos/cpulocals.h"
#include <lyos/clocksource.h>

PRIVATE u64 read_jiffies(struct clocksource * cs)
{
    return jiffies;
}

PRIVATE struct clocksource jiffies_clocksource = {
    .name = "jiffies",
    .rating = 1,
    .read = read_jiffies,
};

/*****************************************************************************
 *                                clock_handler
 *****************************************************************************/
/**
 * <Ring 0> This routine handles the clock interrupt generated by 8253/8254
 *          programmable interval timer.
 * 
 * @param irq The IRQ nr, unused here.
 *****************************************************************************/
PUBLIC int clock_handler(irq_hook_t * hook)
{	
#if CONFIG_SMP
    if (cpuid == bsp_cpu_id) {
#endif
	   if (++jiffies >= MAX_TICKS)
		  jiffies = 0;
#if CONFIG_SMP
    }
#endif
    
    struct proc * p = get_cpulocal_var(proc_ptr);
	if (p && p->counter)
		p->counter--;

    return 1;
}

PUBLIC int init_time()
{
    arch_init_time();
    
    return 0;
}

PUBLIC int init_bsp_timer(int freq)
{
    if (init_local_timer(freq)) return -1;
    if (put_local_timer_handler(clock_handler)) return -1;

    return 0;
}

PUBLIC int init_ap_timer(int freq)
{
    if (init_local_timer(freq)) return -1;

    return 0;
}
