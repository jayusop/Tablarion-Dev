# Third-party notices

Tablarion is licensed under the GNU Affero General Public License v3.0 or later (see `LICENSE`).
It is built with the following third-party software, used under the licences listed below.

| Component | Use | Licence |
|---|---|---|
| JUCE 8 framework (Raw Material Software Ltd.) | GUI, audio and plug-in framework | AGPLv3 (licence option chosen by this project) |
| Steinberg VST3 SDK (bundled with JUCE) | VST3 plug-in format | GPLv3 (licence option chosen by this project) |
| Apple AudioUnitSDK (bundled with JUCE) | AU plug-in format | Apache License 2.0 |
| zlib | ZIP / compression (.gp files) | zlib licence |
| libpng | PNG images | libpng/zlib licence |
| IJG libjpeg | JPEG images | Independent JPEG Group licence |
| FLAC, Ogg Vorbis | audio codecs (in JUCE audio formats) | BSD licences |
| HarfBuzz | text shaping | Old MIT licence |
| SheenBidi | bidirectional text | Apache License 2.0 |

The complete licence texts are in the JUCE source tree (`ThirdParty/JUCE/LICENSE.md` and the
`LICENSE*` files under `ThirdParty/JUCE/modules`). The GPLv3 is available at
<https://www.gnu.org/licenses/gpl-3.0.html>. GPLv3 code may be combined with AGPLv3 code
(AGPLv3 section 13 / GPLv3 section 13).

## Trademarks

* VST is a registered trademark of Steinberg Media Technologies GmbH. This product uses the VST3
  SDK under the GPLv3 and does **not** use the VST logo.
* Audio Units and macOS are trademarks of Apple Inc.
* Guitar Pro is a trademark of Arobas Music.
* SoundFont is a registered trademark of Creative Technology Ltd.
* All other trademarks are the property of their respective owners.

Tablarion is an independent project. It is not affiliated with, endorsed by or sponsored by
Steinberg, Apple, Arobas Music, Roland, Creative Technology or any other company named here.

## Content

* No Guitar Pro software, code, sound banks or samples are included or required. Guitar Pro
  files are read for interoperability only, using an independent implementation of the
  file formats.
* All instrument sounds are synthesised by original code at run time. The program ships no
  samples. SoundFont banks loaded by the user are subject to their own licences.
* The application icon and user interface artwork are original works created for this project.
