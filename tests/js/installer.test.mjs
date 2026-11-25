// site/installer.js: the browser installer's logic (#356), without a browser: Web MIDI, fetch and fm1-dock's
// functions are passed in. Run with `node --test tests/js/` (make check-site-js).
import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import * as I from "../../site/installer.js";

test("a browser with Web MIDI can install", () => {
  assert.equal(I.webMidiSupport({ requestMIDIAccess: () => {} }).ok, true);
});

test("a browser without Web MIDI is told to use Chrome or Edge", () => {
  const s = I.webMidiSupport({});
  assert.equal(s.ok, false);
  assert.match(s.message, /Chrome or Edge/);
});

const FW = { name: "FM1-8P", version: "abc1234", file: "fm1-8p.fwsc", sha256: "00", size: 10, product: "FM-1_948" };
const MOD = { name: "FM1-8P", version: "abc1234", file: "fm1_8p.dock", sha256: "00", size: 10, abi: 2 };
const CATALOG = { firmwares: { "fm1-8p": FW }, modules: { "fm1-8p": MOD } };

test("our identity is FM-1_948", () => {
  assert.equal(I.IDENTITY, "FM-1_948");
});

test("the firmware entry is FM1-8P's", () => {
  assert.deepEqual(I.firmwareEntry(CATALOG), FW);
});

test("a catalog whose package has another identity is refused before anything is fetched", () => {
  const cat = { firmwares: { "fm1-8p": { ...FW, product: "FM-1_921" } } };
  assert.throws(() => I.firmwareEntry(cat), (e) => e.code === "identity" && /FM-1_921/.test(e.message));
});

test("a catalog without our firmware is refused", () => {
  assert.throws(() => I.firmwareEntry({ firmwares: {} }), (e) => e.code === "nocatalog");
});

test("the install names FM1-8P, its version and its identity", () => {
  assert.equal(I.installLabel(FW), "FM1-8P abc1234 (firmware identity FM-1_948)");
});

test("the confirm text keeps fm1-dock's question, which names what the FM-1 runs now, and says how to go back", () => {
  // fm1-dock's installOn asks `Install ${name} on the FM-1? It runs ${dev.id.text} now.`
  const q = `Install ${I.installLabel(FW)} on the FM-1? It runs FM-1_915 now.`;
  const text = I.confirmText(q);
  assert.ok(text.startsWith(q));
  for (const want of [/FM1-8P abc1234/, /FM-1_948/, /runs FM-1_915 now/, /V15/, /Firmware Hub/, /OCT-/, /unplug/])
    assert.match(text, want);
});

const BASE = "https://example.test/fm1-8p/";
const BYTES = Uint8Array.of(1, 2, 3);

// fetch serving these {path: body} under BASE; records what was asked
function fakeFetch(files) {
  const asked = [];
  const f = async (url) => {
    const path = String(url).slice(BASE.length);
    asked.push(path);
    if (!(path in files)) return { ok: false, status: 404 };
    const body = files[path];
    return { ok: true, status: 200, json: async () => body, arrayBuffer: async () => body.buffer };
  };
  f.asked = asked;
  return f;
}

// fm1-dock's functions as installFirmware uses them; log: what was called, in order
function fakeLib(log, { product = "FM-1_948", running = "FM-1_915", answer = true } = {}) {
  return {
    entryFor: async (midi, entry) => { log.push("entryFor"); return entry; },
    checkPackage: async (raw, entry) => { log.push(["checkPackage", raw.length, entry.file]); return { product, image: raw }; },
    installOn: async (midi, pkg, name, ask, step) => {
      log.push(["installOn", name]);
      if (!await ask(`Install ${name} on the FM-1? It runs ${running} now.`)) return null;
      log.push("write");
      return pkg.product;
    },
  };
}

test("install: the catalog, then the package, checked, its identity shown, then fm1-dock's installOn", async () => {
  const log = [], asked = [];
  const fetch = fakeFetch({ "fw/index.json": CATALOG, "fw/fm1-8p.fwsc": BYTES });
  const got = await I.installFirmware({ midi: {}, fetch, base: BASE, lib: fakeLib(log),
    ask: async (q) => { asked.push(q); return true; }, say: (t) => log.push(["say", t]), step: () => {} });
  assert.equal(got, "FM-1_948");
  assert.deepEqual(fetch.asked, ["fw/index.json", "fw/fm1-8p.fwsc"]);
  const said = log.findIndex((x) => x[0] === "say" && /FM1-8P abc1234/.test(x[1]) && /FM-1_948/.test(x[1]));
  const install = log.findIndex((x) => x[0] === "installOn");
  assert.ok(said >= 0 && said < install, "the identity is shown before installOn");
  assert.deepEqual(log[install], ["installOn", "FM1-8P abc1234 (firmware identity FM-1_948)"]);
  assert.equal(asked.length, 1);
  assert.match(asked[0], /It runs FM-1_915 now/);
  assert.match(asked[0], /OCT-/);
  assert.ok(log.includes("write"));
});

