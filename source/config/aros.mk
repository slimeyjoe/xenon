# AROS build (x86_64, ABIv11: deadwood's AROS, release 2026.09 and AROS One 1.3) with the AROS
# cross gcc. Source ~/amiga-sdk/env.sh first (x86_64-aros-gcc and the SDK sysroot on PATH, see
# tests/aros/README.md "Environment").
#   make TARGET=aros
# The Amiga backend's parts (backends/amiga/README.md "Layout"): the shared core, the exec
# system glue and the AROS flavour; no chipset (sound through AHI, chunky video upload).
# (make's built-in CC=cc would win over ?=: replace only that default)
ifeq ($(origin CC),default)
CC      := x86_64-aros-gcc
endif
STRIP   ?= x86_64-aros-strip
BACKEND := amiga
BACKEND_DIRS := $(addprefix backends/amiga/,core exec aros)
# the Amiga family's icon and release archive rules (Makefile)
AMIGA_FAMILY := 1
ICON_FLAVOUR := aros
# the player's ReadMe.txt (Makefile, backends/amiga/readme/)
README_PARTS := title-aros intro requirements-aros install-aros controls options-aros limitations-aros credits tools-aros
# xenon-aros-x86_64-<version>.lha (named like the other flavours' archives), a stripped
# executable and its upload description
DIST_NAME   := aros-x86_64
DIST_STRIP  := $(STRIP) --strip-unneeded
DIST_UPLOAD_README := backends/amiga/aros/upload.readme
EXESUF  :=

CSTD     := -std=gnu99         # the AROS headers use the asm keyword
WARN     := -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wvla \
            -Wno-unused-parameter -Wno-pointer-sign   # STRPTR is unsigned char *
OPT      ?= -O2
# AROS_SDK: the SDK's Development directory, the compiler's sysroot (env.sh sets it)
SYSROOT  := $(if $(AROS_SDK),--sysroot=$(AROS_SDK))
CFLAGS   := $(CSTD) $(WARN) $(OPT) $(SYSROOT) -DXE_AMIGA_AROS
LDLIBS   := -lm

# the Opus voice decoder (docs/voice.md §3, §5.3): variable-length arrays for libopus's
# scratch memory (the AROS SDK's <alloca.h> does not declare alloca under these flags)
OPUS_CSTD := -std=gnu99 $(SYSROOT)
OPUS_DEFS := -DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API -DVAR_ARRAYS
include third_party/opus/opus.mk
CFLAGS     += $(OPUS_ENGINE_CFLAGS)
EXTRA_SRCS += $(OPUS_SRCS)

COMPILE_C   = $(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c -o $@ $<
COMPILE_CXX = $(error C++ is not used in the AROS build)
LINK        = $(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)
LINK_TEST   = $(CC) $(CFLAGS) -o $@ $< $(ENGINE_OBJS) $(LDLIBS)
