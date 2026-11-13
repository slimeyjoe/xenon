# Xenon: przewodnik polskiego tłumacza

Ten dokument jest dla każdego, kto tłumaczy lub redaguje polską wersję *Xenon ～夢幻の肢体～*: dla ludzi i dla agentów.

Opiera się na angielskim [TRANSLATION_GUIDE_EN.md](TRANSLATION_GUIDE_EN.md) oraz [STORY_BIBLE.md](STORY_BIBLE.md). Z angielskiego przewodnika obowiązują w całości:
- §5, zasady wierności;
- §6, czego nie wolno spłaszczyć;
- §3, pułapki potoku: martwe klucze, duplikaty, wspólne linie, dzielone napisy.

Tutaj są zasady **dodatkowe** albo **inne** dla polszczyzny. Przy sprzeczności wygrywa ten dokument.

**Odwołania:** Lnnnn to numer linii klucza `//@` w `translation/script_en.txt`; japoński tekst tej linii pokazuje `view/script_en.txt` (`make text-view`, EN §2). Polska wersja ma te same numery linii (§1).

**Status:** Faza 0 zakończona 2026-11-09. Wszystkie pozycje są zatwierdzone przez redaktora, chyba że oznaczono je **[propozycja]**.

---

## 0. Decyzje redaktora (2026-11-09)

| Sprawa | Decyzja |
|---|---|
| Źródło | Tłumaczymy **z japońskiego**. Angielski, STORY_BIBLE i przewodnik EN służą tylko do kontroli. |
| Imiona | Hepburn jak w EN: Kouji / Kohji, Ryouko, Sayaka, Mai, Megumi, Tracy, Lanhua, Far / Farneria, Bianca. |
| Honoryfikatywy | **Nie zostawiamy** -san / -kun / -sama / -sensei. Formy adresatywne oddajemy po polsku (§4). |
| 触媒 | **medium**, także 触媒の女 = „medium”, nigdy „kobieta-medium” ani „katalizator”. |
| 精神感応者 | telepata / telepatka. Nigdy „medium”, bo to słowo jest zajęte przez 触媒. |
| Śmiechy | Po polsku (§8). Bez Fufu / Ufufu / Kukuku. |
| プリプリ (Lanhua) | „Mam focha!” (§8) |
| Wymuszone 様 w DREAM_02 | „szanowny pan” + czasowniki w 3. osobie (§4.3) |
| Tracy → Kohji | „mój panie”, we wszystkich trasach (§4.3) |
| Motto L7 | Z łaciny, bo japoński jest luźnym przekładem łaciny. Używamy opublikowanego przekładu **Franciszka Wężyka** (1882), dla XIX-wiecznej patyny: „A gdy niebios nie zbłagam, wzruszyć zdołam piekła...” (źródło w `notes_pl.tsv`). |
| Silnik i glify | Poza zakresem tłumaczenia. Port ma obsługę UTF-8 i polskie glify (§2, `source/lang/README.md`). |
| 既視感覚 | **déjà vu**, z akcentami (port rysuje é i à) |
| Tytuł generatora | „Generator Wzmacniania Energii Duchowej z Atestem Galaktycznych Piratów” (§6) |
| ＤＲＥＡＭ | Kwestie ramujące tłumaczymy dosłownie za japońskim (§6) |
| Filozof (L3525) | „Zhuangzi”, nie „Czuang-tsy”, bo tradycyjnej formy nikt dziś nie rozpozna |

---

## 1. Pliki

- `translation/script_pl.txt` to polski skrypt w UTF-8, **wyrównany liniami** z `script_en.txt`:
  - te same klucze `//@` w tych samych liniach (klucz wskazuje japoński tekst w plikach gry, EN §2; podgląd z japońskim: `view/script_pl.txt`);
  - wszystkie linie `#` zostają;
  - zmieniają się wyłącznie linie z tłumaczeniem.
  
  Dzięki temu każde Lnnnn z dokumentacji EN i biblii fabuły jest ważne także dla PL.
- **Nie dodajemy linii.** Notatki tłumacza idą do `translation/notes_pl.tsv` w formacie `linia<TAB>notatka`.
- Mieszane końcówki CRLF/LF: edytujemy bajtowo (`tools/set_line.py`, EN §1). Nigdy edytorem, który normalizuje końce linii.
- Narzędzia uruchamiamy z katalogu `text/`. Grę budujemy i sprawdzamy jak w EN §1 (kroki 7–9), z `build/pl/` w miejsce `build/en/` i `--lang pl`.
- Próbki kalibracyjne i robocze materiały leżą w `sandbox/claude-notes/pl/` (poza gitem).
- **Co znaczy „przetłumaczone”:** szkielet powstał jako bajtowa kopia pliku EN.
  - Linia różna od EN = przetłumaczona.
  - Linia identyczna z EN, ale bez liter albo równa kluczowi (kody, `‥‥`) = zostaje bez zmian.
  - Reszta = do zrobienia. Plik zawsze daje się zbudować.

