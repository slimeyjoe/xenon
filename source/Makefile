# SPDX-License-Identifier: GPL-3.0-or-later
# The AGS engine (C's Ware's AGS.EXE, PC-98) - GNU Make 4
#
#   make [TARGET=host|sdl3|amiga|warpos|aros] [V=1]  build the engine for a target (default: host,
#                                             the headless backend the tests drive; sdl3 = the
#                                             player; the others the Amiga family's programs)
#   make data SRC=...                         import the original game from your copy (floppy or
#                                             HDD images, a zip, an installed copy) into
#                                             ../resources/original/ and derive the resources
#                                             from it (../docs/game-data.md)
#   make pack                                 build the asset pack $(PAK) (fonts, translated
#                                             scripts, all languages; lang/README.md)
#   make voice-pack WINDAT=...                the optional voice pack $(VOICE_PAK) from the
#        [CODEC=opus|ldp6]                    game's Windows release (../docs/voice.md)
#   make text-view                            the translations with the Japanese of the game
#                                             written back (../text/view/, ../text/README.md)
#   make TARGET=host test [SAN=1]             build and run the conformance tests
#   make TARGET=host e2e [E2E_ARGS=...]       play the game end to end (tests/e2e/README.md)
#   make TARGET=sdl3 e2e [E2E_ARGS=...]       the same on the SDL3 backend offscreen, with the
#                                             presentation (rendered pixels) checks
#   make TARGET=aros e2e [E2E_ARGS=...]       the AROS build in hosted AROS (tests/e2e/aros.py)
#   make TARGET=amiga e2e [E2E_ARGS=...]      the Amiga build in WinUAE (tests/e2e/amiga.py)
#   make TARGET=amiga|warpos|aros dist        the release archive $(BUILD)/<program>-<version>-<target>.lha
#                                             (program, icons, the player's ReadMe; no pack; the
#                                             icons are made from the game's logo: make data first)
#   make clean                                remove ../build/
#   make data-clean                           remove everything made from the game outside ../build/
#   make distclean                            clean + data-clean: only the committed files are left
#
# The engine (engine/) is the interpreter, the picture and animation formats, the text printer
# and the sound API of the original. Everything below them - the frame loop, the screen, the
# PMD music driver (lrvne's PMD module), the FM chip, the mixer and the voice player, fonts,
# the pack, the backends - is lrvne
# (the submodule lrvne/), compiled from source into the engine by its make fragment, which
# also defines the targets and their switches (lrvne/mk/lrvne.mk: LRVNE_YMFM, LRVNE_OPUS,
# LRVNE_PMD, LRVNE_CONTROL, SAN).

LRVNE     := lrvne
TARGET    ?= host
# Everything built goes to the top-level build/ (../build from here): build/<TARGET>/ for each
# target, the packs and their intermediates (fonts, translated scripts) in build/ itself.
BUILDROOT := ../build
BUILD     := $(BUILDROOT)/$(TARGET)