test("install: a package whose identity is not FM-1_948 is refused and nothing is written", async () => {
  const log = [];
  const fetch = fakeFetch({ "fw/index.json": CATALOG, "fw/fm1-8p.fwsc": BYTES });
  await assert.rejects(I.installFirmware({ midi: {}, fetch, base: BASE, lib: fakeLib(log, { product: "FM-1_921" }),
    ask: async () => true, say: () => {}, step: () => {} }), (e) => e.code === "identity");
  assert.ok(!log.some((x) => x[0] === "installOn"));
});

test("install: a catalog naming another identity stops before the package is fetched", async () => {
  const log = [];
  const fetch = fakeFetch({ "fw/index.json": { firmwares: { "fm1-8p": { ...FW, product: "FM-1_921" } } }, "fw/fm1-8p.fwsc": BYTES });
  await assert.rejects(I.installFirmware({ midi: {}, fetch, base: BASE, lib: fakeLib(log),
    ask: async () => true, say: () => {}, step: () => {} }), (e) => e.code === "identity");
  assert.deepEqual(fetch.asked, ["fw/index.json"]);
  assert.deepEqual(log, []);
});

test("install: no catalog on the site", async () => {
  await assert.rejects(I.installFirmware({ midi: {}, fetch: fakeFetch({}), base: BASE, lib: fakeLib([]),
    ask: async () => true, say: () => {}, step: () => {} }), (e) => e.code === "nocatalog");
});

test("install: canceled at the question, nothing is written", async () => {
  const log = [];
  const fetch = fakeFetch({ "fw/index.json": CATALOG, "fw/fm1-8p.fwsc": BYTES });
  const got = await I.installFirmware({ midi: {}, fetch, base: BASE, lib: fakeLib(log),
    ask: async () => false, say: () => {}, step: () => {} });
  assert.equal(got, null);
  assert.ok(!log.includes("write"));
});

const DOCK = Uint8Array.of(9, 8, 7, 6);
const DOCK_SHA = createHash("sha256").update(DOCK).digest("hex");
const DOCK_CATALOG = { modules: { "fm1-8p": { ...MOD, sha256: DOCK_SHA } } };

function fakeDockLib(log, { abi = 2 } = {}) {
  return {
    moduleInfo: (blob) => ({ abi, name: "FM1-8P", fx: false }),
    addModule: async (midi, mod, ask, progress) => {
      log.push(["addModule", mod.blob.length, mod.info.name]);
      return (await ask(`Add ${mod.info.name} to the fm1-dock on the FM-1?`)) ? mod.info.name : null;
    },
  };
}

test("add to fm1-dock: the module, its SHA-256 and ABI checked, then fm1-dock's addModule", async () => {
  const log = [];
  const fetch = fakeFetch({ "fw/index.json": DOCK_CATALOG, "fw/fm1_8p.dock": DOCK });
  const got = await I.addToDock({ midi: {}, fetch, base: BASE, lib: fakeDockLib(log), ask: async () => true, progress: () => {} });
  assert.equal(got, "FM1-8P");
  assert.deepEqual(fetch.asked, ["fw/index.json", "fw/fm1_8p.dock"]);
  assert.deepEqual(log, [["addModule", 4, "FM1-8P"]]);
});

test("add to fm1-dock: a module that is not the catalog's file is refused", async () => {
  const log = [];
  const fetch = fakeFetch({ "fw/index.json": { modules: { "fm1-8p": { ...MOD, sha256: "ab".repeat(32) } } }, "fw/fm1_8p.dock": DOCK });
  await assert.rejects(I.addToDock({ midi: {}, fetch, base: BASE, lib: fakeDockLib(log), ask: async () => true,
    progress: () => {} }), (e) => e.code === "badpkg");
  assert.deepEqual(log, []);
});

