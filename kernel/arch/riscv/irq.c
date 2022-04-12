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

#include <lyos/types.h>
#include <lyos/ipc.h>
#include "sys/types.h"
#include "lyos/const.h"
#include "lyos/proc.h"
#include "string.h"
#include <errno.h>
#include <signal.h>
#include "lyos/global.h"
#include "lyos/proto.h"
#include <asm/const.h>
#include <asm/proto.h>
#include <asm/smp.h>
#include <asm/csr.h>
#include <asm/cpulocals.h>
#include <lyos/cpufeature.h>
#include <lyos/vm.h>

void arch_init_irq(void) { plic_scan(); }

void irq_entry_handle(reg_t cause)
{
    switch (cause & ~CAUSE_IRQ_FLAG) {
    case IRQ_S_TIMER:
        riscv_timer_interrupt();
        break;
    case IRQ_S_EXT:
        plic_handle_irq();
        break;
    }
}