**Narzędzia** (wszystkie z `--lang pl`):
- `python3 tools/fitcheck.py --lang pl --pad "LINIA"`: dopasowanie do okna.
- `python3 tools/set_line.py --lang pl --pad L "TEKST"`: jedna linia.
- `python3 tools/set_line.py --lang pl --pad --tsv plik.tsv [--dry-run]`: wiele linii naraz (`L<TAB>tekst`), wszystko albo nic.
- `python3 tools/lint_translation.py --lang pl [--all]`: postęp, błędy znaków i długości, duplikaty (ostatnia kopia wygrywa!), pozostawione honoryfikatywy i śmiechy, zakazane terminy, formy rodzajowe w strefach neutralnych.
- `python3 tools/lint_translation.py --lang pl --line L`: czy wpis jest żywy, duplikatem czy martwym kluczem, i w których skryptach występuje.

---

## 2. Znaki i długości

**Znaki** (tak rysuje je port; kontrakt z silnikiem jest spełniony):
- Dozwolone są ASCII, `ąćęłńóśźżĄĆĘŁŃÓŚŹŻ` oraz `é à É À` (dla „déjà vu”). **Każda z tych liter zajmuje 1 bajt**, czyli jedną kolumnę, jak ASCII. Port czyta tekst w UTF-8 i ma te litery w foncie (`source/fonts/latin8-pl.txt`).
- Tabelę znaków i szerokości trzyma jedno miejsce: `tools/charset_pl.py`. Czyta ją także build portu (`source/tools/langs.py`), więc nasze pomiary i silnik zgadzają się z definicji.
- **Niedozwolone:** „ ” (stosujemy prosty `"`), – i — (stosujemy `-` albo `...`), … (stosujemy `...`) i obce litery spoza tabeli (§11).
- Pełnoszerokie znaki japońskie (`‥`, `◆`, `　`, `ＤＲＥＡＭ`) wolno zostawić tam, gdzie stoją w EN. Liczą się jako 2 bajty.

**Okno tekstu** (EN §4, zweryfikowane w AGS.EXE):
- wiersz ma 60 bajtów;
- strona ma 4 wiersze, czyli **≤ 240 bajtów na linię**;
- opcja menu ma **≤ 60 bajtów** (z `◆ `);
- oryginalny silnik łamał wiersz w środku słowa, dlatego plik ma układ wyrównany spacjami (`fitcheck.py --lang pl --pad`); port łamie wiersze na granicy słów i sam usuwa to wyrównanie (EN §4, „In the port”), więc o zmieszczeniu się linii decydują 4 wiersze strony i jeden wiersz opcji menu. Gdzie łamanie portu daje inną liczbę wierszy, mówi to `build/pl/lint.txt`.

Polszczyzna jest dłuższa od angielskiego o mniej więcej 10–20%, ale japońskie linie nie przekraczają 175 bajtów, więc miejsca zwykle starcza. Wcięcie na początku linii przejmujemy z linii EN.

---

## 3. Płeć gramatyczna: co japoński ukrywa, tego polski nie zdradza

Polski czas przeszły i przymiotniki mają rodzaj („obudziłem / obudziłam”, „sam / sama”). Japoński go nie ma.

Analiza całego skryptu z 2026-11-09 pokazuje, że płeć jest ukryta **tylko w jednym miejscu**. Wszystkie inne zagadki tożsamości dzieją się w obrębie jednej płci: Kouji / Kohji, ［私］ w DREAM_01–03, ［男］, ［男の声］, Mai / Lanhua, Far / Farneria. Tam rodzaj jest bezpieczny, byle nie zdradzić imienia.

### 3.1 Prolog: sen L90–L345 musi być NEUTRALNY
- Wybór „wyjdź z pokoju” (F64) w S00C prowadzi przez ten sam sen i ten sam korytarz prosto do **S0401**, gdzie śniącą jest **Tracy**.
- Japoński wariant korytarza zmienia tylko trzy końcówki だ / な, tak żeby nie brzmiały męsko:
  - L313 → L355;
  - L319 → L357;
  - L345 → L359.
- Pozostałe klucze są wspólne z trasami męskimi.

