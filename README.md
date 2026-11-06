# Tablarion — Tablature Sound Module

A free macOS virtual instrument (VST3 / AU / Standalone) that opens Guitar Pro score files and plays them
with an articulation-aware sound engine: notation interpreters, per-string voices, and per-instrument
effect chains. All sounds are synthesised by original code; no third-party samples are included.

## Build (Xcode)

```sh
brew install cmake        # once
./setup.sh -DTABLARION_SOURCE_URL=https://github.com/<you>/Tablarion
open build/Tablarion.xcodeproj
```
`setup.sh` clones JUCE 8.0.9 and generates `build/Tablarion.xcodeproj`. Set `TABLARION_SOURCE_URL` to
the public address of your source code. The URL appears in the About box (see *Licence* below).

Build the **Tablarion_All** scheme (Release for best performance). The build copies the plug-ins to
`~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`.
### Choosing the CPU (Apple Silicon / Intel)

| Command | Builds | Output folder |
|---|---|---|
| `./build.sh` | universal: Apple Silicon (arm64) + Intel (x86_64) | `build/` |
| `./build.sh arm64` | Apple Silicon only | `build-arm64/` |
| `./build.sh x86_64` | Intel only | `build-x86_64/` |
| `./build.sh all` | all three | all of the above |

Each build ends up in `<folder>/Tablarion_artefacts/Release/` (Standalone app, VST3, AU). Any Mac can build
any of these targets. Plug-ins are copied to `~/Library/Audio/Plug-Ins` only when they can run on the Mac that
built them (universal or that Mac's own CPU). Force it with `-DTABLARION_INSTALL_PLUGINS=ON/OFF`.

In Xcode: generate the project with `./setup.sh -DTABLARION_ARCH=<universal|arm64|x86_64|native>`, then build
with the run destination **Any Mac (Apple Silicon, Intel)** or *Product > Archive*. With *My Mac*, Xcode
builds only the current Mac's own architecture. `native` builds only for the current Mac, which is quickest
during development. Minimum macOS version is 12.

## Usage

* Drop a `.gp` (GP7/8), `.gpx` (GP6), `.gp5`, `.gp4` or `.gp3` file onto the panel, or click **OPEN**.
* **PLAY / STOP**, **LOOP**, **POSITION** bar, **SPEED** (25–200 %).
* **SYNC** follows the DAW transport and tempo (bar 1 of the song = DAW bar 1).
* MIDI input plays the selected part (click a part row) using that track's instrument.
* **EXPORT WAV** renders the whole song to a 24-bit WAV file.
* **LOAD SF2** loads a SoundFont 2 bank. *Hybrid* mode keeps the modeled guitars and basses, which handle
  techniques best, and uses the SoundFont for drums, keys and orchestral parts. Each track can override
  this with its **ENGINE** setting. A `.sf2` placed in `~/Library/Application Support/Tablarion/SoundFonts`
  loads automatically.

## Engine overview

| Layer | Implementation |
|---|---|
| Readers | GP3/4/5 binary, GP6 BCFZ/BCFS container, GP7/8 zip + GPIF XML |
| Conductor (`Sequencer.cpp`) | repeats and alternate endings, tempo map, ties, let ring, hammer-on/pull-off, legato and shift slides, slide in/out, bends, whammy, vibrato, natural/artificial/pinch/tapped harmonics, trills, tremolo picking, grace notes, brush, arpeggio and rasgueado, natural chord strumming, swing, humanize |
| Strings | dual-polarization waveguide: Lagrange fractional delay, dispersion allpass with phase compensation, frequency-dependent decay, palm mute, dead notes, pickup-position comb |
| Keys | modal piano with inharmonic partials and double-string beating, FM tine e-piano, drawbar organ, modal mallets |
| Others | VA ensembles (strings, brass, reeds, flute, choir, pads, leads, synth bass), synthesized GM drum kit with hi-hat choke |
| SoundFont 2 | full generator model, DAHDSR envelopes, filter, LFOs, loops, exclusive classes, guitar articulations |
| FX | 4× oversampled amp sims (clean/jazz/crunch/high-gain/bass) with cabinet, acoustic body resonator, chorus, compressors, FDN hall reverb, glue compressor, look-ahead limiter |

## Developer tools

`cmake -B build-cli -DTABLARION_BUILD_TOOLS=ON` adds:
* `ScoreTest <file|dir>`: parser regression test
* `RenderTest <score> <out.wav> [sec] [sf2]`: offline render with timing and level report
* `UISnapshot <score> <out.png>`: renders the editor to an image

Icon source: `Resources/Icon/DrawIcon.swift` (run `swift DrawIcon.swift icon_1024.png`).

## Licence and redistribution

Tablarion is free software under the **GNU Affero General Public License v3.0 or later**
(`LICENSE`). This licence is required because the project uses JUCE under its AGPLv3 option and
the Steinberg VST3 SDK under its GPLv3 option. Under these options you do not need to sign
anything or pay any fee. When you distribute binaries (DMG/ZIP, website, forum...):

1. Publish the complete corresponding source code (this repository, including build scripts) at
   the URL set in `TABLARION_SOURCE_URL`, or ship it alongside the binaries.
2. Keep `LICENSE` and `THIRD_PARTY_NOTICES.md` with the binaries. The build copies them into every
   bundle's `Contents/Resources`.
3. Modified versions must stay under the AGPLv3 and say that they were modified.

Closed-source distribution would need a commercial JUCE licence and a signed Steinberg VST3
licence agreement instead.

### Trademarks and interoperability

VST is a registered trademark of Steinberg Media Technologies GmbH. Audio Units is a trademark of
Apple Inc. Guitar Pro is a trademark of Arobas Music. SoundFont is a registered trademark of
Creative Technology Ltd. These names are used only to describe compatibility. The product, its
icon and its UI do not use their logos, and Tablarion is not affiliated with or endorsed by these
companies. Guitar Pro files are read for interoperability through an independent implementation;
no Guitar Pro software, code or sound data is included. SoundFont banks you load are covered by
their own licences. See `THIRD_PARTY_NOTICES.md`.
