/* SPDX-License-Identifier: BSD-2-Clause */
/* wosprobe: the OS structure layout as vbccppc sees it WITHOUT #pragma amiga-align (what a
 * plain build of the Amiga backend would get). Compared with the 68k table in wosprobe.c. */
#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <devices/timer.h>
#include <devices/audio.h>
#include <devices/ahi.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/intuitionbase.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <utility/tagitem.h>
#include <hardware/custom.h>
#include "audio_paula_irq.h"
#include "oslayout.h"
#include "wosprobe.h"

#define WP_VALUE(name, v) (ULONG)(v),
const ULONG wp_nat_layout[] = { AM_OSLAYOUT(WP_VALUE) WP_LAYOUT_END };
