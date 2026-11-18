# Voice support: plan

Status: steps 1–5, 7 and 8 of §7 are **implemented** (cue table, main-pack cues, voice pack,
engine, HAL with the SDL3, null and Amiga-family backends, Opus); automated e2e checks of the
voice are not. Build and play:

    make data SRC=…                                    # the PC-98 game, docs/game-data.md
    make pack && make voice-pack WINDAT=…/XENON.DAT     # ../build/xenon.pak, ../build/xenon_voice.pak
                                                       # (Opus, 48 MB; CODEC=ldp6: 261 MB)
    ../build/sdl3/xenon --pak ../build/xenon.pak       # finds xenon_voice.pak next to the pack

**Since the move to lrvne** (the platform layer, `source/lrvne`) the generic half of this plan is
lrvne's and the names below are history where they say otherwise:

| here | now |
|---|---|
| the XPAK container, `XPAK_T_VOICECUE` (11), `XPAK_T_VOICE` (12), `XPAK_T_META` | lrvne's pack format 2 (`lrvne/pack.h`): `LRV_PT_VOICECUE` (5), `LRV_PT_VOICE` (6), `LRV_PT_META` (1); languages by tag |
| `engine/voice/vdec*.c`, `third_party/opus` | lrvne's decoders (`lrvne/vdec.h`: pcm8, ldp4/ldp6, Opus with `LRVNE_OPUS=1`) and its vendored libopus |
| the ring, the resampler, `voice_pump`, `voice_mix` | lrvne's player (`lrvne/voice.h`: `lrv_voice_play`, `_stop`, `_pump`, `_mix`) |
| `hal->voice_mode` `XE_VOICE_NONE/MIX/CHANNEL`, `voice_open` ... | `hal->aux_mode` `LRV_AUX_NONE/MIX/CHANNEL`, `aux_open` ... (`lrvne/hal.h`) |
| `--music-volume`, `MUSICVOL`, `VOICEVOL` | `--volume`, `VOLUME`, `VOICEVOLUME` |
| `mkvoice.py` writing the pack itself | `mkvoice.py` reading `XENON.DAT` and giving the clips to `lrvne.voicepack` |

What stays the engine's is the cue table and when a clip plays, waits and is cut
(`engine/voice/voice.c`, §4); the music's default volume while the voice is on (§6) is the
game's profile key `voice_music_volume` (docs/profile.md); the tools that read the Windows
release (`arc3.py`, `ldp.py`) are the game's.

Goal: play the voice acting of the Windows 95 release (docs/re/other-versions.md §2.4, §4) in
every language, from an optional second pack `xenon_voice.pak`, on any backend that chooses to
support it, without changing anything for builds or users without it.

## 1. Source material

* Clips: the Windows `XENON.DAT`, `ldp6` (22 050 Hz, 6-bit log-delta PCM, §2.4). 4913 clips are
  referenced by the story scripts, 4218 of them distinct (the Windows release stores a renamed
  copy per repetition of a line); 4.32 hours; median 3.3 s, longest 21 s.
