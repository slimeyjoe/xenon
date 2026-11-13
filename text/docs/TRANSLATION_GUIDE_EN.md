# Xenon English translation: editor's guide

Read this before you change a single line of `translation/script_en.txt`, whether you are a human or an LLM.

It covers:
1. how the text gets into the game, and the traps that silently break fixes;
2. the engine's hard limits;
3. fidelity rules, with real mistakes found in the 2026-11 audit;
4. how to keep the game's voices and mood intact.

The companion document **[STORY_BIBLE.md](STORY_BIBLE.md)** gives the plot, the character dossiers and the emotional design. Read it before translating anything in a scene you don't know. It contains spoilers.

Line references like **L4381** mean the line number of an entry's key line in `translation/script_en.txt`. Its English is the line right below, L4382. The key line points at the Japanese in the game's own files (§2); `view/script_en.txt` (`make text-view`) shows the Japanese on that same line.

---

## 0. Where the text lives

`translation/script_en.txt` in this repository is the master copy (README). It holds every fix
made since November 2026: 453 mistranslations, 387 polish items, the Kouji/Kohji tags, the
"spirit energy" rename, the 59 reader-flagged lines, the locked terms (catalyst, capsule,
ＤＲＥＡＭ) and the consistency pass (Ryouko-sensei, telepathy braces, laughs). §11 lists the
work. Older copies of the translation (the PC-98 projects) lack them: never copy text back from
there.

The game is built from this file by the port (`../source`, `make pack`). The PC-98 patch's
workflow, which merged the text into the game's script files on a disk image, is not part of
this repository.

---

## 1. Quick start: changing one line safely

All line numbers in the docs and the tools are the line of the **key line**; the English is the line below. Run every command from `text/`. The tools read the Japanese from the game's files, so import the game once first (`make data` at the repository root, `docs/game-data.md`), and write the view: `(cd ../source && make text-view)`.

0. **Save a baseline.** The lint already reports some known, pre-existing issues and exits 1. You only need to make sure you add no new ones:
   `python3 tools/lint_translation.py > /tmp/lint.before`
1. **Find the line.** Grep the Japanese in the view, not the English:
   `grep -n '例の原因不明の昏睡' view/script_en.txt`
   Take the number of the `//` line; it is the same in `translation/script_en.txt`.
2. **Check that the entry is live:** `python3 tools/lint_translation.py --line 4381`. It answers one of three ways:
   - `LIVE`: edit this entry.
   - `DUPLICATE`: the game uses the **last** copy for every scene. Edit that one, and make the English work everywhere it is used.
   - `INERT`: this entry never reaches the game (§3, dead keys). Find the live entry for the text in the view; pieces of strings with line breaks are matched by repair entries near the end of the file (§2).
   
   It also lists the game scripts that use the line. Read those scenes.
3. **Write the new English.**
   - Follow the fidelity rules (§5), the speaker's voice (§7, STORY_BIBLE.md) and the locked terms (§8).
   - Keep the speaker tag (`[Kohji]: `) and any leading padding the old line has.
4. **Fit it to the window:** `python3 tools/fitcheck.py --pad "[Tracy]: Why, that would be you, Kohji-sama."`
   - It must say `OK`. For menu choices (lines starting with `◆`), add `--menu`.
5. **Replace the line:** `python3 tools/set_line.py --pad 11822 "[Tracy]: Why, that would be you, Kohji-sama."`
   - The script pads, checks encoding and length, and replaces only the English line below `//` line 11822.
   - It keeps that line's CRLF/LF ending. The file mixes both (§2), and a normalising editor would rewrite every line.
   - It refuses if the number isn't a `//` line.
6. **Lint and compare:** `python3 tools/lint_translation.py > /tmp/lint.after; diff /tmp/lint.before /tmp/lint.after`
   - Only the issues you meant to fix may disappear, and nothing may be added.
   - Also check `git diff --stat`: one file, the lines you changed.
7. **Build:** `cd ../source && make pack` (it needs the original game: `make data`, `docs/game-data.md` at the repository root). It rebuilds every language's scripts (`build/en/scripts/*.U.CC`) and the pack. `build/en/lint.txt` lists what the build decided about each string: spaces restored, row counts changed by the word wrap, layouts kept verbatim (`source/lang/README.md`).
8. **Confirm the text reached the game.** This step is mandatory, because a dead or overridden entry fails silently. Use a distinctive fragment of the new line and of the old one:
   ```sh
   grep -alF -- 'that would be you' ../build/en/scripts/*.U.CC   # must list the script(s) from step 2
   grep -alF -- 'your own business' ../build/en/scripts/*.U.CC   # the old text: must list nothing (unless used elsewhere)
   ```
   Use `-F`, because `[Tag]` is a regex character class, and `-a`, because the scripts are bytecode.
