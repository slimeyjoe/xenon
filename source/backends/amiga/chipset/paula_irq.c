/* The Paula audio interrupt handler (68k code in both builds: the WarpOS build compiles this
 * file with the 68k compiler, config/warpos.mk).
 *
 * SetIntVector(INTB_AUDn) handler, a1 = is_Data = the shared state (audio_paula_irq.h); one
 * instance for the music's channels and one for the voice's. Called at the start of every
 * block of the stream's first channel, when the DMA has latched the location/length registers
 * for it: points the registers at the other block and signals the audio task to refill the
 * one that just finished. */
#define __NOLIBBASE__
#define XE_PAULA_IRQ_IMPL
#include "os_begin.h"
#include <exec/types.h>
#include <exec/execbase.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include "os_end.h"
#include "amcompat.h"
#include "audio_paula_irq.h"

#define SysBase (*(struct ExecBase **)4L)
#define HWCUSTOM ((volatile struct Custom *)0xdff000)

void am_paula_irq_handler(AM_REG(a1) struct am_paula_irq *p)
{
    int i;
    ULONG nxt;
    HWCUSTOM->intreq = (UWORD)p->intmask;
    p->ints++;
    nxt = p->cur ^ 1;
    p->cur = nxt;
    for (i = 0; i < 2; i++) {
        HWCUSTOM->aud[p->hi_ch[i]].ac_ptr = (UWORD *)p->hi[nxt];
        HWCUSTOM->aud[p->hi_ch[i]].ac_len = (UWORD)p->len;
        if (p->bits14) {
            HWCUSTOM->aud[p->lo_ch[i]].ac_ptr = (UWORD *)p->lo[nxt];
            HWCUSTOM->aud[p->lo_ch[i]].ac_len = (UWORD)p->len;
        }
    }
    Signal(p->task, p->sigmask);
}
