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
#include "unistd.h"
#include "stddef.h"
#include "errno.h"
#include "assert.h"
#include "lyos/const.h"
#include "string.h"
#include "lyos/proc.h"
#include "lyos/global.h"
#include "lyos/proto.h"
#include <lyos/sysutils.h>
#include "proto.h"
#include "const.h"
#include "global.h"

PUBLIC int do_getsetid(MESSAGE * p)
{
    int retval = 0;
    struct pmproc * pmp = pm_endpt_proc(p->source), * pmpi;
    if (!pmp) return EINVAL;
    uid_t uid;
    gid_t gid;
    int tell_vfs = 0;
    MESSAGE msg2fs;

    memset(&msg2fs, 0, sizeof(MESSAGE));

    switch (p->REQUEST) {
    case GS_GETEP:
        p->ENDPOINT = p->source;
        break;
    case GS_GETPID:
        p->PID = pmp->tgid;
        break;
    case GS_GETTID:
        p->PID = pmp->pid;
        break;
    case GS_GETPPID:
        pmpi = pm_endpt_proc(pmp->parent);
        if (pmpi == NULL) panic("proc invalid parent");

        p->PID = pmpi->pid;
        break; 
    case GS_GETUID:
        retval = pmp->realuid;
        break;
    case GS_GETGID:
        retval = pmp->realgid;
        break;
    case GS_GETEUID:
        retval = pmp->effuid;
        break;
    case GS_GETEGID:
        retval = pmp->effgid;
        break; 
    case GS_GETPGRP:
        p->PID = pmp->procgrp;
        break;
    case GS_GETPGID:
        if (p->PID == 0) {
            p->PID = pmp->procgrp;
            break;
        }

        retval = ESRCH;
        for (pmpi = &pmproc_table[0]; pmpi < &pmproc_table[NR_PROCS]; pmpi++) {
            if (pmpi->pid == p->PID) {
                p->PID = pmpi->procgrp;
                retval = 0;
                break;
            }
        }
        break;
        
    case GS_SETUID:
        uid = p->NEWID;

        if (uid != pmp->realuid && pmp->effuid != SU_UID) return EPERM;
        
        pmp->realuid = pmp->effuid = uid;

        msg2fs.type = PM_VFS_GETSETID;
        msg2fs.u.m3.m3i3 = GS_SETUID;
        msg2fs.ENDPOINT = p->source;
        msg2fs.UID = pmp->realuid;
        msg2fs.EUID = pmp->effuid;

        tell_vfs = 1;
        break;
    case GS_SETGID:
        gid = p->NEWID;

        if (gid != pmp->realgid && pmp->effuid != SU_UID) return EPERM;
        
        pmp->realgid = pmp->effgid = gid;

        msg2fs.type = PM_VFS_GETSETID;
        msg2fs.u.m3.m3i3 = GS_SETGID;
        msg2fs.ENDPOINT = p->source;
        msg2fs.GID = pmp->realgid;
        msg2fs.EGID = pmp->effgid;

        tell_vfs = 1;
        break;

    case GS_SETSID:
        if (pmp->procgrp == pmp->pid) {
            p->PID = -1;
            retval = EPERM;
            break;
        }

        pmp->procgrp = pmp->pid;
        p->PID = pmp->procgrp;

        break;
    default:
        retval = EINVAL;
        break;
    }

    if (tell_vfs) {
        send_recv(SEND, TASK_FS, &msg2fs);
        return SUSPEND;
    }

    return retval;
}

PUBLIC int do_getprocep(MESSAGE * p)
{
    pid_t pid = p->PID;
    int i;

    struct pmproc * pmp = pmproc_table;
    for (i = 0; i < NR_PROCS; i++, pmp++) {
        if (pmp->pid == pid) {
            p->ENDPOINT = pmp->endpoint;
            return 0;
        }
    }

    return ESRCH;
}

PUBLIC int do_pm_getinfo(MESSAGE * p)
{
    vir_bytes dest = (vir_bytes) p->BUF;
    size_t len;
    int request = p->REQUEST;
    vir_bytes src_addr;

    switch (request) {
    case PM_INFO_PROCTAB:
        src_addr = (vir_bytes) pmproc_table;
        len = sizeof(struct pmproc) * NR_PROCS;
        break;
    default:
        return EINVAL;
    }

    return data_copy(p->source, (void*) dest, SELF, (void*) src_addr, len);
}

PUBLIC int do_pm_getepinfo(MESSAGE* p)
{
    endpoint_t endpoint = p->ENDPOINT;
    struct pmproc* pmp = pm_endpt_proc(endpoint);
    if (pmp == NULL) return -ESRCH;

    p->EP_EFFUID = pmp->effuid;
    p->EP_EFFGID = pmp->effgid;

    return pmp->pid;
}
