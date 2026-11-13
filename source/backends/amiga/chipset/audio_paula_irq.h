/* State shared between the Paula driver (audio_paula.c, the audio task: 68k or PPC) and its
 * audio interrupt handler (paula_irq.c, always 68k code). Only ULONG and pointer fields,
 * so both compilers lay it out the same way (checked by the layout guard, warpos/oslayout.h).
 *
 * It lives in chip RAM, aligned to 32 bytes (a PPC cache line), and is split by writer:
 * line 0 is written only by the handler, the rest only by the task (before the interrupt is
 * enabled, then flushed). On WarpOS the task invalidates line 0 before reading it, so a
 * cached copy never hides the handler's writes, and never drops a pending write of its own. */
#ifndef XE_AMIGA_AUDIO_PAULA_IRQ_H
#define XE_AMIGA_AUDIO_PAULA_IRQ_H

#define AM_PAULA_IRQ_LINE 32

struct am_paula_irq {
    /* line 0: written by the handler */
    volatile ULONG cur;             /* block whose address is in the DMA registers */
    volatile ULONG ints;            /* interrupts taken */
    ULONG pad0[6];
    /* lines 1-2: written by the task before the interrupt is enabled */
    struct Task *task;              /* signalled at every interrupt (on WarpOS a 68k mirror) */
    ULONG sigmask;
    ULONG len;                      /* AUDxLEN: words per block */
    ULONG bits14;
    ULONG hi_ch[2], lo_ch[2];       /* channel numbers: [0] left side, [1] right side */
    UBYTE *hi[2], *lo[2];           /* chip blocks (lo only for 14-bit) */
    ULONG intmask;                  /* INTF_AUDn of the channel whose interrupt this is */
    ULONG pad1[3];
};

/* the handler: SetIntVector(INTB_AUDn) with is_Data = the shared state (a1). Only its
 * address is used outside paula_irq.c (as is_Code), hence no parameters here. */
#ifndef XE_PAULA_IRQ_IMPL
void am_paula_irq_handler(void);
#endif

#endif