test("add to fm1-dock: a module of another ABI than the catalog's is refused", async () => {
  const log = [];
  const fetch = fakeFetch({ "fw/index.json": DOCK_CATALOG, "fw/fm1_8p.dock": DOCK });
  await assert.rejects(I.addToDock({ midi: {}, fetch, base: BASE, lib: fakeDockLib(log, { abi: 3 }), ask: async () => true,
    progress: () => {} }), (e) => e.code === "badpkg" && /ABI 3/.test(e.message));
  assert.deepEqual(log, []);
});

test("messages: our refusals in our words, the rest in fm1-dock's (explain)", () => {
  const explain = (e) => `fm1-dock says ${e.code}`;
  const ours = Object.assign(new Error("identity FM-1_921, not FM-1_948"), { code: "identity" });
  assert.equal(I.messageFor(ours, explain), ours.message);
  assert.equal(I.messageFor(Object.assign(new Error("x"), { code: "notfound" }), explain), "fm1-dock says notfound");
});

test("one at a time: a second press while one runs does nothing", async () => {
  const one = I.oneAtATime();
  let release;
  const first = one.run(() => new Promise((r) => { release = r; }));
  assert.equal(one.busy, true);
  let ran = false;
  assert.equal(await one.run(async () => { ran = true; }), undefined);
  assert.equal(ran, false);
  release("done");
  assert.equal(await first, "done");
  assert.equal(one.busy, false);
});

test("one at a time: free again after a failure", async () => {
  const one = I.oneAtATime();
  await assert.rejects(one.run(async () => { throw new Error("boom"); }));
  assert.equal(one.busy, false);
});

// ---- installFlow: what the Install button does, start to the status line (site/install.js only wires it)

const explainStub = (e) => `fm1-dock: ${e.code}`;
const midiOk = async () => ({});

function flowArgs(over = {}) {
  return { requestMIDIAccess: midiOk, fetch: fakeFetch({ "fw/index.json": CATALOG, "fw/fm1-8p.fwsc": BYTES }), base: BASE,
    ask: async () => true, say: () => {}, progress: () => {}, explain: explainStub, ...over };
}

test("installFlow: canceled at the install question, nothing was written", async () => {
  const text = await I.installFlow(flowArgs({ lib: fakeLib([]), ask: async () => false }));
  assert.equal(text, "Canceled: nothing was written.");
});

test("installFlow: canceled at the update-mode question, the FM-1 stays in update mode", async () => {
  const lib = { ...fakeLib([]), installOn: async (midi, pkg, name, ask) =>
    (await ask(`The FM-1 is in update mode: an install was cut off. Finish it with ${name}?`)) ? pkg.product : null };
  const text = await I.installFlow(flowArgs({ lib, ask: async () => false }));
  assert.equal(text, "Canceled. The FM-1 stays in update mode until an install is finished.");
});

test("installFlow: done names what the FM-1 runs", async () => {
  assert.equal(await I.installFlow(flowArgs({ lib: fakeLib([]) })), "Done: the FM-1 runs FM1-8P (FM-1_948).");
});

test("installFlow: MIDI not allowed is fm1-dock's denied message, and nothing is fetched", async () => {
  const fetch = fakeFetch({});
  const text = await I.installFlow(flowArgs({ lib: fakeLib([]), fetch, requestMIDIAccess: async () => { throw new Error("no"); } }));
  assert.equal(text, "fm1-dock: denied");
  assert.deepEqual(fetch.asked, []);
});

test("installFlow: a refusal is put in words, not thrown", async () => {
  const text = await I.installFlow(flowArgs({ lib: fakeLib([], { product: "FM-1_921" }) }));
  assert.match(text, /FM-1_921/);
});

// ---- addFlow: what the Add button does

function addArgs(over = {}) {
  return { requestMIDIAccess: midiOk, fetch: fakeFetch({ "fw/index.json": DOCK_CATALOG, "fw/fm1_8p.dock": DOCK }), base: BASE,
    lib: fakeDockLib([]), ask: async () => true, say: () => {}, progress: () => {}, explain: explainStub, ...over };
}

test("addFlow: done says how to pick it", async () => {
  assert.equal(await I.addFlow(addArgs()), "Done: FM1-8P is in fm1-dock. Hold HOME on the FM-1 and pick it.");
});

test("addFlow: canceled, nothing was sent", async () => {
  assert.equal(await I.addFlow(addArgs({ ask: async () => false })), "Canceled: nothing was sent.");
});

test("addFlow: no fm1-dock answering is put in words", async () => {
  const lib = { ...fakeDockLib([]), addModule: async () => { throw Object.assign(new Error(""), { code: "nodock" }); } };
  assert.equal(await I.addFlow(addArgs({ lib })), "fm1-dock: nodock");
});

