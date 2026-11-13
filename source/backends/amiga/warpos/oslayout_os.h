/* The OS headers oslayout.h needs (no include guard; include it between os_begin.h and
 * os_end.h). */
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
