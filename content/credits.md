# Credits

<!-- TODO(jdf): intro verbiage goes here. -->

**words** is a reimplementation of Wordle (2008) by its original author,
Jonathan Feinberg. The source code is available on
[GitHub](https://github.com/jdf/words) under the
[MIT License](https://github.com/jdf/words/blob/main/LICENSE).

## Engine

The layout engine is C++ compiled to WebAssembly with
[Emscripten](https://emscripten.org/) (MIT), and links these libraries:

- [FreeType](https://freetype.org/) — font rendering, under the FreeType
  License. Portions of this software are copyright © The FreeType Project
  (www.freetype.org). All rights reserved.
- [HarfBuzz](https://harfbuzz.github.io/) — text shaping (MIT).
- [Abseil](https://abseil.io/) — C++ utilities (Apache License 2.0).
- [Clipper2](https://github.com/AngusJohnson/Clipper2) by Angus Johnson —
  polygon geometry (Boost Software License 1.0).
- [utf8proc](https://juliastrings.github.io/utf8proc/) — Unicode
  processing (MIT).
- [zlib](https://zlib.net/) by Jean-loup Gailly and Mark Adler — PDF
  compression (zlib license).

The stop-word lists come from
[cue.language](https://github.com/vcl/cue.language), copyright 2009 IBM
Corp., under the Apache License 2.0.

The built-in sample texts are word counts of public-domain books,
courtesy of [Project Gutenberg](https://www.gutenberg.org/) and
[Wikisource](https://wikisource.org/).

## Fonts

The typefaces are the original Wordle's font collection:

- **SIL Open Font License**: Gentium and Scheherazade (SIL
  International), BPreplay (George Triantafyllakos), ChunkFive and League
  Gothic ([The League of Movable Type](https://www.theleagueofmoveabletype.com/)),
  and Loved by the King (Kimberly Geswein).
- **Apache License 2.0**: Open Sans (Steve Matteson, for Google).
- **Public domain / CC0**: Goudy Bookletter 1911 (Barry Schwartz), MPH 2B
  Damase (Mark Williamson), and the Larabie Fonts faces — Berylium,
  Coolvetica, Duality, Expressway, Gnuolane, Gunplay, Kenyan Coffee, Mail
  Ray Stuff, Primer Print, Sexsmith, Steelfish, Teen, and Typodermic —
  placed under CC0 by Ray Larabie.
- Included by permission of
  [Typodermic Fonts](https://typodermic.com/) (Ray Larabie): Boopee,
  Enamel Brush, Headlight, Kelvingrove, Meloche, Owned, Scheme, Silentina
  Movie, Superclarendon, Sweater School, Synthemesc, Tank Lite,
  Telephoto, and Vigo.
- **GNU General Public License**: Chandas and Uttara (Mihail Bayaryn,
  incorporating DejaVu glyphs).
- **Their own licenses**: Nafees Nastaleeq (Center for Research in Urdu
  Language Processing, Lahore), JSL Blackletter (Jeff Lee), Alpha Fridge
  Magnets (Daniel Gauthier, GautFonts), LetterOMatic! (Nate Piekos,
  Blambot), Powell Antique (Dieter Steffmann), and ArTarumianBakhum
  (Ruben Tarumian).

## Development tools

Built with [CMake](https://cmake.org/) and
[vcpkg](https://vcpkg.io/); tested with
[Catch2](https://github.com/catchorg/Catch2),
[ApprovalTests.cpp](https://github.com/approvals/ApprovalTests.cpp),
[Google Benchmark](https://github.com/google/benchmark), and
[Puppeteer](https://pptr.dev/). None of these ship with the app.
