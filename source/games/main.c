/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The program: the AGS engine (engine/) with the native modules of the games it supports
 * (games/<game>/), registered before it runs. Its name (XE_PROGRAM, from the game's game.mk)
 * is where a backend looks for the pack (<program>.pak) and keeps the saves. */
#include "xe.h"
#include "version.h"
#include "xenon/xenon.h"

/* XE_PROGRAM is a bare word on the command line (some compiler drivers lose quotes) */
#define STRING_(x) #x
#define STRING(x)  STRING_(x)
#define PROGRAM    STRING(XE_PROGRAM)

/* the AmigaOS Version command's string */
const char xe_amiga_version[] = "$VER: " PROGRAM " " XE_VERSION " (" XE_VERSION_DATE ")";

int main(int argc, char **argv)
{
    xenon_register();
    return xe_main(PROGRAM, argc, argv);
}