**Techniki:**
- podmiot nieosobowy: „Śnił mi się dziwny sen”, „dotarło do mnie”, „owionęło mnie”, „coś mi się stało”;
- konstrukcje z celownikiem i bezosobowe: „wydawało mi się”, „słychać”;
- fraza nominalna: „Przy próbie wstania z łóżka…”;
- czas teraźniejszy: „Odrywam przyssawki”.

Japoński sam miesza czasy (L134 kończy się na 消す), więc w tym fragmencie czas teraźniejszy jest uprawniony.

**Zakazane w tym fragmencie:** „miałem / miałam”, „zauważyłem”, „nagi / naga” (piszemy „nagość”), „sam / sama”, „chory” w formie orzecznika.

Męski posmak zostaje tylko w doborze słów: L112 「な」, L132 「じゃないか」.

### 3.2 Otwarcie DREAM_04 (L17541–L17549)
- Tracy jest ［私］ i jeszcze nie padło jej imię.
- **Pułapką są kwestie Far do niej**:
  - L17543 夢遊病者みたい: „Chodzisz jak we śnie”, nie „lunatyczka”;
  - L17547: „Stres?”, nie „Zestresowana?”;
  - L17571: rozkaźnik albo rzeczownik, nie „żebyś nie zdradziła”.
- Rodzaj żeński jest dozwolony od **L17551** (あなたの彼, „twój chłopak”) i L17573 (〜わ).

### 3.3 Głosy bez właściciela
- **Głos hipnotyczny (L13–L34) i głos budzący (L661, L4551):** tylko formy bez rodzaju, czyli „obudzisz się”, „policzę”, „wrócisz”. Nigdy „czekałam” ani „będziesz widział”.
- **《何たる無様な醜態だ》 (L13621):** fraza nominalna („Cóż za żałosne widowisko”), identyczna z L7896.
- **Te same słowa, różni rozmówcy:** te linie tłumaczymy osobno.
  - L5671 (Kohji do Bianki) i L16547 (Bianca do Koujiego), i、今なんて: „Co powiedziałaś” / „Co powiedziałeś”, albo neutralnie „Co ty mówisz?”;
  - 「私は‥‥」 w L2671, L14601 i L17575: wszędzie „Ja...”.
- **L2036 ［私］：‥‥‥‥‥‥。** występuje i w S0104 (Kohji), i w S0401 (Tracy). Tag „[Ja]” jest neutralny.

---

## 4. Formy adresatywne

Polskie „ty” kontra „pan / pani” niesie japońską grzeczność (keigo) lepiej niż angielski. Zasada ogólna:

- japoński plain (zwykły styl) → **ty**;
- japońskie です / ます, 様 lub 先生 → **pan / pani** (czasownik w 3. osobie);
- **zmianę rejestru** robimy tam, gdzie robi ją japoński, i musi ona być słyszalna.

### 4.1 Odpowiedniki honoryfikatywów

| JP | PL | Przykład |
|---|---|---|
| 〜さん (do obcych / grzecznie) | pan / pani + imię | „pani Mai”, „panie Kohji”, „pani Bianca” |
| 〜クン / 〜くん | znika; zostaje samo imię | 宏治クン = „Kouji” |
| 涼子先生 | pani doktor | „To przecież pani doktor mówiła…” |
| プロフェッサ | pani profesor | „asystować pani profesor” |
| ファルネリア様 | pani Farneria | „ratunek pani Farnerii” |
| コウジ様 (Lanhua, reszta załogi) | pan Kohji / panie Kohji | „niech pan uważa”, „dla pana Kohjiego” |
| コウジ様 (**Tracy**) | **mój pan / mój panie** | „nie jest pan moim prawdziwym panem” |
| 宏治様 wymuszone w DREAM_02 | **szanowny pan** + 3. osoba | „co szanowny pan zamierza?” |
| お姉さん (Mai → Ryouko) | siostro | „Tak, siostro...” |
| ボク (Sayaka drażni Koujiego) | chłopczyku | „czym się tak denerwujesz, chłopczyku?” |

Wołacz imion obcych = mianownik („Lanhua!”, „Far!”). Po „pan / pani” imię się odmienia (§5).

### 4.2 Macierz: kto do kogo

Źródło: analiza całego skryptu z cytatami. Pełne zestawienie jest w notatkach Fazy 0.

