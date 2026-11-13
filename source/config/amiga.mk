# AmigaOS 3.1+ build with vbcc 0.9h (C only). Source ~/amiga-sdk/env.sh first (vc on PATH).
#   make TARGET=amiga [CPU=68040]
VC      ?= vc +aos68k
CPU     ?= 68040
BACKEND := amiga
# the backend's parts (backends/amiga/README.md "Layout"): the shared core, exec glue, the
# classic chipset (Paula, planar chip RAM), the AmigaOS 3.x flavour
BACKEND_DIRS := $(addprefix backends/amiga/,core exec chipset os3)
# the Amiga family's icon and release archive rules (Makefile)
AMIGA_FAMILY := 1
# the player's ReadMe.txt (Makefile, backends/amiga/readme/)
README_PARTS := title-os3 intro requirements-os3 install-os3 controls options-os3 limitations-os3 credits tools-os3
EXESUF  :=
# vbcc writes no dependency files: every object depends on every header (Makefile)
AUTODEPS := 0

# -c99: C99 mode; -speed: optimise for speed; -cpu: code generation target.
# No -sd (small data): the engine's static data may exceed 64 KB.
# backends/amiga/include carries <devices/ahi.h> (the AHI developer header, not in the NDK).
OPT      ?= -O2 -speed
# XE_OPN_ASM_68K: opn_lite's per-sample render loop in 68020+ assembly (engine/snd/
# opn_lite_68k.s, bit-identical to the C loop; OPNASM=0 builds the C loop instead)
OPNASM   ?= 1
ifeq ($(OPNASM),1)
OPNDEF   := -DXE_OPN_ASM_68K
EXTRA_SRCS += engine/snd/opn_lite_68k.s
endif
DIST_NAME := amiga
CFLAGS   := -c99 -cpu=$(CPU) $(OPT) -DXE_AMIGA_OS3 $(OPNDEF) -Ibackends/amiga/include
COMPILE_S   = vasmm68k_mot -Fhunk -quiet -o $@ $<
# -lmieee: software floating point (the engine's offline render helper uses double)
LDLIBS   := -lamiga -lmieee

COMPILE_C   = $(VC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
COMPILE_CXX = $(error C++ is not available in the Amiga build)
LINK        = $(VC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)
LINK_TEST   = $(VC) $(CFLAGS) -o $@ $< $(ENGINE_OBJS) $(LDLIBS)
