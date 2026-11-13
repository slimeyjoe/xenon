# WarpOS (PowerPC, WarpUP 5.x) build with vbcc 0.9h: the Amiga backend compiled for the PPC.
# Source ~/amiga-sdk/env.sh first (vc on PATH, the warpos config, see ~/amiga-sdk/README).
#   make TARGET=warpos
# The executable is a WarpOS (EHF) file: 68k startup + PPC code + a few 68k objects for the
# parts that run on the 68k (M68K_SRCS below).
# Design notes: backends/amiga/README.md "WarpOS"; probes/warpos/README.md.
VC      ?= vc +warpos
VC68K   ?= vc +aos68k
BACKEND := amiga
# the backend's parts (backends/amiga/README.md "Layout"): the shared core, the classic
# chipset, the WarpOS flavour with its system glue (instead of exec/)
BACKEND_DIRS := $(addprefix backends/amiga/,core chipset warpos)
# the Amiga family's icon and release archive rules (Makefile)
AMIGA_FAMILY := 1
# the player's ReadMe.txt (Makefile, backends/amiga/readme/)
README_PARTS := title-os3 intro requirements-os3 install-os3 controls options-os3 limitations-os3 credits tools-os3
EXESUF  :=
DIST_NAME := warpos
AUTODEPS := 0

# -c99; -O2 -speed; XE_AMIGA_WARPOS is the flavour (core/amcompat.h).
# No 68k assembly: opn_lite renders with its C loop. The OS headers are read under
# #pragma amiga-align (backends/amiga/core/os_begin.h), never the whole program.
OPT      ?= -O2 -speed
CFLAGS   := -c99 $(OPT) -DXE_AMIGA_WARPOS -Ibackends/amiga/include
# the PPC has an FPU: libm, not the 68k build's software floating point (-lmieee)
LDLIBS   := -lamiga -lm

# the Opus voice decoder (docs/voice.md §3, §5.3): vbcc has no alloca, so libopus's own
# scratch stack (120 KB, allocated at the first clip)
OPUS_CSTD := -c99
OPUS_DEFS := -DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API -DNONTHREADSAFE_PSEUDOSTACK
include third_party/opus/opus.mk
CFLAGS     += $(OPUS_ENGINE_CFLAGS)
EXTRA_SRCS += $(OPUS_SRCS)

# 68k objects linked into the PPC executable: the once-per-frame OS call, the Paula interrupt
# handler, the layout guard's reference table. They are ordinary sources of their parts, so
# the Makefile lists them; this static pattern rule (an explicit rule: it wins over the
# Makefile's generic %.c rule) has them compiled by the 68k compiler (BUILD is set before this
# file is read).
M68K_SRCS  := backends/amiga/core/frameos.c backends/amiga/chipset/paula_irq.c \
              backends/amiga/warpos/oslayout68k.c
M68K_OBJS  := $(patsubst %.c,$(BUILD)/%.o,$(M68K_SRCS))
CFLAGS68 := -c99 -O2 -cpu=68020 -DXE_AMIGA_WARPOS -Ibackends/amiga/include
$(M68K_OBJS): $(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC68K $<"
	$(Q)$(VC68K) $(CFLAGS68) $(INCLUDES) -c -o $@ $<

COMPILE_C   = $(VC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
COMPILE_CXX = $(error C++ is not available in the WarpOS build)
LINK        = $(VC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)
LINK_TEST   = $(VC) $(CFLAGS) -o $@ $< $(ENGINE_OBJS) $(LDLIBS)