**Prolog i ośrodek (DREAM_01):**
- Ryouko → Kouji: **ty**.
- Kouji → Ryouko: **pani doktor** (zawsze keigo).
- Sayaka → Kouji: **ty** („chłopczyku”).
- **Kouji → Sayaka: linia po linii** za japońskim. Keigo daje „pani”, plain daje „ty”, z przeskokiem w tej samej scenie (L1743 „Niech pani…”, L1805 „O czym ty mówisz?”).
- L2949: Sayaka mówi gołe 宏治 przy Mai (zaznacza teren). Kompensujemy to słowem „kochanie”.
- Mai → Kouji: **pan** („panie Kouji”).
  - Uwaga narratora o jej dziwnym 「サン」 (L1417) mówi teraz o dziwnie brzmiącym „panie”.
  - W scenie intymnej (L6071) jest samo „Kouji”.
  - W pożegnaniu formalny wołacz przechodzi w „ty”: L7108 „Panie Kouji... kochałam cię...”.
  - L7116 „Proszę zamknąć oczy...”: znów keigo.
- Kouji → Mai: **pani Mai**.
  - Prośba z L5989 brzmi więc „Proszę nie mówić do mnie »pani«”.
  - W scenie intymnej przechodzi na **ty** (gołe 舞).
  - W demaskacji wraca do „pani” (L7020, L7028). Ten powrót jest celowy.
- Kouji ↔ Megumi: **ty** w obie strony.
- Megumi → Sayaka: **pani**; Sayaka → Megumi: **pani** (urzędowo).
- Ryouko → Sayaka: chłodne **pani**, od L7072 pogardliwe **ty**.
- Sayaka → Ryouko: **pani doktor**, za plecami ironicznie. Od L7070 gołe „Ryouko” i **ty**.
- Ryouko → Mai: **ty**; Mai → Ryouko: **siostro**.

**Statek (DREAM_01):**
- Kohji → załoga: **ty**. Obcy na początku dostaje „pani” (L2116 „Pani Far... neria”, L2369 „pani Tracy?”), potem samo imię.
- Far → Kohji: „panie Kohji” przy przedstawieniu (L2048), od L2138 **ty**.
- Tracy → Kohji: **pan** („panie Kohji”).
- Lanhua → Kohji: **pan**.
- Tracy / Lanhua → Far: **pani profesor**. Far → Tracy / Lanhua: **ty**.
- Far → Bianca: **pani** (kapitan do rozbitka, L5685).
- Bianca → Kohji: obcesowe **ty**.

**DREAM_02:**
- Pirat → wszyscy: **ty**, wyniośle (貴様 = „ty”, w razie potrzeby „nędzniku” albo „głupcze”).
- Ryouko → pirat: **ty**, aż do rozkazu L8726. Od L8746 **szanowny pan**, z wpadkami w plain przy emocjach (L9180).
- Megumi → pirat: **ty**. Zgrywa się w „szanownego pana” w L10411–L10457; prawdziwa zmiana następuje w L11188.
- Sayaka → pirat: chłodne **ty**. Uległe „szanowny pan” dopiero od L11166.
- Mai → pirat: **pan**. W finale haremowym (L11190) **szanowny pan**.
- Kohji ↔ Kouji (telepatia): **ty** w obie strony.

**DREAM_03:**
- Kouji → załoga: **ty**.
- Tracy → Kouji:
  - **mój panie**, w 3. osobie „mój pan”;
  - kontrast あなた (Kouji) / コウジ様 (pirat) w L14777–L14853 to „pan” / „mój pan”;
  - intymne あなた w L14829 to nagłe **ty**.
- Lanhua → Kouji: **panie Kohji**.
- Tracy ↔ Lanhua: **ty** w obie strony.
- Tracy, Lanhua → Far: **pani Farneria**.
- Wszyscy → Bianca: **pani Bianca**.
- Far → Kouji: **ty**. Zwrot w L13627–L13633 widać tylko w tonie.
- Bianca → Kouji: złoczyńca z **ty** („ty nędzniku”), po przemianie (L16495) miękkie **ty**.
- Gdy Far albo Bianca mówią do niego コウジ zamiast „ty” (コウジの言うこと, この胸も‥‥みんなコウジのモノ), po polsku jest **ty** z wołaczem „Kohji” (L14237, L17303). 3. osoba zostaje tylko tam, gdzie mowa o Kohjim jako kimś innym (L14055, L14085) albo w docinkach (L13851).

**DREAM_04:**
- Kohji → Tracy: **ty**, przy czym miękkie 君 i szorstkie お前 różnią się słownictwem, nie formą.
- Tracy → Kohji: w izbie chorych コウジさん = „panie Kohji” (L18081); w retrospekcji miłosnej „panie Kohji” jako wołacz przy **ty** (L18233, jak L7108) albo czułe „pan Kohji” w 3. osobie (L18131). Gołe „Kohji” i **ty** od L18257 (krok słyszalny), powrót do „panie Kohji” w L18551 za JP. Od L18651 **mój panie**. To źródło jej formy w DREAM_01–03.
- Lanhua → Kohji: od L19269 「はーい、ご主人様」, na polecenie Tracy (L19267 「ご主人様と言いなさい」).
  - ご主人様 (pan jako właściciel) to coś innego niż コウジ様: gołe **„panie”**. L19267 „Mów do niego: panie”, L19269 „Taaak, panie”.
  - U Megumi w D2 (L10307, L10335) komiczne „panie i władco”.
