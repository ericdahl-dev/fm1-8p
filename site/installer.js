// SPDX-License-Identifier: GPL-3.0-or-later
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
// fm1-dock's own words for these point at controls on its page (its meter, its catalog): ours point at fm1-dock's page
const ROOM = {
  addons: (e) => "FM1-8P needs the room where some of the synths you added to fm1-dock are: nothing was written. " +
    `Remove them on fm1-dock's own page first, then install again. They are: ${e.message}`,
  addonsota: () => "The FM-1 is in update mode, and synths you added to fm1-dock sit where FM1-8P goes: nothing was " +
    "written. Finish the install with the package you started it with, then remove those synths on fm1-dock's own " +
    "page before you install FM1-8P.",
  space: (e) => "Not enough room in fm1-dock for FM1-8P: remove another synth you added, on fm1-dock's own page, " +
    `then press again. ${e.message}`,
};
// an error in words for the page: ours as they are, the rest in fm1-dock's words (its install/fm1install.js explain)
export function messageFor(e, explain) {
  if (OURS.includes(e.code)) return e.message;
  if (ROOM[e.code]) return ROOM[e.code](e);
  return explain(e);
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

// the usage stats a flow reports through track(event, props) (site/analytics.js's): <what>_started, _succeeded, and
// _failed with the error's code only ("other" without one), never its message. A track that throws, or returns a
// promise that rejects, changes nothing here, and nothing waits for it.
const CANCELED = { code: "canceled" };
function reporting(track, what) {
  const send = (event, props) => {
    if (typeof track !== "function") return;
    try {
      const r = props === undefined ? track(event) : track(event, props);
      if (r && typeof r.then === "function") r.then(undefined, () => {});
    } catch (_) { /* the stats are never the installer's problem */ }
  };
  return {
    started: () => send(`${what}_started`),
    succeeded: () => send(`${what}_succeeded`),
    failed: (e) => send(`${what}_failed`, { error: e && typeof e.code === "string" ? e.code : "other" }),
  };
}

const UPDATE_MODE = /^The FM-1 is in update mode/;   // fm1-dock's installOn: finishing an install that was cut off

async function midiAccess(requestMIDIAccess) {
  try { return await requestMIDIAccess({ sysex: true }); } catch (_) { throw fail("denied", "MIDI not allowed"); }
}

// what the Install button does: MIDI access, installFirmware, and the status line it ends with (never throws).
// progress(value 0..100 or null, text) follows fm1-dock's steps; explain: fm1-dock's (install/fm1install.js).
export async function installFlow({ requestMIDIAccess, fetch, base, lib, ask, say, progress, explain, track }) {
  const note = reporting(track, "install");
  let resuming = false, reads = 1, bar = 0;
  note.started();
  try {
    const midi = await midiAccess(requestMIDIAccess);
    say("Looking for the FM-1 …");
    const counted = { ...lib, checkPackage: async (raw, entry) => {
      const pkg = await lib.checkPackage(raw, entry);
      reads = Math.max(1, Math.ceil(pkg.image.length / 512));   // (the FM-1 reads the image 512 bytes at a time)
      return pkg;
    } };
    const step = (k, n) => {
      if (k === "verify") bar = Math.min(10, n / 8);
      else if (k === "write") bar = 10 + Math.min(89, (89 * n) / reads);
      else if (k === "done") { progress(100, ""); return; }
      const said = explain({ code: k, message: "" });
      if (said.startsWith("Error:")) return;
      progress(k === "verify" || k === "write" ? bar : null, said + (k === "write" ? ` (${Math.round(bar)} %)` : ""));
    };
    const askAndNote = (q) => { resuming = UPDATE_MODE.test(q); return ask(q); };
    const got = await installFirmware({ midi, fetch, base, lib: counted, ask: askAndNote, say, step });
    if (got !== null) { note.succeeded(); return `Done: the FM-1 runs FM1-8P (${got}).`; }
    note.failed(CANCELED);
    return resuming ? "Canceled. The FM-1 stays in update mode until an install is finished." : "Canceled: nothing was written.";
  } catch (e) {
    note.failed(e);
    return messageFor(e, explain);
  }
}

// what the Add button does: MIDI access, addToDock, and the status line it ends with (never throws).
// progress(value 0..100, text) while the module is sent.
export async function addFlow({ requestMIDIAccess, fetch, base, lib, ask, say, progress, explain, track }) {
  const note = reporting(track, "dock_add");
  note.started();
  try {
    const midi = await midiAccess(requestMIDIAccess);
    say("Looking for fm1-dock on the FM-1 …");
    const got = await addToDock({ midi, fetch, base, lib, ask,
      progress: (x, t) => progress(Math.round(x * 100), `Sending: ${t}`) });
    if (got === null) { note.failed(CANCELED); return "Canceled: nothing was sent."; }
    note.succeeded();
    return `Done: ${got} is in fm1-dock. Hold HOME on the FM-1 and pick it.`;
  } catch (e) {
    note.failed(e);
    return messageFor(e, explain);
  }
}

// may the page's buttons be pressed? Web MIDI, the catalog loaded (a failed load keeps them off), nothing running
export function buttonsEnabled({ midi, catalogOk, busy }) {
  return midi && catalogOk && !busy;
}