test("buttons: enabled only with Web MIDI, a loaded catalog, and nothing running", () => {
  assert.equal(I.buttonsEnabled({ midi: true, catalogOk: true, busy: false }), true);
  assert.equal(I.buttonsEnabled({ midi: false, catalogOk: true, busy: false }), false);
  assert.equal(I.buttonsEnabled({ midi: true, catalogOk: false, busy: false }), false);
  assert.equal(I.buttonsEnabled({ midi: true, catalogOk: true, busy: true }), false);
});

test("messages: fm1-dock's room errors in our words, without fm1-dock's page controls", () => {
  const explain = () => "fm1-dock's text (Remove in the meter below)";
  const e = (code, message = "") => Object.assign(new Error(message), { code });
  const addons = I.messageFor(e("addons", "CS-1, FM-1 SID."), explain);
  assert.match(addons, /nothing was written/);
  assert.match(addons, /CS-1, FM-1 SID\./);
  assert.match(addons, /fm1-dock's own page/);
  assert.match(I.messageFor(e("addonsota"), explain), /update mode/);
  const space = I.messageFor(e("space", "FM1-8P needs 60 KB, fm1-dock has 40 KB free."), explain);
  assert.match(space, /60 KB/);
  for (const m of [addons, space, I.messageFor(e("addonsota"), explain)]) assert.doesNotMatch(m, /meter below|catalog/);
});

// ---- the usage stats the flows report (site/analytics.js sends them): start, success, failure by error class

function spy() {
  const got = [];
  const track = (event, props) => { got.push(props === undefined ? [event] : [event, props]); };
  return { got, track };
}

test("installFlow reports start and success", async () => {
  const { got, track } = spy();
  await I.installFlow(flowArgs({ lib: fakeLib([]), track }));
  assert.deepEqual(got, [["install_started"], ["install_succeeded"]]);
});

test("installFlow reports a cancel as a failure of class canceled, at either question", async () => {
  const { got, track } = spy();
  await I.installFlow(flowArgs({ lib: fakeLib([]), ask: async () => false, track }));
  const lib = { ...fakeLib([]), installOn: async (midi, pkg, name, ask) =>
    (await ask(`The FM-1 is in update mode: an install was cut off. Finish it with ${name}?`)) ? pkg.product : null };
  await I.installFlow(flowArgs({ lib, ask: async () => false, track }));
  assert.deepEqual(got, [["install_started"], ["install_failed", { error: "canceled" }],
    ["install_started"], ["install_failed", { error: "canceled" }]]);
});

test("installFlow reports a failure by its error code only", async () => {
  const { got, track } = spy();
  await I.installFlow(flowArgs({ lib: fakeLib([], { product: "FM-1_921" }), track }));
  await I.installFlow(flowArgs({ lib: fakeLib([]), requestMIDIAccess: async () => { throw new Error("no"); }, track }));
  const lib = { ...fakeLib([]), installOn: async () => { throw Object.assign(new Error("serial 1234"), { code: "foreign" }); } };
  await I.installFlow(flowArgs({ lib, track }));
  const plain = { ...fakeLib([]), installOn: async () => { throw new Error("serial 1234"); } };
  await I.installFlow(flowArgs({ lib: plain, track }));
  assert.deepEqual(got.filter((g) => g[0] === "install_failed"), [["install_failed", { error: "identity" }],
    ["install_failed", { error: "denied" }], ["install_failed", { error: "foreign" }], ["install_failed", { error: "other" }]]);
});

test("addFlow reports start, success, cancel and failure", async () => {
  const { got, track } = spy();
  await I.addFlow(addArgs({ track }));
  await I.addFlow(addArgs({ ask: async () => false, track }));
  const lib = { ...fakeDockLib([]), addModule: async () => { throw Object.assign(new Error("x"), { code: "nodock" }); } };
  await I.addFlow(addArgs({ lib, track }));
  assert.deepEqual(got, [["dock_add_started"], ["dock_add_succeeded"],
    ["dock_add_started"], ["dock_add_failed", { error: "canceled" }],
    ["dock_add_started"], ["dock_add_failed", { error: "nodock" }]]);
});

// ---- the page works the same without PostHog: blocked, failing to load, or throwing on every call

import * as A from "../../site/analytics.js";

// a PostHog whose every property throws, init and capture included
const throwingPosthog = new Proxy({}, { get: () => { throw new Error("posthog broke"); } });
const everyCallThrows = new Proxy({}, { get: () => () => { throw new Error("posthog broke"); } });

const SCENARIOS = {
  "install done": () => I.installFlow(flowArgs({ lib: fakeLib([]), track: T })),
  "install canceled": () => I.installFlow(flowArgs({ lib: fakeLib([]), ask: async () => false, track: T })),
  "install refused": () => I.installFlow(flowArgs({ lib: fakeLib([], { product: "FM-1_921" }), track: T })),
  "install denied": () => I.installFlow(flowArgs({ lib: fakeLib([]), requestMIDIAccess: async () => { throw new Error("no"); }, track: T })),
  "add done": () => I.addFlow(addArgs({ track: T })),
  "add canceled": () => I.addFlow(addArgs({ ask: async () => false, track: T })),
  "add no dock": () => I.addFlow(addArgs({ track: T, lib: { ...fakeDockLib([]),
    addModule: async () => { throw Object.assign(new Error(""), { code: "nodock" }); } } })),
};
let T;   // the track the scenarios pass

async function results(track) {
  T = track;
  const out = {};
  for (const [name, run] of Object.entries(SCENARIOS)) out[name] = await run();
  return out;
}

test("without PostHog (absent, failing to load, or throwing on every call) the flows end the same", async () => {
  const unhandled = [];
  const onRejection = (e) => unhandled.push(e);
  process.on("unhandledRejection", onRejection);
  try {
    const want = await results(undefined);
    const tracks = {
      "no track": undefined,
      "library blocked": A.createAnalytics({ load: () => Promise.reject(new TypeError("Failed to fetch")) }).track,
      "load throws": A.createAnalytics({ load: () => { throw new Error("blocked"); } }).track,
      "every property throws": A.createAnalytics({ load: async () => throwingPosthog }).track,
      "every call throws": A.createAnalytics({ load: async () => everyCallThrows }).track,
      "track throws": () => { throw new Error("track broke"); },
      "track rejects": () => Promise.reject(new Error("track broke")),
      "track never settles": () => new Promise(() => {}),
    };
    for (const [name, track] of Object.entries(tracks)) assert.deepEqual(await results(track), want, name);
    await new Promise((r) => setTimeout(r, 20));
    assert.deepEqual(unhandled, []);
  } finally {
    process.off("unhandledRejection", onRejection);
  }
});

test("track returns nothing to wait for, loaded or not", async () => {
  const slow = A.createAnalytics({ load: () => new Promise(() => {}) });
  assert.equal(slow.track("install_started"), undefined);
  const broken = A.createAnalytics({ load: async () => everyCallThrows });
  await broken.ready;
  assert.equal(broken.track("install_failed", { error: "lost" }), undefined);
});

// ---- the page's track before analytics.js has loaded: kept (at most 50), replayed once it is there

test("lateTrack: an install_started tracked before the stats load is delivered after", () => {
  const late = I.lateTrack(), got = [];
  late.track("install_started");
  late.track("install_failed", { error: "denied" });
  assert.deepEqual(got, []);
  late.attach({ track: (e, p) => got.push(p === undefined ? [e] : [e, p]) });
  late.track("install_succeeded");
  assert.deepEqual(got, [["install_started"], ["install_failed", { error: "denied" }], ["install_succeeded"]]);
});

test("lateTrack: it keeps at most 50 before the stats load", () => {
  const late = I.lateTrack(), got = [];
  for (let i = 0; i < 80; i++) late.track("emulator_start", { i });
  late.attach({ track: (e, p) => got.push(p.i) });
  assert.deepEqual(got, [...Array(50).keys()]);
});

test("lateTrack: never throws and returns nothing, whatever the stats do", () => {
  const late = I.lateTrack();
  assert.equal(late.track("install_started"), undefined);
  assert.doesNotThrow(() => late.attach({ track: () => { throw new Error("broke"); } }));
  assert.equal(late.track("install_succeeded"), undefined);
  const rejects = I.lateTrack();
  rejects.attach({ track: () => Promise.reject(new Error("broke")) });
  assert.equal(rejects.track("install_started"), undefined);
});

test("lateTrack: stats that never load (attach(null)) drop what was kept and everything after", () => {
  const late = I.lateTrack();
  late.track("install_started");
  assert.doesNotThrow(() => late.attach(null));
  assert.equal(late.pending, 0);
  late.track("install_succeeded");
  assert.equal(late.pending, 0);
});