- Far → Tracy, Bianca → Tracy: **ty**.

### 4.3 Trzy rodzaje „pana”

Formy nie mogą się mieszać:
1. **„panie Kohji” / „pani Mai”**: zwykła grzeczność (〜さん, 〜様 załogi).
2. **„mój panie”**: oddanie Tracy, poważne. Rodzi się w D4 (L18651, L18661 „Jestem twoją niewolnicą, mój panie...”).
3. **„szanowny pan”**: wymuszona i komiczna uległość w DREAM_02 (Ryouko, Megumi, finał haremowy). Ma w sobie ironię, dlatego nie trafia do D4.

---

## 5. Imiona: odmiana

| Imię | Odmiana |
|---|---|
| Kouji, Kohji | jak przymiotnik: Koujiego, Koujiemu, Koujim (N., Ms.); wołacz Kouji. Po „pan” też: „z panem Koujim”, „dla pana Kohjiego”. |
| Ryouko, Mai, Megumi, Tracy | nieodmienne |
| **Far** | **nieodmienne**. Nigdy „Fara” ani „Farowi”, bo kobiece imię na spółgłoskę się nie odmienia, a odmiana brzmiałaby męsko. |
| Farneria | Farnerii, Farnerię, Farnerią |
| Sayaka | Sayaki, Sayace, Sayakę, Sayaką |
| Lanhua | Lanhui, Lanhuę, Lanhuą (wzór: Papua, Papui) |
| Bianca | według Poradni PWN (A. Wolański, 2020; K. Kłosińska, 2021) odmienia się jak *Bianka*, a „c” zmienia się na „k” tylko w dopełniaczu: D. **Bianki**, C. **Biance**, B. **Biancę**, N. **Biancą**, Ms. **Biance**, W. *Bianco* (w dialogu wołacz zostaje „Bianca!”, §4.1). |

**Rozdział Kouji / Kohji** (EN §6) dotyczy tylko pisowni. Bohaterowie **nie słyszą** różnicy.
- Wszystkie formy odmienione też zachowują rozdział: „Koujiego” to student, „Kohjiego” to pirat.
- Tagi idą za umysłem, nie za ciałem: tak jak w EN §6.

---

## 6. Słownik terminów

