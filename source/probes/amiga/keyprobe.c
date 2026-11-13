/* Keyboard input injected through input.device, as a keyboard would send it: for testing
 * the game's key handling (Ctrl held = skip waits) in a window and on its own screen.
 *
 *   keyprobe STEP...
 *
 * Steps, run in order:
 *   wN   wait N ticks (1/50 s)
 *   d    Ctrl down (raw key 0x63)
 *   u    Ctrl up
 *   b    Ctrl let go as Poseidon's USB boot keyboard class reports a modifier released
 *        alone (AROS, virt68k): raw key 0x63 again, without the up flag, Ctrl's
 *        qualifier bit cleared
 *   s    Ctrl let go in the same USB report as another key: no Ctrl event at all, only
 *        that key's (here LShift up, 0xE0) with the bit cleared
 *   kXX  raw key XX (hex, 0x80 set = up)
 *   mN   N relative mouse moves of one pixel, one tick apart (a flood of MOUSEMOVE
 *        messages for a window with REPORTMOUSE)
 *   MN   N relative mouse moves at once, no wait between them
 *   oF   log the steps to file F from here on (default: none)
 *
 * Each step is logged with the time since the start (ms), to be matched with the game's log.
 * Not to the console: a write to the shell window started from can activate it, and the
 * game window then gets no keys ("Run >file" does not redirect a vbcc program's stdout). */
#include <exec/types.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Device *TimerBase;
static struct MsgPort *g_tport, *g_iport;
static struct timerequest *g_treq;
static struct IOStdReq *g_ireq;
static struct EClockVal g_t0;
static ULONG g_efreq;
static UWORD g_qual;

static ULONG ms(void)
{
	struct EClockVal e;
	unsigned long long a, b;
	ReadEClock(&e);
	a = ((unsigned long long)e.ev_hi << 32) | e.ev_lo;
	b = ((unsigned long long)g_t0.ev_hi << 32) | g_t0.ev_lo;
	return (ULONG)((a - b) * 1000ULL / g_efreq);
}

static void inject(struct InputEvent *ie)
{
	ie->ie_NextEvent = NULL;
	g_ireq->io_Command = IND_WRITEEVENT;
	g_ireq->io_Data = ie;
	g_ireq->io_Length = sizeof *ie;
	g_ireq->io_Flags = 0;
	DoIO((struct IORequest *)g_ireq);
}

static void rawkey(UWORD code)
{
	struct InputEvent ie;
	memset(&ie, 0, sizeof ie);
	if ((code & 0x7f) == 0x63) {
		if (code & 0x80)
			g_qual &= ~IEQUALIFIER_CONTROL;
		else
			g_qual |= IEQUALIFIER_CONTROL;
	}
	ie.ie_Class = IECLASS_RAWKEY;
	ie.ie_Code = code;
	ie.ie_Qualifier = g_qual;
	inject(&ie);
}

/* a raw key with the qualifier as it is: no tracking of its own bit */
static void rawkey_as(UWORD code)
{
	struct InputEvent ie;
	memset(&ie, 0, sizeof ie);
	ie.ie_Class = IECLASS_RAWKEY;
	ie.ie_Code = code;
	ie.ie_Qualifier = g_qual;
	inject(&ie);
}

static void mouse_rel(WORD dx, WORD dy)
{
	struct InputEvent ie;
	memset(&ie, 0, sizeof ie);
	ie.ie_Class = IECLASS_RAWMOUSE;
	ie.ie_Code = IECODE_NOBUTTON;
	ie.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE | g_qual;
	ie.ie_X = dx;
	ie.ie_Y = dy;
	inject(&ie);
}

int main(int argc, char **argv)
{
	int i;
	FILE *log = 0;
	g_tport = CreateMsgPort();
	g_treq = (struct timerequest *)CreateIORequest(g_tport, sizeof *g_treq);
	g_iport = CreateMsgPort();
	g_ireq = (struct IOStdReq *)CreateIORequest(g_iport, sizeof *g_ireq);
	if (!g_treq || !g_ireq || OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)g_treq, 0) ||
	    OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)g_ireq, 0)) {
		printf("keyprobe: cannot open timer.device / input.device\n");
		return 20;
	}
	TimerBase = g_treq->tr_node.io_Device;
	g_efreq = ReadEClock(&g_t0);
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		long n = strtol(a + 1, 0, a[0] == 'k' ? 16 : 10), j;
		switch (a[0]) {
		case 'o':
			if (log)
				fclose(log);
			log = fopen(a + 1, "w");
			continue;
		case 'w':
			Delay(n);
			break;
		case 'd':
			rawkey(0x63);
			break;
		case 'u':
			rawkey(0xe3);
			break;
		case 'b':
			g_qual &= ~IEQUALIFIER_CONTROL;
			rawkey_as(0x63);
			break;
		case 's':
			g_qual &= ~IEQUALIFIER_CONTROL;
			rawkey_as(0xe0);
			break;
		case 'k':
			rawkey((UWORD)n);
			break;
		case 'm':
			for (j = 0; j < n; j++) {
				mouse_rel((j & 1) ? -1 : 1, 0);
				Delay(1);
			}
			break;
		case 'M':
			for (j = 0; j < n; j++)
				mouse_rel((j & 1) ? -1 : 1, 0);
			break;
		default:
			if (log)
				fprintf(log, "keyprobe: bad step %s\n", a);
			continue;
		}
		if (log) {
			struct timeval tv;
			GetSysTime(&tv);
			fprintf(log, "keyprobe %6lu ms (systime %lu): %s\n", (unsigned long)ms(),
			        (unsigned long)(tv.tv_secs % 1000) * 1000UL + tv.tv_micro / 1000, a);
			fflush(log);
		}
	}
	if (log)
		fclose(log);
	CloseDevice((struct IORequest *)g_ireq);
	CloseDevice((struct IORequest *)g_treq);
	DeleteIORequest((struct IORequest *)g_ireq);
	DeleteIORequest((struct IORequest *)g_treq);
	DeleteMsgPort(g_iport);
	DeleteMsgPort(g_tport);
	return 0;
}
