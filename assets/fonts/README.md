# Fonts from the original Wordle

The font collection shipped by wordle.net (2008–2014), carried into
this project by its author. All faces are either liberally licensed
(e.g. Gentium and Scheherazade under the SIL OFL, League Gothic and
ChunkFive from the League of Movable Type, OpenSans under Apache 2.0,
MPH 2B Damase, Goudy Bookletter 1911, Chandas/Uttara, etc.) or
included by special arrangement with Ray Larabie of Typodermic Fonts
(Coolvetica, Steelfish, Kenyan Coffee, Gnuolane, Telephoto, Vigo, and
the other Typodermic faces). Attribution lives in content/credits.md
(the app's Credits dialog).

A 2026 license audit moved six faces whose redistribution rights were
doubtful (Gill Sans, SBL Hebrew, Chrysanthi Unicode, IranNastaliq,
GrilledCheese BTN, and the unattributed kingtype) out of the repo to
~/fonts-needing-license-work, with findings in that directory's
README. Hebrew and Kana coverage went with them.

`capabilities.txt` records which fonts can render which scripts, keyed by
Unicode block — the original FontManager's knowledge, verbatim.

Note: the original's Devanagari entry pointed at the Java runtime's Lucida
Sans, which we don't ship; chandas and uttara (present here) are the
Devanagari faces instead.
