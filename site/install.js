// SPDX-License-Identifier: GPL-3.0-only
// The install section of FM1-8P's page (#356): the buttons, wired to site/installer.js (the logic, tested in
// tests/js) and fm1-dock's vendored installer (vendor/fm1-dock, unmodified). Web MIDI with SysEx: Chrome or Edge.
import { entryFor, checkPackage, installOn, addModule, explain } from "./vendor/fm1-dock/install/fm1install.js";
import { moduleInfo } from "./vendor/fm1-dock/dock.js";
import { webMidiSupport, loadCatalog, firmwareEntry, installLabel, installFirmware, addToDock, messageFor, oneAtATime }
  from "./installer.js";

const base = new URL("./", location.href);
const $ = (id) => document.getElementById(id);
const fwBtn = $("fm1-install-btn"), fwBar = $("fm1-install-bar"), fwSay = $("fm1-install-status");
const dockBtn = $("fm1-add-btn"), dockBar = $("fm1-add-bar"), dockSay = $("fm1-add-status");
const support = webMidiSupport(navigator);
const guard = oneAtATime();
const ask = async (q) => window.confirm(q);

async function midiAccess() {
  try { return await navigator.requestMIDIAccess({ sysex: true }); } catch (_) {
    throw Object.assign(new Error("MIDI not allowed"), { code: "denied" });
  }
}

function setBusy(busy) {
  fwBtn.disabled = dockBtn.disabled = busy || !support.ok;
}

addEventListener("beforeunload", (e) => { if (guard.busy) { e.preventDefault(); e.returnValue = ""; } });

if (!support.ok) {
  $("fm1-browser").textContent = support.message;
  $("fm1-browser").hidden = false;
}
setBusy(false);

loadCatalog(fetch, base).then((cat) => {
  $("fm1-package").textContent = `This page installs ${installLabel(firmwareEntry(cat))}.`;
}).catch((e) => {
  $("fm1-package").textContent = messageFor(e, explain);
  fwBtn.disabled = dockBtn.disabled = true;
});

fwBtn.addEventListener("click", () => guard.run(async () => {
  setBusy(true);
  fwBar.hidden = true; fwBar.value = 0;
  try {
    const midi = await midiAccess();
    fwSay.textContent = "Looking for the FM-1 …";
    let reads = 1;
    const step = (k, n) => {
      if (k === "verify") { fwBar.hidden = false; fwBar.value = Math.min(10, n / 8); }
      if (k === "write") { fwBar.hidden = false; fwBar.value = 10 + Math.min(89, (89 * n) / reads); }
      if (k === "done") { fwBar.value = 100; return; }
      const said = explain({ code: k, message: "" });
      if (!said.startsWith("Error:")) fwSay.textContent = said + (k === "write" ? ` (${Math.round(fwBar.value)} %)` : "");
    };
    const lib = { entryFor, installOn,
      checkPackage: async (raw, entry) => { const pkg = await checkPackage(raw, entry); reads = Math.ceil(pkg.image.length / 512); return pkg; } };
    const got = await installFirmware({ midi, fetch, base, lib, ask, step, say: (t) => { fwSay.textContent = t; } });
    fwSay.textContent = got === null ? "Cancelled: nothing was written." : `Done: the FM-1 runs FM1-8P (${got}).`;
  } catch (e) {
    fwSay.textContent = messageFor(e, explain);
    console.error(e);
  } finally { setBusy(false); }
}));

dockBtn.addEventListener("click", () => guard.run(async () => {
  setBusy(true);
  dockBar.hidden = true; dockBar.value = 0;
  try {
    const midi = await midiAccess();
    dockSay.textContent = "Looking for fm1-dock on the FM-1 …";
    const got = await addToDock({ midi, fetch, base, lib: { moduleInfo, addModule }, ask,
      progress: (x, t) => { dockBar.hidden = false; dockBar.value = Math.round(x * 100); dockSay.textContent = `Sending: ${t}`; } });
    dockSay.textContent = got === null ? "Cancelled: nothing was sent."
      : `Done: ${got} is in fm1-dock. Hold HOME on the FM-1 and pick it.`;
  } catch (e) {
    dockSay.textContent = messageFor(e, explain);
    console.error(e);
  } finally { setBusy(false); }
}));
