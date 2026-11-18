# WarpOS probes: PowerPC executables (vc +warpos) with a 68k part (vc +aos68k).
# vc on PATH (with its warpos and aos68k configs). Run them with ../../lrvne/rigs/winuae/run-wos.sh
# (README.md).
VCPPC	= vc +warpos
VC68K	= vc +aos68k
INC	= -DLRV_AMIGA_WARPOS $(addprefix -I../../lrvne/backends/amiga/,core chipset warpos include)
CFLAGS	= -c99 -O2 $(INC)
CFLAGS68 = -c99 -O1 -cpu=68020 $(INC)

PROGS	= wosprobe stallprobe

all: $(PROGS)

wosprobe: wosprobe.o wosprobe_nat.o wosprobe68k.o
	$(VCPPC) -o $@ $^ -lamiga

stallprobe: stallprobe.c
	$(VCPPC) $(CFLAGS) -o $@ $< -lamiga

wosprobe.o: wosprobe.c wosprobe.h ../../lrvne/backends/amiga/warpos/oslayout.h
	$(VCPPC) $(CFLAGS) -c -o $@ $<

wosprobe_nat.o: wosprobe_nat.c wosprobe.h ../../lrvne/backends/amiga/warpos/oslayout.h
	$(VCPPC) $(CFLAGS) -c -o $@ $<

wosprobe68k.o: wosprobe68k.c wosprobe.h ../../lrvne/backends/amiga/warpos/oslayout.h
	$(VC68K) $(CFLAGS68) -c -o $@ $<

clean:
	rm -f $(PROGS) *.o

.PHONY: all clean
