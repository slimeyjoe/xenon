/* Start of a block of AmigaOS headers (no include guard: used once per block).
 *
 * vbcc's PowerPC backend aligns int and pointers to 4 bytes, but the OS structures are laid
 * out for the 68k (2-byte alignment) and neither the NDK nor the WarpOS headers say so.
 * On WarpOS's PPC side every OS header must therefore be read under #pragma amiga-align,
 * and nothing else: vc.lib (FILE, ...) and the engine use natural alignment, so <stdio.h>,
 * <string.h> and the project headers come after os_end.h. Everywhere else (the 68k, the
 * 68k objects of the WarpOS build, AROS, whose headers carry their own layout) both
 * headers are empty. warpos/oslayout.h lists the structures the backend uses; the WarpOS
 * build compares their layout with the 68k compiler's at startup. */
#if defined(XE_AMIGA_WARPOS) && defined(__PPC__)
#pragma amiga-align
#endif
