/* The OS structure layout as the 68k compiler sees it: the reference for the WarpOS build's
 * startup check (sys_wos.c am_sys_check). Compiled with the 68k compiler (config/warpos.mk). */
#include "os_begin.h"
#include "oslayout_os.h"
#include "os_end.h"
#include "audio_paula_irq.h"
#include "oslayout.h"

#define AM_LAYOUT_VALUE(name, v) (ULONG)(v),
const ULONG am_oslayout68k[] = { AM_OSLAYOUT(AM_LAYOUT_VALUE) 0xFFFFFFFFUL };