* Cues: the Windows scripts name the clip(s) of every voiced line. `re-tools/voicemap.py` aligns them
  with the PC-98 messages and writes `resources/voice/voicemap.tsv` (checked in; regenerate with
  `python3 -I re-tools/voicemap.py --dat …/XENON.DAT --report resources/voice/report.txt`, after
  `make data` has made `resources/scripts/`; the report quotes the game's lines and is not
  committed):

  | | |
  |---|---|
  | cues | 5046 on 5023 messages, every clip reference of the Windows scripts placed |
  | matched exactly | 4968 |
  | matched with edited text (similarity ≥ 0.6) | 77 — the voice says what the Windows text says, which may differ slightly from the PC-98 / translated subtitle |
  | on an empty message | 27 — Windows hangs the clip on the empty `\n　{KEEP}` message after a `{W0}` line (S00 prologue, dream openings) on purpose: the line is drawn with the graphics off and faded in first, the empty message comes after the fade, so the voice starts on the visible text. (An earlier version moved these cues onto the line itself; the voice then played on a black screen.) |
  | override | 1 (`resources/voice/overrides.tsv`: S0205 `KOH0069`, line rewritten in Windows) |
  | reviewed, kept | `RAN0136`, `KOU1075`, `KOU0171B` (Windows rewrote / re-attributed the line), `RYO0116Z` (PC-98 tags the line 宏治; the text is Ryōko's) — in the report |

  Key of a cue: script, offset of the message's `FD` string token **in the Japanese bytecode**,
  part (the segment after the n-th `{KEY}`, 0x0C). The table's columns are `script offset page
  part clip match crc`; `crc` is the CRC-32 of the line (the token's Shift-JIS bytes), so a tool
  can check that the offset still names the same line without the table carrying the game's
  text (`tests/check_pack.py`). `re-tools/voicemap.py --show [SCRIPT...]` prints the cues with
  their lines, read from your scripts, and checks every crc. The translation dictionaries key
  their entries the same way (`source/tools/tlkeys.py`), so `tests/check_text.py` checks that
  every voiced part has translated text, and `make text-view` lists each entry's clips. 16 messages have 2–3 voiced parts with clips
  `…A`/`…B`/`…C`, e.g. the S00 countdown "ひとつ‥{KEY}ふたつ‥{KEY}みっつ".
* Translations: `tl_extract.py` only replaces string contents, so the token structure of every
  English and Polish script is identical to the Japanese one (verified: 80 scripts, 0
  differences). A cue therefore maps to the k-th string token of the script in every language.

## 2. Packs

### 2.1 Cue tables go into the main pack

`mkpack.py` reads `voicemap.tsv` and, for each language it packs, walks the Japanese and that
language's bytecode token by token in parallel (ccdis `tokenize`) to turn each Japanese `FD`
offset into that language's offset. It emits one entry per script and language:

    name = script ("S0101"), type XPAK_T_VOICECUE (11), lang = ja/en/pl
    "VCUE", u16 version (1), u16 count, then count × 20 bytes, sorted by (offset, part):
      u32 offset of the message's FD token in that language's bytecode, u8 part, 3 reserved,
      char clip[12] (NUL padded)

20 bytes × 5046 cues × 3 languages ≈ 300 KB (111 entries: 37 voiced scripts × 3 languages). Keeping the cues with the scripts means a
translation fix rebuilds only the main pack, and the voice pack depends on nothing but the
Windows release. The build fails if a cue offset does not land on an `FD` token, so a stale table
cannot ship.

### 2.2 `xenon_voice.pak`

Same XPAK container (engine/pack/pack.h), built by a new `source/tools/mkvoice.py` from
`XENON.DAT` and `voicemap.tsv`, containing only referenced clips:

* `XPAK_T_VOICE` (new, 12): one entry per clip name (`RYO0212`), payload = the encoded clip.
  Identical clips are stored once and several entries point at the same offset (261 MB instead
  of 301 MB for ldp6). The engine's reader does not care; `source/tools/xpak.py`'s writer gets a
  dedupe option.
* Clips are read in ranges (a new `xpak_read(pk, e, off, dst, n)` over `hal->pack_read`), not
  with `xpak_load`, so a decoder streams a clip without holding it in RAM.
* `XPAK_T_META` "voice": `codec=ldp6|opus|pcm8`, `rate=…`, `cues=<md5 of voicemap.tsv>`, version.
  One codec per pack: a backend or build that cannot decode it logs it and runs without voice.
* `make voice-pack` (needs `WINDAT=…/XENON.DAT`), output `build/xenon_voice.pak`; `CODEC=opus`
  (default, needs ffmpeg with libopus) or `CODEC=ldp6`, `OPUS_KBPS=24`.

## 3. Codec (question 2)

| Codec | Size (4.32 h) | Decoder | Quality | Verdict |
|---|---|---|---|---|
| **ldp6** (as shipped) | 261 MB | ~40 lines of integer C: per sample one table lookup and an add; zero-run codes | the original, bit-exact | **baseline, always compiled in** |
| **Opus** 24 kbit/s mono (libopus, BSD) | 48 MB (measured) | libopus 1.6.1, vendored, decoder only, fixed point | transparent for speech at this rate (listening test below); the whole 11 kHz band | **default pack; host and SDL3 builds** |
| PCM8 11–22 kHz | 171–343 MB | none: the samples can be DMA'd as they are | 8-bit | optional, for weak Amigas (§5.3) |
| Speex | ~40 MB | libspeex | worse than Opus at every rate; wideband (16 kHz) loses the 8–11 kHz band the Windows audio really has (other-versions.md §4) | **no** |
| Vorbis (stb_vorbis, public domain) | ~60–80 MB | single file, float | below Opus at low rates | fallback only if libopus is unwanted |
| IMA ADPCM, FLAC | 190 MB / >300 MB | trivial / moderate | lossy re-quantisation / bigger than ldp6 | no |

Speex has been superseded by Opus (same authors; Xiph deprecated it). Its only remaining argument,
a light fixed-point decoder for slow CPUs, does not apply here: ldp6 is lighter still and is
the original data. So: **ldp6 everywhere, Opus as the compact option where libopus is linked**.

Opus as implemented:

* **Bit rate, by listening (2026-11-13).** Eight clips chosen to be hard (breaths, screams, a
  laugh, the male lead) at 16/24/32/48 kbit/s against the original: 24 kbit/s was judged
  completely acceptable on MAI0200 (breathy), RAN0601 (a long scream) and KOU0157 (male); 16
  kbit/s had a lot of artifacts. The pack is built at 24 (`OPUS_KBPS`), 48 MB for all 4913
  clips (VBR; the loud clips go above 24, the quiet ones below).
* **Encoding** (`mkvoice.py --codec opus`): ldp6 → PCM (`source/tools/ldp.py`) → ffmpeg's libopus at
  24 kHz (22050 is not an Opus rate; 24 kHz keeps the whole 11 kHz band), `-application audio`,
  20 ms frames, in parallel processes (~2 min). The Ogg output is parsed and only the packets
  kept: the clip is `"opus"`, u32 rate, u32 sample count (exact: from the last granule
  position, checked against the ldp length), u16 pre-skip, u16 0, then u16 length + packet
  (layout in mkvoice.py). No libogg or opusfile in the engine.
* **Decoding** (`engine/voice/vdec_opus.c`, behind `XE_HAVE_OPUS`): the vendored libopus 1.6.1
  in `source/third_party/opus` (BSD), decoder only (the 64 C files a static fixed-point build's
  decoder links, found from the link map), `FIXED_POINT`, `DISABLE_FLOAT_API`, `USE_ALLOCA`
  (`third_party/opus/opus.mk`, its own flags). One OpusDecoder (~18 KB) is allocated through
  the pack's HAL on the first clip and reset per clip. `engine/voice/vdec.c` picks the decoder
  by the clip's magic (`ldp4`/`ldp6`/`opus`); the voice pack's `rate` meta sets the output rate.
* **Checked** by `tests/check_voice.py` against the system libopus (ctypes; the float build):
  lengths exact, 60 clips at ≥ 70 dB SNR (fixed vs float differ in the last bits).
* **Amiga family** (§5.3): the WarpOS and AROS builds have Opus (`config/warpos.mk`,
  `config/aros.mk`; played on the real Sam460ex under WarpOS); the 68k build does not (229 % of
  a 68040/25) and needs the ldp6 pack.
  libopus without `alloca()` uses `NONTHREADSAFE_PSEUDOSTACK` (`OPUS_DEFS`).

## 4. Engine

New module `engine/voice/`:

* `vdec.h` — decoder interface: `vdec_open(d, pack, entry)` (picks the decoder by the clip's
  magic), `vdec_read(d, int16 *out, n) → frames`, `d->rate`, `vdec_close`, `vdec_supports(codec)`.
  Implementations `vdec_ldp.c` (always; port of `source/tools/ldp.py`, checked against it),
  `vdec_opus.c` (`#ifdef XE_HAVE_OPUS`); `vdec_pcm8.c` if the Amiga needs it.
* `voice.c` — pack, cues, playback state:
  * `voice_init()`: ask the backend for the voice pack (§5); none → voice off, nothing else runs.
    Read the meta; codec not compiled in → log once, off.
  * on script load (`load_script`, game.c): load that script's `VOICECUE` table for the current
    language (absent → no cues for the script).
  * `voice_message(offset)`: called from `host_message` (the message's `FD` offset is
    `text − script_buf − 2`, since cc_vm passes `buf + pos + 2`); binary search; start the
    part-0 clip.
  * `voice_part(k)`: called by `text_print` after the k-th `{KEY}` (0x0C) wait returns; starts
    the part-k clip.
  * `voice_stop()`.
  * `voice_pump()`: from `sys_frame_wait`, next to `snd_pump`; decodes ahead into the output
    (§5) — about 200 ms of lead, so a late tick never starves it.
* Behaviour — follows the Windows engine (§4.1), with one addition marked *port*:
  * a message's part-0 clip starts with the message, while its text types out; any clip
    still playing is stopped first;
  * at an in-message `{KEY}` and at the end of the message, the engine **waits for the clip to
    end before the key prompt** (▼ / the `{KEY}` wait) appears; then the next part's clip starts
    after the `{KEY}` wait. A clip therefore never runs into the next message;
  * CANCEL (ESC / right button) held during that wait stops the clip at once (Windows);
    *port*: OK pressed during the wait does the same (Windows ignores OK until the clip has
    ended, which feels unresponsive; `--voice-strict` restores that). Decided 2026-11-13; revisit
    after play-testing;
  * skip (CTRL; Windows: TAB / middle button) held when a message starts: text appears at
    once and no clip starts (*port*: also for timed messages, whose clip Windows starts even
    when skipping); held at an in-message `{KEY}`: the wait is passed and no further clip of
    the message starts; held while a clip plays: the clip is cut;
  * timed waits (`{W n}`): the clip plays and the message waits for it to end before
    continuing, as Windows does;
  * script load and jumps, title return, save/load, system menu, quit: stop;
  * no music ducking; voice gain in the engine (`--voice-volume 0..100`).
* Trace event `XE_TR_VOICE` (name = clip, a0 = part, a1 = 0 start / 1 stop / 2 end) so the
  control-mode e2e tests can assert cues without listening (e.g. S00 page 2 → `RYO0701`).

### 4.1 What the Windows engine does (XENON.EXE, verified in the code)

* Voice and sampled effects share **one** output: `0x405180` stops whatever plays and starts the
  named clip (`<name>.ldp` through the arc3 reader); with its second argument set it also keeps
  the name and a periodic callback (`0x405140`) restarts the clip whenever it has ended (a
  loop). `0x405200` clears the loop and stops; `0x405210` = playing. The player (`0x40e040`)
  has a priority check before replacing a clip, but every call uses the same priority, so a
  new clip always replaces the old one.
* Script opcodes: `800B n` plays clip `"<n>"` once (the sampled sound effects, 364 uses;
  e.g. `1` ×87, `24` ×41), `8039 n` plays it as a loop (36 uses: `25` ×15, `4` ×8, `2002`, …),
  `800C` stops (50 uses), `8047 name` plays a clip and waits for its end or a key (unused).
  The message clips are not opcodes: they follow the message text (§2.3 of
  docs/re/other-versions.md).
* Message routine `0x409BA0` (text, clip count, clip names):
  1. skip key held and no timed wait → text instantly, **no clip**; otherwise clip 0 starts,
     then the text types out (typing is cut short by skip or CANCEL held);
  2. after each text segment that ends in a key wait (`s`, PC-98 `{KEY}`): if skip is held,
     go on (no wait, no further clips); otherwise **idle while the clip plays unless CANCEL
     is held**, stop it, wait for a key (`0x409E00`: OK or skip; animated prompt), start the
     next clip;
  3. at the end: if a clip was started, again **idle while it plays unless CANCEL is held**, stop
     it; then the end-of-message key wait unless the message has a timed wait; clear the window
     unless `k` (keep).
* Input groups (`0x4047A0` / `0x404990`, Win32 virtual keys): OK = Return, Space, left
  button; CANCEL = Esc, right button; skip = Tab, middle button; plus the cursor keys and
  numpad 2/4/6/8. OK is not looked at while a clip plays.
* Stops elsewhere: script load and script end (`0x4064EC`, `0x40676E`), video playback (`.vid`).
  Two easter eggs play clips from the keyboard: `M` held while a dialog is drawn plays `1`; and
  during a game, with Shift+Ctrl held, `D` shows a debug line (`Scen_no：%s  Page_no：%d`) and
  typing `ISASIHOHIW` (EXE 0x41AFF8) plays `baa` (0x4090E9): the unreferenced `BAA` clip,
  1.08 s of a voice actor saying "baa" (identified by listening).

## 5. HAL and backends (questions 1 and 3)

### 5.1 Finding the pack

`pack_open(uint32_t *size)` gains a pack id: `pack_open(int which, uint32_t *size)` with
`XE_PACK_MAIN` / `XE_PACK_VOICE`. The backend resolves `XE_PACK_VOICE` as:

1. `--voice-pak FILE` (or `--no-voice` → NULL);
2. `xenon_voice.pak` in the directory the main pack was found in (next to `xenon.pak`, or next
   to the executable when the main pack is bundled into it);
3. `xenon_voice.pak` in the current directory.

NULL = no voice, silently (one info log line). The Amiga family: `VOICEPAK=` (or `NOVOICE`),
`xenon_voice.pak` next to `PAK=`, `PROGDIR:xenon_voice.pak`, the current directory.

### 5.2 Output: each backend picks a mode

New HAL field `voice_mode`, set by the backend:

| Mode | What the backend provides | Engine does | Intended for |
|---|---|---|---|
| `XE_VOICE_NONE` (0, default) | nothing | voice off; the pack is not even opened | any backend that does not care; the Amiga with `AUDIO=none` |
| `XE_VOICE_MIX` | nothing beyond its music audio | mixes the decoded, resampled, gain-scaled voice into the mono stream it already renders (push: in `snd_pump`; pull: inside `xe_audio_render`, under the same audio lock) | backends with one output stream: null/`--render-wav`; the Amiga with `PAULA14` or `VOICEMIX` |
| `XE_VOICE_CHANNEL` | `voice_open(rate) → accepted rate`, `voice_close()`, `voice_space()`, `voice_push(pcm, frames)`, `voice_flush()` (drop everything queued, for a cut), `voice_queued()` (frames not yet played) | decodes in `voice_pump` and pushes mono 16-bit at the clip rate; the backend plays it on its own channel, resampling and mixing as it likes | SDL3; the Amiga's Paula and AHI drivers (§5.3) |

Push from the engine's tick for `CHANNEL` keeps the decoders single-threaded (no audio lock);
`MIX` in pull mode runs the decoder on the backend's audio thread, which is why `voice_*` calls
take the audio lock there, exactly like the sequencer calls.

* **SDL3**: `XE_VOICE_CHANNEL` with a second `SDL_AudioStream` bound to the same device; SDL
  mixes the bound streams and converts 22 050 Hz to the device rate. `voice_space` /
  `voice_queued` from `SDL_GetAudioStreamQueued`, `voice_flush` = `SDL_ClearAudioStream`. The
  backend opens the device itself (`SDL_OpenAudioDevice`) and binds the music stream (with its
  pull callback) to it: a device opened with `SDL_OpenAudioDeviceStream` refuses further
  streams. At the end of a clip the engine pushes 0 frames and the backend flushes the stream:
  SDL holds the last 7 frames back for its resampler until then, so `SDL_GetAudioStreamQueued`
  would never reach 0 and the clip would never end. Voice is on whenever audio is (not in
  `--control`).
* **null**: `XE_VOICE_MIX` into its (silent) music stream, so clips run in real time and the
  trace shows them; only with an explicit `--voice-pak FILE`, so the e2e scenarios keep their
  timing unless they ask for voice. `--voice-out channel` replaces the mix with a pretend
  `XE_VOICE_CHANNEL` device that plays its queue in real time and holds back 7 frames until the
  end-of-clip push, like SDL, so the SDL3 path can be driven headless too (both clip-end bugs
  of that path were found only by listening).

### 5.3 Amiga family (implemented 2026-11-13)

Researched first (verified, not assumed), then built; backends/amiga/README.md "Voice" has the
backend side.

* **Measurements** (`source/probes/amiga/voicebench.c`, `mkbenchpak.py`, `run-voicebench.sh`;
  WinUAE's cycle-exact 68040/25, within 5 % of the real board for integer code, optimistic for
  multiplies: probes README "Probe 3"): ldp6 decoding **18 %** of the CPU, linear resampling
  plus 8-bit conversion 4.8 % (the resampling is not done in the end, see below), Opus **229 %**.
  WinUAE's 68060 timing gave the 68040/25's numbers unchanged, so it measures nothing; a 060/50
  is estimated at 70-80 % for Opus. Opus adds ~260 KB to the 68k program. So: ldp6 everywhere,
  Opus only in the WarpOS and AROS builds (the user's decision).
* **Paula** (8-bit music: one left and one right channel; the other pair was confirmed free in
  `audio_paula.c`'s allocation masks): `XE_VOICE_CHANNEL` on the free pair, a second
  audio.device allocation, double-buffered chip blocks as long as the music's, its own block
  interrupt, 8-bit, `AUDxVOL` for the volume. The driver reports the clip's rate as accepted
  (Paula plays 22 030 Hz for 22 050, 0.09 % slow), so the engine copies samples without
  resampling (`next_out`'s step-1.0 path). `PAULA14` takes all four channels → `XE_VOICE_MIX`.
* **AHI**, evaluated: (a) the engine mixes (`MIX`): robust with any AHI setup, but the voice is
  resampled to the music's rate (11 kHz on a 68040) and the music volume must be a software
  gain; (b) a second `CMD_WRITE` stream (device API): the voice at its own rate, ahi.device
  mixes (on the CPU in every driver of the AHI source - Paula, Toccata, Aura, Wavetools,
  Filesave, Void all return `AHISF_MIXING` - or on a DSP card), volumes free in `ahir_Volume`;
  but a unit mixes only its "Channels" preference's worth of streams (`devcommands.c`
  `AddWriter`: extra ones go to the silent list; 4 when there are no preferences); (c) the
  low-level API: channels guaranteed, but exclusive use of the hardware and the most code.
  Built: (b), with `VOICEMIX` falling back to (a) for single-channel units.
* **Volumes**: `hal->set_volume(XE_VOL_MUSIC | XE_VOL_VOICE, percent)`: a backend that applies
  a volume in its output returns 1 and the engine leaves the samples alone (on 8-bit Paula a
  software gain of 50 % would throw away one of the 8 bits). Not in `MIX` mode.
* **Disk** (the one real risk found): probes README "Probe 4" measured FFS on the real A1200:
  a small read after a seek ~5 ms with the default buffers, a backward seek walks the file from
  its start at ~25 ms per MB. The ldp6 decoder read 256 bytes at a time (~64 reads per second of
  audio), and the clips were laid out by name, so a dialogue jumped between speakers' regions
  (798 backward seeks between consecutive cues, median 102 MB). Now: a 16 KB read-ahead in the
  engine (`vdec_in`, every backend; RAN0601 takes 11 pack reads instead of ~500), `p_read`
  skips a `Seek` that would not move, and `mkvoice.py` lays the clips out in the cue table's
  order (57 backward seeks, median 2.5 MB). The ReadMe recommends PFS3 or SFS for the pack.
* **Compiler bugs found on the way** (vbcc 0.9h, m68k and presumably PPC):
  1. constant folding converts a 64-bit integer ≥ 2³¹ to `double`, and some constant `double`
     → 16-bit conversions, with the wrong sign: libopus's `QCONST16`/`QCONST32` came out
     negative and CELT's output was garbage (11.5 dB SNR; SILK and the range coder were fine).
     Found by tracing the decoder stage by stage against the host; the same source built with
     GCC for m68k under qemu was bit-exact. Local change in
     `third_party/opus/celt/fixed_generic.h` (`#ifdef __VBCC__`); the 68k decode is then
     bit-exact with the host's 32-bit-multiply path (82 dB from the 64-bit one).
  2. `-O2` dropped an `i += …` of a loop variable inside the loop in `voice.c`'s `meta_get`
     (the Amiga build read the codec as `codec=ldp6`); it now uses a separate index.
* **Linux**: libopus needs `HAVE_ALLOCA_H` with `-std=c99` on glibc (the build broke with GCC
  16); AROS uses `VAR_ARRAYS`, vbcc `NONTHREADSAFE_PSEUDOSTACK` (120 KB, allocated at the first
  clip).

## 6. Settings and command line

`--voice-pak FILE`, `--no-voice`, `--voice-volume N`, `--voice-strict` (OK does not cut a clip, as in Windows); later in-game
settings stored with the other settings (`storage_write`). The voice is Japanese in every language;
`--no-voice` is the way to opt out.

The Amiga family's names (shell arguments and icon tool types): `VOICEPAK=`, `NOVOICE`,
`VOICEVOL=`, `MUSICVOL=`, `VOICESTRICT`, and `VOICEMIX` (the engine mixes the voice into the
music, for an AHI unit with one channel); backends/amiga/readme/options-os3.txt.

`--music-volume 1..100` scales the FM chip's output (music and the FM sound effects; one chip).
The default is engine-wide: 50 % while the voice is on (`VOICE_MUSIC_VOLUME` in
`engine/voice/voice.h`, chosen by listening on 2026-11-13), 100 % otherwise, so a backend that
adds a voice path (AHI, Paula) gets the same balance without doing anything; `--music-volume 100`
keeps the music at full volume with the voice.
The voice cannot simply be made louder: its speech already measures −18.6 dBFS RMS (median of
60 clips) with peaks near full scale, about as loud as the loudest song (XENON_01, −19.3 dBFS;
XENON_12 −30.3, XENON_17 −28.1). The voice sounds too quiet because the bright FM music masks
speech in the same band (found by listening, 2026-11-13), so the balance is set by turning
the music down.

## 7. Steps

1. ✅ `re-tools/voicemap.py`, `resources/voice/voicemap.tsv`, `overrides.tsv`, the review report.
2. ✅ `mkpack.py --voicemap`: `XPAK_T_VOICECUE` per language with the offset translation and its
   checks; `voicemap=<md5>` in the pack meta. `tests/check_pack.py`: every Japanese cue lands on
   the string whose crc voicemap.tsv names.
3. ✅ `source/tools/mkvoice.py` + `make voice-pack` (ldp6); shared-offset dedupe in `xpak.py`.
   4913 clips, 261 MB, under 2 s.
4. ✅ Engine: `engine/voice/` (`vdec_ldp.c`, `voice.c`), the `text_print` hooks, the behaviour
   rules, `XE_TR_VOICE` / `XE_WAIT_VOICE`. `tests/check_voice.py`: the engine decoder equals
   `source/tools/ldp.py` sample for sample (59 clips, reads of 1, 7, 512 samples; run by `make test`
   when `build/xenon_voice.pak` exists).
5. ✅ HAL: `pack_open(which, …)`, `voice_mode` + the channel functions; SDL3 `CHANNEL`, null
   `MIX`; Amiga `NONE` at that step (since step 8: §5.3).
   Checked by driving the null backend in control mode: clips start with their message, play
   in real time (RYO0701: 243 ticks = 4.31 s), the prompt follows the clip, OK cuts, CTRL starts
   none, the S00 countdown plays RYO0708A/B/C on its three parts, English uses the same cues;
   400 clips over S00–S0102 without a warning. The SDL3 backend opens its voice stream (dummy
   audio driver); listened to by the user on 2026-11-13 (after the two clip-end fixes of
   step 7).
6. e2e (when `XENON.DAT` is available, like the other asset-dependent tests): voice pack build,
   cue events at known messages in all three languages, a cut on advance, CTRL skip.
7. ✅ Opus (§3): `third_party/opus`, `XE_HAVE_OPUS`, `vdec_opus.c`, `mkvoice.py --codec opus`,
   `make voice-pack CODEC=…`; 24 kbit/s by listening. While testing it, a clip could fail to end
   on the `CHANNEL` path: the end-of-clip push was made when the ring was empty but the
   resampler still held its last step, so a later push left SDL's held-back frames queued for
   good (seen with ldp6 RYO0704 on `--voice-out channel`). The push now waits for the resampler
   to drain (`drained()` in voice.c).
8. ✅ Amiga family (§5.3): Paula's free channels, a second AHI stream, `MIX` for `PAULA14` and
   `VOICEMIX`; Opus in the WarpOS and AROS builds; hardware volumes (`set_volume`); read-ahead
   and play-order layout for slow file systems.
9. Later: in-game settings, e2e voice checks.

## 8. Open points

* The Windows behaviour around timed waits was traced only as far as "the message waits for
  the clip": whether it then also waits the `{W n}` time was not established (the port waits the
  clip, then the original time).
* The 77 lines whose Windows text was edited: the subtitle and the voice differ slightly; list
  them for the translators (`voicemap.tsv` rows with `fuzzy`).
* `KOU0171B` on a line PC-98 gives to 宏治: keep (Windows re-attributed it) or drop.
