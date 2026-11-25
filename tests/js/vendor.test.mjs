// The vendored fm1-dock installer (site/vendor/fm1-dock) as site/install.js uses it: if an update of the vendored
// copy changes what we rely on, this fails.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import * as fm1install from "../../site/vendor/fm1-dock/install/fm1install.js";
import * as dock from "../../site/vendor/fm1-dock/dock.js";

const SRC = readFileSync(new URL("../../site/vendor/fm1-dock/install/fm1install.js", import.meta.url), "utf8");

test("fm1install.js loads outside a browser and has what install.js calls", () => {
  for (const f of ["entryFor", "checkPackage", "installOn", "addModule", "explain", "productOf"])
    assert.equal(typeof fm1install[f], "function", f);
  assert.equal(typeof dock.moduleInfo, "function");
});

test("installOn's question names the firmware and what the FM-1 runs now (confirmText builds on it)", () => {
  assert.ok(SRC.includes("ask(`Install ${name} on the FM-1? It runs ${dev.id.text} now.`)"));
});

test("an install of FM-1_948 cut off in update mode can be finished (fm1-dock's loader check takes FM-1_9xx)", () => {
  const m = /const OUR_LOADER = \(id\) => (\/.*?\/i)\.test/.exec(SRC);
  assert.ok(m);
  const re = new Function(`return ${m[1]}`)();
  assert.ok(re.test("ota-FM-1_948"));
});

test("checkPackage reads the identity as tools/site_build.py product_of does", () => {
  const raw = new Uint8Array(20 * 0x30 + 64);
  const id = "FM-1_948";
  for (let i = 0; i < 20; i++) raw[i * 0x30 + 0x2f] = i < id.length ? (id.charCodeAt(i) + i + 1) & 0xff : 0x7d;
  assert.equal(fm1install.productOf(raw), "FM-1_948");
});

test("a browser without Web MIDI gets fm1-dock's own Chrome-or-Edge message from explain", () => {
  assert.match(fm1install.explain({ code: "nomidi" }), /Chrome or Edge/);
});
