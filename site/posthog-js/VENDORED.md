# PostHog's browser library (self-hosted)

The page's cookie-free usage stats (`site/analytics.js`) use PostHog's browser library,
[posthog-js](https://github.com/PostHog/posthog-js), self-hosted here so the page loads scripts only from its
own site. `module.slim.no-external.js` is copied unmodified from the npm package `posthog-js@1.447.10`
(published 2026-11-10; npm integrity
`sha512-KQnjJxd68NuYqLxK8nqIzII4DquMFQcrMwKrX2MGjfyLbIQKDJN/I3YyqHS1ffYyx3piICpDuz4k4nX9kw3EoQ==`),
`dist/module.slim.no-external.js`, with the package's `LICENSE`:

| file | SHA-256 |
| --- | --- |
| `module.slim.no-external.js` | `b2ae8d3e93d6f3d237779c2d913b179c0ec196534fc67435a76959d402bb74df` |
| `LICENSE` | `d3fb92ac01f0bef4078f89aac9f20f76da3383ad45becf6204abdf5495f43f8f` |

The slim bundle is PostHog's core without its extensions (session recording, autocapture, heatmaps, surveys and
the rest are not in it), and the no-external build never loads a script from another site.
`site/analytics.js` also turns each of them off in its init.

This note is FM1-8P's, not PostHog's. `tests/test_site.py` checks every file against this table: do not edit
them here. To update, take the same file from a newer `posthog-js` package at least two weeks old, update the
version, the integrity and the table, and run `make check`.

The file's last line names a source map (`module.slim.no-external.js.map`) that is not copied: browsers fetch it
only with the developer tools open.

## License

`posthog-js` is `Apache-2.0`, with parts under `MIT` (`LICENSE`, the package's, holds both), copyright PostHog
(Hiberly, Inc.) and Mixpanel, Inc.
