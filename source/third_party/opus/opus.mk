# libopus 1.6.1 (https://opus-codec.org, BSD licence: COPYING), decoder only, fixed point.
# Only the files the decoder links are vendored (the decoder's objects from a full static build
# of CELT, SILK and SILK fixed; docs/voice.md §3).
# Local change: celt/fixed_generic.h has QCONST16/QCONST32 variants for vbcc (#ifdef __VBCC__),
# whose constant folding gets two of the originals' conversions wrong (docs/voice.md §5.3).
# A target opts in with (OPUS_DEFS and OPUS_CSTD set before, where they differ)
#   include third_party/opus/opus.mk
#   CFLAGS     += $(OPUS_ENGINE_CFLAGS)
#   EXTRA_SRCS += $(OPUS_SRCS)
# USE_ALLOCA needs alloca() declared: HAVE_ALLOCA_H makes libopus include <alloca.h> (glibc's
# <stdlib.h> declares it only outside strict C99, and an undeclared alloca truncates the
# pointer on 64-bit targets; GCC 14+ refuses to compile it). A target without alloca uses
# NONTHREADSAFE_PSEUDOSTACK or VAR_ARRAYS instead.
OPUS_DIR     := third_party/opus
OPUS_SRCS    := $(sort $(wildcard $(OPUS_DIR)/celt/*.c $(OPUS_DIR)/silk/*.c $(OPUS_DIR)/src/*.c))
OPUS_DEFS    ?= -DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API -DUSE_ALLOCA -DHAVE_ALLOCA_H
OPUS_CSTD    ?= -std=c99
OPUS_CFLAGS   = $(OPUS_CSTD) $(OPT) $(OPUS_DEFS) -I$(OPUS_DIR)/include -I$(OPUS_DIR)/celt \
                -I$(OPUS_DIR)/silk -I$(OPUS_DIR)/src
OPUS_ENGINE_CFLAGS := -DXE_HAVE_OPUS -I$(OPUS_DIR)/include
