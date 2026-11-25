// SPDX-License-Identifier: GPL-3.0-or-later
// The install section of FM1-8P's page (#356): the buttons, wired to site/installer.js (the logic, tested in
// tests/js) and fm1-dock's vendored installer (vendor/fm1-dock, unmodified). Web MIDI with SysEx: Chrome or Edge.
import { entryFor, checkPackage, installOn, addModule, explain } from "./vendor/fm1-dock/install/fm1install.js";
import { moduleInfo } from "./vendor/fm1-dock/dock.js";
import { webMidiSupport, loadCatalog, firmwareEntry, installLabel, installFlow, addFlow, messageFor, oneAtATime,
  buttonsEnabled } from "./installer.js";

const base = new URL("./", location.href);
const $ = (id) => document.getElementById(id);
const fwBtn = $("fm1-install-btn"), fwBar = $("fm1-install-bar"), fwSay = $("fm1-install-status");
const dockBtn = $("fm1-add-btn"), dockBar = $("fm1-add-bar"), dockSay = $("fm1-add-status");
const support = webMidiSupport(navigator);
const guard = oneAtATime();
const ask = async (q) => window.confirm(q);
const requestMIDIAccess = (opts) => navigator.requestMIDIAccess(opts);
let catalogOk = false;

// the usage stats (site/analytics.js), loaded so that nothing here depends on them: blocked, failing to load or
// throwing, the page works the same. Until they are there, track drops what it gets.
let stats = null;
const track = (event, props) => { try { if (stats) stats.track(event, props); } catch (_) { /* never ours */ } };
const iframe = document.querySelector("#play iframe");
import("./analytics.js").then((m) => {
  stats = m.createAnalytics();
  m.watchEmulatorStart(window, iframe, track);
}).catch(() => {});

function setBusy(busy) {
  fwBtn.disabled = dockBtn.disabled = !buttonsEnabled({ midi: support.ok, catalogOk, busy });
}

// a status line and its progress bar: progress(value or null, text)
function reporter(bar, line) {
  return {
    say: (t) => { line.textContent = t; },
    progress: (value, text) => {
      if (value !== null) { bar.hidden = false; bar.value = value; }
      if (text) line.textContent = text;
    },
  };
}

addEventListener("beforeunload", (e) => { if (guard.busy) { e.preventDefault(); e.returnValue = ""; } });

if (!support.ok) {
  $("fm1-browser").textContent = support.message;
  $("fm1-browser").hidden = false;
}
setBusy(false);

loadCatalog(fetch, base).then((cat) => {
  $("fm1-package").textContent = `This page installs ${installLabel(firmwareEntry(cat))}.`;
  catalogOk = true;
}).catch((e) => {
  $("fm1-package").textContent = messageFor(e, explain);
}).finally(() => setBusy(guard.busy));

fwBtn.addEventListener("click", () => guard.run(async () => {
  setBusy(true);
  fwBar.hidden = true; fwBar.value = 0;
  const { say, progress } = reporter(fwBar, fwSay);
  say(await installFlow({ requestMIDIAccess, fetch, base, lib: { entryFor, checkPackage, installOn }, ask, say, progress, explain, track }));
  setBusy(false);
}));

dockBtn.addEventListener("click", () => guard.run(async () => {
  setBusy(true);
  dockBar.hidden = true; dockBar.value = 0;
  const { say, progress } = reporter(dockBar, dockSay);
  say(await addFlow({ requestMIDIAccess, fetch, base, lib: { moduleInfo, addModule }, ask, say, progress, explain, track }));
  setBusy(false);
}));