# The game whose data `make data`, `make pack`, the tests and the release archives work on:
# a directory games/<GAME>/ with its game.mk (the program's name, the profile, the archive's
# parts) and its native modules. Without GAME, the only one there is.
GAME      ?= $(notdir $(patsubst %/,%,$(dir $(wildcard games/*/game.mk))))
ifneq ($(words $(GAME)),1)
$(error set GAME to one of: $(GAME))
endif
include games/$(GAME)/game.mk

# the engine (engine/) and the games' native modules with the program's main (games/)
ENGINE_SRCS := $(sort $(wildcard engine/*.c engine/*/*.c))
GAMES_SRCS  := $(sort $(wildcard games/*.c games/*/*.c))
APP_SRCS    := $(ENGINE_SRCS) $(GAMES_SRCS)
APP_INCLUDES := -Iengine -Igames
APP_DEFS    := -DXE_PROGRAM=$(APP_NAME)
APP_VERSION := $(shell sed -n 's/^\#define XE_VERSION *"\(.*\)"/\1/p' engine/version.h)
# the voice pack's Opus clips; the 68k build cannot decode them in real time (ldp6 there)
LRVNE_OPUS ?= $(if $(filter amiga,$(TARGET)),0,1)
# the music driver: lrvne's PMD module
LRVNE_PMD := 1

ifeq ($(wildcard $(LRVNE)/mk/lrvne.mk),)
$(error the lrvne submodule is missing: git submodule update --init)
endif

# the Amiga family's release archives (lrvne/mk/amiga-dist.mk): the player's ReadMe from the
# game's parts, the icons made by tools/mkicon.py (below)
AMIGA_DIST := $(GAME_DIST)
ifeq ($(TARGET),aros)
LRVNE_DIST_README_PARTS := $(addprefix $(AMIGA_DIST)/,$(addsuffix .txt,title-aros intro \
    requirements-aros install-aros controls options-aros limitations-aros credits tools-aros))
LRVNE_DIST_UPLOAD_README := $(AMIGA_DIST)/upload-aros.readme
ICON_FLAVOUR := aros
else
LRVNE_DIST_README_PARTS := $(addprefix $(AMIGA_DIST)/,$(addsuffix .txt,title-os3 intro \
    requirements-os3 install-os3 controls options-os3 limitations-os3 credits tools-os3))
ICON_FLAVOUR := os3
endif
LRVNE_DIST_DRAWER := $(GAME_DRAWER)
LRVNE_DIST_ICON := $(BUILDROOT)/$(TARGET)/$(APP_NAME).info
LRVNE_DIST_DRAWER_ICON := $(BUILDROOT)/$(TARGET)/drawer.info

.DEFAULT_GOAL := all
include $(LRVNE)/mk/lrvne.mk

.PHONY: all clean test e2e dist
all: $(EXE)

clean:
	rm -rf $(BUILDROOT)

# ---------------------------------------------------------------------------------------------
# Conformance tests (host target). Every test compares the engine with data from the original
# game or a verified reference tool (tools/, ../re-tools/), never with its own output. A test
# program is the engine alone (no games, no main), and the library, on the test's own HAL
# (tests/test_hal.h); tests/run.sh runs them with their Python comparers.
# ---------------------------------------------------------------------------------------------
TEST_SRCS := $(sort $(wildcard tests/*.c))
TEST_EXES := $(patsubst tests/%.c,$(BUILD)/tests/%$(EXESUF),$(TEST_SRCS))
TEST_OBJS := $(filter-out $(BUILD)/engine/app.o,$(call lrvne_obj,$(ENGINE_SRCS))) $(LRVNE_LIB_OBJS)
-include $(patsubst tests/%.c,$(BUILD)/tests/%.d,$(TEST_SRCS))

$(BUILD)/tests/%$(EXESUF): $(BUILD)/tests/%.o $(TEST_OBJS)
	@mkdir -p $(dir $@)
	@echo "  LINK  $@"
	$(Q)$(LRVNE_LD) $(OPT) -o $@ $< $(TEST_OBJS) $(LDLIBS)

test: $(TEST_EXES) $(EXE)
	$(Q)GAME=$(GAME) EXE=$(EXE) PAK=$(PAK) VOICE_PAK=$(VOICE_PAK) tests/run.sh $(BUILD)/tests

# ---------------------------------------------------------------------------------------------
# The game's data (../docs/game-data.md), which the repository does not carry: `make data
# SRC="..."` imports it from your copy of the original release (D88/FDI floppy images, HDI/NHD
# hard-disk images, a zip of them, or a directory with the game's files or the images; several
# SRCs add up; without SRC the directories in the environment variable LRVNE_MEDIA) into
# ../resources/original/, checking every file against ../resources/inventory.tsv, then derives
# the resources the RE documents and the tests use (DATA_PARTS, default all: images anim
# scripts sfx music saves vgm; DATA_PY = a Python with Pillow, and with unicorn for vgm, the
# tests' reference recordings). Parts whose module is missing are skipped with a note.
# ---------------------------------------------------------------------------------------------
GAMEDIR    ?= ../resources/original/game
DATA_PY    ?=
LRVNE_PY   := python3 -I
LRVNE_TOOL := $(LRVNE_PY) $(LRVNE)/tools/lrvne
.PHONY: data data-derive data-check
data:
	$(Q)test -n "$(SRC)$$LRVNE_MEDIA" || { echo "data: set SRC=<your copy of the game> (../docs/game-data.md)"; exit 1; }
	$(Q)$(LRVNE_PY) tools/gamedata.py import $(SRC)
	$(Q)$(LRVNE_PY) tools/gamedata.py derive $(if $(DATA_PY),--python $(DATA_PY)) $(DATA_PARTS)
data-derive:
	$(Q)$(LRVNE_PY) tools/gamedata.py derive $(if $(DATA_PY),--python $(DATA_PY)) $(DATA_PARTS)
data-check:
	$(Q)$(LRVNE_PY) tools/gamedata.py check

# The translators' view (../text/README.md): the dictionaries with the Japanese written back from
# GAMEDIR into ../text/view/ (not committed: the dictionaries' keys only point at the game's
# script, tools/tlkeys.py), plus view/lines.tsv with every entry's place, voice clips and text.
.PHONY: text-view
text-view:
	$(Q)$(LRVNE_PY) tools/tlkeys.py --game $(GAMEDIR) view

# Everything made from the original game outside ../build/ (the ignored paths of ../.gitignore):
# the imported files, the derived resources, the voice review report, the disassembly
# listings, the translators' view, the e2e goldens' local pictures.
DATA_GENERATED := ../resources/original ../resources/images ../resources/anim \
	../resources/scripts ../resources/music/vgm ../resources/sfx/blobs ../resources/sfx/vgm \
	../resources/saves ../resources/voice/report.txt \
	$(addprefix ../docs/re/listing/,ags.asm pmd.asm funcs.tsv pmd-funcs.tsv) \
	../text/view tests/e2e/golden
.PHONY: data-clean distclean
data-clean:
	rm -rf $(DATA_GENERATED)
distclean: clean data-clean

# ---------------------------------------------------------------------------------------------
# The asset pack: fonts (lrvne's xfont from the X11 bitmap fonts lrvne carries), the translated
# scripts (tools/tl_extract.py from the translation dictionaries in ../text/) and everything
# from the original game (tools/mkpack.py, on lrvne's pack writer). The same for every target.
# The pack is named after the program, which is where a backend looks for it. TL_COMPARE=DIR
# also reports the English strings that differ from DIR/*.U.CC in ../build/en/lint.txt.
# ---------------------------------------------------------------------------------------------
X11FONTS   ?= $(LRVNE)/fonts/x11
TL_DICT_EN ?= ../text/translation/script_en.txt
TL_DICT_PL ?= ../text/translation/script_pl.txt
PAK        ?= $(BUILDROOT)/$(APP_NAME).pak
.PHONY: pack
pack:
	$(Q)$(LRVNE_PY) tools/gamedata.py check -q
	$(Q)mkdir -p $(BUILDROOT)/fonts $(BUILDROOT)/en $(BUILDROOT)/pl
	$(Q)$(LRVNE_TOOL) xfont -o $(BUILDROOT)/fonts/ank8.xfnt $(X11FONTS)/8x16rk.bdf
	$(Q)$(LRVNE_TOOL) xfont -o $(BUILDROOT)/fonts/lat8.xfnt $(X11FONTS)/8x16.bdf $(LRVNE)/fonts/latin8-pl.txt
	$(Q)$(LRVNE_TOOL) xfont -o $(BUILDROOT)/fonts/k16.xfnt $(X11FONTS)/jiskan16.bdf
	$(Q)rm -rf $(BUILDROOT)/en/scripts $(BUILDROOT)/pl/scripts
	$(Q)$(LRVNE_PY) tools/tl_extract.py --lang en --dict $(TL_DICT_EN) --game $(GAMEDIR) \
	    --klm lang/en/klm.txt -o $(BUILDROOT)/en/scripts --lint $(BUILDROOT)/en/lint.txt $(if $(TL_COMPARE),--compare $(TL_COMPARE))
	$(Q)$(LRVNE_PY) tools/tl_extract.py --lang pl --dict $(TL_DICT_PL) --game $(GAMEDIR) \
	    --klm lang/pl/klm.txt -o $(BUILDROOT)/pl/scripts --lint $(BUILDROOT)/pl/lint.txt
	$(Q)$(LRVNE_PY) tools/mkpack.py --game $(GAMEDIR) --profile $(GAME_PROFILE) \
	    --game-def $(GAME_PACK_DEF) --lang-ja --scripts en=$(BUILDROOT)/en/scripts \
	    --scripts pl=$(BUILDROOT)/pl/scripts --fonts $(BUILDROOT)/fonts --strings lang \
	    --voicemap ../resources/voice/voicemap.tsv -o $(PAK)

# The optional voice pack (../docs/voice.md): the voice clips of the Windows 95 release, every
# clip the cue table names. WINDAT = that release's data archive. The cue tables themselves are in
# the main pack (make pack); both come from ../resources/voice/voicemap.tsv.
# CODEC=opus (default): re-encoded at OPUS_KBPS with ffmpeg's libopus, ~48 MB, for builds with
# the Opus decoder (all but TARGET=amiga). CODEC=ldp6: the original data, ~261 MB, decoded
# everywhere.
VOICE_PAK ?= $(BUILDROOT)/$(APP_NAME)_voice.pak
CODEC     ?= opus
OPUS_KBPS ?= 24
.PHONY: voice-pack
voice-pack:
	$(Q)test -n "$(WINDAT)" || { echo "voice-pack: set WINDAT=<the Windows release's data archive> (../docs/voice.md)"; exit 1; }
	$(Q)mkdir -p $(dir $(VOICE_PAK))
	$(Q)$(LRVNE_PY) tools/mkvoice.py --dat $(WINDAT) --voicemap ../resources/voice/voicemap.tsv \
	    --codec $(CODEC) --bitrate $(OPUS_KBPS) -o $(VOICE_PAK)

# ---------------------------------------------------------------------------------------------
# End-to-end scenarios through the control mode of the null backend (TARGET=host) or the SDL3
# backend rendering offscreen (TARGET=sdl3: adds the presentation checks), tests/e2e/run.py;
# the Amiga and AROS builds replay them in emulators. Needs Python 3 with numpy and Pillow.
# E2E_ARGS passes options: make e2e E2E_ARGS="-k title --update"
# ---------------------------------------------------------------------------------------------
E2E_BACKEND ?= $(if $(filter sdl3,$(TARGET)),sdl3,null)
ifeq ($(TARGET),amiga)
# the host build makes the reference frames (tests/e2e/README.md "Amiga in WinUAE"); lrvne's
# rigs run the emulator (lrvne/rigs/README.md)
e2e: $(EXE) lrvne-rig-tools
	$(Q)$(MAKE) --no-print-directory TARGET=host
	$(Q)python3 -I tests/e2e/amiga.py --amiga-bin $(EXE) --pak $(PAK) --out $(BUILD)/e2e $(or $(E2E_ARGS),present)
else ifeq ($(TARGET),aros)
# hosted AROS, the guest reads its screen back (tests/e2e/aros.py; lrvne/rigs/README.md)
e2e: $(EXE)
	$(Q)$(MAKE) --no-print-directory TARGET=host
	$(Q)python3 -I tests/e2e/aros.py --aros-bin $(EXE) --pak $(PAK) --out $(BUILD)/e2e $(or $(E2E_ARGS),present)
else
e2e: $(EXE)
	$(Q)python3 -I tests/e2e/run.py --bin $(EXE) --pak $(PAK) --out $(BUILD)/e2e --backend $(E2E_BACKEND) $(E2E_ARGS)
endif

# ---------------------------------------------------------------------------------------------
# Release archives of the Amiga family (`make TARGET=amiga|warpos|aros dist`, lrvne's
# lrvne-dist): $(BUILD)/<program>-<version>-<target>.lha, which unpacks to <drawer>.info (the
# drawer icon) and the drawer with the program, its icon and ReadMe.txt (GAME_DRAWER, GAME_DIST
# of the game's game.mk). The pack is not in it: it is the game's data,
# which the player builds from the original release (the ReadMe says how). The version is
# engine/version.h's; the release is tagged v<version> in git.
#
# The Workbench icons: tools/mkicon.py (needs Pillow) cuts them out of the game's picture
# (GAME_ICON_FRAME, which `make data` derives) and gives them the tool types of the target's
# flavour. They are not committed: the picture is the game's art.
# ---------------------------------------------------------------------------------------------
ICON_FRAME ?= $(GAME_ICON_FRAME)
ifneq ($(LRVNE_DIST_NAME),)
dist: lrvne-dist
endif
$(LRVNE_DIST_ICON): tools/mkicon.py $(ICON_FRAME)
	@mkdir -p $(dir $@)
	@echo "  ICON  $@"
	$(Q)$(LRVNE_PY) tools/mkicon.py --flavour $(ICON_FLAVOUR) -o $@ $(ICON_FRAME) >/dev/null
$(LRVNE_DIST_DRAWER_ICON): tools/mkicon.py $(ICON_FRAME)
	@mkdir -p $(dir $@)
	@echo "  ICON  $@"
	$(Q)$(LRVNE_PY) tools/mkicon.py --flavour $(ICON_FLAVOUR) --drawer -o $@ $(ICON_FRAME) >/dev/null
$(ICON_FRAME):
	@echo "$@ is missing: import the game and derive the resources first (make data SRC=...)"; exit 1
