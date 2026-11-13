# MorphOS probes for the Xenon port: 68k AmigaOS binaries (vbcc), run under MorphOS's 68k
# emulation in QEMU pegasos2 (README.md, ../../tests/morphos/mos.sh). vc on PATH (VBCC = its
# tree), then make. Plain GNU make.

VBCC	?= $(HOME)/amiga-sdk/vbcc
PATH	:= $(VBCC)/bin:$(PATH)
export PATH

VC	= vc +aos68k
CFLAGS	= -c99 -cpu=68020 -O1

PROGS	= rtgprobe blitprobe

all: $(PROGS)

rtgprobe: rtgprobe.c
	$(VC) $(CFLAGS) -o $@ $< -lamiga

blitprobe: blitprobe.c
	$(VC) $(CFLAGS) -o $@ $< -lamiga

clean:
	rm -f $(PROGS)

.PHONY: all clean
