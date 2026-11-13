/* SPDX-License-Identifier: GPL-3.0-or-later */
/* uaequit [BASE]: ends the WinUAE session the guest runs in, the last line of every rig run's
 * startup-sequence (run-lib.sh emu_script).
 *
 * WinUAE's boot ROM area ("rtarea") carries an entry point for guest programs, the uaelib
 * call, at rtarea + 0xFF60: a C function taking the function number first; number 13 asks the
 * emulator to exit. The area sits at 0xF00000 unless something else claims that address: a
 * Blizzard PPC board's ROM does, and WinUAE then moves the area (its log says where: "RTAREA
 * located at ..."; run-wos.sh passes it). BASE is that address in hex, with or without 0x. */

typedef unsigned long (*uae_call)(unsigned long function);

#define UAELIB_OFFSET   0xFF60UL
#define UAELIB_EXIT     13UL
#define RTAREA_DEFAULT  0xF00000UL

static unsigned long parse_hex(const char *s)
{
    unsigned long v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    else if (s[0] == '$')
        s++;
    for (; *s; s++) {
        int d = *s >= '0' && *s <= '9' ? *s - '0' :
                *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
                *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : -1;
        if (d < 0)
            break;
        v = v * 16 + (unsigned long)d;
    }
    return v;
}

int main(int argc, char **argv)
{
    unsigned long rtarea = argc > 1 ? parse_hex(argv[1]) : RTAREA_DEFAULT;
    uae_call call = (uae_call)(rtarea + UAELIB_OFFSET);
    call(UAELIB_EXIT);
    return 0;                       /* not reached when the emulator honours the call */
}