| JP | PL | Status |
|---|---|---|
| 触媒 / 触媒の女 | medium | **zablokowane** |
| 精神感応者 | telepata / telepatka | **zablokowane** |
| 棺桶 (enkoder) | kapsuła | **zablokowane** (2026-11-09). Gdy japoński żartuje z samego słowa (D3 L14011–L14013 棺桶とは言ってくれるわね), urządzenie zostaje „kapsułą”, a „trumna” pojawia się tylko jako porównanie: „leżałaś jak w trumnie?”, „Trumna? No wiesz!” (decyzja 2026-11-10). |
| エーテル光子動力 | energia eterowo-fotonowa | **zablokowane** (2026-11-10). Nie „eteryczna”. |
| ＤＲＥＡＭ | ＤＲＥＡＭ (pełnoszerokie, jak na ekranie) | **zablokowane**. Kwestie ramujące tłumaczymy **dosłownie za japońskim**, bo po polsku to naprawdę obce słowo: L274 „Gdyby brać to dosłownie, znaczyłoby po prostu »sen«...”, L284 „Wygląda jak alfabet... nie, to coś innego. To jest...”, L857 to echo L274. |
| 霊子エナジー | energia duchowa | **zablokowane** (2026-11-09) |
| 大宇宙海賊 / 宇宙海賊 | galaktyczny pirat / kosmiczny pirat | **zablokowane** (2026-11-09). Rozróżniamy je, tak jak japoński. |
| 思念 / 思念体 | myśl / byt myślowy | **zablokowane** (2026-11-09) |
| サイキック・プログラム・エンコーダ | psychiczny enkoder programów | **zablokowane** (2026-11-09) |
| ドリームプログラム | Program Snów | **zablokowane** (2026-11-09) |
| 既視感覚 | déjà vu | **zablokowane**. Z akcentami; „deja vu” bez nich to błąd lintu. |
| 法務局 | Biuro do Spraw Prawnych | **zablokowane** (2026-11-09) |
| 重核中性子爆弾 | ciężkojądrowa bomba neutronowa | **zablokowane** (2026-11-09) |
| 惑星エイレート / 恒星ナーズ / ナーズ星系 | planeta Elath / gwiazda Nuz / układ Nuz | **zablokowane**; nieodmienne |
| 第１次〜第３次調査隊 / セカンドシップ | pierwsza, druga, trzecia ekspedycja badawcza / drugi statek | **zablokowane** |
| 大宇宙海賊御用達霊子エナジー増幅ジェネレータ | Generator Wzmacniania Energii Duchowej z Atestem Galaktycznych Piratów | **zablokowane**. 御用達 = „z atestem”. Nazwę wymyślił pirat (L7346) i każe ją zapamiętać od razu (L8908), więc ma być długa i napuszona. |
| 大宇宙海賊霊子エナジー増幅ジェネレータ２ | Galaktyczno-Piracki Generator Wzmacniania Energii Duchowej 2 | **zablokowane**. Kopia zbudowana w świecie Koujiego, bez 御用達, czyli bez „atestu”. |
| モルモット | królik doświadczalny | **zablokowane** |
| デジタルな (o charakterze) | zero-jedynkowa | **zablokowane** |
| 荘子 | Zhuangzi | **zablokowane**; nieodmienne |
| ご主人様 | panie (gołe; „Mów do niego: panie”, L19267–L19269); u Megumi w D2 komiczne „panie i władco” | **zablokowane** |
| 肉奴隷 (D4) | seksualna niewolnica | **zablokowane** (2026-11-10) |
| お尻の穴 (Tracy, D4) | dziurka w pupie | ustalone (2026-11-10); 肛門 = odbyt |
| 雑音は消して (D3) | wyłączamy szum / trzeba wyłączyć szum | **zablokowane** (2026-11-10) |
| 不可抗力 (D3) | siła wyższa | **zablokowane** |
| 愛の結晶 (Lanhua o Quadrze) | owoc miłości | **zablokowane** |
| というような毎日を、過ごしましたとさ (koniec D3) | I tak oto, powiadają, upływał dzień za dniem... | **zablokowane**; identycznie we wszystkich zakończeniach |

---

## 7. Konwencje zapisu

- Wielokropek `...`; dźwięki w `*gwiazdkach*` polską onomatopeją (`*stuk*`, `*łup!*`, `*chlip*`); myśli w `(nawiasach)`; telepatia 《》 jako `{…}`; przechwycony komunikat 『』 jako `[[…]]` (EN §7).
- **Tagi mówiących** (zablokowane 2026-11-09):
  - ［私］ / ［オレ］ = [Ja]
  - ［男］ = [Mężczyzna]
  - ［男の声］ = [Męski głos]
  - ［女］ = [Kobieta]
  - ［女の声］ = [Kobiecy głos]
  - ［目の前の女性］ = [Kobieta przede mną]
  - ［右の女］ / ［左の女］ = [Kobieta po prawej] / [Kobieta po lewej]
  - ［女性一同］ = [Wszystkie kobiety]
  - ［女の子］ = [Dziewczyna]
  - Imiona bez zmian. Tagi zgodne z EN §6, w tym moment przejścia [Ja] → [Kohji] w L2092.
  - Oczywiste pomyłki w tagach JP poprawiamy, gdy zmylą czytelnika: L7568 ma ［宏治］, ale mówi pirat (na Ziemi, w ciele studenta), więc [Kohji]. W DREAM_02 ［宏治］ = wyłącznie prawdziwy student.
- Bez tyldy `~` na przeciąganie głosek (bajt 0x7E PC-98 rysuje jako nadkreślenie ‾; lint ERROR). Przeciągamy literami: „Cooo!?”, „Tyyy!”.
- Dźwięki małą literą (`*chlup*`, `*puls, puls*`); wersaliki tylko dla głośnych (`*BOOONG*`, `*PLASK!*`).
- Linie złożone wyłącznie z `‥‥‥‥。`, kody (`waku`, `xenon_12`, `s0401`) i łacinę zostawiamy bez zmian.
- **Identyczny japoński = identyczny polski.** Echa (L1855 / L1872) i powtórki (prolog ×3) muszą być rozpoznawalnie równoległe (EN §6).
- Zachowujemy japońskie niespójności, jeśli są celowe albo ciekawe, np. プロフェクタ / プロジェクタ (L7022 / L7024: „profektor” / „projektor”).

---

## 8. Śmiechy, wykrzykniki, powiedzonka

