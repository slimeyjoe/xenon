# Xenon ～夢幻の肢体～: story bible

> **Spoiler warning.** This document reveals the whole plot, including every ending and twist. It exists so that translators and editors keep the English faithful to the story's intent.

**Companion:** [TRANSLATION_GUIDE_EN.md](TRANSLATION_GUIDE_EN.md) gives the editing rules, engine limits and locked terms.

**References:** **Lnnnn** is the line of an entry's key in `translation/script_en.txt`, whose Japanese `view/script_en.txt` shows on the same line (`make text-view`); its English is on the next line. Script files (`S0106` etc.) are the original game's; `make data` (repository root, `docs/game-data.md`) decodes them into `resources/scripts/`, with a structured listing of each in `resources/scripts/listing/`.

**Sources:**
- The structure and facts below come from a full read of the Japanese script. It was done in six parallel passes, each with line citations, followed by a fact-check.
- §7 also draws on one outside critical review, attributed there.
- Anything inferred rather than stated is marked *(inference)*.

**Adult content:** the game is an adult title with explicit and partly non-consensual scenes. This document describes those scenes only at the level of story function and effect.

---

## 1. At a glance

- *Xenon ～夢幻の肢体～* ("Xenon: the limbs of phantasmagoria") is a PC-98 adult visual novel by C's Ware, 1994. The engine is `AGS.EXE`.
- Writer: Hiroyuki Kanno (see §7). He later wrote *EVE Burst Error* and *YU-NO*.
- **Premise:** Kouji, a young man, is a paid subject at a research and medical centre. He undergoes hypnotherapy with Ryouko for a recurring nightmare. In the dream he is "Kohji", aboard a spaceship near an alien planet, and he cannot tell which world is real.
- **Structure:** a prologue whose first choices send the player into one of four "DREAMs". Each is a different take on the same cast:
  - **DREAM_01**: the serious mystery. One ending.
  - **DREAM_02**: the pirate's mind in Kouji's body; parody and power fantasy. One ending.
  - **DREAM_03**: Kouji's mind in the pirate's body among his crew; harem comedy. Four endings.
  - **DREAM_04**: a prequel from Tracy's point of view. One ending.
