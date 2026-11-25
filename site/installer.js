// SPDX-License-Identifier: GPL-3.0-only
// The browser installer's logic for FM1-8P's page (#356): pure functions, with Web MIDI, fetch and fm1-dock's
// installer (vendor/fm1-dock, passed in as `lib`) given to them, so tests/js runs them in node without a browser.
// site/install.js wires them to the page.

const NO_MIDI = "This browser has no Web MIDI with SysEx, so it cannot reach the FM-1. Open this page in Chrome or Edge " +
  "on a computer (not Safari or Firefox, and not on an iPhone or iPad).";

// can this browser reach the FM-1? -> { ok, message }
export function webMidiSupport(nav) {
  if (nav && typeof nav.requestMIDIAccess === "function") return { ok: true, message: "" };
  return { ok: false, message: NO_MIDI };
}

export const IDENTITY = "FM-1_948";             // firmware/pins.env FM1_IDENTITY: our .fwsc's package identity
export const ID = "fm1-8p";                     // our entries in fw/index.json (tools/site_build.py catalog)

function fail(code, msg) { const e = new Error(msg); e.code = code; return e; }

// a package identity other than ours: nothing may be written
function checkIdentity(product) {
  if (product !== IDENTITY)
    throw fail("identity", `The package's firmware identity is ${product}, not FM1-8P's ${IDENTITY}: nothing was written.`);
}

// fw/index.json's FM1-8P firmware entry, its identity checked
export function firmwareEntry(cat) {
  const e = cat && cat.firmwares && cat.firmwares[ID];
  if (!e) throw fail("nocatalog", "There is no FM1-8P package on this page.");
  checkIdentity(e.product);
  return e;
}

// what fm1-dock's installOn names the firmware in its question ("Install <label> on the FM-1? It runs <identity> now.")
export function installLabel(entry) {
  return `FM1-8P ${entry.version} (firmware identity ${entry.product})`;
}

export const GO_BACK = "To go back to the stock firmware V15, install it from the FM-1 Firmware Hub " +
  "(thefirmwarehub.com). If the FM-1 does not start after an install, hold OCT- while you switch it on: the USB rescue.";

// the confirm text: fm1-dock's question, then what to know before saying yes
export function confirmText(question) {
  return `${question}\n\nFM1-8P is a beta, and this installer has not been tested on an FM-1 yet. Do not unplug the FM-1 ` +
    `while it writes.\n\n${GO_BACK}`;
}

// fw/index.json from the site at base (fm1-dock's own catalog() reads it beside vendor/fm1-dock, not here)
export async function loadCatalog(fetch, base) {
  let r;
  try { r = await fetch(new URL("fw/index.json", base), { cache: "no-cache" }); } catch (e) { r = null; }
  if (!r || !r.ok) throw fail("nocatalog", `Could not load this page's package list (fw/index.json${r ? `: HTTP ${r.status}` : ""}).`);
  return r.json();
}

async function loadBytes(fetch, base, file) {
  const r = await fetch(new URL("fw/" + file, base));
  if (!r.ok) throw fail("nocatalog", `Could not load ${file} (HTTP ${r.status}).`);
  return new Uint8Array(await r.arrayBuffer());
}

// FM1-8P onto the FM-1 on midi: the catalog, the device's entry (fm1-dock's entryFor: an SMK-37 is refused), the
// package checked by fm1-dock's checkPackage (SHA-256, identity, update loader, every file's CRC) and by its identity
// again here, the identity shown (say), then fm1-dock's installOn, which asks first (ask gets confirmText).
// lib: { entryFor, checkPackage, installOn } from vendor/fm1-dock/install/fm1install.js.
// -> the identity the FM-1 reports after the install, or null when canceled
export async function installFirmware({ midi, fetch, base, lib, ask, say, step }) {
  const entry = firmwareEntry(await loadCatalog(fetch, base));
  const want = await lib.entryFor(midi, entry);
  const pkg = await lib.checkPackage(await loadBytes(fetch, base, want.file), want);
  checkIdentity(pkg.product);
  say(`Package checked: FM1-8P ${entry.version}, firmware identity ${pkg.product}.`);
  return lib.installOn(midi, pkg, installLabel({ ...entry, product: pkg.product }), (q) => ask(confirmText(q)), step);
}

async function sha256hex(bytes) {
  const h = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
  return [...h].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// FM1-8P's .dock into the fm1-dock on the FM-1 on midi: the catalog's module, checked against its SHA-256 and ABI
// (as fm1-dock's loadModule does), then fm1-dock's addModule, which asks first and leaves the other synths, the
// settings and the presets as they are. lib: { moduleInfo } from vendor/fm1-dock/dock.js, { addModule } from its
// install/fm1install.js. -> the module's name, or null when canceled
export async function addToDock({ midi, fetch, base, lib, ask, progress }) {
  const cat = await loadCatalog(fetch, base);
  const entry = cat && cat.modules && cat.modules[ID];
  if (!entry) throw fail("nocatalog", "There is no FM1-8P module on this page.");
  const blob = await loadBytes(fetch, base, entry.file);
  if (await sha256hex(blob) !== entry.sha256) throw fail("badpkg", `${entry.file}: SHA-256 differs`);
  const info = lib.moduleInfo(blob);
  if (info.abi !== entry.abi) throw fail("badpkg", `${entry.file}: ABI ${info.abi}`);
  return lib.addModule(midi, { blob, info }, ask, progress);
}

const OURS = ["identity", "nocatalog"];
// an error in words for the page: ours as they are, the rest in fm1-dock's words (its install/fm1install.js explain)
export function messageFor(e, explain) {
  return OURS.includes(e.code) ? e.message : explain(e);
}

// a guard for a button: run(fn) runs fn unless one is running (then nothing, undefined); busy while it runs
export function oneAtATime() {
  const g = {
    busy: false,
    async run(fn) {
      if (g.busy) return undefined;
      g.busy = true;
      try { return await fn(); } finally { g.busy = false; }
    },
  };
  return g;
}