9. **Look at it in the game:** `../build/sdl3/xenon --pak ../build/xenon.pak --lang en` (with a save from before the scene, or play to it). The e2e tests (`make TARGET=host e2e`) play the prologue and other scenes in English; a golden screenshot that shows the changed line fails until it is recorded again (`E2E_ARGS="--update"`, `source/tests/e2e/README.md`).
10. **Commit** `translation/script_en.txt`. `git diff --stat` should list that one file, with the lines you changed.

## 2. The translation file format

```
//@S0106 26F2 0 23396b76
[Far]: Well, let's wait for her to wake up. Fortunately, it doesn't   seem to be that coma...
#optional translator note (ignored by the tools)
```

The same entry in `view/script_en.txt`, the line the key stands for:

```
//［ファル］：ま、彼女が目覚めるまで待ちましょう。幸い、例の昏睡ではないようだから‥‥。
```

- **The pair:** a key line, then the English on the next line. The key names a piece of the original script by place, not by its text: the script (`S0106`), the offset of the string in its decompressed bytecode (`26F2`), the segment of that string (`0`: the text between the control codes the engine acts on, such as a key wait; `n.k` the k-th line of a segment, `n.j` a segment with its line breaks as spaces), and a checksum (CRC-32) of that Japanese. The build reads the Japanese from the game, **exactly as stored**, including full-width spaces, `‥` and control characters, and looks the English up by that text: an entry translates its text wherever it occurs, not only at the place the key names. If the game's file holds other text there, the build and the tests stop. Never edit a key line by hand; `../source/tools/tlkeys.py locate TEXT` gives the key for a piece of Japanese (for a new repair entry).
- **Inert entries:** `//-` keys matched nothing in the game, in the PC-98 projects either (§3, dead keys; L5, the Latin motto, has a literal `\n`). They are kept so that the line numbers stay; they have no effect.
- **Notes:** lines starting with `#` or `##` are notes. Lines starting with `!//` are disabled entries (L15013).
- **Line endings:** the file has **mixed line endings**, about 3,600 CRLF and 16,000 LF. Preserve them byte for byte.
- **Line breaks:** a literal `\n` (backslash n) in either line is a line break.
- **Order:** the file is in game order up to the DREAM_∞ END card at L19501. From **L19503 to the end** are **repair entries**:
  - copies of lines whose main entry can never match (dead keys, see §3);
  - pieces of strings that contain line breaks.
  
  Some of these pieces start with a stray character (`ｯ`, `(`, `?`, `L`, `!`) because the game string has a control byte in front; the view shows it.
- **The captain's log, L4375:** in S0106 this text is one string with line breaks, while this file has it on one line. The build tries such a string whole, with its line breaks written as spaces, so L4375 reaches the game like any other line. (The PC-98 patch could not do that and replaced the line with a hand-written patch.)
- **Menus outside the story:** the load menu, the CG gallery and music mode (K/L/M.CC) and the engine's own system menu are not in this file. The port translates them in `../source/lang/<lang>/klm.txt` and `strings.txt`.

---

## 3. Pipeline pitfalls (each one has bitten this project)

