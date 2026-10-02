# MaxLabel User Manual

**A PFML editor and proofreading tool — the step before TIFA.**

Version 0.2.0 ｜ Licence MPL-2.0 ｜ 中文：[manual.zh-CN.md](manual.zh-CN.md)

---

## Contents

1. [What this is](#1-what-this-is)
2. [Installation](#2-installation)
3. [Quick start](#3-quick-start)
4. [Concepts: segments, where the text comes from, what comes out](#4-concepts-segments-where-the-text-comes-from-what-comes-out)
5. [The window](#5-the-window)
6. [Working](#6-working)
7. [Audio](#7-audio)
8. [The PFML pane](#8-the-pfml-pane)
9. [How languages are decided](#9-how-languages-are-decided)
10. [Vocabulary and G2P](#10-vocabulary-and-g2p)
11. [Command line](#11-command-line)
12. [Keyboard reference](#12-keyboard-reference)
13. [Known limitations](#13-known-limitations)

---

## 1. What this is

A forced aligner like TIFA can take text directly, but it needs a carrier that answers three
questions: **where the word boundaries are, which language each run is in, and how each character
is pronounced.** That carrier is PFML (Pronunciation Flow Markup Language).

Writing PFML by hand is both difficult and easy to get wrong, and the cost is high: **the aligner
skips a sample whose PFML is invalid, with no fallback.** The MinLabel-era flow of "audio plus a
`.lab`" has no place in a text-first chain. MaxLabel fills that gap.

```
AudioSlicer ──► (text / .lab+json / .pfml)
                        │
                        ├──► MaxLabel ──► song.pfml          ← this project
                        │
                        └──► TIFA ──► TextGrid + diagnosis.json
```

**Input** (all optional): audio + `.pfml` / `.txt` / `.lab` / `.json`
**Output**: `.pfml` only

### Text first, audio optional

This is the largest difference from MinLabel. MinLabel is *labels attached to audio*; MaxLabel is
*annotations attached to text*.

So **a segment with no recording is a normal case, not a broken one**: open a directory holding
only `.txt` files and the segments are still listed, the languages are still split, the `.pfml` is
still written. The audio panes simply do not appear, rather than the whole tool degrading. The
order of work changes with it — it used to be "listen, then label"; now it is "settle the text and
the pronunciations first, and look at the audio only when it helps".

### What it does not do

- **No alignment.** That is TIFA's job, not this tool's.
- **No fixing of bad slices.** That is an upstream problem.
- **No conversion on TIFA's output side.** TextGrid and `diagnosis.json` are out of scope.

---

## 2. Installation

### Release archives (recommended)

Download the archive for your platform from
[Releases](https://github.com/KakaruHayate/MaxLabel/releases) and unpack it. **No separate Qt
install is needed.**

| Platform | File | Run |
|---|---|---|
| Windows x64 | `MaxLabel-windows-x64.zip` | `MaxLabel.exe` |
| Linux x64 | `MaxLabel-linux-x64.tar.gz` | `./MaxLabel` |
| macOS arm64 | `MaxLabel-macos-arm64.tar.gz` | `MaxLabel.app` |

Every data file is already in the archive (`models/`): the language-detection model, the Chinese / Japanese / English / Cantonese pronunciation dictionaries, and the phoneme table. Unpack and run — **there is nothing else to download**.

> **The command-line program `maxlabel_cli` sits in the same directory and does not depend on Qt.**
> Use it for batch validation and scripting, with no graphical environment at all.

### Building from source

You need CMake ≥ 3.18, a C++17 compiler, and Qt 6 (Widgets / Multimedia / Svg / LinguistTools).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

For the core library and the CLI only, on a machine without Qt:

```bash
cmake -S . -B build -DMAXLABEL_BUILD_UI=OFF
```

`MAXLABEL_BUILD_UI` defaults to `ON`, and with it a missing Qt is an **error** rather than a silent
downgrade — otherwise a CI job could pass without ever compiling the window it claims to check.

---

## 3. Quick start

```bash
MaxLabel /path/to/dataset
```

1. The left-hand list shows the **segments** found in the directory. Click one.
2. The **text** in the middle is the lyric line. The **strip** below it lays the line out as blocks.
3. **Click** a block to select one character, **drag** across blocks to select a run, **double-click**
   to set a pronunciation.
4. With a selection: `1`–`5` for the language, `W` to mark a word, `P` to pin a pronunciation,
   `I` to insert a sound.
5. The **PFML pane** at the bottom shows exactly what will be written. `Ctrl+S` saves — though every
   change is already on disk.

`F1` opens the in-app manual, which carries the same shortcut tables.

---

## 4. Concepts: segments, where the text comes from, what comes out

### A segment

**Files sharing a basename are one segment:**

```
song.wav      ← audio (optional)
song.pfml     ← PFML (optional)
song.txt      ← plain lyric text (optional)
song.lab      ← a MinLabel-era syllable line (optional)
song.json     ← a MinLabel-era project sidecar (optional)
```

Of the four text files, **exactly one** becomes the segment's text, by this precedence:

```
.pfml  >  .txt  >  .lab
```

the same order the aligner uses. `.json` contributes no text; it carries the syllable and phoneme
sequences, used to pre-fill alongside a `.lab`.

**Opening a `.pfml` reads the markup rather than showing it as the text.** Scopes become language
spans, `<word>` becomes a word boundary, and a `<word>` with `phonemes`, plus `<phoneme>`, become
pronunciation annotations — so editing a file this tool wrote does not corrupt it.

### Output

**`.pfml` only, in the segment's own directory, named `<id>.pfml`.**

Every change is written **immediately**. There is no pending state, so switching segments or
directories cannot lose work.

### A limitation of PFML itself

`<word>` can only nest *inside* `<scope>`, not the other way round. So **a word that crosses a
language boundary cannot be expressed**: if half a word is Chinese and half Japanese, the tool
reports it in the status bar and leaves it as plain text rather than silently splitting or dropping
it.

---

## 5. The window

```
┌────────────┬──────────────────────────────────────────────┐
│ SEGMENTS   │ AUDIO  (the whole pane collapses when there   │
│            │        is no recording)                       │
│ PROJECT    ├──────────────────────────────────────────────┤
│            │ transport │ annotations │ languages │  time  │
│ LANGUAGE   ├──────────────────────────────────────────────┤
│            │ the strip                                     │
│ WORDS      ├──────────────────────────────────────────────┤
│            │ TEXT                                          │
│ PRONUN.    ├──────────────────────────────────────────────┤
│            │ PFML (editable)                               │
└────────────┴──────────────────────────────────────────────┘
```

The arrangement follows Aegisub: **audio on top, a toolbar in the middle, text below.** A
spectrogram reads time along X and frequency along Y, so the audio pane is full width rather than a
narrow side column — a narrow column cannot show a spectrogram at all.

### The left rail

| Group | Contents |
|---|---|
| SEGMENTS | Every segment found. An unsaved one carries `*`; one with no audio is marked "(no audio)" |
| PROJECT | Open Folder / Save / Previous / Next / Undo / Redo |
| LANGUAGE OF SELECTION | `1 Chinese`, `2 Japanese`, `3 English`, `4 Korean`, `5 Cantonese` — each button wears **the colour that language takes in the text** |
| WORD BOUNDARIES | Mark Word / Clear Words / Re-split Languages |
| PRONUNCIATION | Set Pronunciation / Insert Phonemes / Non-lexical / Clear Overrides |

---

## 6. Working

### The strip is the main way in

The line is laid out as blocks — **one character per block, one word for Latin**. This exists to
get around a specific difficulty: CJK has no word boundary to snap to, so selecting a run in the
text box means dragging across exactly the right characters and then travelling to a button in the
rail. That is both slow and easy to get wrong.

| Gesture | Result |
|---|---|
| **Click** | Select that block |
| **Drag** | Select the run between two blocks — a word is usually longer than one character, and `<word>` needs the whole range |
| **Double-click** | Open the pronunciation dialog for that block; on a `+tag`, change or remove that inserted sound |

A block carries its state, so nothing has to be looked up elsewhere:

- **The reading above the character** is what is pinned to it (`di` above `的`)
- **A cyan bar along the bottom** means this is a fixed word
- **A dotted outline** means the pronunciation was written by hand — turning **red** when the
  phoneme is not in the vocabulary
- **A `+n` block** means an `n` was inserted here; it has no width, so it gets a block of its own

### How overlapping marks resolve

**A later mark wins over the range it names.** Concretely, in two different ways:

- A **word boundary** is a bare mark, so an overlapped one **keeps whatever sticks out**. Marking
  `天气` inside an existing `今天天气` leaves `今天` as a word of its own rather than deleting it.
- A **pronunciation** carries a value, so an overlapped one is **replaced whole**. The reading
  written for one range is not the reading for a shorter one; trimming it would produce a wrong
  value, not a compromise.

### Taking a mark back

**Repeating the same operation on the same range undoes it:**

| Operation | How to take it back |
|---|---|
| `W` mark word | Press `W` again, when the range is already exactly that word |
| `P` set pronunciation | **Remove** in the dialog |
| `I` insert a sound | Double-click the `+tag` and use **Remove**; or insert again at the same place, which replaces rather than stacking |
| Language | **Re-split Languages** (`R`), which discards the automatic spans |

### Undo

**One history for the whole window**, covering text, languages, words, pronunciations and PFML
edits.

Typing is coalesced after you stop (a pause writes one step, not one per character), so `Ctrl+Z`
undoes *an edit* rather than *a keystroke*. Neither text pane has an undo stack of its own: a text
widget with its own stack silently ignores every annotation, which is worse than having no undo at
all.

### After you have decided a language by hand

Once a span has been decided by hand, **editing the text re-derives the language spans** — the old
offsets no longer describe the new text. The status bar says so while manual marks are present
rather than discarding them silently. To keep them, stop editing that text, or re-split
deliberately with `R`.

---

## 7. Audio

Audio is **not required**. With no recording, the audio pane, the transport and the time readout
collapse together, and the annotation half of the toolbar stays — marking a word is a text
operation and has nothing to do with whether a recording exists.

| Action | Effect |
|---|---|
| Click the waveform | Move the playhead |
| Drag the waveform | Select a range |
| `≋` | Waveform / spectrum |
| `▶` | Play the selection, or the whole file |

The **spectrogram** is a time–frequency plot: time along X, frequency along Y on a logarithmic axis
(60 Hz – 10 kHz). Its dynamic range is 75 dB below the peak with a gamma correction, so the harmonic
stacks and formants of real singing are legible rather than saturating into blocks.

`«` and `»` next to the play button nudge by half a second; `⌫` clears the selection.

---

## 8. The PFML pane

The pane at the bottom is **what gets written**, and it is **editable**.

What you type there is read back into the text and the strip above — the two cannot disagree.

**A fragment that does not parse changes nothing:**

- the pane turns red
- the panes derived from it (text, strip) go **dim**, rather than going on showing the last
  fragment that did parse — which would read as "nothing is wrong"
- the reason is in the status bar and in the pane's tooltip

Undo goes through the window's single history, so `Ctrl+Z` steps *that edit* rather than the
keystrokes. It is applied after a pause (one second), so half-typed tags do not produce a syntax
error per character.

---

## 9. How languages are decided

Segmentation borrows GPT-SoVITS's front end (`split-lang` pre-split, BudouX word segmentation,
fastText detection), because its behaviour on multilingual lyrics is the one that has been
exercised.

**The governing rule: never guess what a script can settle.**

| Script | Decided as |
|---|---|
| Kana (hiragana / katakana) | Japanese |
| Hangul | Korean |
| Latin letters | English |
| **Han** | **Not guessed** |

Han characters are shared by Chinese, Japanese and Cantonese, and cannot be told apart by looking.
A Han run with nothing further to go on is therefore reported as **undetermined** — shown in the
text as a **red wavy underline instead of a colour**, because a colour reads as an answer and there
is not one yet. Press `1` / `2` / `3` to decide it.

Digit runs, punctuation and whitespace, when they would stand alone, are folded into their
neighbours rather than being called a language.

---

## 10. Vocabulary and G2P

**Both are already in the release**, and work with no arguments at all. The data sits in `models/`
beside the program:

| Data | Contents |
|---|---|
| `models/vocab.txt` | 220 phoneme symbols (en 42 / ja 40 / yue 70 / zh 65) |
| `models/dictionaries/`, `models/cpp_pinyin/` | the Chinese / Japanese / English / Cantonese pronunciation dictionaries |

So out of the box:

- the pronunciation dialog **lists the dictionary's candidates**; one click fills them in — which
  matters most for the polyphonic characters of Chinese and Japanese;
- every phoneme is **checked**, and one that is not in the table is pointed out, rather than being
  discovered when the aligner skips the whole sample.

The table is taken from `tifa.vocab.json`, embedded in the TIFA model `tifa-1.0-st` — that is, it is
**the same set of symbols the aligner knows**. Entries are written `<language>/<phoneme>`: the same
`a` is a different symbol in English and in Chinese, and English has no `a` at all (it has
`aa`/`ae`/`ah`…). A flat list without the language would let "an `a` inside an English run" through
unnoticed, which is exactly the mistake this check exists to catch.

`--vocab` / `--g2p-dir` / `--g2p` still exist, as **overrides**: a different table, a different set
of dictionaries.

> **Korean is the exception**: the TIFA model has no Korean phonemes. The tool can label a run as
> Korean, but the phonemes there cannot be checked — that is the edge of what the model covers, not
> a gap in the table.

G2P uses `tifa_ggml_g2p` from [tifa.cpp](https://github.com/KakaruHayate/tifa.cpp) — the same code
TIFA itself uses, so "the reading this tool can look up" and "the reading the aligner will look up"
are the same reading.

Where the data files come from, and their licences, is written up in
[models/README.md](../models/README.md).

---

## 11. Command line

`maxlabel_cli` does not depend on Qt, which makes it suitable for batch work and scripting.

```
maxlabel_cli — PFML project tool

  scan <dir>                 list the segments found in a directory
  show <dir> <id>            print one segment's PFML
  validate <file.pfml>       parse-check a PFML fragment
  set <dir> <id> [file]      write <id>.pfml (reads stdin when no file)
  langs <file> [-l <lang>]   split a transcript by language, print the PFML
  phoneme <symbol> [-l zh,en] [--vocab <file>]
                             check a phoneme against the model vocabulary
  candidates <text> --g2p <config.json> [--dicts <dir>] [-l zh,en]
                             list the pronunciations the pipeline offers
```

The global option `--models <dir>` sets where the data files (the BudouX models, the detector
weights) are looked up.

`validate` is the one most worth scripting: it goes through **the same gate the window uses before
saving**.

```bash
for f in out/*.pfml; do maxlabel_cli validate "$f" || echo "BAD: $f"; done
```

---

## 12. Keyboard reference

### Global

| Key | Action |
|---|---|
| `Ctrl+O` | Open folder |
| `Ctrl+S` | Save (every change is already on disk) |
| `Z` / `X` | Previous / next segment |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` / `Ctrl+Y` | Redo |
| `F1` | Manual |

### Annotating

| Key | Action |
|---|---|
| `1` `2` `3` `4` `5` | Language of the selection: Chinese / Japanese / English / Korean / Cantonese |
| `W` | Mark as one word (press again to take it back) |
| `P` | Set the pronunciation |
| `I` | Insert phonemes |
| `R` | Re-split the languages |

### Audio

**These only take effect once the audio pane has the focus**, so `Space` remains a space in the text
panes.

| Key | Action |
|---|---|
| `Space` / `B` | Play the selection, or the whole file |
| `H` | Stop |
| `Q` / `W` | Back / forward half a second |
| `←` / `→` | The same, from the arrow keys |
| `Ctrl+Space` | Play, from anywhere in the window |

---

## 13. Known limitations

- **A word crossing a language boundary cannot be expressed** (see §4). The tool reports it rather
  than handling it silently.
- **`<reading>` / `<path>` / `<group>` are not modelled.** A fragment using them is refused in the
  text pane with a reason, rather than half-understood and then written back broken.
- **After a language is decided by hand, editing the text re-derives the spans** (see §6). The old
  offsets no longer describe the new text.
- **At very narrow window widths**, the rightmost language buttons in the toolbar can be clipped.
  The rail carries the same commands, each with a number key, so this is cosmetic rather than
  functional.
- **Qt's own translations are not bundled**, so a few strings inside Qt itself (mainly the file
  dialog on Linux) are English. The application's own text, and the standard buttons on its dialogs
  (OK / Cancel / Close), are translated.

---

## Licence

MPL-2.0, the same as [tifa.cpp](https://github.com/KakaruHayate/tifa.cpp).
