/* Compiler and ABI differences between the Amiga family's toolchains (README.md "Layout").
 *
 * The only place in the backend that tests compiler or CPU macros (__VBCC__, __M68K__,
 * __AROS__, ...). Everything else tests a flavour (XE_AMIGA_OS3, XE_AMIGA_WARPOS,
 * XE_AMIGA_AROS: exactly one is set by config/<TARGET>.mk), and that only where the code
 * differs by flavour, not by compiler. Include it after the OS headers (<exec/types.h>). */
#ifndef XE_AMIGA_AMCOMPAT_H
#define XE_AMIGA_AMCOMPAT_H

/* IPTR: an integer that holds a pointer (tag data, ReadArgs results). AROS defines it (64 bits
 * on x86_64); the NDK 3.x and WarpOS headers do not, and there a pointer is a ULONG. */
#if !defined(__AROS__)
typedef ULONG IPTR;
typedef LONG SIPTR;
#endif

/* 68k code: what the Paula interrupt handler and the WarpOS 68k objects are compiled as */
#if (defined(__VBCC__) && defined(__M68K__)) || defined(__mc68000__)
#define AM_CPU_68K 1
#endif

/* A parameter in a 68k register (vbcc): the interrupt handler gets is_Data in a1, and the
 * once-per-frame call (frameos.h) its struct in a0 when WarpOS calls it with Run68K. Other
 * compilers pass them the C way: those functions are then only ever called from C. */
#if defined(__VBCC__) && defined(__M68K__)
#define AM_REG(r) __reg(#r)
#else
#define AM_REG(r)
#endif

/* ReadArgs' result array: IPTR * on AROS, LONG * in the NDK 3.x headers */
#if defined(__AROS__)
#define AM_RDARGS_ARRAY(a) (a)
#else
#define AM_RDARGS_ARRAY(a) ((LONG *)(a))
#endif

/* The Workbench startup message (NULL from a shell): its name depends on the startup code. */
#if defined(__VBCC__)
extern struct WBStartup *_WBenchMsg;
#define am_wbmsg _WBenchMsg
#elif defined(__AROS__)
extern struct WBStartup *WBenchMsg;
#define am_wbmsg WBenchMsg
#endif

#endif
