# AmigaOS 3.1 probes for the Xenon port.  vbcc only: vc on PATH (VBCC = its tree), then make.
# The runs need the rig's uaequit too: make -C ../../tests/amiga.  Plain GNU make.

VBCC	?= $(HOME)/amiga-sdk/vbcc
PATH	:= $(VBCC)/bin:$(PATH)
export PATH

VC	= vc +aos68k
CFLAGS	= -c99 -cpu=68020 -O1

PROGS	= sbprobe trailerprobe opnbench seekprobe iconprobe winprobe keyprobe

all: $(PROGS)

sbprobe: sbprobe.c vbserver.o
	$(VC) $(CFLAGS) -o $@ sbprobe.c vbserver.o -lamiga

vbserver.o: vbserver.s
	vasmm68k_mot -Fhunk -quiet -o $@ $<

seekprobe: seekprobe.c
	$(VC) $(CFLAGS) -o $@ $< -lamiga

iconprobe: iconprobe.c
	$(VC) $(CFLAGS) -o $@ $< -lamiga

winprobe: winprobe.c
	$(VC) $(CFLAGS) -o $@ $< -lamiga

keyprobe: keyprobe.c
	$(VC) $(CFLAGS) -o $@ $< -lamiga

# voicebench (probe 5): the engine's voice decoders built like the game, plus libopus as the
# WarpOS build has it (pseudo-stack: vbcc has no alloca). Not in PROGS: libopus takes a while.
VB_SRC	= ../..
VB_OPUS	= $(VB_SRC)/third_party/opus
VB_CF	= -c99 -cpu=68040 -O2 -speed
VB_OPUSDEF = -DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API -DNONTHREADSAFE_PSEUDOSTACK \
	  -I$(VB_OPUS)/include -I$(VB_OPUS)/celt -I$(VB_OPUS)/silk -I$(VB_OPUS)/src
VB_OPUSOBJ = $(patsubst $(VB_OPUS)/%.c,vbobj/opus/%.o,$(wildcard $(VB_OPUS)/celt/*.c $(VB_OPUS)/silk/*.c $(VB_OPUS)/src/*.c))
VB_ENGINE = $(VB_SRC)/engine/pack/pack.c $(VB_SRC)/engine/voice/vdec.c \
	  $(VB_SRC)/engine/voice/vdec_ldp.c $(VB_SRC)/engine/voice/vdec_opus.c
vbobj/opus/%.o: $(VB_OPUS)/%.c
	@mkdir -p $(dir $@)
	$(VC) $(VB_CF) $(VB_OPUSDEF) -c -o $@ $<
voicebench: voicebench.c $(VB_ENGINE) $(VB_OPUSOBJ)
	$(VC) $(VB_CF) -I$(VB_SRC) -I$(VB_SRC)/engine -I$(VB_SRC)/hal -DXE_HAVE_OPUS \
	    -I$(VB_OPUS)/include -o $@ voicebench.c $(VB_ENGINE) $(VB_OPUSOBJ) -lamiga -lmieee

# opnbench: the engine's opn_lite core + 20 s of a song's register log, built like the game
# (68040, -O2 -speed). SONG=XENON_nn picks the song (default XENON_24, the busiest); the
# binary is opnbench-<song>, its log bench-<song>/opnbench_log.h (opnbench is the default
# song's binary, for run-bench.sh).
SONG	?= XENON_24

opnbench: opnbench-$(SONG)
	cp opnbench-$(SONG) $@

# as the game is built (config/amiga.mk): the C core plus its 68k render loop
OPNCFLAGS = -c99 -cpu=68040 -O2 -speed -DXE_OPN_ASM_68K -I../../engine
OPNASM    = opn_lite_68k.o

# with a listing, so symprof.py can attribute samples inside the assembly loop too
opn_lite_68k.o: ../../engine/snd/opn_lite_68k.s
	vasmm68k_mot -Fhunk -quiet -L opn_lite_68k.lst -o $@ $<

bench-%/opnbench_log.h: mkopnlog.py ../../../resources/music/vgm/%.vgm
	mkdir -p bench-$*
	python3 mkopnlog.py ../../../resources/music/vgm/$*.vgm 20 $@

opnbench-%: opnbench.c ../../engine/snd/opn_lite.c ../../engine/snd/opn_core.h bench-%/opnbench_log.h $(OPNASM)
	$(VC) $(OPNCFLAGS) -Ibench-$* -o $@ opnbench.c ../../engine/snd/opn_lite.c $(OPNASM) -lamiga

# opnprof: opnbench with a PC-sampling profiler (opnprof.c, ciaprof.s). The core is compiled
# to assembly with -Dstatic= (so its functions have names), assembled with a listing
# (bench-<song>/opn_lite_prof.lst) and linked in; symprof.py maps the samples onto it:
#   make opnprof-XENON_24; ./run-prof.sh XENON_24 11025   (WinUAE)
#   python3 symprof.py bench-XENON_24/opn_lite_prof.lst bench-XENON_24/prof-11025.txt
opnprof-%: opnprof.c ciaprof.o ../../engine/snd/opn_lite.c ../../engine/snd/opn_core.h bench-%/opnbench_log.h $(OPNASM)
	$(VC) $(OPNCFLAGS) -Dstatic= -S -o bench-$*/opn_lite_prof.asm ../../engine/snd/opn_lite.c
	vasmm68k_mot -Fhunk -quiet -L bench-$*/opn_lite_prof.lst -o bench-$*/opn_lite_prof.o bench-$*/opn_lite_prof.asm
	$(VC) $(OPNCFLAGS) -Ibench-$* -o $@ opnprof.c bench-$*/opn_lite_prof.o ciaprof.o $(OPNASM) -lamiga

# host side of "opnbench sum" (bit-exactness of the Amiga build), built with the host cc
opnsum-%: opnsum.c ../../engine/snd/opn_lite.c ../../engine/snd/opn_core.h bench-%/opnbench_log.h
	cc -std=c99 -O2 -I../../engine -Ibench-$* -o $@ opnsum.c ../../engine/snd/opn_lite.c

ciaprof.o: ciaprof.s
	vasmm68k_mot -Fhunk -quiet -o $@ $<

# trailerprobe = hunk executable + payload + 16-byte trailer
#   ("XPKT" | u32 LE payload size | 8 bytes pad), see mktrailer.py.
# trailerprobe.hunk: the plain executable; trailerprobe.raw: payload and
# trailer appended raw (LoadSeg rejects it); trailerprobe: payload and
# trailer wrapped in a HUNK_DEBUG block (LoadSeg skips it).
trailerprobe: trailerprobe.c payload.bin mktrailer.py
	$(VC) $(CFLAGS) -o $@.hunk $< -lamiga
	python3 mktrailer.py raw $@.hunk payload.bin $@.raw
	python3 mktrailer.py debug $@.hunk payload.bin $@
	chmod 755 $@ $@.raw

payload.bin:
	printf 'XENON-PAYLOAD-TEST-0123456789abcdef\n' > $@
	head -c 4000 /dev/zero | tr '\0' 'x' >> $@

clean:
	rm -rf $(PROGS) vbserver.o ciaprof.o trailerprobe.hunk trailerprobe.raw payload.bin opnbench-* opnprof-* opnsum-* bench-* voicebench vbobj

.PHONY: all clean opnbench
