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
