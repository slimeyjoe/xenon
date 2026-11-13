/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * trailerprobe: finds its own executable (CLI: GetProgramDir() +
 * GetProgramName(); Workbench: WBStartup sm_ArgList[0]), reads the last
 * 16 bytes, checks for the "XPKT" trailer (magic, u32 LE payload size,
 * 8 bytes pad) that the Makefile appends after the last hunk, and prints
 * the first bytes of the payload.
 *
 * Output goes to stdout when started from the CLI, and is always also
 * appended to DH0:out.txt (a Workbench-started program has no stdout).
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern struct WBStartup *_WBenchMsg;	/* vbcc startup.o */

static BPTR logfh;

static void
out(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsprintf(buf, fmt, ap);
	va_end(ap);
	if (!_WBenchMsg) {
		fputs(buf, stdout);
		fflush(stdout);
	}
	if (logfh)
		Write(logfh, buf, n);
}

int
main(int argc, char **argv)
{
	char path[512];
	UBYTE trailer[16];
	UBYTE head[16];
	BPTR fh, dir = 0;
	LONG size, got, i;
	ULONG psize;
	int ok;

	/* From Workbench there is no stdout: append to DH0:out.txt ourselves.
	 * (From the CLI the shell holds that file open for the redirect.) */
	if (_WBenchMsg) {
		logfh = Open("DH0:out.txt", MODE_READWRITE);
		if (logfh)
			Seek(logfh, 0, OFFSET_END);
	}

	out("trailerprobe: started from %s (_WBenchMsg=%p argc=%d)\n",
	    _WBenchMsg ? "WORKBENCH" : "CLI", _WBenchMsg, argc);

	path[0] = 0;
	if (_WBenchMsg) {
		struct WBArg *wa = _WBenchMsg->sm_ArgList;
		out("WB: sm_NumArgs=%ld wa_Lock=0x%lx wa_Name=\"%s\"\n",
		    _WBenchMsg->sm_NumArgs, wa->wa_Lock,
		    wa->wa_Name ? wa->wa_Name : "(null)");
		dir = wa->wa_Lock;
		if (wa->wa_Name)
			strcpy(path, wa->wa_Name);
		if (dir) {
			char dn[256];
			if (NameFromLock(dir, dn, sizeof(dn)))
				out("WB: wa_Lock dir = \"%s\"\n", dn);
		}
	} else {
		char dn[256];
		dir = GetProgramDir();
		if (GetProgramName(path, sizeof(path)))
			out("CLI: GetProgramName=\"%s\" argv[0]=\"%s\"\n", path,
			    argv[0]);
		else
			out("CLI: GetProgramName failed, argv[0]=\"%s\"\n", argv[0]);
		if (dir && NameFromLock(dir, dn, sizeof(dn)))
			out("CLI: GetProgramDir=0x%lx \"%s\"\n", dir, dn);
		else
			out("CLI: GetProgramDir=0x%lx (no name)\n", dir);
	}

	/* open relative to the program's directory */
	{
		BPTR old = 0;
		if (dir)
			old = CurrentDir(dir);
		fh = Open(path, MODE_OLDFILE);
		if (dir)
			CurrentDir(old);
	}
	if (!fh) {
		out("Open(\"%s\") failed, IoErr=%ld\n", path, IoErr());
		goto done;
	}
	size = Seek(fh, 0, OFFSET_END);
	size = Seek(fh, 0, OFFSET_CURRENT);
	out("file \"%s\" size %ld\n", path, size);
	if (size < 16) {
		out("too small\n");
		Close(fh);
		goto done;
	}
	Seek(fh, -16, OFFSET_END);
	got = Read(fh, trailer, 16);
	ok = got == 16 && memcmp(trailer, "XPKT", 4) == 0;
	psize = (ULONG)trailer[4] | (ULONG)trailer[5] << 8 |
	    (ULONG)trailer[6] << 16 | (ULONG)trailer[7] << 24;
	out("trailer read %ld bytes: %02x %02x %02x %02x -> %s, payload size "
	    "%lu\n", got, trailer[0], trailer[1], trailer[2], trailer[3],
	    ok ? "XPKT FOUND" : "NO TRAILER", psize);
	if (ok && psize + 16 <= (ULONG)size) {
		Seek(fh, size - 16 - psize, OFFSET_BEGINNING);
		got = Read(fh, head, 16);
		out("payload at offset %lu, first %ld bytes:", size - 16 - psize,
		    got);
		for (i = 0; i < got; i++)
			out(" %02x", head[i]);
		out("  \"");
		for (i = 0; i < got; i++)
			out("%c", head[i] >= 32 && head[i] < 127 ? head[i] : '.');
		out("\"\n");
	}
	Close(fh);
done:
	out("trailerprobe: done\n");
	if (logfh)
		Close(logfh);
	return 0;
}
