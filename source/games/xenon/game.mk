# SPDX-License-Identifier: GPL-3.0-or-later
# Xenon ～夢幻の肢体～ (C's Ware, 1994): what the build knows of the game (included by the
# Makefile for GAME=xenon). The engine itself knows nothing of it: the game is its pack's
# profile (profile.txt) and the native modules in this directory.

# the program: its name is where a backend looks for the pack (xenon.pak next to it) and
# where it keeps the saves (<user data>/xenon/<profile id>)
APP_NAME := xenon

# make pack: the profile, the tool side of the game (pack.py: languages, the user-defined
# glyph, the data fixes)
GAME_PROFILE := games/xenon/profile.txt
GAME_PACK_DEF := games/xenon/pack.py

# the Amiga family's release archives: the drawer, the ReadMe parts, the icons' picture (the
# XENON lettering of the opening's last frame, which `make data` derives from 0100B.ADT)
GAME_DRAWER    := Xenon
GAME_DIST      := games/xenon/dist/amiga
GAME_ICON_FRAME := ../resources/anim/0100B/28.png
