# Xenon engine - GNU Make 4
#
#   make [TARGET=host|sdl3|amiga|warpos|aros] [V=1]  build the engine for a target (default: host;
#                                             warpos = the Amiga backend for PowerPC/WarpOS)
#   make data SRC=...                         import the original game from your copy (floppy or HDD images,
#                                             a zip, an installed copy) into ../resources/original/ and
#                                             derive the resources from it (../docs/game-data.md)
#   make pack                                 build the asset pack ../build/xenon.pak (fonts, translated scripts,
#                                             all languages; needs the original game (make data), the fonts
#                                             are in fonts/x11; languages and the translation sync: lang/README.md)
#   make text-view                            the translations with the Japanese from the game written back
#                                             (../text/view/, for translators; ../text/README.md)
#   make data-clean                           remove everything made from the game outside ../build/
#   make distclean                            clean + data-clean: only the committed files are left
#   make voice-pack WINDAT=.../XENON.DAT      the optional voice pack ../build/xenon_voice.pak from the
#        [CODEC=opus|ldp6]                    Windows 95 release (docs/voice.md; opus needs ffmpeg)
#   make TARGET=host test                     build and run the conformance tests
#   make TARGET=host e2e [E2E_ARGS=...]       play the game end to end (tests/e2e/README.md)
#   make TARGET=sdl3 e2e [E2E_ARGS=...]       the same on the SDL3 backend offscreen, with the
#                                             presentation (rendered pixels) checks
#   make TARGET=aros e2e [E2E_ARGS=...]       the AROS build in hosted AROS: the same stages
#                                             (tests/e2e/aros.py)
#   make TARGET=amiga e2e [E2E_ARGS=...]      the Amiga build in WinUAE: presentation test
#                                             (default "present"; "game --scenario NAME" replays
#                                             a scenario), tests/e2e/amiga.py
#   make TARGET=amiga dist                    the release archive ../build/amiga/xenon-amiga-<version>.lha (executable,
#                                             icons, player ReadMe, no pack; tests/amiga/run-dist.sh verifies it;
#                                             the icons are made from the game's logo, so make data first)
#   make TARGET=warpos dist                   the same for WarpOS: ../build/warpos/xenon-warpos-<version>.lha
#   make clean
#
# Each target is described by config/<TARGET>.mk, which sets the compiler, flags, the backend
# directory and the way objects are produced (vbcc and gcc differ). Everything else is generic.

TARGET ?= host
# Everything built goes to the top-level build/ (../build from here): build/<TARGET>/ for each
# target, the asset pack and its intermediates (fonts, translated scripts) in build/ itself.
BUILDROOT := ../build
BUILD     := $(BUILDROOT)/$(TARGET)
include config/$(TARGET).mk

EXE      ?= $(BUILD)/xenon$(EXESUF)

