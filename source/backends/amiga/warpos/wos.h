/* WarpOS only: calling 68k code from the PPC (sys_wos.c). */
#ifndef XE_AMIGA_WOS_H
#define XE_AMIGA_WOS_H

#include "os_begin.h"
#include <exec/types.h>
#include <exec/libraries.h>
#include "os_end.h"

/* 68k register values for am_call68k: d0-d7, a0-a6 (PPArgs order) */
struct am_regs68k {
    ULONG d[8];
    ULONG a[7];
};

/* Call 68k code with registers (results come back in *r; returns d0). */
ULONG am_call68k(void *code, struct am_regs68k *r);

/* Call a 68k library function by base and LVO (a6 = base). */
ULONG am_call68k_lib(struct Library *base, LONG lvo, struct am_regs68k *r);

#endif
