# SDL3 build (Linux/Wayland and anything else SDL3 runs on). C engine + C++ ymfm core.
CC      ?= cc
CXX     ?= c++
BACKEND := sdl3
CONTROL := 1
E2E_BACKEND := sdl3
EXESUF  :=

SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)

CSTD     := -std=c99
WARN     := -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wvla
OPT      ?= -O2 -g

CFLAGS   := $(CSTD) $(WARN) $(OPT) $(SDL_CFLAGS) -D_DEFAULT_SOURCE -DXE_TARGET_SDL3 -DXE_HAVE_YMFM
LDLIBS   := $(SDL_LIBS) -lm -lstdc++

EXTRA_SRCS := $(wildcard third_party/ymfm/*.cpp) $(wildcard engine/snd/*.cpp)
ifeq ($(EXTRA_SRCS),)
LDLIBS := $(SDL_LIBS) -lm
endif

# the Opus voice decoder (docs/voice.md §3)
include third_party/opus/opus.mk
CFLAGS     += $(OPUS_ENGINE_CFLAGS)
EXTRA_SRCS += $(OPUS_SRCS)

COMPILE_C   = $(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c -o $@ $<
COMPILE_CXX = $(CXX) -std=c++14 $(OPT) -Ithird_party $(INCLUDES) -MMD -MP -c -o $@ $<
LINK        = $(CXX) $(OPT) -o $@ $(OBJS) $(LDLIBS)
LINK_TEST   = $(CC) $(OPT) -o $@ $< $(ENGINE_OBJS) $(LDLIBS)
