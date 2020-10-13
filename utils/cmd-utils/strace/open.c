#include <stdio.h>
#include <fcntl.h>

#include "types.h"
#include "xlat.h"
#include "proto.h"

#include "xlat/open_access_modes.h"
#include "xlat/open_mode_flags.h"

static void print_open_mode(int flags)
{
    const char* str;

    str = xlookup(&open_access_modes, flags & O_ACCMODE);
    flags &= ~O_ACCMODE;
    if (str) {
        printf("%s", str);

        if (flags) putchar('|');
    }

    if (!str || flags) print_flags(flags & ~O_ACCMODE, &open_mode_flags);
}

int trace_open(struct tcb* tcp)
{
    MESSAGE* msg = &tcp->msg_in;

    printf("open(");
    print_path(tcp, msg->PATHNAME, msg->NAME_LEN);
    printf(", ");

    print_open_mode(msg->FLAGS);

    if (msg->FLAGS & O_CREAT) {
        printf(", ");
        print_mode_t(msg->MODE);
    }
    printf(")");

    return RVAL_DECODED;
}
