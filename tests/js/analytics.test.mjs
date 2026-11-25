// site/analytics.js: the page's cookie-free usage stats (PostHog), without a browser: the PostHog library is
// passed in (a fake here), so these run in node. Run with `node --test tests/js/` (make check-site-js).
import { test } from "node:test";
import assert from "node:assert/strict";
import * as A from "../../site/analytics.js";

test("config: cookieless, no person profiles, the US host", () => {
  const c = A.initConfig();
  assert.equal(c.api_host, "https://us.i.posthog.com");
  assert.equal(c.cookieless_mode, "always");
  assert.equal(c.persistence, "memory");
  assert.equal(c.person_profiles, "never");
});

test("config: recording, autocapture, heatmaps, console logs and surveys are off in the init itself", () => {
  // the project has recording, console logs and heatmaps on: the page's init must turn each off
  const c = A.initConfig();
  assert.equal(c.disable_session_recording, true);
  assert.equal(c.autocapture, false);
  assert.equal(c.capture_heatmaps, false);
  assert.equal(c.enable_recording_console_log, false);
  assert.equal(c.disable_surveys, true);
});

test("config: page views are captured, nothing else of PostHog's own", () => {
  const c = A.initConfig();
  assert.equal(c.capture_pageview, true);
  assert.equal(c.capture_pageleave, false);
  assert.equal(c.capture_dead_clicks, false);
  assert.equal(c.capture_exceptions, false);
  assert.equal(c.capture_performance, false);
  assert.equal(c.rageclick, false);
  assert.equal(c.disable_external_dependency_loading, true);
  assert.equal(c.advanced_disable_flags, true);
});

test("the project's public token", () => {
  assert.equal(A.TOKEN, "phc_uzsUUTbN2k3EJVDvtTGBtrRgPmdWrRxzxbLZcsw7Vvm4");
});

// a PostHog stand-in: what init and capture got
function fakePosthog() {
  const ph = { inits: [], captured: [] };
  ph.init = (token, config) => { ph.inits.push({ token, config }); return ph; };
  ph.capture = (event, properties) => { ph.captured.push({ event, properties }); };
  return ph;
}

test("every event PostHog sends, its page view too, carries site: fm1-8p", () => {
  const ev = A.initConfig().before_send({ event: "$pageview", properties: { $current_url: "https://x/?utm_source=y" } });
  assert.equal(ev.properties.site, "fm1-8p");
  assert.equal(ev.properties.$current_url, "https://x/?utm_source=y");
});

test("track: our event, with the site tag and nothing else", async () => {
  const ph = fakePosthog();
  const a = A.createAnalytics({ load: async () => ph });
  a.track("install_started", { serial: "1234", sysex: [0xf0] });
  await a.ready;
  assert.deepEqual(ph.inits.map((i) => i.token), [A.TOKEN]);
  assert.deepEqual(ph.captured, [{ event: "install_started", properties: { site: "fm1-8p" } }]);
});

test("track: a failure carries only an allow-listed error class and the site tag", async () => {
  const ph = fakePosthog();
  const a = A.createAnalytics({ load: async () => ph });
  a.track("install_failed", { error: "foreign", message: "FM-1 serial 1234", detail: "F0 7E", bytes: [1, 2] });
  a.track("dock_add_failed", { error: "space" });
  a.track("install_failed", { error: "The FM-1 FM-1_915 serial 1234 said no" });
  a.track("install_failed", {});
  await a.ready;
  assert.deepEqual(ph.captured.map((c) => c.properties), [
    { site: "fm1-8p", error: "foreign" }, { site: "fm1-8p", error: "space" },
    { site: "fm1-8p", error: "other" }, { site: "fm1-8p", error: "other" }]);
});

test("track: the error classes are the installer's codes and canceled, all short words", () => {
  for (const c of ["foreign", "mismatch", "lost", "canceled", "denied", "notfound", "identity", "nocatalog", "badpkg", "nodock"])
    assert.ok(A.ERROR_CLASSES.includes(c), c);
  for (const c of A.ERROR_CLASSES) assert.match(c, /^[a-z]{2,12}$/);
});

test("track: an event that is not ours is not sent", async () => {
  const ph = fakePosthog();
  const a = A.createAnalytics({ load: async () => ph });
  a.track("$identify", { $set: { serial: "1234" } });
  a.track("device_serial", { serial: "1234" });
  await a.ready;
  assert.deepEqual(ph.captured, []);
});

// a window stand-in: its listeners, its document's focused element, and timers run by hand
function fakeWindow() {
  const listeners = {}, timers = [];
  const win = {
    document: { activeElement: null },
    addEventListener: (t, f) => { (listeners[t] ||= []).push(f); },
    removeEventListener: (t, f) => { listeners[t] = (listeners[t] || []).filter((g) => g !== f); },
    setTimeout: (f) => { timers.push(f); },
    fire: (t) => { for (const f of [...(listeners[t] || [])]) f({ type: t }); for (const f of timers.splice(0)) f(); },
    listeners,
  };
  return win;
}

test("emulator_start: the first time focus goes into the emulator's iframe, once", () => {
  const win = fakeWindow(), iframe = { tagName: "IFRAME" }, got = [];
  A.watchEmulatorStart(win, iframe, (e) => got.push(e));
  win.document.activeElement = { tagName: "BUTTON" };
  win.fire("blur");                                  // focus left for another tab or window: not the emulator
  assert.deepEqual(got, []);
  win.document.activeElement = iframe;
  win.fire("blur");                                  // a click into the emulator (its Start button)
  win.fire("blur");
  assert.deepEqual(got, ["emulator_start"]);
  assert.equal((win.listeners.blur || []).length, 0);
});

test("emulator_start: a track that throws, or no iframe, changes nothing", () => {
  const win = fakeWindow(), iframe = {};
  A.watchEmulatorStart(win, iframe, () => { throw new Error("broke"); });
  win.document.activeElement = iframe;
  assert.doesNotThrow(() => win.fire("blur"));
  assert.doesNotThrow(() => A.watchEmulatorStart(fakeWindow(), null, () => {}));
  assert.doesNotThrow(() => A.watchEmulatorStart(undefined, undefined, undefined));
});
