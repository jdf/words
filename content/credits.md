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
- [utf8proc](https://juliastrings.github.io/utf8proc/) — Unicode processing
  (MIT).
- [zlib](https://zlib.net/) by Jean-loup Gailly and Mark Adler — PDF compression
  (zlib license).

The stop-word lists come from
[cue.language](https://github.com/vcl/cue.language), copyright 2009 IBM Corp.,
under the Apache License 2.0.

## The Library

Every book behind **Use a Book** — and the built-in sample texts — is a word
count of a public-domain work digitized, transcribed, and proofread by the
volunteers of [Project Gutenberg](https://www.gutenberg.org/), the oldest
digital library: founded by Michael S. Hart, who invented the ebook in 1971, and
sustained by volunteers ever since. Thank you for keeping tens of thousands of
books free for everyone. If this little library pleases you, consider
[donating to Project Gutenberg](https://www.gutenberg.org/donate/).

A few of the multilingual sample texts come from
[Wikisource](https://wikisource.org/), with equal gratitude.

Thank you, Jon Orwant, for the suggestion that became **Use a Book**: that a
shelf of well-loved public-domain books belongs right in the app.

## Fonts

The typefaces are the original Wordle's font collection:

- **SIL Open Font License**: Gentium and Scheherazade (SIL International),
  BPreplay (George Triantafyllakos), ChunkFive and League Gothic
  ([The League of Movable Type](https://www.theleagueofmoveabletype.com/)), and
  Loved by the King (Kimberly Geswein).
- **Apache License 2.0**: Open Sans (Steve Matteson, for Google).
- **Public domain / CC0**: Goudy Bookletter 1911 (Barry Schwartz), MPH 2B Damase
  (Mark Williamson), and the Larabie Fonts faces — Berylium, Coolvetica,
  Duality, Expressway, Gnuolane, Gunplay, Kenyan Coffee, Mail Ray Stuff, Primer
  Print, Sexsmith, Steelfish, Teen, and Typodermic — placed under CC0 by Ray
  Larabie.
- Included by permission of [Typodermic Fonts](https://typodermic.com/) (Ray
  Larabie): Boopee, Enamel Brush, Headlight, Kelvingrove, Meloche, Owned,
  Scheme, Silentina Movie, Superclarendon, Sweater School, Synthemesc, Tank
  Lite, Telephoto, and Vigo.
- **GNU General Public License**: Chandas and Uttara (Mihail Bayaryn,
  incorporating DejaVu glyphs).
- **Their own licenses**: Nafees Nastaleeq (Center for Research in Urdu Language
  Processing, Lahore), JSL Blackletter (Jeff Lee), Alpha Fridge Magnets (Daniel
  Gauthier, GautFonts), LetterOMatic! (Nate Piekos, Blambot), Powell Antique
  (Dieter Steffmann), and ArTarumianBakhum (Ruben Tarumian).

## Credits From the Original Wordle

Wordle collected many debts in its day, and this reimplementation inherits them
gladly.

Thank you, [Martin Wattenberg](https://www.bewitched.com/), for the central idea
of just throwing stuff at the screen until it fits. I raise my glass to the
philosophy of "the dumbest possible thing that works."

Thank you, [Frank van Ham](https://www.linkedin.com/in/frankvanham/), for
explaining hierarchical bounding boxes to me. Without that notion, this would
have been too slow for interactive use.

Thank you, [Katherine McVety](http://katherinemcvety.com/), for your expert
guidance about color, and for your loving indulgence while I worked obsessively
on this for two weeks.

Thank you, علیرضا فرخی, for your research and assistance in making Wordle look
beautiful in Nastaliq scripts.

धन्यबाद, Eric Nedervold, for long and detailed email discussions about the
rendering of Devanagari, and for giving such careful attention to the results.

Thanks, [Matt McKeon](http://www.mattmckeon.com/), for your wonderfully simple
and obvious (in retrospect) solution to a vexing technical design problem.

Thanks, [Jesse Kriss](http://jklabs.net/) and Fernanda Viégas, for friendly
testing and brainstorming. Fernanda, thanks especially for allowing me to
completely destroy your Java plugin.

Asante, [Mark Dingemanse](https://markdingemanse.net/), for your cheerful
assistance in getting Wordle to speak in the extended ranges of Latin-derived
alphabets.

The stop-word lists were gifts in many tongues: hvala lepa, Mitja Decman
(Slovene); hvala vam, Marko Rakar and Marko Tadic (Croatian); dankon, Fabio
Bettani (Esperanto); תודה רבה, Irina Ros (Hebrew); sağ olun, Mert Torun and
Mesut Aydemir (Turkish, and great help tracking down a locale-dependent bug);
gratias tibi ago, Evan Smith (Latin); ďakujem, Brendor (Slovak); and gràcies,
Carlos Gimenez and your students at Collegi Sant Gabriel de Viladecans
(Catalan).

Bugfinders: Eric Wilcox, Bernard Kerr, Peeter Sällström Randsalu, Stephan Geue,
Jacob Tardell, Drew Harry, John Cullen, Brian Clegg, Mark Cathcart, Tania Hunt,
Shane Curcuru, Turadg Aleahmad, Mike Lindstrom, Ottó Oláh, Steven Woolley,
Alistair McKinnell, Chris Searle, δασκαλάκος, Stuart Axon, Yuriy Opryshko, Greg
Haines, and Kathleen DeWitt.

### Palettes

**Heat** is the work of [Yoon Soo Lee](http://yoonsoo.com/). **Chilled Summer**
(wi.hi.fi) and **Blue Meets Orange** (shiz0-media) came from the Adobe Kuler
community, as did palettes from the original not yet ported here: Indian Earthy
(Pillai.SubbiahMuthus), Firenze (matthepworth), Kindled (monnacat), Shooting
Star (hitapillow), Organic Carrot (blazingbunny), Milk Paints (dianesteinberg),
and Moss (franco_weezer).

## Development tools

Built with [CMake](https://cmake.org/) and [vcpkg](https://vcpkg.io/); tested
with [Catch2](https://github.com/catchorg/Catch2),
[ApprovalTests.cpp](https://github.com/approvals/ApprovalTests.cpp),
[Google Benchmark](https://github.com/google/benchmark), and
[Puppeteer](https://pptr.dev/). None of these ship with the app.