| JP | PL | Kto |
|---|---|---|
| フン | **Phi.** (samotne 「フン‥‥。」 = „Phi...”) | wszyscy, głównie pirat |
| フンフン | Mhm, mhm | |
| フフン | Ha! (zadowolenie z siebie) | |
| フフ | **He, he** (kobiety, które się drażnią: Ryouko, Sayaka, Far; także mężczyźni i Bianca); **Hi, hi** (Tracy, Mai) | |
| ンフフ | Mm, he, he | głównie Ryouko |
| ウフフ / うふふ | Hi, hi, hi | głównie Tracy |
| フフフ | He, he, he | |
| **クックック** | **He, hee, heee** (narastający chichot pirata) | pirat |
| フッフッフ | He, he, he | |
| ハハハ / アハハ | Ha, ha, ha | |
| フハハ / フハハハ | **Bua, ha, ha** (śmiech złoczyńcy) | pirat, Bianca |
| ホホホ / ホーッホッホ | **Ho, ho, ho** (wyniosły śmiech damy); **Ha, ha, ha**, gdy to udawany, zakłopotany śmiech dla przykrycia (Ryouko L8222). Do weryfikacji w lekturze. | Sayaka, Tracy, Bianca; Ryouko |
| ふむ / フム | Hm (namysł, nie śmiech) | |
| エヘヘ | He, he... (zmieszanie) | |
| ふーん | Hmm... (powątpiewanie; to nie フン) | |
| **プリプリ** (Lanhua) | **Mam focha!**, w 3. osobie „Lanhua ma focha!” | wyłącznie Lanhua, 11 razy, zawsze o sobie |
| うるうる | *chlip* | |
| ポッ | *rumieniec* | |

Pisownia z przecinkami według normy PWN: „ha, ha”, „hi, hi”. Śmiechu mówionego nie zapisujemy w gwiazdkach, bo gwiazdki są dla dźwięków.

---

## 9. Głosy po polsku

Wzorcowe linie są w próbkach (`sandbox/claude-notes/pl/calibration*.md`).

| Postać | Polszczyzna |
|---|---|
| Kouji (student) | Potoczny, ironiczny wobec siebie, zmieszany. Do pani doktor grzeczny. W DREAM_03 (オレ) bardziej szorstki: „Ej...”, „No ładnie...”. |
| Kohji pirat | Wyniosły, lekko archaiczny szyk: „Czegóż ty płaczesz?”, „Wątłe ciało, wątły i duch...”. Pompatyczny żargon, w tym terminologia Kanta po polsku (formy zmysłowości, a posteriori). Komizm wynika z kontrastu. Bez pastiszu Sienkiewicza. |
| Kohji amnezjak (D1) | Rzeczowy, spokojny, pytający. |
| Kohji (D4) | Miękki, kontrolujący. お前 / 君 oddaje słownictwo, nie forma. |
| Ryouko | Lekka, przekorna, „lekarką” (nie „lekarzem”). Pod spodem stal. |
| Sayaka | Gładka, przebiegła, z wyższością. „Chłopczyku”, „doświadczona kobieta”. |
| Mai | Cicha, grzeczna, zagadkowa. „Panie Kouji”. O Zhuangzi mówi własnymi słowami (L3525–L3535). Jedyny znaleziony polski cytat ze snu o motylu (Wikicytaty, „Czuang-tsy”) nie podaje tłumacza, więc go nie przejmujemy. |
| Megumi | Zadziorna, bezpośrednia. „Głuptas”, dziecinne „psieplasiam”. |
| Tracy | Dama: staranna, uprzejma składnia, „mój panie”. |
| Lanhua D1 | Chłodna, oszczędna w słowach. |
| Lanhua D2–D4 | Szczebiotliwa: „suuuper”, „stra-a-asznie”, mówi o sobie „Lanhua”, „Mam focha!”. |
| Far | Krótka, sucha, rzeczowa. Emocje nazywa kolorami („kolor podziwu”), bez dopisywania barw. |
| Bianca | Trzy rejestry (EN §7). W D3 złoczyńca („ty nędzniku”) przechodzi w miękkość. W D4 chłopczyca („no to co?”). |

### 9.1 Naturalna polszczyzna: wnioski z lektury DREAM_01 (2026-11-09)

Redaktor przeczytał i przegrał DREAM_01. Poniższe linie były wierne, ale brzmiały jak przekład. Dlatego **wierność znaczeniu, nie słowom**: jeśli zdanie nie mogłoby wyjść spod pióra polskiego prozaika, przepisz je tym samym sensem.

