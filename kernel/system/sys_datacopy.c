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
#include "stddef.h"
#include "lyos/const.h"
#include "string.h"
#include "lyos/proc.h"
#include "lyos/global.h"
#include "lyos/proto.h"
#include "page.h"
#include <errno.h>
#include "arch_proto.h"

PUBLIC int sys_datacopy(MESSAGE * m, struct proc * p_proc)
{
    void * src_addr = m->SRC_ADDR;
    endpoint_t src_ep = m->SRC_EP == SELF ? p_proc->endpoint : m->SRC_EP;

    void * dest_addr = m->DEST_ADDR;
    endpoint_t dest_ep = m->DEST_EP == SELF ? p_proc->endpoint : m->DEST_EP;

    int len = m->BUF_LEN;

    return vir_copy(dest_ep, dest_addr, src_ep, src_addr, len);
}