| Trap | What happens | How to detect | What to do |
|---|---|---|---|
| **Dead key (inert entry)** | In the PC-98 projects' file 33 keys held a character Python's `shift_jis` codec can't encode, usually `～` U+FF5E or full-width `－`, and 16 more matched nothing (a literal `\n`, text the game doesn't have). They never reached the game; they are the `//-` lines now. Editing one does nothing. | `lint_translation.py --line L` says `INERT`; `--all` lists all 49. | Edit the live entry for the text (find it in the view); most are in the repair section (L19503+). |
| **Duplicate Japanese line** | The build (`source/tools/tl_extract.py`, the same rule as the PC-98 patch's `xenreplacer.py`) makes one dict keyed by the Japanese. The **last** English copy is used for **every** occurrence in every scene. | `lint_translation.py` WARNs on duplicates with different English. | Write one English line that works in all contexts, and put it on the last copy. Earlier copies are cosmetic. |
| **Shared lines across routes** | DREAM_02 and DREAM_03 show the same conversations from opposite sides, with literally the same strings (e.g. the limbo talk S0205 ↔ S0304A/S0305B, Bianca's raid lines). A line may appear in several scenes. | `lint_translation.py --line L` lists the scripts that use it. | Translate it so it works in every scene. Never "fix" it for one route only. |
| **Split strings** | One Japanese sentence is stored as two or more strings shown one after another in the same window, e.g. `(Psychic...` / ` Program Encoder...?)` at L535/L537. | Neighbouring entries that only make sense together; each piece is translated by its own entry. | Split the English at the same point. The combined length still has to fit the window. |
| **Non-shift_jis English** | `—` em dash, `–`, accented letters (é, à). The build stops: the game's fonts have no glyph for them (the PC-98 patch crashed or printed `?`). Curly quotes “ ” ‘ ’ *do* encode, but they render full-width and cost 2 bytes each, so avoid them too. | `fitcheck.py` / `lint_translation.py` ERROR. | Use ASCII only: `...` for ellipses, `-` or `...` for dashes, straight `'` and `"`. |
| **Line endings** | A normalising editor rewrites every CRLF/LF, and the diff becomes useless. | `git diff --stat` shows the whole file changed. | Use the byte-preserving replace in §1. |

---

## 4. Engine limits (verified)

These limits were verified by disassembling the engine `AGS.EXE`. The text printer is at code offset ~0xBBA2 past the MZ header (`ndisasm -b16 -e 3584`).

**In the port.** The port keeps the same text window (60 columns, 4 rows a page, a menu choice on one row), but it draws translations in UTF-8 and wraps them at word boundaries. The build removes this file's padding and puts back the spaces the padded layout lost, so the padding (`--pad`) keeps the file in the PC-98 layout but no longer decides where a row breaks. What still decides whether a line fits is the page (4 rows) and, for menus, the one row; where the word wrap needs a different number of rows than the padded layout, `build/en/lint.txt` says so (`row count A -> B`).

- **Rows:** a row is **60 bytes**. ASCII is 1 byte; full-width characters (`‥`, `◆`, ideographic space `　`) are 2 bytes. The original engine wraps at byte 60 **in the middle of a word**, so the file pads lines with spaces to push a word onto the next row. `fitcheck.py --pad` does this for you.
- **The speaker tag counts:** `[Kohji]: ` uses the first 9 bytes of row 1.
- **Pages:** a page holds **4 rows = 240 bytes**. When a 5th row starts, the engine shows a prompt, waits for a key, clears the box and goes on. Text past 240 bytes is not lost, but it is split behind an unplanned key press, usually mid-sentence. **Keep every line ≤ 240 bytes.**
- **Menus:** a menu choice is **one row, ≤ 60 bytes**, including `◆ ` and any leading `　` the original has.
  - The menu routine (AGS.EXE ~0xBC5C) draws each option unwrapped, padded with `%-*s` to the longest option, at the window origin (column 10 in normal play). It highlights as wide as the longest option.
  - On closing, it clears only **60** columns. Anything past byte 60 stays on screen until the next message is cleared.
  - Past 62 bytes it would also draw over the window frame, because the text window interior is 62 columns wide.
  - Fixed: L485 and L811 used to be 61 bytes, which left a stray `.` at the window's right edge after the choice.
- **What the file shows at row edges:** when a row ends exactly at byte 60 on a word boundary, the next word starts the next row with no space. The file then shows something like `otherwomen`. That is correct; don't insert a space.
- **The original Japanese** never exceeds 175 bytes per line, under 3 rows. Most English lines are 1–2 rows.

---

## 5. Fidelity rules, with real mistakes from the audit

The audit found **453** real mistranslations and **392** smaller deviations (387 of these were polished; 5 were skipped for good reason). Almost none were caused by the length limits. Nearly all were misreadings of these kinds:

**1. Rhetorical negatives are affirmatives.** `〜じゃない`, `〜んじゃない？` and `〜ものか` usually *assert* or *deny the opposite*.
- L5123 `最近の若い女の子がやりそうな事じゃない。` was "Not really something you'd expect a young girl to do." It is now "Isn't that just the sort of thing young girls these days would do?"
- L10723 `涼子が研究を続行するものか。` was "Ryouko will continue her research." It is now "As if Ryouko would continue her research."
- L10161 `あなたの為に言ってるんじゃない。` was "I'm not speaking for your sake." It is now "I'm saying this for your own good!"

**2. Subjectless Japanese: work out who does what.** Use the speaker, the honorifics and the verb of giving or receiving.
- L3373 `（何か言いたげだったけど‥‥。）` was "(I wanted to say something…)". It is now "(She looked like she wanted to say something…)".
- L16499 `あげるわ、そんなの` (giving) was "give it to me". It is now "keep it, I don't care about that thing".
- L18159 `乳首をしごかせてくれよ` (causative, *let me*) had his and her swapped.
- L17361 `牽引ビームに《クワドラ》が捉えられた` had the captor and the captured swapped.
- L14819 `忘れろって` (an order: *you told me to forget*) was "you said you'd forgotten".

**3. Idioms are not literal.**
- ラチがあかない "get nowhere" was rendered "the latch" (L7702).
- 甘く見る "underestimate" was "look fondly" (L9606).
- 代名詞 "byword" was "pronoun" (L13058).
- テク "(bedroom) technique" was "technology" (L8588).
- ハンディ "handicap" was "handy" (L18545).
- 骨折り損 "wasted effort" is not "break a bone". L12303 is right; L12607 is still wrong.
- 利用しない手はない "be a fool not to use it" (L10201).

**4. Stock phrases.**
- `いいか` at the start of a lecture is "Listen", not "It's fine" (L9598).
- `確か` is "if I recall", not "definitely".
- `失礼だな` is "how rude", not "I'm sorry" (L13044).

**5. Negation and degree.**
- L2305 `あまりにナチュラルな笑みだったので` means "so natural that…". It had been turned into "didn't seem very natural".
- L13435 `舌を噛まずに` means *without* biting her tongue.

**6. Never invent details, above all in dream narration.**
- After the first dream, Ryouko quizzes the player on what they saw (S0102). An invented adjective can make an answer unfair. The corridor "cold" (since fixed) and the "expensive-looking dress" (L1009) were both like this.
- Directions and numbers must match exactly. Left and right were swapped twice (L12051, L13575). "15:00, so the sync is at 20:00" had become "in 15 to 20 hours" (L14693).

**7. Keep setups and callbacks.** Many lines exist to be answered, echoed or paid off later. Before changing a line, read 5–10 lines either side.
- L11822 `コウジ様の事ですわ` ("That would be you, Kohji-sama") sets up "M-me?!". The old text, "It's your own business.", killed it.
- Echoes must use the same English both times. Examples:
  - 野暮は聞きっこなし "no tactless questions" (Sayaka L5153; Tracy's variant 〜ですね at L6604);
  - マイナスのマイナスはプラス (L7476 and L9382);
  - 冗談とイカの頭が嫌い (the same phrase at L9698 and L11750; variants at L10475 「冗談は、イカの頭の次に嫌いだ」 and L4933 「精神医学もイカの頭も無い」);
  - 雑音は消して (L11886, L12471, L13559);
  - 不可抗力 (L12952, L12966, L12978, L12469).

**8. Menu text must match what the choice does.** Before rewording a `◆` line, read what follows it in the script: `less ../resources/scripts/listing/S0102.txt`, the structured listing of the original (UTF-8; `make data` makes it). Each `SEN` block lists the choices (`-> D<n>`); the block labelled `D<n>:` below it is what that choice does.

**9. Keep register; don't escalate.**
- Don't make lines cruder than the Japanese. `もっともっと突き上げて` is not "fuck me harder"; `バカ言うなよ！` is not "STOP FUCKING WITH ME!".
- Don't sanitise them either.
- The narration's own distancing remarks must stay, e.g. `私はそんな事は命令していない` (L8974) and `こればかりは本当のようだった` (L9924).

**10. Don't resolve what the Japanese leaves open.** See §6.

---

## 6. What the translation must not flatten

*Xenon* is about not knowing which world is real or who you are. It is written by Hiroyuki Kanno; STORY_BIBLE.md §7 has the critical context. A translation can easily make the text clearer than the original, and every such clarification costs the game its effect.

- **Kouji vs Kohji.** 宏治 and コウジ sound identical. The game's identity riddle and its jokes depend on that:
  - 「オレは宏治であって、コウジではない」 makes Far reply "you're not some religion", because she can't hear a difference (L13072–L13074);
  - 「宏治ではなくコウジであると言っておこうか、宏治」 (L9584);
  - the women say 宏治様 when Kohji demands コウジ様 (L8726 vs L8746).
  
  The English spelling split makes the difference visible on the page. Never let the characters *hear* it.
- **Speaker tags follow the mind, not the body:**
  - `［宏治］` → `[Kouji]`, the student's mind, even inside the pirate's body (DREAM_03);
  - `［コウジ］` → `[Kohji]`: the amnesiac in DREAM_01, the pirate's mind (also when it occupies Kouji's body in DREAM_02), and Kohji in DREAM_04.
  - `［私］`/`［オレ］` → `[Me]` until the game itself names the speaker (e.g. DREAM_01 switches at L2092; DREAM_04 tags its opening ［私］ up to L17575 and ［トレイシー］ from L17579, and her narration names herself at L18003).
  - Unattributed voices stay unattributed: `［男］`, `［男の声］`, the waking voice at L661, and the intrusive 《何たる無様な醜態だ》 at L13621.
- **The first-person split** 私 (dream self / the pirate) vs 僕 (the student) shows the mind switching mid-scene; the speaker tags switch with it (［宏治］ at L6876, back to ［コウジ］ at L7142). English has only "I", so the tags carry it. Keep them exact.
- **The dream/reality ambiguity:**
  - "This is not a dream" (L14613) vs "It feels like I'm in a dream" (L14571);
  - Kohji's "when the consciousness in the dream fades, the one in reality recovers" (L9726);
  - Tracy: "there is no way to tell dream from reality" (L5433–L5441).
  
  Never add certainty.
- **Loops and deja vu are deliberate.**
  - The prologue session with Ryouko replays verbatim three times in DREAM_01 (L44–L68), with further partial replays in S0107/S0108 and S0301/S0301B, and Kouji notices.
  - Sentences are reused across worlds (L1275, L565/L567).
  - Echoes are deliberate: L1855 「やけに、暑かった」 → L1872 「やけに、暑い」, the same thought in the present tense.
  - Mai and Lanhua share lines: L5885 = L6361 is verbatim, and L5871 is a near-echo of L2681.
  
  Identical Japanese must become identical English, and echoes must stay recognisably parallel. Don't "improve" one copy.
- **The Zhuangzi motif:** Mai's butterfly dream (L3525–L3543) and its callbacks (L4983, L5299, the DREAM_01 ending). L3533 must say *"maybe I am the dream the butterfly is having"*.
- **Tonal contrast between routes.** Don't even it out:
  - DREAM_01 is quiet, uncanny and melancholic;
  - DREAM_02 is bombastic parody (the pirate's grandiose speech undercut by slapstick);
  - DREAM_03 is harem comedy with sudden metaphysics;
  - DREAM_04 is a claustrophobic, monologue-heavy erotic melodrama.
- **Expectation-subverting setups:** e.g. Tracy re-crosses her legs at the bedside (S0108, L5341), and then gives a parapsychology lecture instead of a love scene. Keep the set-up as suggestive, and the turn as dry, as the Japanese.

---

## 7. Voice and mood

### Conventions
- Ellipses `...`; sound effects in `*asterisks*`; inner thoughts in `(parentheses)`.
- Telepathic or intrusive speech, written 《…》 in the JP, is `{…}` in the EN. All 72 such lines were unified on 2026-11-08. Bianca's 『』 at L14311 is a hijacked intercom broadcast, not telepathy, and keeps `[[…]]`.
- Honorifics are kept: -kun, -sensei, -sama, -san, -chan, Onee-san. Far is "Professor" (プロフェッサ).
- 涼子先生 is "Ryouko-sensei", never "Doctor Ryouko" or "Dr. Ryouko" (unified 2026-11-08; 49 of the 52 lines).
  - Allowed exception: when Kouji addresses her as 涼子先生 to her face, it may be "you", as English uses no title in the third person there (L3153, L3185, L3189).
- Laughs and interjections (unified 2026-11-08, about 215 lines). Japanese-style laughs are kept, like the honorifics:

  | JP | EN |
  |---|---|
  | フン | "Hmph", for everyone, including lone beats (「フン‥‥。」 = "Hmph...") |
  | フンフン | "Hm-hm" |
  | フフン | "Heh" (smug) |
  | フフ | "Fufu" for the women; "Heh" for Kouji, Kohji and Bianca (her rough register); "Ehehe" for Lanhua |
  | ンフフ | "Nfufu" for the women; "Ehehe" for Lanhua |
  | ウフフ / うふふ | "Ufufu" (Tracy's ladylike laugh, also Sayaka and Ryouko) |
  | フフフ | "Fufufu" |
  | クックック | "Kukuku" |
  | フッフッフ | "Heh heh heh" (Kohji, Kouji, the man's voice) |
  | ふむ / フム | "Hm" (thinking), never a laugh, and not "Hmph" (that is フン) |

  Don't use `*chuckle*` for a spoken laugh: asterisks are for sound effects.
- The silent reaction line `［コウジ］：‥‥‥‥‥‥。` is a beat: keep it.
- Mai's katakana `宏治サン` is commented on in the narration as an odd intonation (L1417, L7660). Keep one consistent rendering.

### Voices (full dossiers in STORY_BIBLE.md §5)
| Character | Japanese markers | English |
|---|---|---|
| Kouji 宏治 | 僕 (DREAM_01), オレ (DREAM_02/03); polite to Ryouko and Sayaka; wry asides | Casual, contractions, flustered; a straight man |
| Kohji the pirate コウジ | 私, 貴様, 〜のだ, うむ, フン, クックック, grandiose jargon | Haughty, no contractions in grand moments, "Hmph", "you fool"; undercut by slapstick |
| Kohji the amnesiac (DREAM_01) | 私, calm, analytical | Neutral, wry, questioning |
| Kohji in DREAM_04 | mostly 僕, smooth and manipulative | Soft-spoken and controlling |
| Ryouko 涼子 | 宏治クン, 〜わ/かしら, フフ, breezy jargon | Teasing, playful, quietly steely |
| Sayaka 沙夜香 | あなた, 〜じゃない, ボク teasing ("little boy") | Smooth, sly, superior |
| Mai 舞 | です/ます, 宏治サン, few words | Enigmatic, dream-obsessed (early DREAM_01); soft and devoted (late DREAM_01, DREAM_02) |
| Megumi 恵 | drawn-out vowels, バカ/ばーか, "Papa" | Bratty, bossy, affectionate |
| Tracy トレイシー | ですわ/ますわ, コウジ様, ス・テ・キ; DREAM_04 mostly inner monologue | Ladylike, gracious, formal |
| Lanhua ランファ, DREAM_01 | polite です/ます, コウジさん, long silences | Cool, minimal, cryptic ("Please remember the dream.") |
| Lanhua ランファ, DREAM_02/03/04 | ですぅ, refers to herself as ランファ, プリプリ (huffy), puns, コウジ様ぁ | Bubbly, childlike, stretched vowels |
| Far ファル (= Farneria) | 〜わ/のよ, curt, reads emotions as colours | Crisp, dry, quick to slap |
| Bianca ビアンカ | DREAM_01: blunt (あんた, じゃないのさ). DREAM_03: changes with power, 我/貴様 villain → アタシ rough → soft わ. DREAM_04: tomboy pilot (〜さ, 〜だぜ) | Keep each register distinct |

---

## 8. Locked terms (glossary)

Use these renderings. Change one only as a project-wide decision, and then change every occurrence.

| Japanese | English | Notes |
|---|---|---|
| 宏治 / コウジ | Kouji / Kohji | See §6. |
| 涼子, 沙夜香, 舞, 恵 | Ryouko, Sayaka, Mai, Megumi | |
| トレイシー, ランファ, ファル (ファルネリア), ビアンカ | Tracy, Lanhua, Far (Farneria), Bianca | Far and Farneria are **one person**. |
| 《クワドラ》 | the Quadra | |
| 大宇宙海賊 | galactic pirate / Galactic Pirate | Was "Universal Pirate" once. L9773 still says "space pirate". |
| 霊子エナジー | spirit energy | Was "Reiko energy" until 2026-11. |
| 大宇宙海賊御用達霊子エナジー増幅ジェネレータ | Galactic Pirate-Approved Spirit Energy Amplifying Generator | 御用達 = "by appointment to", not "Company". |
| 大宇宙海賊霊子エナジー増幅ジェネレータ２ | Galactic Pirate Spirit Energy Amplifying Generator 2 | The copy built in Kouji's world. |
| サイキック・プログラム・エンコーダ / エンコーダ | Psychic Program Encoder / encoder | Variants still in the EN: "Psycho-Encoder" (L14013), "Psychic Encoder" (L14315). |
| 棺桶 (for the encoder) | capsule | Locked (2026-11-07). The coffin image is deliberately not used; "capsule-shaped" at L2014–L2016, L3815, L14011–L14013, L14045. |
| ドリームプログラム(マシン) | Dream Program (machine) | Ryouko's device. |
| 思念 / 思念体 | thought / thought body | Keep consistent (L5277, L7178, L9672). |
| 触媒 | catalyst | Locked (2026-11-07). The same word is used for Mai (reality) and Farneria (dream); the parallel matters, so always "catalyst", and "catalyst woman" for 触媒の女. 精神感応者 is "telepath", not "medium". |
| 既視感覚 | deja vu | ASCII only (é is not shift_jis). |
| Far's ～の色 (emotion colours) | "the color of <emotion>" | Never invent hues or "aura"; Far names a hue only where the JP does (yellow, light blue, green). |
| 惑星エイレート | Elath | |
| 恒星ナーズ / ナーズ星系 | Nuz / the Nuz system | |
| 第１次/第２次/第３次調査隊 | 1st/2nd/3rd investigative team (expedition) | セカンド/サードシップ = "second/third ship". L2204 still says "Squadron". |
| 法務局 | Legal Affairs Bureau | |
| 重核中性子爆弾 | heavy-nucleus neutron bomb (target) | Current EN: "heavy neutron bombs" (L7076, L7168). |
| ＤＲＥＡＭ (terminal, L272/L855/L857) | ＤＲＥＡＭ (full-width, as on screen) | Locked (2026-11-07; replaced the earlier "ONEIROS"). It links to Ryouko's "Dream Program" and the DREAM_n chapter cards. In Japanese, the framing lines decode a foreign word. In English they are adapted so as not to translate plain English: "taken at face value" (L274, L857) and "an ordinary word… no, something else" (L284). The hint that it means more than it says is kept. |

**Alternate romanizations in English criticism:** Kōji, Ryoko, Ranpha, Fal. Don't adopt them; the spellings above are locked.

---

## 9. Checklist for every change

- [ ] I read the scene around the line, at least ±10 lines, and know who is speaking (the mind).
- [ ] The entry is live: not a dead key, and it's the last copy of a duplicate. If the line is shared with other scenes, it works in all of them.
- [ ] The meaning matches the Japanese. No added or dropped details, and negation, subject, numbers and directions are right.
- [ ] Callbacks, echoes and puns are preserved. Repeated Japanese gets identical English.
- [ ] Voice and register match the character and the route's mood.
- [ ] `fitcheck.py --pad` says OK, and I pasted `PADDED_RAW` (≤ 240 bytes; menus ≤ 60).
- [ ] Line endings are untouched (`git diff --stat`), and `lint_translation.py` shows no new issues.
- [ ] `make pack` passes, `build/en/lint.txt` shows nothing unexpected for my lines, the new text is in `build/en/scripts/` (§1 step 8) and on screen, and I committed `translation/script_en.txt`.

---

## 10. Known open items

Line = JP line.

**Fixed on 2026-11-07:**
- **Terms locked:** 触媒 = catalyst (25 lines unified), 棺桶 = capsule, ＤＲＥＡＭ = ＤＲＥＡＭ (ONEIROS removed; L272–L284, L855, L857). See §8.
- **The 59 reader-flagged lines:** these were wordplay, meaning errors and Far's aura colours. A second check found 35 confirmed, 14 minor, 9 localisation decisions and 1 fine (L14053). All proposed changes were applied, with the decision items resolved:
  - the "Lanhua = *mai*" gloss at L7208;
  - sign/signature for 星座/正座 (L10425);
  - ewe/baa for 君/黄身/白身 (L19503 and its overridden copy L13287);
  - "G-Gee?!"/old geezer (L15349–L15353);
  - Monte Cristo for 魔太郎 (L9962–L9964);
  - the accelerator-switch line (L16403–L16413);
  - Far's colours as "the color of <emotion>" (L3681, L3755, L6746, L13951).
  
  The report is `sandbox/claude-notes/story/flagged-lines-verification.md` on the working machine.
- **Speaker tags:** L815, L3639, L6624, L8880, L11046, L11050, L13124, L16843, L18967; also "you-sama" → "Kouji-sama" (L11174).
- **The 61-byte menu choices** L485 and L811 are now ≤ 60 bytes. The quiz answer that echoes L811 (L815) was updated to match.
- **The dead memo entry L5789** was corrected ("Contact", ASCII hyphens). It still never reaches the game; the repair entries L19595/L19589 show the memo.

**Fixed on 2026-11-08:**
- **Ryouko-sensei:** the 16 "Doctor Ryouko"/"Dr. Ryouko" lines now say "Ryouko-sensei" (§7).
- **Telepathy brackets:** the 19 quoted or `[…]` 《…》 lines now use `{…}` (L7424, L7428, L11128–L11156, L13621, L14545). While there, L7428 regained its subject ("Are you...still...confused...?").
- **ス・テ・キ:** Tracy's L15213 is now "Won-der-ful...", which matches the narration's quote at L15171.
- **L4801:** "Where she could be?" was changed to "Where on earth could she be?".
- **Already consistent, so removed from this list:** 愛の結晶 ("fruit of love", 8 lines), 冗談とイカの頭 ("jokes and squid heads"; L4933 is a different idiom), 不可抗力 ("inevitable" in the L12952 scene; Tracy's L12469 has a different meaning).

**Also fixed on 2026-11-08:**
- **雑音は消して** (a running gag: one girl cuts off the other's intercom confession) is "switch off… static" in all three lines (L11886 Tracy, L12471 and L13559 Lanhua).
- **野暮は聞きっこなし** is "no tactless questions" (L5153 Sayaka "…, now."; L6604 Tracy "…, then.").
- **Laughs** were unified per §7. Ryouko's ふむ at L899, L967 and L1119 was wrongly "*chuckle*" and is now "Hm". L8820 lost an invented "Humph".
- **Laugh leftovers:** フッフッフ at L9984 ("Hu hu hu") and L16491 ("Heheheh") is now "Heh heh heh..."; フム at L8104 is "Hm", not "Hmph".
- **L2014 棺桶** already says "capsule-shaped machine", per the locked term; removed from the reader-notes list.

**Still open:**
- **Older reader notes not in the 59:** e.g. L12607 骨折り損; the readers' watch-lists are in `sandbox/claude-notes/story/reader-notes/`.
- **Consistency:**
  - other interjections are not yet checked: hiragana ふふ, フッ, エヘヘ, ほぉ (currently "Ho-", "Hoh-", "Ho--", "Hoh") and the remaining ふむ renderings ("Interesting", "Indeed", "Fine" are context choices, probably fine).
- **Source oddities to leave alone (don't "fix"):**
  - L1111: the JP tags Ryouko's line ［宏治］. The lint will always WARN on it; that is expected.
  - L7568: a ［宏治］ tag inside a Kohji scene.
  - L17983: ［宏治］ inside Kohji's DREAM_04 scene.
  - The single-letter lines `Ｂ` `Ｃ` `Ｄ` `Ｅ` (L1, L6804, L9524, L7130), probably engine markers.

---

## 11. History and artefacts

- **2026-11 full audit:** 858 findings were re-evaluated with context: 453 confirmed, 392 minor, 9 localisation, 4 rejected. All 453 confirmed items were fixed. 387 of the 392 minor items were polished; 5 were skipped. The fixes were written in each character's voice and fitted to the window.
- **Follow-ups:** the Kouji/Kohji tag fix and the spirit-energy rename.
- **Docs and tools:** this guide, STORY_BIBLE.md and the Linux tools.
- **Reader-flagged lines:** the 59 lines flagged while writing the story bible, plus speaker tags, the 61-byte menus and the dead memo entry.
- **Locked terms:** catalyst, capsule, ＤＲＥＡＭ; the ＤＲＥＡＭ framing lines adapted for English readers.
- **Consistency pass (2026-11-08):** Ryouko-sensei, `{…}` telepathy, the laugh table in §7, the "static" and "no tactless questions" gags, and the later laugh leftovers (§10).
- **Working notes** (audit reports, per-item reasoning, diffs, reader notes) are in the git-ignored `sandbox/claude-notes/` of the machine where the work was done. They are not in the repository.
- **Tools:** `tools/fitcheck.py`, `tools/set_line.py`, `tools/lint_translation.py`, `tools/charset_pl.py` (the character table, which the port's build reads too). The build: `../source/tools/tl_extract.py`.
- **Where:** the work above was done in a fork of the Xenon PC-98 translation project, before the text moved here.
- **The PC-98 patch** (until 2026-11-13 the way this text was tested) merged it into the game's `.CC` files and compressed them for a disk image. Its hardest bug, kept here as a warning for anyone who writes such tools: AGS.EXE starts its LZSS ring buffer **zeroed** (loader ~0x8A44), while textbook LZSS fills it with spaces. Runs of ASCII spaces (the `--pad` padding) in a file's first 4 KB were compressed as copies from that buffer and became NUL in the game: the string ended early, the rest ran as bytecode, and the game quit to DOS with "`sNNNN.cc page=N, ...`". This broke every build from the full audit's fixes until the encoder and the decoder were both made to start zeroed. The port's LZSS (`source/tools/lzss.py`, `source/engine/pack/lzss.c`) starts zeroed as the game does.