| JP | Źle (kalka) | Dobrze |
|---|---|---|
| 一触即発の現状を打開する | „wyrwie nas z tego zapalnego impasu” (impas nie bywa zapalny) | „przełamie tę sytuację na ostrzu noża” |
| 残像が目に焼き付いていた | „miałem w oczach wypalony powidok” | „obraz… wciąż tkwił mi pod powiekami” |
| 構造的に | „z założenia konstrukcyjnego” | „z założenia” |
| ワインレッドの色調 | „winnoczerwony odcień” | „bordowy” |
| 露出度の高い胸部分 | „mocno odsłonięta góra stroju” | „strój ze sporym dekoltem” |
| 手でコンコンと叩く | „zapukałem knykciami” | „zastukałem w szybę” |
| 胸の下から | „spod mojej piersi” | „spode mnie” |
| 肉付きは意外に悪くない | „kształty miała zaskakująco niezłe” | „figurę miała, o dziwo, całkiem, całkiem” |
| 見知らぬ病室のような場所で目覚めた | „obudziłem się w obcej sali” | „ocknąłem się w nieznanym miejscu, jakby w szpitalu” |
| 妙なモノを採取された | „pobrano ode mnie coś dziwnego” | „pobrano ode mnie dość osobliwą próbkę” |
| 言い様のない、緊張の不安 | „napięty, nieopisany niepokój” | „trudny do nazwania lęk, aż cały się spinałem” |
| 正確には…私が認識したと言うべきか | „to ja odebrałem jej świadomość jako zawstydzoną” | „to ja wyczułem w jej świadomości coś na kształt wstydu” |
| １階分だけ階段を利用する | „zejść schodami jedno piętro” | „zejść schodami na dół” |

**Zasady, które z tego wynikają:**
- **Kolokacja przed słownikiem.** Przymiotnik i rzeczownik muszą do siebie pasować po polsku, nie tylko osobno oddawać japońskie znaczenia.
- **Prostszy czasownik.** Zamiast rzeczownika z przydawkami wybierz zwykły czasownik: „zastukałem”, nie „zapukałem knykciami”. Zamiast „odcienia koloru” wybierz nazwę koloru.
- **Keigo to forma, nie przysłówek.** ご帰還願った to nie „uprzejmie odesłani”. Grzeczność niesie „pan / pani” (§4), a ironię niesie ton zdania.
- **Polski potrzebuje podmiotu i adresata tam, gdzie japoński je opuszcza.** „Z dala od Elath chyba nic nie grozi” rodzi pytanie: komu? Pisz „Dopóki nie zbliżymy się do Elath, nic nam chyba nie grozi”. To nie dodaje pewności (§0), tylko uzupełnia gramatykę.
- **Glosy w nawiasach** (リビド（性衝動）) pomijamy, jeśli polski czytelnik ich nie potrzebuje: samo „libido”.
- **Ten sam opis dwóch różnych osób.** Gdy japoński powtarza frazę (あどけなさの残る przy dwóch kobietach), zmieniamy ją, chyba że powtórzenie jest celowym echem.
- **Piętra po polsku:** 1階 = parter, 2階 = pierwsze piętro.
- **„to by” osobno** w zdaniu nadrzędnym okresu warunkowego: „jakby się obudziła, to by się szarpała” (D3 L11436, uwaga redaktora 2026-11-10).
- **Linie jednowierszowe:** linie z japońskim wcięciem (ramki hipnozy, napisy wyśrodkowane), które w EN mieszczą się w jednym wierszu, muszą mieć ≤ 60 bajtów razem z wcięciem.
- **Dziwność zamierzona zostaje.** L2437 浮上 („wynurzyć się” w kosmosie) jest dziwne także w oryginale, bo następne linie to komentują. Takie miejsca oddajemy słowem zauważalnie nie na miejscu, ale gramatycznym.

---

## 10. Sceny dla dorosłych

- Ta sama dosłowność co w japońskim. Bez pruderyjnych eufemizmów i bez wulgarnego przelicytowania.
- Sugestia zostaje sugestią: „w takim stanie” dla その姿.
- Pieszczotliwe słowa zostają pieszczotliwe: クリちゃん = „guziczek” (L9096, L9130), nie kliniczna „łechtaczka”.
- Język ciała opisujemy rzeczowo.
- Kwestie ekstatyczne są krótkie i rytmiczne, jak w JP.

---

## 11. Otwarte sprawy

- Tytuł generatora (御用達).
- **Na później, po ukończeniu tłumaczenia, wymaga zatwierdzenia redaktora:** zamiana prostych `"` na polskie „ ” oraz `-` (tam, gdzie pełni funkcję myślnika) na półpauzę –. Port czyta UTF-8, więc technicznie to możliwe (potrzebne są tylko glify). Decyzja 2026-11-09: na razie nie. Do tego czasu oba znaki są zabronione (lint ERROR). Zmiana będzie mechaniczna, z przeglądem wszystkich wystąpień.
