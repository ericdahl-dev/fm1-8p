# The page's fonts

Self-hosted, so the page loads no font from another site. Each folder holds the latin subset of one family as
woff2, unmodified, and the family's license as `OFL.txt`. Both are under the SIL Open Font License, Version 1.1.
The files come from the Fontsource npm packages, version 5.3.0.

| Folder | Family | Weights | Copyright | Fontsource package |
|---|---|---|---|---|
| `instrument-sans/` | Instrument Sans | 400 to 700 (variable) | The Instrument Sans Project Authors (https://github.com/Instrument/instrument-sans) | `@fontsource-variable/instrument-sans` |
| `dm-mono/` | DM Mono | 400, 500 | The DM Mono Project Authors (https://www.github.com/googlefonts/dm-mono) | `@fontsource/dm-mono` |

Instrument Sans sets the headings and the text, DM Mono the tone list, code and the installer's status.

`SHA256SUMS` lists each file's SHA-256, as `sha256sum -c SHA256SUMS` checks it from this folder; so does
tests/test_site.py.
