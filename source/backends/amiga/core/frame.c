/* The main task's once-per-frame OS call (frameos.h): the shared struct and the call. */
#include "os_begin.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include "os_end.h"
#include <stdint.h>
#include "amiga.h"
#include "amsys.h"
#include "frameos.h"

#define LINE 32                     /* PPC cache line */
#define SIZE ((sizeof(struct am_frameos) + LINE - 1) & ~(ULONG)(LINE - 1))

struct am_frameos *am_fr;
static void *g_mem;

int am_frame_init(void)
{
    g_mem = AllocMem(SIZE + LINE, MEMF_PUBLIC | MEMF_CLEAR);
    if (!g_mem)
        return -1;
    am_fr = (struct am_frameos *)(((uintptr_t)g_mem + LINE - 1) & ~(uintptr_t)(LINE - 1));
    return 0;
}

void am_frame_free(void)
{
    if (g_mem)
        FreeMem(g_mem, SIZE + LINE);
    g_mem = 0;
    am_fr = 0;
}

void am_frame_call(ULONG ops)
{
    am_fr->ops = ops;
    am_frameos_call(am_fr, SIZE);
}
