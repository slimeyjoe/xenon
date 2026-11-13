/* dbgtype FILE | dbgtype ECHO TEXT: copies FILE (or TEXT and a newline) to the kernel debug output,
 * one RawPutChar per byte. aros.sh's QEMU runs boot with debug=serial, where that is the
 * serial port (serial.log on the host), and the live CD is read-only: this is how the run's
 * output gets out. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

int main(int argc, char **argv)
{
    UBYTE buf[512];
    LONG n, i;
    BPTR fh;
    if (argc < 2)
        return 10;
    if (argc > 2 && !strcmp(argv[1], "ECHO")) {
        const char *t = argv[2];
        while (*t)
            RawPutChar(*t++);
        RawPutChar('\n');
        return 0;
    }
    fh = Open((CONST_STRPTR)argv[1], MODE_OLDFILE);
    if (!fh)
        return 10;
    while ((n = Read(fh, buf, sizeof buf)) > 0)
        for (i = 0; i < n; i++)
            RawPutChar(buf[i]);
    Close(fh);
    return 0;
}
