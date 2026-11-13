# The Amiga rig's guest helper (README.md): uaequit, which every WinUAE run's startup-sequence
# ends with. vbcc: vc on PATH (VBCC = its tree), then make. Plain GNU make.

VBCC	?= $(HOME)/amiga-sdk/vbcc
PATH	:= $(VBCC)/bin:$(PATH)
export PATH

VC	= vc +aos68k
CFLAGS	= -c99 -cpu=68020 -O1

all: uaequit

uaequit: uaequit.c
	$(VC) $(CFLAGS) -o $@ $< -lamiga

clean:
	rm -f uaequit

.PHONY: all clean
