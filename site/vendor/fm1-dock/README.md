# fm1-dock's browser installer (vendored)

These files are from [fm1-dock](https://github.com/joakimeriksson/fm1-dock) by Joakim Eriksson, copied
unmodified from commit `60a89df6554f6df3823f03a4c893b6715bfef9b5` (2026-11-24, main), with the same
relative layout as fm1-dock's `docs/` (`install/fm1install.js` imports `../dock.js`):

| file | SHA-256 |
| --- | --- |
| `dock.js` | `5863dc2bcc3886a7339ed18ae7a90c71a7c32570555dc6198fc987cfdcddec48` |
| `install/fm1install.js` | `6594898dc21c875765bc3a17592451f2cc4c0130d1cc8cd6baaf6e6244ea1bd5` |
| `install/fm1ota.js` | `d770fc406e063b056d9c0c00786c17d7f8e49d9a9a9cc4823a054070390ee989` |
| `install/ports.js` | `cc093304e3d609b47151663c3d0915daddc4f70b1ceddf107afbb6255d8e0a2b` |
| `LICENSE` | `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986` |

`tests/test_site.py` checks every file against this table: do not edit them here. To update, copy the
files from a newer fm1-dock commit, update the commit and the table, and run `make check`
(`tests/js/vendor.test.mjs` checks what `site/install.js` relies on).

## Credit and license

- fm1-dock and its installer (`dock.js`, `install/fm1install.js`, `install/ports.js`): Joakim Eriksson
  and the fm1-dock contributors.
- `install/fm1ota.js`, the FM-1 USB-MIDI update protocol over Web MIDI: Leo Kuroshita (@kurogedelic),
  Hügelton Instruments, from Felucca (hugelton/Felucca), as adapted in fm1-dock.

All are GPL-3.0-only (`LICENSE`, fm1-dock's). FM1-8P is GPL-3.0 too; FM1-8P's page uses them through
its own `site/install.js` and `site/installer.js`.
