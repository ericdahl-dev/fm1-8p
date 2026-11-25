// site/install.js only wires the page to site/installer.js and fm1-dock's vendored installer; it needs a DOM, so
// this reads it (#362 review M3): the right functions passed through, and no logic of its own left in it.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const SRC = readFileSync(new URL("../../site/install.js", import.meta.url), "utf8");

test("install.js takes fm1-dock's functions from the vendored copy", () => {
  assert.match(SRC, /import \{[^}]*\bentryFor\b[^}]*\bcheckPackage\b[^}]*\binstallOn\b[^}]*\baddModule\b[^}]*\bexplain\b[^}]*\} from "\.\/vendor\/fm1-dock\/install\/fm1install\.js";/);
  assert.match(SRC, /import \{ moduleInfo \} from "\.\/vendor\/fm1-dock\/dock\.js";/);
});

test("install.js passes them to installFlow and addFlow", () => {
  assert.match(SRC, /installFlow\(\{[^}]*lib: \{ entryFor, checkPackage, installOn \}[^}]*explain[^}]*\}\)/s);
  assert.match(SRC, /addFlow\(\{[^}]*lib: \{ moduleInfo, addModule \}[^}]*explain[^}]*\}\)/s);
});

test("install.js decides the buttons with buttonsEnabled, catalog included", () => {
  assert.match(SRC, /buttonsEnabled\(\{ midi: support\.ok, catalogOk, busy \}\)/);
});

test("install.js has no status text of its own (installFlow and addFlow give it)", () => {
  assert.doesNotMatch(SRC, /Canceled|Done:/);
});

// the usage stats (site/analytics.js) are loaded so that the installer never depends on them: a blocker that
// blocks analytics.js or the PostHog library leaves install.js and its buttons as they are
test("install.js imports nothing of the stats statically", () => {
  for (const m of SRC.matchAll(/^import\b[^;]*from\s+"([^"]+)"/gm)) assert.doesNotMatch(m[1], /analytics|posthog/i, m[1]);
});

test("install.js loads the stats with a dynamic import that catches its failure", () => {
  assert.match(SRC, /import\("\.\/analytics\.js"\)[\s\S]*?\.catch\(/);
});

test("install.js passes its track to installFlow and addFlow, and watches the emulator", () => {
  assert.match(SRC, /installFlow\(\{[^\n]*\btrack \}\)/);
  assert.match(SRC, /addFlow\(\{[^\n]*\btrack \}\)/);
  assert.match(SRC, /watchEmulatorStart\(window, [^)]*iframe[^)]*, track\)/);
});

const STATS = readFileSync(new URL("../../site/analytics.js", import.meta.url), "utf8");

test("analytics.js loads the self-hosted PostHog library, nothing from another site", () => {
  assert.match(STATS, /import\("\.\/posthog-js\/module\.slim\.no-external\.js"\)/);
  assert.doesNotMatch(STATS, /import\("https?:/);
});