- **Epigraph** (L5): *Flectere si nequeo superos, Acheronta movebo* (Virgil, *Aeneid* VII; also Freud's motto for *The Interpretation of Dreams*). The game frames everything as dream analysis.

---

## 2. Structure and routing

### 2.1 Prologue (S00 / S00B / S00C)
The three prologue files share their opening text but differ in routing:
- **S00** offers only DREAM_01 and has no corridor.
- **S00B** sends every first choice into the corridor, so it offers only DREAM_02/03.
- **S00C** has all four routes.

`AGS.EXE` lists all three (`menu1 s00 s00b s00c l k m`), so the game picks a prologue, probably unlocking the fuller ones with progress. *The selection logic has not been verified.* The table below is S00C.

1. A hypnotic voice (L13–L30): "now you will wake up… everything you see and hear there is reality… as a mirror that faithfully reflects your heart"; then the count to three. Waking leads *into* the session world, so the frame is inverted from the first line.
2. Ryouko's office (L44–L78). This scene will replay several times.
3. The dream: the narrator (私) wakes nameless in a hospital-like room with electrodes on his body (L90–L134). **First choice:**

| Choice | Flag | Leads to |
|---|---|---|
| Leave the room | F64 | a variant corridor → **DREAM_04** (S0401) |
| Try to call a nurse | F65 | **DREAM_01** (S0101) |
| Investigate the room | F66 | the corridor, then a second choice: **stay** to see whose footsteps those are → **DREAM_03** (S0301); **hide** behind a nearby door → **DREAM_02** (S0201) |

The corridor (L311–L345) is the game's signature space: hot air despite the cool rooms, potted plants, nobody around, a high whine, hard footsteps "コッ‥‥コッ" approaching. S0102's quiz has branches for all three first choices (F64/F65/F66).

### 2.2 The routes

| Route | Scripts | Worlds | Choices | Endings |
|---|---|---|---|---|
| DREAM_01 | S0101–S0111 | alternates dream/reality every file | cosmetic hubs, plus the quiz in S0102 | 1: "DREAM_1 … END" (L7282) |
| DREAM_02 | S0201–S0207 | reality (Kouji's body), with glimpses of the dream | hubs, joke loops, false choices | 1: comic harem ending (L11194) |
| DREAM_03 | S0301 → A or B track → S0306A | dream world (the Quadra) | track choice, bathroom choice, Elath line | 4: α, β, γ, δ (L16975, L17107, L17287, L17359) |
| DREAM_04 | S0401–S0403 | dream world (Elath survey ship) | none | 1: "DREAM_∞ … END" (L19501) |

### 2.3 DREAM_03 tracks and endings
- **Tracks:**
  - In S0301, following the footsteps (F9A) jumps to **S0301B**: the engine room and Lanhua. Its Tracy/Lanhua menu is a false choice; both options lead to S0302B, and F98 is never set.
  - Investigating alone leads to a real choice: Tracy explains (F98) → **Tracy (A) track**, S0301A; Lanhua explains (F9A) → **Lanhua (B) track**, S0302B.
- **The A/B files are offset by one:**
  - S0302A = S0303B and S0303A = S0304B are near-identical.
  - S0301A ≈ S0302B and S0304A ≈ S0305B share about half their lines, with the companion swapped.
  - S0306B is a much shorter counterpart of S0305A. It jumps into S0306A, which both tracks share.
- **Bathroom choice** (S0302A/S0303B, L12912): going to look while Far showers sets **F8B**.
- **Elath line** (S0304A L14725 for Tracy → F97; S0305B for Lanhua → F99): saying Kohji's secret password about "the time we spent on planet Elath".

| Ending | Condition | Heroine |
|---|---|---|
| ＤＲＥＡＭ＿３α | F8B = 0 (didn't go to look) | Far |
| ＤＲＥＡＭ＿３β | F8B = 1, F98, no Elath line | Tracy |
| ＤＲＥＡＭ＿３γ | F8B = 1, F9A, no Elath line | Lanhua |
| ＤＲＥＡＭ＿３δ | F8B = 1, Elath line said on either track | Bianca |

The "gentleman" choice leads to Far, although Far's love scene in S0303A/S0304B plays on every path. Speaking the pirate's password leads to Bianca rather than the girl who heard it. *(Inference, for β/γ/δ only: those endings give a heroine not yet romanced on that path.)* All four end with the fairy-tale formula 「というような毎日を、過ごしましたとさ」 (L16973).

### 2.4 The S0102 dream quiz
After the first dream, Ryouko asks Kouji to recall the dream as multiple choice: the room; then a path-dependent question (how he left the room on the F64/F65 paths, L811/L813; the flowers' colour on the F66 path, L827–L831); the door; the corridor; the sound; and the girl's ribbon, outfit and eyes. All flags (F67–F6D, F6F) must be right, or she finds "contradictions" and the session restarts (L1035–L1063; the restart block is gated by F00).
- The decoys are planted: "narrow eyes, glasses" fits Tracy and Ryouko; "boiler room" foreshadows the engine room.
- She also asks leading questions about a heartbeat he never mentioned (L963, L1212).
- **Every detail of the dream narration is quiz-relevant.** Never add or drop descriptive details in S0101.

---

## 3. The worlds, and how the routes relate

### 3.1 The two settings
- **"Reality":** a three-storey medical and research centre in Japan, in winter, "ill-suited to this cramped country" (L3259). It is quiet, half-empty and sterile.
  - DREAM_02 fixes the date at the late 20th century (L10463).
  - Corridors are hot while rooms are cool (L1311–L1315), which is never explained here.
  - Kouji has lived there for weeks. He does twice-weekly computer-aided analysis for about three months, a well-paid side job arranged by his girlfriend Megumi (L4935–L4941).
- **"The dream":** a far-future space setting.
  - Ships of a survey or rescue mission orbit close to the star **Nuz** (ナーズ), near the planet **Elath** (エイレート), where "ruins of a prehistoric civilisation" were found (L2188, L3769).
  - The 1st and 2nd expeditions fell into an unexplained coma (L3791).
  - Hot corridors here are explained by the star (L2543).
  - The calendar is 宇宙歴, and time is counted in base 64 (L7986, L9510).

The two worlds mirror each other room for room, as the bible shows in §6.2.

### 3.2 What each route says is "really" going on
The routes are **alternative versions**, not one continuity. They recast the same characters in different roles. Don't harmonise them in translation.

- **DREAM_01: the frame inverts.**
  - The clinic is a memory replayed **6,012 years** after humanity died out in a war with 重核中性子爆弾 (current EN: "heavy neutron bombs", L7077/L7169; L7162–L7168).
  - Mai and Ryouko turned a military mind-control project into a "cyber-projector" that stored human consciousness in machines (L7174–L7180).
  - Only Kouji's and Mai's minds survive as **thought bodies** (思念体). In the space world they are Kohji and **Lanhua**: "Lanhua means 'to dance' in a foreign tongue" (L7208). 舞, Mai, means dance.
  - Far's crew are aliens who came to study the ruins. Elath is, by implication, the ruined Earth *(inference: never stated)*.
  - The route ends with "Adam and Eve" and a test tube holding "Mai's and my… humanity's only seed" (L7280).
- **DREAM_02 and DREAM_03: two sides of one mind swap.**
  - The pirate Kohji lay down in his "Galactic Pirate-Approved Spirit Energy Amplifying Generator" to spy as an astral body (L7332–L7356). At the same moment Ryouko ran her "Dream Program" machine in Kouji's era.
  - The two machines synchronised across space-time, and the minds swapped (L9668–L9676).
  - DREAM_02 follows the pirate in Kouji's body; DREAM_03 follows Kouji in the pirate's body. They share much of their text, each seen from the other side:
    - the core of the limbo talk (about L9580–L9742 in S0205) recurs verbatim in S0304A/S0305B; the openings and the password exchange differ (L14605–L14631);
    - Bianca's raid lines (L8276ff);
    - the closing mind-chat about a "monaural generator" (L11128–L11156 in S0207 and S0306A).
  - From the pirate's side, Kouji's world is "ancient Earth" (太古の地球, L9664).
  - *Inconsistency to leave alone:* S0207 L11122 has Kohji say 「未来の私の肉体」 "my future body".
- **DREAM_04: a prequel at Elath, Tracy's version.**
  - The Quadra is a survey ship; Far is the professor heading the dig, Tracy her secretary, Bianca the pilot.
  - Kohji is "the sole survivor of the 2nd survey team's accident" (L18061). DREAM_01 also first says "sole" (L2204) and revises it to "one of two" once Bianca's pod is found (「唯一の‥‥いや、唯二の」, L3909).
  - The route shows how Kohji turned Tracy, then Lanhua, into the devoted "slaves" of the later routes. It ends with Far as the next target: "DREAM_∞".

### 3.3 Recurring cast across routes

| Character | DREAM_01 | DREAM_02 | DREAM_03 | DREAM_04 |
|---|---|---|---|---|
| **Kouji** 宏治 | student; his mind is "Kohji" in space | absent (his mind is on the Quadra; heard via sync) | protagonist in the pirate's body | Tracy's ex-lover's name (L18089) |
| **Kohji** コウジ | amnesiac "sole survivor" = Kouji's projected mind | protagonist: the pirate in Kouji's body | an absent voice (limbo, intrusions) | the 2nd-team survivor who grooms Tracy |
| **Ryouko** 涼子 | hypnotherapist; co-mastermind of the projector | mad scientist; subdued, then devoted | flashback; drugs Kouji for an experiment | — |
| **Mai** 舞 | Ryouko's sister; formula genius; becomes Lanhua | catalyst for the machine; devoted; abused | vision; the catalyst | — |
| **Megumi** 恵 | girlfriend; warns him; kidnapped | girlfriend; tied up; "first wife" | mentioned | — |
| **Sayaka** 沙夜香 | undercover Legal Affairs Bureau agent | government operative; antagonist | — | — |
| **Far** ファル (Farneria) | captain/professor of the 3rd team; telepath | kidnapped professor, the catalyst | captive professor, the encoder's inventor; heroine | professor heading the dig |
| **Tracy** トレイシー | Far's assistant and nurse | Kohji's secretary/"slave" | Kohji's secretary; heroine | POV; Far's secretary |
| **Lanhua** ランファ | Far's "assistant" = Mai's thought body | Quadra engineer | Quadra engineer; heroine | crew member Tracy seduces |
| **Bianca** ビアンカ | 2nd-expedition leader's daughter; accuses Kohji | one sync flash with a whip | rival pirate and ex-partner; heroine (δ) | the Quadra's pilot |

---

## 4. Plot summaries

### 4.1 DREAM_01 (S0101–S0111): "Who am I?"
1. **S0101, dream.** Lanhua, a frightened girl in a revealing wine-red outfit with a lemon-yellow ribbon, says only her name. He can recall his height and hobbies "like reading a file", but there's "a net over" his name (L445–L447).
   - In a cold room of capsules, a console reads P.P.E., PSYCHIC‑PROGRAM‑ENCODER.
   - Inside lies a naked red-haired girl (Far). When he reaches for her: "Now decoding…" and alarms.
2. **S0102, reality.** "Kohji-san, it's time to wake up" (L661). The Ryouko scene replays; Kouji notices: "Hasn't this happened before?" (L671).
   - The quiz (§2.4). Ryouko's Freudian reading follows: wish fulfilment, a regression to the womb, libido.
   - He has forgotten Megumi's name. His memory is unsure "even of minutes ago" (L1275).
3. **S0103, reality.** He meets enigmatic Mai at the elevator ("Every dream holds a message", L1483) and girlfriend Megumi in his bed.
   - Sayaka interrupts. She is nominally Ryouko's assistant, a party-girl type.
   - Each erotic approach ends in vertigo and a jump to the other world. The fade-out line L1855 「やけに、暑かった」 is echoed in the present tense by L1872 「やけに、暑い」 in the dream.
4. **S0104, dream.** The engine room. Far introduces herself: "Far… Farneria", a parapsychologist (L2048, L2202).
   - His nameplate says "Kohji". The speaker tag turns from ［私］ to ［コウジ］ (L2092).
   - They are near the star Nuz, and he is the sole survivor of the 2nd investigative team (L2204).
   - Tracy, the glasses-wearing nurse, arrives. Lanhua says their first meeting "was a dream" and asks him to "please remember" (L2665–L2681).
5. **S0105, reality.** The Ryouko scene replays a third time; he pre-empts her.
   - Megumi says it was "a week ago", not yesterday (L3347); the centre will close; "some investigative agency" is moving (L3317).
   - The night rooftop: Mai tells him **Zhuangzi's butterfly dream** (L3525–L3543). He can no longer be sure he isn't "the dream the butterfly is having".
6. **S0106, dream.** A survivor pod brings Bianca (asleep). A derelict 2nd-expedition ship sends a distress signal; its captain's room is under a thin layer of dust (L4343).
   - The **captain's log**: crews collapsing "since *he* came, the survivor of the 1st team"; "at least my daughter Bianca…"; "ruins of a prehistoric civilisation!? No, *he* is…" (L4365–L4399). A micro disc (MD) is tucked inside.
   - The ship falls toward the star. Lanhua appears: "Do you still not understand? Remember the dream." He is struck from behind (L4545).
7. **S0107, reality.** "Kouji-san, it's time to wake" (L4551), the mirror of L661.
   - Megumi phones: get out; "that mind-control machine…" Then a scream (L4807–L4839).
   - Sayaka is caught snooping and kisses him as "hush money".
   - Ryouko theorises that consciousness is 思念 and a human a 思念体; mind control is brainwashing; "…for example, butterflies" (L5277–L5299). This is "the last session".
8. **S0108, dream.** Tracy nursed him. She says Lanhua never left the ship. "Weren't you dreaming?" (L5391)
   - Tracy's **parapsychology lecture**: precognition, déjà vu, and the impossibility of telling dream from reality (L5405–L5461).
   - As he tries to play the MD he blacks out into the reality session, and the disc vanishes.
   - Bianca wakes: "You're the one who wiped out the 2nd expedition!" (L5665). His name is on no team roster. "Who am I?" He is locked in.
9. **S0109, reality.** The centre is emptying. Ryouko's notebook describes the mind-control mechanism; the subject's safety is "not guaranteed". A memo reads "Sayaka, contact: Legal Affairs Bureau".
   - Mai comes at night. She speaks Lanhua's exact lines, then comforts him; they sleep together. "When you wake again, we can be together" (L6089).
10. **S0110, dream.** Lanhua offers the same comfort; a Mai picture layer opens her scene, so the two women visually merge (§6.6). The MD reappears.
    - The MD shows the captain and Bianca shouting *his name* at empty air.
    - Lanhua: "face reality, recognise the dream as a dream… I'll be waiting below."
    - Far has never heard of Lanhua. Kohji takes Far's gun and flies to Elath.
11. **S0111, the ending.** In the ruins on Elath, the corridor is the clinic's. The narration switches to 僕/宏治 as he opens the computer-room door (L6876).
    - Megumi is tied up there. Ryouko declares his consciousness will be "projected"; her sister found the formula.
    - Mai strikes him from behind. Sayaka bursts in, revealed as the Legal Affairs Bureau agent sent by Megumi's father. Ryouko condemns the military's neutron-bomb policy (L7076), and Sayaka defends it as a "righteous thunderbolt" (L7078).
    - Mai: "I loved you… close your eyes."
    - Then the reveal (§3.2), told by Lanhua. Far's crew leave with their memories altered. Two machine-minds remain, 2,000 years of battery left, hoping humanity will flourish again.

### 4.2 DREAM_02 (S0201–S0207): the pirate on ancient Earth
1. **S0201.** The capsule room again. Then 「だんだん、思い出してきたぞ‥‥」: the narrator is **the pirate Kohji**, who lay down in his generator with the kidnapped telepath Farneria as catalyst. "There can be no failure… surely" (L7358).
2. **S0202.** He wakes in Kouji's body in hospital room 201. "I am the space pirate Kohji," he announces into the intercom, to a woman's voice (L7516–L7522).
   - Slapstick: he walks into manual doors. He sneers at the "rotten-fish-eyed" natives.
   - Mai weeps that he doesn't know her and vows to restore him. Megumi says he's her boyfriend and that "this is Earth, obviously!" (L7840).
3. **S0203.** At night a newspaper dated 西暦 proves this is "ancient Earth" (L7990). He diagnoses Ryouko's machine: a bug in the transmission routine. Two machines firing in phase across time explain the swap (L8110–L8122).
   - Sayaka: "government-related" (L8156). He exposes her as a credit-stealing small-time crook.
   - Ryouko tries to buy the bug-fix with sex. He reverses the bargain and subjugates her.
4. **S0204.** A "contract": she obeys him absolutely. He demands "call me Kohji-sama", and she says "Kouji-sama" (L8726–L8746). He renames the machine "Generator 2".
   - Ryouko drugs her sister Mai and pressures her into the scene and the "slave" pledge (L8972–L9262). The narration notes "I ordered no such thing" (L8974). Mai slaps Ryouko and leaves in tears.
5. **S0205.** On the astral plane he speaks with Kouji's mind (§3.2). They plan a forced 3-second sync in 5 hours to defeat Bianca. The password for the crew is "the time we spent on Elath".
   - Sayaka's seduction is a job ("if it weren't for work…", L9944). He turns it into a punitive, forced encounter; the narration admits her tears are real (L9924).
6. **S0206.** Sayaka has tied up Megumi. He decides Megumi wants a dominant man; she ends up calling him "master" and accepts a proposal to be his "slave wife" (L10503).
7. **S0207.** He wakes bound in a chair; Sayaka plans to use the machine to rise in the Bureau.
   - He wins with a rakugo trick, 饅頭こわい reverse psychology: he begs her not to enter the formula, which is exactly what he needs at exactly the sync time ("What time is it?!" "Just now!", L10901–L10903).
   - Ryouko bursts in and beats Sayaka. He "dies" and then sits up.
   - **Ending:** stuck in the past, he keeps a harem: Megumi as "first wife", Ryouko, Mai and a subjugated Sayaka. He chats telepathically with Kouji weekly and plans a monarchy. "Kyaa, Kouji-sama!" (L11192, a dead key; the live copy is L19575)

### 4.3 DREAM_03 (S0301–S0306A): the student among the pirate's crew
1. **S0301.** The capsule room. Tracy or Lanhua finds him: "Kohji-sama".
   - Flashback to reality: Ryouko seduces, drugs and straps Kouji into her machine, with Mai unconscious as the "catalyst". "It'll only fry 20–30 thousand synapses" (L11442, a dead key; the live copy is L19539).
   - He wakes in a soft bed between Tracy and Lanhua competing to be "number one". Track choice (§2.3).
2. **A: S0301A / B: S0302B.** A Q&A with the chosen companion explains the Quadra, the generator, Farneria as catalyst, and "the great space pirate is you".
   - He tests reality: a punch to the wall hurts. "Maybe I'm only dreaming that it hurts" (L12787).
   - Reality voice-overs: Ryouko and a "man", the pirate in Kouji's body, discuss the machine's bug.
3. **A: S0302A / B: S0303B.** The VIP room: Far is a hostile captive.
   - **Bathroom choice** (F8B). Peeking gets him hit. Waiting brings a vision of Mai apologising, with a man's voice exploiting her guilt (L13177–L13193).
   - He frees Far. She reads his "clear green" sincerity and a "warm colour" unlike the cruel pirate's (L13034–L13058).
   - "I'm Kouji, not Kohji." "You're not some religion" (L13072–L13074).
4. **A: S0303A / B: S0304B.**
   - Far invented the Psychic Program Encoder for psychiatric treatment; Kohji "made it worse" (L14009–L14117). She admits her "myopia", and they become lovers.
   - An intrusive voice says 《何たる無様な醜態だ》 "What a pathetic disgrace" (L13621).
   - Bianca raids: "To Kohji, the cowardly and feeble so-called space pirate…" (L14311).
5. **A: S0304A / B: S0305B.**
   - Bianca: five years ago Kohji's trick left her in a military prison (L14525–L14531). She whips Kouji down and takes Far and the encoder.
   - Limbo with Kohji (§3.2). Kouji wakes; it is 15:00, so the sync will be at 20:00.
   - The **Elath line** choice. Saying it reopens the companion's wound: "You told me to forget that time" (L14819/L15975). Then a love scene.
   - Not saying it: he confesses he isn't Kohji. Tracy already knew (L14779); Lanhua laughs it off, "Kohji-sama is Kohji-sama" (L15957), and he suspects she knows.
6. **A: S0305A / B: S0306B.** The "Trojan Horse operation": hiding in a locker while Bianca captures the Quadra, with "high flexibility, adapting as circumstances require" (L15165).
   - Tracy's version of Bianca: Kohji's partner, who betrayed *him* (L15231–L15243).
   - Cutaway: Bianca assaults the captive Far. Then the reveal: Kouji and the girls are watching from Bianca's quarters.
7. **S0306A, shared.** The duel: Kouji stalls; at the sync Kohji takes over for one second and only grabs her arm (L16365–L16385).
   - A self-destruct switch is in Bianca's molar; Kouji removes it with a kiss. "Say you love her," Kohji orders (L16511).
   - An accidental blast (a カチッ after Bianca's kiss, L16581; she apologises, L16701) destroys the encoder, which Kouji had meant to destroy anyway (L16705). It also strands him. The epilogue is a harem sitcom aboard the Quadra. Then the night scene of the flag-selected ending (§2.3).

### 4.4 DREAM_04 (S0401–S0403): Tracy's melancholy
1. **S0401.** An unnamed ［私］ wakes "like a sleepwalker". Far is the professor on the Elath dig.
   - A flash-forward shows a degradation scene. Then: "Yes… I am Tracy… the survey ship Quadra, near the planet Elath" (L18003–L18005). She is the professor's secretary, and the lewd slave of her master Kohji-sama.
   - The flashback: she nursed Kohji, the sole survivor, who resembled **her old lover Kouji** (宏治, L18089).
   - With soothing "you're not fulfilled" and "true self" talk, he reframes her shame as love. Her address shifts from Kohji-san to Kohji to Kohji-sama (L18081 → L18651).
2. **S0402.** Her "training" is told as willing ecstasy. Bianca is the friendly pilot.
   - Tracy kisses Lanhua to hide a secret, and Kohji orders her to "win that girl over". She seduces Lanhua with the same "true self" rhetoric.
3. **S0403.** Kohji takes Lanhua while Tracy must only watch; she cries "for no reason" (L19413–L19415). Placated by her "privileges", she receives the next order: "Now make Far your captive" (L19485).
   - "DREAM_∞": an endless cycle.

---

## 5. Character dossiers

Speech profiles give the Japanese markers and the English rendering rule. "Do/don't" notes come from errors found in the 2026-11 audit.

### Kouji (宏治), the student
- **Who:** a young man taking a well-paid side job as a research subject. His girlfriend Megumi arranged it through her father, the centre's sponsor (L1601, L3281, L4941). He admits it was "quick money" (L3477–L3479).
  - Wry, decent, easily flustered. Passive: in DREAM_01 the women hold all the initiative and all the knowledge.
- **Speech:** 僕 in DREAM_01, オレ in DREAM_02/03 (with Ryouko occasionally 僕). Polite です/ます to Ryouko (涼子先生) and Sayaka; casual with Megumi (bare 恵) and the crew.
  - Deadpan one-liners: 「現実とは、こういうものさ」 (L1395); 「横着が文明を発展させてきたんだ」 (L3341).
  - In DREAM_03 he is the tsukkomi (straight man) to Kohji: 「なんだよ、その２０％増しは！」 (L9624).
- **Arc:** embarrassment → déjà-vu unease → suspicion (L2729, L3195) → doubting his own existence (L3541).
  - In DREAM_03 he moves from denial to pragmatic acceptance (L12789). He is a reluctant stand-in who wins by bluff, decency and comedy.
  - He frees Far, refuses Tracy's love for a past he didn't live (L14843), and destroys the encoder.
- **Do/don't:** keep him likeable and slightly self-deprecating. In DREAM_03 sex scenes he turns oddly dominant (L13843, L15423), possibly the pirate leaking in *(inference)*. Translate this as written; don't explain it.

### Kohji (コウジ): one name, three faces
1. **The amnesiac, DREAM_01:**
   - Narrates in calm, analytical 私. He is tagged ［私］ until he reads his nameplate (L2092).
   - Dry self-irony: 「たちの悪い冗談だな」 (L158). He has a leader's instincts: "at least *I* would never have [let a stranger roam]" (L2597).
   - He grows active only once: he takes Far's gun, and still stays gentle (L6590, L6758).
   - He is Kouji's projected mind.
2. **The great space pirate, DREAM_02/03:**
   - A self-styled genius and would-be ruler of the galaxy, aiming at "information supremacy" (L7350–L7354).
   - **Speech:** 私, 貴様 to everyone, 〜のだ/〜であろう, うむ; tic 「フン、まぁいい」; laughs クックック / フハハハ; when fading, 「んひゃひゃ」 (L9710). Grandiose jargon: アポステリオリ (L9688), メタモルフォーゼ.
   - Catchphrases: 「我がゆくは、覇王への道のみ」 (L11088; variant at the end of L8584); 「冗談とイカの頭が嫌い」, shared with Tracy (L9698, L11750).
   - **The comedy engine:** he claims rationality and power, but the puny body fails him. Automatic doors open just before his kick; "I never make the same mistake twice" (L10064); he kicks a wall and bleeds from the shin.
   - **Darker side:** his conquests escalate in coercion. The text frames them with comic music and narration, and also with authorial distance (§6.8).
   - Rare softness: 「舞、私は貴様の事を、一番大事に思っていた」 (L10849), sincere or a performance; and his "small apology" to Kouji (L11184).
   - **English:** haughty, no contractions in grand moments, "Hmph", "you fool", "puny one" (脆弱なる者).
3. **The groomer, DREAM_04:**
   - Speaks mostly in soft **僕**, the student's pronoun. He is melancholic about a God who saved only him (L18075–L18079).
   - Methodical: reassurance, reframing shame as love, then escalation. He switches to オレ in the flash-forward.
   - **English:** soft-spoken, intimate, controlling. Not the bombastic pirate.

### Ryouko (涼子)
- **Who:** hypnotherapist, physician ("I'm a doctor", L72) and self-described scientist. In her late twenties (vain about it: 「切り捨てれば二十歳」, L9014; 「三十路前」, L10945). Short hair, sharp narrow eyes (切れ長, L1247).
  - **DREAM_01:** co-mastermind with her sister Mai. She projects Kouji's consciousness to preserve humanity. Opposed to the military's neutron-bomb policy (L7072–L7076).
  - **DREAM_02:** a reckless mad scientist ("failure is the mother of success", L7396; the machine is "theoretically perfect", L7392). Subdued, then a jealous devotee. She drugs her own sister unprompted.
  - **DREAM_03:** in the flashback she seduces and drugs Kouji, joking about frying synapses.
- **Speech:** 宏治クン (katakana クン); 〜わ/〜かしら/頂戴; フフ, ホホホ, おーっほっほ; coquettish 「あ・げ・る」.
  - Didactic Freud and consciousness jargon, delivered breezily: 願望充足, リビド（性衝動）, 思念体.
  - Steel underneath: 「副作用ということは、あり得ないわ。絶対に。」 (L3201); 「健忘症。」 (L3205).
- **Do/don't:** keep the teasing light and the jargon precise (TL guide §5). Her leading questions in S0102 must stay leading.

### Mai (舞)
- **Who:** Ryouko's younger sister. She is
  - in DREAM_01, the genius who found the "16th-order transformation formula" (L6982) and Lanhua's real self;
  - in DREAM_02/03, the psychic "catalyst" (触媒) for Ryouko's machine, the reality twin of Farneria (L8844, L11446).
- **Presence:** translucent skin "so pale it seems unreal" (L3461). Appears unnoticed, passes him without seeing him, always "strolling". Knows his dreams without being told (L1527, L4611).
- **Speech:** polite です/ます, few words. Calls him **宏治サン** with katakana サン, an odd intonation that the narration comments on (L1417, L7660). In DREAM_01 she asks him to drop the -san (L5989).
  - Key lines: 「夢にはメッセージが込められているんです」 (L1483); the Zhuangzi passage (L3525–L3535); 「宏治サン、私、好きだった‥‥」 (L7108).
- **Arc:** enigma → comforter → conspirator who saves him (DREAM_01); devoted and abused (DREAM_02).
  - The outside review calls her the devoted, caring figure. That holds for DREAM_02 and late DREAM_01. In early DREAM_01 she is enigmatic and dream-obsessed; Ryouko, Sayaka and Megumi are the ones who ask about his mental state.
- **Do/don't:**
  - She echoes Lanhua. L5885 = L6361 is verbatim: translate it identically. L5871 「お願いです‥‥思い出して下さい。」 is a near-echo of L2681 「思い出して下さい。」: keep the English parallel.
  - Don't romanticise the DREAM_02 coercion or harden it (TL guide §5.9).

### Megumi (恵)
- **Who:** Kouji's girlfriend (L1204, L7766). She is boyish, "digital" but single-minded, jealous and bossy-affectionate.
  - Her "Papa" is the centre's sponsor and a politician (L1715, L10449). Kouji once jabs that he isn't her blood father (L1611).
  - She introduced the job and regrets it (L3277–L3281). She is the only one who tells Kouji the truth plainly: get out; "your head is a mess"; the mind-control machine. The plot keeps silencing her: abducted mid-call, tied up twice.
- **Speech:** drawn-out vowels (もぉ, なによぉ, ちょっとぉ); バカ/ばーか; あなたねぇ; お父様/パパ; threats like ぶつわよ.
- **Do/don't:** "Idiot!" is her signature, and replies depend on it (L6898, L6906).

### Sayaka (沙夜香)
- **Who:**
  - DREAM_01: nominally Ryouko's assistant, in truth an **undercover investigator (内偵) for the Legal Affairs Bureau** sent via Megumi's father (L7052–L7058). Pro-military. She rescues Kouji at gunpoint, and her bullet damaged his memory (L7198).
  - DREAM_02: a "government-related" operative who wants to seize the research to rise in the Bureau (L10645). She is the bondage and sadist figure of that route, then humiliated, then a servile harem member.
- **Speech:** あなた, 〜じゃない/〜わよ/かしら, フフ, ホーッホッホ. She teases him as **ボク** ("little boy", L1815, L5047) and calls herself お姉さん; ironic 「涼子センセ」. Idiom 「野暮は聞きっこなし」 (L5153), echoed by Tracy as 「野暮は聞きっこなしですね」 (L6604); the current English differs, which is an open item.
- **Do/don't:** keep the "little boy" sting; 内偵 is not "PI" (L7058).

### Far / Farneria (ファル / ファルネリア)
- **One person:** 「超心理学者のファル、ファルネリア」 (L2202). The speaker tag is always ［ファル］. Red crystalline hair, a receiver-like ornament over one ear (L2052–L2054).
- **She is a telepath who reads emotions as colours**: yellow for gratitude, "clear green" for sincerity, "the colour of panic" (L2106, L13034, L6515).
  - DREAM_01: captain and "Professor" of the 3rd team, rational and suspicious. Ready with her gun, though the safety was on (L6748). She forgets Kohji at the end (L6800–L6802).
  - DREAM_02/03: a kidnapped professor of the Royal Science and Technology Bureau (王立科学技術局, L14017). She invented the encoder for psychiatric treatment. Hostile → trusting → lover; heroine of ending α.
  - DREAM_04: the dig's chatty professor and Kohji's next target.
- **Speech:** 〜わ/〜よ/〜のね/頂戴; フン; teacherly 「いいわ、聞きなさい」; calls herself お姉さん when teasing (L13753). Quick to slap (バキッ). Calls him コウジさん, then コウジ.
- **Mirror:** in reality, Ryouko is the other scientist whose therapeutic machine was misused, and Mai the other catalyst *(the parallel is structural, not stated)*.
- **Do/don't:** the JP says only "the colour of X". Don't invent hues (TL guide §10).

### Tracy (トレイシー)
- **Who:** blonde, small glasses, a large bust (a running gag), and a natural, kind smile (L2305).
  - DREAM_01: Far's assistant with nursing qualifications; clumsy (a pratfall in both branches); gives the parapsychology lecture.
  - DREAM_02/03: Kohji's "secretary, chief of staff and slave". Literal-minded, recites spec sheets, has the Elath wound. She knew early that Kouji wasn't Kohji: 「同一人物だとしても、ちっとも驚きません」 (L14789).
  - DREAM_04: POV. The chaste secretary, conditioned into devotion. Her tears show through.
- **Speech:** refined ojou-sama keigo: 〜ですわ/〜ますわ/〜ですの; ウフフ, ホホホ; "コウジ様"; spaced 「ス・テ・キ」; once ワタクシ. DREAM_04 is mostly her parenthesised inner monologue.
- **Do/don't:** keep her gracious and formal ("I shall take my leave"). Her catchphrase 冗談とイカの頭 needs one fixed rendering.

### Lanhua (ランファ)
- **Who:**
  - DREAM_01: Far's "capable assistant", cold and evasive, appearing and vanishing. In truth **Mai's thought body**: 「ランファ…『舞う』という意味」 (L7208).
  - DREAM_02/03: the Quadra's engineer and mecha fanatic, childlike, the B-track heroine. Her first time was with Kohji at Elath, and he forbade her to say "I love you" (L16165, L16205).
  - DREAM_04: Tracy's seduced recruit.
- **Speech:**
  - DREAM_01: polite, minimal, many silences; コウジさん (hiragana さん, versus Mai's katakana サン).
  - DREAM_03: refers to herself in the third person as 「ランファ」; 〜ですぅ/〜モン; プリプリ (huffy), ウルウル (teary); baby talk 「なのれーす」; puns (「しろみ！なんちってぇ」, L13287); old slang 「あたりきしゃりき」; 「愛の結晶」 for the ship.
- **Do/don't:** keep her register. The current EN flattens it. Her one 宏治様 in kanji (L15901) must stay "Kouji-sama".

### Bianca (ビアンカ)
- **Who:**
  - DREAM_01: the 2nd-expedition leader's daughter. An escape-pod survivor who accuses Kohji of wiping out the team (L4381, L5665).
  - DREAM_03: a rival "pirate" with an electric whip; Kohji's former partner. Their histories conflict: hers says he abandoned her (L14527), Tracy's says she betrayed him (L15243). She dominates the captive Far, melts when kissed, and is the heroine of ending δ, where she reveals she was only chasing Kohji (L17335–L17345).
  - DREAM_04: the friendly pilot.
- **Speech changes with power.** Keep the registers distinct:
  1. Villain: 我こそは, 貴様, 〜だ/〜ぞ, フハハ.
  2. Rough feminine with Far: アタシ, 〜じゃない, 〜だい.
  3. Street slang: 「ざけんなっ！」.
  4. Soft feminine once she melts: 「あげるわ、そんなの」, 「私も、私もよっ！」.
  5. Tomboy pilot in DREAM_04: 〜さ, 〜だぜ.

### Minor voices
- **The captain of the 2nd expedition** (Bianca's father): his handwritten log (L4365–L4399) and the MD footage.
- **The waking voice:** 「コウジさん／宏治さん、目覚める時間ですよ」 (L661/L4551). Untagged; keep it so.
- **［男］ / ［男の声］:** the pirate in Kouji's body, heard from DREAM_03. Kouji must not recognise his own voice.
- **《…》 intrusions:** Kohji's telepathic voice.

---

## 6. Emotional design: what the game makes you feel, and how

### 6.1 Unease and unreality (the research centre)
- **Devices:**
  - Empty corridors "like a school after hours" (L1317); hot corridors in winter with cool rooms (L1313, L2777).
  - Inorganic spaces with token potted plants; a high whine that isn't tinnitus; footsteps that approach and are swallowed by doors.
  - Thick automatic doors; an anachronistic red phone on the rooftop (L3413).
  - The centre empties day by day: no receptionist; Mai has "never seen it crowded" (L5931). Day and night swap without warning (L5207).
- **Preserve:** the plain, slightly clinical narration and the small wrong details. Don't explain them.

### 6.2 Déjà vu and loops
- **Devices:**
  - The Ryouko session replays in full three times (S0102, S0105, S0107), with further partial replays in S0107 and S0108, and Kouji counts them (L671 → L2699 → L5229).
  - Whole sentences recur across worlds. The fade-out line L1855 is echoed in the dream by L1872, in the present tense; L1275 recurs; the capsule block L469–L603 appears in three routes.
  - Mirror architecture: hot corridors in both worlds; the computer room = the encoder room (both with music `xenon_13`); the "boiler room" quiz decoy = the engine room.
- **Preserve:** identical Japanese becomes identical English, always. These repetitions are the horror.

### 6.3 Identity doubt
- **Devices:**
  - A net over his name (L447); the nameplate; the tag switch 私 → コウジ.
  - Forgotten names (Megumi, Mai); "a body not my own" (L1353); "Who am I?" (L5711–L5729).
  - The pronoun switch 私/僕 at the moment the worlds merge (L6876).
  - The Kouji/Kohji homophone: "I'm Kouji, not Kohji" can't be heard, only written (L13072).
- **Preserve:** the speaker tags, the unnamed speakers, and the ambiguity of who "I" is.

### 6.4 The dream/reality inversion (Zhuangzi)
- **Devices:**
  - The opening hypnosis calls the session world "reality… a mirror of your heart".
  - Mai's butterfly (L3525–L3543), echoed by Ryouko's "for example, butterflies" (L5299).
  - Tracy's "no way to tell dream from reality" (L5433–L5441).
  - Kohji's acceptance: 「誰の夢かはわからないが、やるだけやらしてもらうさ」 (L6666).
  - DREAM_01's ending literalises it: the "reality" is the dream.
- **Preserve:** every hedge. Never add certainty, and keep the butterfly line exact.

### 6.5 Desire as the trigger for crossing worlds (DREAM_01)
- **Devices:** every erotic approach in the first half is interrupted, or collapses into vertigo and a world switch (L593, L1687, L1849). The protagonist is passive: women initiate, lead and hold the knowledge.
- Ryouko's Freudian commentary (wish fulfilment, libido) reads as the game commenting on its own genre.
- **Preserve:** the abruptness of the cuts and the euphemism of 「アレ」 (L1283, L1775). Don't make it explicit.

### 6.6 The two women who become one (DREAM_01)
- Mai and Lanhua share lines, gestures (closing the eyes, clasping hands) and the blow from behind.
- Their back-to-back "consolation" scenes are bridged by a Mai picture layer at the start of Lanhua's, and by the lines "I overlaid another woman on Mai" / "Lanhua overlapped with a woman I'd met before" (L6091, L6281). This is the visual match-cut the outside review describes, placed just before the worlds merge.
- The name key "Lanhua means 'to dance'" (L7208) is invisible to English readers; see the guide's open items.

### 6.7 Elegy (the end of DREAM_01)
- A quiet coda in untagged 「」 dialogue between two machine-minds (L7242–L7274): radioactivity near zero, 2,000 years of battery, "Adam and Eve", humanity's only seed in a test tube.
- **Preserve:** the stillness and the lack of tags. Don't add exposition.

### 6.8 Parody and power fantasy (DREAM_02), and its discomfort
- **Devices:**
  - Grandiose speech constantly undercut by slapstick (doors, kicks, sneezes, the cigarette).
  - Running gags: "fry 20–30 thousand synapses", "theoretically perfect", the generator's absurd name nobody can remember.
  - Comic music (`xenon_25`).
  - The rakugo trick at the climax.
- **The conquests escalate in coercion and are played for comedy. The text keeps its own distance:** 「私はそんな事は命令していない」 (L8974); 「こればかりは本当のようだった」 (L9924); and "Kouji-sama", not "Kohji-sama", in every submission line.
- **Preserve:** both the comedy and the distancing cues. Neither soften nor amplify.

### 6.9 Warmth, harem comedy and sudden metaphysics (DREAM_03)
- **Devices:**
  - The crew's devotion; bickering rivals; running gags (「雑音は消して」, 「不可抗力」, 「愛の結晶」, Far's slaps).
  - Kouji's decency winning trust: he frees Far, and Tracy knows he isn't Kohji.
  - Abrupt swings from slapstick to limbo metaphysics to tender confession.
- **Irony:** the "gentleman" choice leads to Far; the pirate's password leads to Bianca.
- **Preserve:** each gag's fixed phrasing, the tonal whiplash, and the 《》 intrusions.

### 6.10 Claustrophobic melancholy (DREAM_04)
- **Devices:**
  - Almost all inner monologue; no choices; no comic relief.
  - "Kindness" alternating with humiliation; tears she cannot explain.
  - The corridor of the prologue (往来の廊下) becomes a place of exposure.
  - The ending label ∞.
- **Preserve:** her narration's self-deception and the cracks in it (L19413–L19415, L18983).

### 6.11 Payoffs for playing several routes
- DREAM_02 and DREAM_03 show the same scenes from opposite sides (§3.2). DREAM_04 explains the crew's servility in DREAM_01–03.
- Recurring props: the capsule girl, the generator's name, "Elath", the synapse joke, 冗談とイカの頭.
- **Preserve:** cross-route consistency of every shared line and term.

### 6.12 Music cues (names only; useful for spotting scene types)

| Track | Typical use |
|---|---|
| xenon_01 | title; DREAM_01 reveal |
| xenon_12 | Ryouko's office; the clinic |
| xenon_13 | the capsule/encoder room and the reality computer room (the shared machine theme) |
| xenon_17 | eerie dream exploration, limbo, flashbacks |
| xenon_03 | Mai's scenes, reused after Lanhua's (a musical clue) |
| xenon_16 | revelations (Bianca's accusation, "they're shouting my name", Tracy's identity) |
| xenon_25 | the pirate's comic theme; Bianca's entrances |
| xenon_24 / 23 / 22 / 11 | erotic scene / seduction / romance / afterglow |
| xenon_15 | disasters and attacks |
| xenon_06 | the DREAM_02/03 harem epilogues |

---

## 7. Context and critical reading

**Outside source:** "Xenon: Mugen no Shitai and the Erasure of Self", *Ogre Run*, 25 March 2026, <https://ogrerun.com/2026/03/25/xenon-mugen-no-shitai-and-the-erasure-of-self/>. It is paraphrased here; read the original for its argument. The reviewer's points:

- **Authorship and lineage:** *Xenon* is written by Hiroyuki Kanno at C's Ware. It sits between *Desire* (1994) and *EVE Burst Error* (1995), on the way to *YU-NO* (1996), and was made under very short deadlines.
- **The core theme:** *Xenon* uses the erotic genre as a vehicle for the fragmenting and erasure of the self. The reviewer reads it as an "arthouse eroge" and compares it to Chris Marker's *La Jetée* and Resnais's *Je t'aime, je t'aime*.
- **Route 1 (DREAM_01)** is the high point and most serious: a passive protagonist who can no longer tell the two worlds apart, peaking in a match-cut between two sex scenes.
- **Route 2 (DREAM_02)** inverts the passive late-80s/90s otaku protagonist into a pulp power-fantasy hero, at once parody and wish fulfilment. Its erotic scenes become more violent.
- **The tension between the two routes:** the reviewer links it to post-bubble anxieties about masculinity, and to the devoted, caring woman (Mai) as the "salvation" figure.
- **Routes 3 and 4** explore dominating and being dominated. Route 4 is told from Tracy's point of view.
- **"Pornographic brilliance":** Kanno's strength is in the circumstances around sex rather than the act. The review's prime example is the S0108 scene where an erotic set-up turns into a parapsychology lecture.

**What the script confirms** (checked during this analysis):

| Claim | Verdict |
|---|---|
| Zhuangzi's butterfly dream is cited; dream/reality is central to DREAM_01 | **Confirmed** (L3525–L3543, callbacks L4983, L5299; the ending literalises it) |
| DREAM_01 is a serious mystery with a passive protagonist; two sex scenes are match-cut | **Mostly confirmed.** The bridge (§6.6) comes just *before* the merge, not during it |
| DREAM_02: the pirate's mind in Kouji's body, a parody power fantasy with more aggressive sex | **Confirmed.** His power is constantly undercut by slapstick |
| DREAM_03 is set in the dream world and has several endings | **Confirmed**: four (§2.3) |
| DREAM_04 is Tracy's POV with little plot, titled 「肉奴隷：トレイシーの憂鬱」 | **Partly.** Tracy's POV is confirmed, but it has a real prequel plot. The title is not in the script text and may be on a title graphic |
| Megumi is the girlfriend, her father on the board; she got him the job; he did it for money | **Mostly.** Her father is the centre's *sponsor* and a politician; "board" is not stated |
| Mai is the devoted, caring figure | **Partly.** True in DREAM_02 and late DREAM_01; early DREAM_01 Mai is enigmatic, and she is also a co-conspirator and the catalyst |
| Bianca is a dominatrix with a whip | **DREAM_03 yes** (electric whip, dominates Far). DREAM_02: only a whip flash; there Sayaka is the bondage figure. DREAM_04: friendly pilot |
| Tracy's S0108 lecture subverts an erotic set-up | **Partly.** The set-up is mild (re-crossing her legs at his bedside, L5341); the turn is pure philosophy |

Alternate romanizations used in English writing: Kōji, Ryoko, Ranpha, Fal. The project locks Kouji/Kohji, Ryouko, Lanhua, Far.

---

## 8. Motifs and recurring elements (quick reference)
- **Places and things:**
  - Corridors, doors, capsules (棺桶, "coffin", in the Japanese, rendered "capsule"; "like a gravestone", L14463), stars through a porthole.
  - Telephones and intercoms as channels of warning.
  - The micro disc, which crosses the dream boundary, vanishes and returns.
  - Two notebooks with a slip tucked between the pages: the captain's log with the MD (L4393), and Ryouko's notebook with the Bureau memo (L5787).
- **Bodies and actions:** a blow to the back of the head; closing the eyes and clasping the hands; colour-reading; heat and sweat versus cold.
- **Mind words:** 思念 / 思念体 (thought / thought body), 投影 (projection), シンクロ (sync), アストラル界 (astral plane), 既視感覚 (déjà vu, the game's rarer form), 触媒 (catalyst).
- **Numbers that must stay exact:**
  - 6,012 years; 2,000 years of battery; "ten months".
  - 18,000 s = 5 h; a 3-second sync; 15:00 → 20:00; five years since Bianca's betrayal.
  - Sayaka 85-61-86; Far 88-59-88.
  - "20–30 thousand synapses".
- **Structural markers:** single full-width letters on their own lines (Ｂ L1, Ｃ L6804, Ｅ L7130, Ｄ L9524), probably engine markers; don't translate them. Ending cards: ＤＲＥＡＭ＿１/２/３α–δ/∞ ‥‥ ＥＮＤ.
