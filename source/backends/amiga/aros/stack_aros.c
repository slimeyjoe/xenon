/* The main task's stack on AROS (flavour.h am_main_stack). Started from an icon the task gets
 * the icon's stack, from a shell the shell's (256 KB). Hosted AROS runs host code (Xlib through
 * the X11 driver, signal handling) on the calling task's stack: with 64 KB the X11 driver
 * crashed the whole system within minutes (probes/aros/README.md), as its own startup code
 * warns ("some X servers need a larger than normal stack"). So a smaller stack is swapped for a
 * fresh 256 KB one with NewStackSwap before the program starts. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include "amiga.h"
#include "flavour.h"

#define MIN_STACK (256UL * 1024UL)

static int (*g_body)(void);

static LONG trampoline(void)
{
    return g_body();
}

int am_main_stack(int (*body)(void))
{
    struct Task *t = FindTask(NULL);
    struct StackSwapStruct sss;
    struct StackSwapArgs args;
    IPTR have = (IPTR)t->tc_SPUpper - (IPTR)t->tc_SPLower;
    UBYTE *mem;
    int rc;
    if (have >= MIN_STACK)
        return body();
    mem = AllocMem(MIN_STACK, MEMF_ANY);
    if (!mem)
        return body();                  /* better than not starting */
    g_body = body;
    sss.stk_Lower = mem;
    sss.stk_Upper = mem + MIN_STACK;
    sss.stk_Pointer = mem + MIN_STACK;
    rc = (int)NewStackSwap(&sss, (APTR)trampoline, &args);
    FreeMem(mem, MIN_STACK);
    return rc;
}
