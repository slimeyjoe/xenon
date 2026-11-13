# Host build: native gcc/clang, null backend, used for the conformance tests and tools.
CC      ?= cc
CXX     ?= c++
BACKEND := null
CONTROL := 1
EXESUF  :=

CSTD     := -std=c99
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
            -Wconversion -Wno-sign-conversion -Wvla
OPT      ?= -O2 -g
SAN      ?=
# SAN=1 enables the sanitizers: make TARGET=host SAN=1 test
ifeq ($(SAN),1)
OPT      := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
endif

# ymfm (C++) is built on the host too: tests/test_opn compares opn_lite with it.
CFLAGS   := $(CSTD) $(WARN) $(OPT) $(INCLUDES) -DXE_TARGET_HOST -DXE_HAVE_YMFM
LDLIBS   := -lm
EXTRA_SRCS := $(wildcard third_party/ymfm/*.cpp) $(wildcard engine/snd/*.cpp)

# the Opus voice decoder (docs/voice.md §3)
include third_party/opus/opus.mk
CFLAGS     += $(OPUS_ENGINE_CFLAGS)
EXTRA_SRCS += $(OPUS_SRCS)

COMPILE_C   = $(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c -o $@ $<
COMPILE_CXX = $(CXX) -std=c++14 $(OPT) -Ithird_party $(INCLUDES) -MMD -MP -c -o $@ $<
LINK        = $(CXX) $(OPT) -o $@ $(OBJS) $(LDLIBS)
LINK_TEST   = $(CXX) $(OPT) -o $@ $< $(ENGINE_OBJS) $(LDLIBS)
