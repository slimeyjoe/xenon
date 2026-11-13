/* Shared between wosprobe.c (PPC) and wosprobe68k.c (68k). Only ULONG and pointer fields:
 * the layout is the same for both compilers. Include after the OS headers. */
#ifndef WOSPROBE_H
#define WOSPROBE_H

#define WP_LAYOUT_END 0xFFFFFFFFUL
#define WP_RAN_MAGIC  0x600DF00DUL

struct wp_vb {                  /* vertical-blank server data (chip RAM) */
    struct Task *task;          /* task to signal (a PPC task's 68k mirror) */
    ULONG mask;
    ULONG every;                /* signal every N frames */
    ULONG count;                /* frames seen */
    ULONG sent;                 /* signals sent */
};

struct wp_proc {                /* the 68k DOS process started from PPC */
    struct Task *parent;
    ULONG mask;
    struct Task *self;
    volatile ULONG ran;
};

#endif
