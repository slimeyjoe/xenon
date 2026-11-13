/* The layout of the AmigaOS structures the backend uses, as a list of sizes and member
 * offsets. Compiled by both vbcc backends: the 68k one (oslayout68k.c)
 * gives the reference, the PowerPC one must agree, which it does only when the OS headers
 * were read under #pragma amiga-align (core/os_begin.h). The WarpOS build checks the two
 * tables at startup (sys_wos.c am_sys_check) and refuses to run on a mismatch.
 *
 * AM_OSLAYOUT(X) calls X(name, value) once per entry; keep it in step with the structures
 * the backend touches. */
#ifndef XE_AMIGA_OSLAYOUT_H
#define XE_AMIGA_OSLAYOUT_H

#include <stddef.h>
#include "frameos.h"

/* Needs the OS headers in oslayout_os.h (between os_begin.h and os_end.h) and
 * audio_paula_irq.h (the state shared with the 68k interrupt handler, also checked). The
 * once-per-frame call's struct (frameos.h) is checked too. */

#define AM_OSLAYOUT(X) \
    X("Node", sizeof(struct Node)) \
    X("Node.ln_Name", offsetof(struct Node, ln_Name)) \
    X("MinNode", sizeof(struct MinNode)) \
    X("List", sizeof(struct List)) \
    X("Message", sizeof(struct Message)) \
    X("MsgPort", sizeof(struct MsgPort)) \
    X("MsgPort.mp_SigBit", offsetof(struct MsgPort, mp_SigBit)) \
    X("MsgPort.mp_MsgList", offsetof(struct MsgPort, mp_MsgList)) \
    X("Task", sizeof(struct Task)) \
    X("Task.tc_SigWait", offsetof(struct Task, tc_SigWait)) \
    X("Task.tc_SPLower", offsetof(struct Task, tc_SPLower)) \
    X("Task.tc_SPUpper", offsetof(struct Task, tc_SPUpper)) \
    X("Process", sizeof(struct Process)) \
    X("Process.pr_CLI", offsetof(struct Process, pr_CLI)) \
    X("Interrupt", sizeof(struct Interrupt)) \
    X("Interrupt.is_Data", offsetof(struct Interrupt, is_Data)) \
    X("Interrupt.is_Code", offsetof(struct Interrupt, is_Code)) \
    X("Library", sizeof(struct Library)) \
    X("SignalSemaphore", sizeof(struct SignalSemaphore)) \
    X("IORequest", sizeof(struct IORequest)) \
    X("IORequest.io_Error", offsetof(struct IORequest, io_Error)) \
    X("IOStdReq", sizeof(struct IOStdReq)) \
    X("IOStdReq.io_Length", offsetof(struct IOStdReq, io_Length)) \
    X("IOStdReq.io_Data", offsetof(struct IOStdReq, io_Data)) \
    X("IOStdReq.io_Offset", offsetof(struct IOStdReq, io_Offset)) \
    X("timeval", sizeof(struct timeval)) \
    X("timerequest", sizeof(struct timerequest)) \
    X("timerequest.tr_time", offsetof(struct timerequest, tr_time)) \
    X("EClockVal", sizeof(struct EClockVal)) \
    X("IOAudio", sizeof(struct IOAudio)) \
    X("IOAudio.ioa_Data", offsetof(struct IOAudio, ioa_Data)) \
    X("IOAudio.ioa_Length", offsetof(struct IOAudio, ioa_Length)) \
    X("IOAudio.ioa_Period", offsetof(struct IOAudio, ioa_Period)) \
    X("AHIRequest", sizeof(struct AHIRequest)) \
    X("AHIRequest.ahir_Version", offsetof(struct AHIRequest, ahir_Version)) \
    X("AHIRequest.ahir_Type", offsetof(struct AHIRequest, ahir_Type)) \
    X("AHIRequest.ahir_Frequency", offsetof(struct AHIRequest, ahir_Frequency)) \
    X("AHIRequest.ahir_Link", offsetof(struct AHIRequest, ahir_Link)) \
    X("TagItem", sizeof(struct TagItem)) \
    X("BitMap", sizeof(struct BitMap)) \
    X("BitMap.Planes", offsetof(struct BitMap, Planes)) \
    X("RastPort", sizeof(struct RastPort)) \
    X("ViewPort", sizeof(struct ViewPort)) \
    X("ViewPort.ColorMap", offsetof(struct ViewPort, ColorMap)) \
    X("GfxBase.DisplayFlags", offsetof(struct GfxBase, DisplayFlags)) \
    X("Screen", sizeof(struct Screen)) \
    X("Screen.ViewPort", offsetof(struct Screen, ViewPort)) \
    X("Screen.RastPort", offsetof(struct Screen, RastPort)) \
    X("Screen.BitMap", offsetof(struct Screen, BitMap)) \
    X("ScreenBuffer.sb_BitMap", offsetof(struct ScreenBuffer, sb_BitMap)) \
    X("Window.UserPort", offsetof(struct Window, UserPort)) \
    X("Window.RPort", offsetof(struct Window, RPort)) \
    X("IntuiMessage", sizeof(struct IntuiMessage)) \
    X("IntuiMessage.Class", offsetof(struct IntuiMessage, Class)) \
    X("IntuiMessage.Code", offsetof(struct IntuiMessage, Code)) \
    X("IntuiMessage.Qualifier", offsetof(struct IntuiMessage, Qualifier)) \
    X("IntuiMessage.MouseX", offsetof(struct IntuiMessage, MouseX)) \
    X("IntuitionBase.FirstScreen", offsetof(struct IntuitionBase, FirstScreen)) \
    X("EasyStruct", sizeof(struct EasyStruct)) \
    X("WBStartup.sm_ArgList", offsetof(struct WBStartup, sm_ArgList)) \
    X("WBArg", sizeof(struct WBArg)) \
    X("DiskObject.do_ToolTypes", offsetof(struct DiskObject, do_ToolTypes)) \
    X("Custom.intreqr", offsetof(struct Custom, intreqr)) \
    X("Custom.aud", offsetof(struct Custom, aud)) \
    X("AudChannel", sizeof(struct AudChannel)) \
    X("am_paula_irq", sizeof(struct am_paula_irq)) \
    X("am_paula_irq.task", offsetof(struct am_paula_irq, task)) \
    X("am_paula_irq.hi", offsetof(struct am_paula_irq, hi)) \
    X("am_paula_irq.intmask", offsetof(struct am_paula_irq, intmask)) \
    X("am_frameos", sizeof(struct am_frameos)) \
    X("am_frameos.relative", offsetof(struct am_frameos, relative)) \
    X("am_frameos.ops", offsetof(struct am_frameos, ops)) \
    X("am_frameos.late_us", offsetof(struct am_frameos, late_us)) \
    X("am_frameos.avail_chip", offsetof(struct am_frameos, avail_chip)) \
    X("am_frameos.ev", offsetof(struct am_frameos, ev)) \
    X("am_frame_ev", sizeof(struct am_frame_ev))

#endif