# Engine sources are portable C99; backends and third-party code are per target. A backend is
# one directory (BACKEND) or, for the Amiga family, the list of its parts (BACKEND_DIRS, set by
# config/<TARGET>.mk; backends/amiga/README.md "Layout"). Every part is on the include path.
ENGINE_SRCS  := $(sort $(wildcard engine/*.c engine/*/*.c))
BACKEND_DIRS ?= backends/$(BACKEND)
BACKEND_SRCS := $(sort $(foreach d,$(BACKEND_DIRS) backends/common,$(wildcard $(d)/*.c)))
EXTRA_SRCS   ?=
# Control mode (test automation protocol, tests/e2e) is built into the host and SDL3 backends;
# config/<TARGET>.mk sets CONTROL := 1 for them.
CONTROL      ?= 0
ifeq ($(CONTROL),1)
CONTROL_SRCS := $(wildcard backends/control/*.c)
else
CONTROL_SRCS :=
endif
ALL_SRCS     := $(ENGINE_SRCS) $(BACKEND_SRCS) $(CONTROL_SRCS) $(EXTRA_SRCS)

obj_of  = $(patsubst %.c,$(BUILD)/%.o,$(patsubst %.cpp,$(BUILD)/%.o,$(patsubst %.s,$(BUILD)/%.o,$(1))))
OBJS    := $(call obj_of,$(ALL_SRCS))
DEPS    := $(OBJS:.o=.d)

# third-party C with its own flags (config/<TARGET>.mk includes third_party/opus/opus.mk)
ifneq ($(OPUS_SRCS),)
$(call obj_of,$(OPUS_SRCS)): CFLAGS = $(OPUS_CFLAGS)
endif

INCLUDES := -I. -Iengine -Ihal -Ibackends/common -Ibackends/control $(addprefix -I,$(BACKEND_DIRS))

ifeq ($(V),1)
Q :=
else
Q := @
endif

# A compiler that writes no dependency files (vbcc: config/amiga.mk sets AUTODEPS := 0) gets a
# coarse rule instead: every object depends on every header, so a header change rebuilds all.
# (Without it, objects compiled against an older struct layout survived and corrupted state.)
.DEFAULT_GOAL := all
AUTODEPS ?= 1
ifeq ($(AUTODEPS),0)
ALL_HEADERS := $(wildcard engine/*.h engine/*/*.h hal/*.h backends/*/*.h backends/*/*/*.h)
$(OBJS): $(ALL_HEADERS)
endif

.PHONY: all clean test tools
.SECONDARY:

all: $(EXE)

$(EXE): $(OBJS)
	@echo "  LINK  $@"
	$(Q)$(LINK)

# Generic compile rules; config/<TARGET>.mk defines COMPILE_C (and COMPILE_CXX when the target
# builds C++). The recipe must produce $@ from $<.
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	$(Q)$(COMPILE_C)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $<"
	$(Q)$(COMPILE_CXX)

# Assembly (only config/amiga.mk lists .s sources, via EXTRA_SRCS, and defines COMPILE_S)
$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "  AS    $<"
	$(Q)$(COMPILE_S)

clean:
	rm -rf $(BUILDROOT)

# ---------------------------------------------------------------------------------------------
# Conformance tests (host target only; they need Python 3 with numpy/Pillow for the references)
# ---------------------------------------------------------------------------------------------
TEST_SRCS := $(sort $(wildcard tests/*.c))
TEST_EXES := $(patsubst tests/%.c,$(BUILD)/tests/%$(EXESUF),$(TEST_SRCS))
# the test objects take part in the header dependency tracking too
DEPS      += $(patsubst tests/%.c,$(BUILD)/tests/%.d,$(TEST_SRCS))
# tests link the engine plus the target's extra sources (ymfm on the host)
ENGINE_OBJS := $(call obj_of,$(ENGINE_SRCS) $(EXTRA_SRCS))

$(BUILD)/tests/%$(EXESUF): $(BUILD)/tests/%.o $(ENGINE_OBJS)
	@mkdir -p $(dir $@)
	@echo "  LINK  $@"
	$(Q)$(LINK_TEST)

test: $(TEST_EXES)
	$(Q)tests/run.sh $(BUILD)/tests

# The game's data (../docs/game-data.md), which the repository does not carry: `make data
# SRC="..."` imports it from your copy of the original release (D88/FDI floppy images, HDI/NHD
# hard-disk images, a zip of them, or a directory with the game's files or the images; several
# SRCs add up) into ../resources/original/, checking every file against
# ../resources/inventory.tsv, then derives the resources the RE documents and the tests use
# (DATA_PARTS, default all: images anim scripts sfx music saves vgm; DATA_PY = a Python with
# Pillow, and with unicorn for vgm, the tests' reference recordings). Parts whose module is
# missing are skipped with a note. `make data-check` verifies GAMEDIR; `make pack` does it first.
GAMEDIR  ?= ../resources/original/game
DATA_PY  ?=
.PHONY: data data-derive data-check
data:
	$(Q)test -n "$(SRC)" || { echo "data: set SRC=<your copy of the game> (../docs/game-data.md)"; exit 1; }
	$(Q)python3 -I tools/gamedata.py import $(SRC)
	$(Q)python3 -I tools/gamedata.py derive $(if $(DATA_PY),--python $(DATA_PY)) $(DATA_PARTS)
data-derive:
	$(Q)python3 -I tools/gamedata.py derive $(if $(DATA_PY),--python $(DATA_PY)) $(DATA_PARTS)
data-check:
	$(Q)python3 -I tools/gamedata.py check $(GAMEDIR)

# The translators' view (../text/README.md): the dictionaries with the Japanese written back from
# GAMEDIR into ../text/view/ (not committed: the dictionaries' keys only point at the game's
# script, tools/tlkeys.py), plus view/lines.tsv with every entry's place, voice clips and text.
.PHONY: text-view
text-view:
	$(Q)python3 -I tools/tlkeys.py --game $(GAMEDIR) view

# Everything made from the original game outside ../build/ (the ignored paths of ../.gitignore):
# the imported files, the derived resources, the voice review report, the disassembly
# listings, the translators' view, the e2e goldens' local pictures. `make distclean` = clean +
# data-clean: back to the committed files (then `make data SRC=...` again).
DATA_GENERATED := ../resources/original ../resources/images ../resources/anim \
	../resources/scripts ../resources/music/vgm ../resources/sfx/blobs ../resources/sfx/vgm \
	../resources/saves ../resources/voice/report.txt \
	$(addprefix ../docs/re/listing/,ags.asm pmd.asm funcs.tsv pmd-funcs.tsv) \
	../text/view tests/e2e/golden
.PHONY: data-clean distclean
data-clean:
	rm -rf $(DATA_GENERATED)
distclean: clean data-clean

# The asset pack: fonts (tools/xfont.py from the X11 bitmap fonts in fonts/x11), the translated
# scripts (tools/tl_extract.py from the translation dictionaries in ../text/) and everything
# from the original game (tools/mkpack.py). The pack is language data, the same for every
# target. GAMEDIR = the original game's files (make data). TL_COMPARE=DIR also reports the
# English strings that differ from DIR/*.U.CC (the PC-98 build's scripts_merge) in
# ../build/en/lint.txt.
X11FONTS ?= fonts/x11
TL_DICT_EN ?= ../text/translation/script_en.txt
TL_DICT_PL ?= ../text/translation/script_pl.txt
PAK      ?= $(BUILDROOT)/xenon.pak
.PHONY: pack
pack:
	$(Q)python3 -I tools/gamedata.py check -q $(GAMEDIR)
	$(Q)mkdir -p $(BUILDROOT)/fonts $(BUILDROOT)/en $(BUILDROOT)/pl
	$(Q)python3 -I tools/xfont.py -o $(BUILDROOT)/fonts/ank8.xfnt $(X11FONTS)/8x16rk.bdf
	$(Q)python3 -I tools/xfont.py -o $(BUILDROOT)/fonts/lat8.xfnt $(X11FONTS)/8x16.bdf fonts/latin8-pl.txt
	$(Q)python3 -I tools/xfont.py --ags $(GAMEDIR)/AGS.EXE -o $(BUILDROOT)/fonts/k16.xfnt $(X11FONTS)/jiskan16.bdf
	$(Q)rm -rf $(BUILDROOT)/en/scripts $(BUILDROOT)/pl/scripts
	$(Q)python3 -I tools/tl_extract.py --lang en --dict $(TL_DICT_EN) --game $(GAMEDIR) \
	    --klm lang/en/klm.txt -o $(BUILDROOT)/en/scripts --lint $(BUILDROOT)/en/lint.txt $(if $(TL_COMPARE),--compare $(TL_COMPARE))
	$(Q)python3 -I tools/tl_extract.py --lang pl --dict $(TL_DICT_PL) --game $(GAMEDIR) \
	    --klm lang/pl/klm.txt -o $(BUILDROOT)/pl/scripts --lint $(BUILDROOT)/pl/lint.txt
	$(Q)python3 -I tools/mkpack.py --game $(GAMEDIR) --lang-ja --scripts en=$(BUILDROOT)/en/scripts \
	    --scripts pl=$(BUILDROOT)/pl/scripts --fonts $(BUILDROOT)/fonts --strings lang \
	    --voicemap ../resources/voice/voicemap.tsv -o $(PAK)

# The optional voice pack (docs/voice.md): the voice clips of the Windows 95 release, every clip
# the cue table names. WINDAT = that release's XENON.DAT. The cue tables themselves are in the
# main pack (make pack); both come from ../resources/voice/voicemap.tsv.
# CODEC=opus (default): re-encoded at OPUS_KBPS with ffmpeg's libopus, ~60 MB, for builds with
# the Opus decoder (host, SDL3). CODEC=ldp6: the original data, ~261 MB, decoded everywhere.
VOICE_PAK ?= $(BUILDROOT)/xenon_voice.pak
CODEC     ?= opus
OPUS_KBPS ?= 24
.PHONY: voice-pack
voice-pack:
	$(Q)test -n "$(WINDAT)" || { echo "voice-pack: set WINDAT=path/to/XENON.DAT (Windows release)"; exit 1; }
	$(Q)mkdir -p $(dir $(VOICE_PAK))
	$(Q)python3 -I tools/mkvoice.py --dat $(WINDAT) --voicemap ../resources/voice/voicemap.tsv \
	    --codec $(CODEC) --bitrate $(OPUS_KBPS) -o $(VOICE_PAK)

# End-to-end scenarios through the control mode of the null backend (TARGET=host) or the SDL3
# backend rendering offscreen (TARGET=sdl3: adds the presentation checks). tests/e2e/run.py;
# needs Python 3 with numpy and Pillow. E2E_ARGS passes options: make e2e E2E_ARGS="-k title --update"
E2E_BACKEND ?= null
.PHONY: e2e
ifeq ($(TARGET),amiga)
# the host build makes the reference frames (tests/e2e/README.md "Amiga in WinUAE")
e2e: $(EXE)
	$(Q)$(MAKE) --no-print-directory TARGET=host
	$(Q)python3 -I tests/e2e/amiga.py --amiga-bin $(EXE) --pak $(PAK) --out $(BUILD)/e2e $(or $(E2E_ARGS),present)
else ifeq ($(TARGET),aros)
# hosted AROS, the guest reads its screen back (tests/e2e/aros.py; tests/aros/README.md)
e2e: $(EXE)
	$(Q)$(MAKE) --no-print-directory TARGET=host
	$(Q)python3 -I tests/e2e/aros.py --aros-bin $(EXE) --pak $(PAK) --out $(BUILD)/e2e $(or $(E2E_ARGS),present)
else
e2e: $(EXE)
	$(Q)python3 -I tests/e2e/run.py --bin $(EXE) --pak $(PAK) --out $(BUILD)/e2e --backend $(E2E_BACKEND) $(E2E_ARGS)
endif

# The Workbench icons of the Amiga family (config/<TARGET>.mk sets AMIGA_FAMILY := 1): the
# XENON lettering of the opening, cut out of the last frame of the logo zoom (ICON_FRAME, which
# `make data` derives from the game's 0100B.ADT) by tools/mkicon.py (needs Pillow). They are
# not committed: the lettering is the game's art. Without the frame `all` leaves them out with
# a notice; dist needs them. drawer.info is the same picture as a drawer icon, for the game's
# directory (installed as <dir>.info). The copies next to the executable are what gets
# installed. The WarpOS build (TARGET=warpos) has the 68k one's; AROS its own tool types
# (ICON_FLAVOUR, config/aros.mk).
AMIGA_FAMILY ?= 0
ifeq ($(AMIGA_FAMILY),1)
ICON_FLAVOUR ?= os3
ICON_FRAME   ?= ../resources/anim/0100B/28.png
ifneq ($(wildcard $(ICON_FRAME)),)
all: $(BUILD)/xenon.info $(BUILD)/drawer.info
else
all: icon-notice
.PHONY: icon-notice
icon-notice:
	@echo "note: no Workbench icons without $(ICON_FRAME) (make data SRC=..., needs Pillow)"
endif
$(BUILD)/xenon.info: tools/mkicon.py $(ICON_FRAME)
	@mkdir -p $(dir $@)
	@echo "  ICON  $@"
	$(Q)python3 -I tools/mkicon.py --flavour $(ICON_FLAVOUR) -o $@ $(ICON_FRAME) >/dev/null
$(BUILD)/drawer.info: tools/mkicon.py $(ICON_FRAME)
	@mkdir -p $(dir $@)
	@echo "  ICON  $@"
	$(Q)python3 -I tools/mkicon.py --flavour $(ICON_FLAVOUR) --drawer -o $@ $(ICON_FRAME) >/dev/null
$(ICON_FRAME):
	@echo "$@ is missing: import the game and derive the resources first (make data SRC=...)"; exit 1

# The player's ReadMe.txt, put together from backends/amiga/readme/ (the parts every flavour
# shares and its own, README_PARTS in config/<TARGET>.mk; plain ASCII for More/MultiView).
README_DIR := backends/amiga/readme
$(BUILD)/ReadMe.txt: $(addprefix $(README_DIR)/,$(addsuffix .txt,$(README_PARTS)))
	@mkdir -p $(dir $@)
	$(Q)cat $^ >$@
all: $(BUILD)/ReadMe.txt

# `make TARGET=amiga dist`: the release archive $(BUILD)/xenon-amiga-$(XE_VERSION).lha (the version
# comes from engine/version.h; the release is tagged v$(XE_VERSION) in git), which unpacks to
# Xenon.info (the drawer icon) + Xenon/{xenon, xenon.info, ReadMe.txt}. The pack is not in it:
# it is the game's data, which the player builds from the original release or obtains
# elsewhere (the ReadMe says how) and puts into Xenon/ as xenon.pak.
# Made with the host's lha (1.14i): -lh5- compression and level-$(LHA_LEVEL) headers, which
# Amiga LhA 2.x extracts (tests/amiga/run-dist.sh unpacks it in WinUAE and compares, see
# tests/amiga/README.md "The release archive"). The icons need `make data` first.
# `make TARGET=warpos dist` makes xenon-warpos-$(XE_VERSION).lha the same way, with the PowerPC
# executable; `make TARGET=aros dist` xenon-aros-x86_64-$(XE_VERSION).lha with a stripped
# executable and an upload description next to it (tests/aros/run-dist.sh checks it).
# config/<TARGET>.mk sets DIST_NAME, or DIST_FILE for another name, DIST_STRIP to strip the
# executable, DIST_UPLOAD_README for a description template (@VERSION@ is replaced).
XE_VERSION := $(shell sed -n 's/^\#define XE_VERSION *"\(.*\)"/\1/p' engine/version.h)
DIST_FILE ?= xenon-$(DIST_NAME)-$(XE_VERSION).lha
DIST      := $(BUILD)/$(DIST_FILE)
DISTDIR   := $(BUILD)/dist
LHA       ?= lha
LHA_LEVEL ?= 1
.PHONY: dist
dist: $(EXE) $(BUILD)/xenon.info $(BUILD)/drawer.info $(BUILD)/ReadMe.txt
	$(Q)rm -rf $(DISTDIR) $(DIST) $(DIST:.lha=.readme)
	$(Q)mkdir -p $(DISTDIR)/Xenon
	$(Q)cp $(EXE) $(BUILD)/xenon.info $(BUILD)/ReadMe.txt $(DISTDIR)/Xenon/
	$(Q)cp $(BUILD)/drawer.info $(DISTDIR)/Xenon.info
	$(if $(DIST_STRIP),$(Q)$(DIST_STRIP) $(DISTDIR)/Xenon/xenon)
	@echo "  LHA   $(DIST)"
	$(Q)cd $(DISTDIR) && $(LHA) a$(LHA_LEVEL)qo5 $(abspath $(DIST)) Xenon.info Xenon
	$(Q)$(LHA) l $(DIST)
	$(if $(DIST_UPLOAD_README),$(Q)sed 's/@VERSION@/$(XE_VERSION)/g' $(DIST_UPLOAD_README) >$(DIST:.lha=.readme))
endif

-include $(DEPS)
