// SPDX-License-Identifier: GPL-3.0-or-later
// The page's cookie-free usage stats, through PostHog: page views, emulator starts, and whether installs succeed
// or fail. Nothing about the FM-1 beyond success or failure (an error class from the allow-list below).
// The page must work the same when PostHog is blocked, fails to load or throws: site/install.js loads this module
// with a dynamic import and a catch, and track() never throws and returns nothing.
// The PostHog library (site/posthog-js, self-hosted) is passed in as load(), so tests/js runs this in node; the page
// uses loadPosthog.

export const SITE = "fm1-8p";
export const TOKEN = "phc_uzsUUTbN2k3EJVDvtTGBtrRgPmdWrRxzxbLZcsw7Vvm4";   // the project's public ingest key
export const API_HOST = "https://us.i.posthog.com";

// posthog.init's config. Cookieless: cookieless_mode "always" (https://posthog.com/docs/libraries/js/config):
// no cookie, no localStorage or sessionStorage identity, the server hashes the visitor (on in the project).
// The project has recording, console logs and heatmaps on: each is turned off here, in the init.
export function initConfig() {
  return {
    api_host: API_HOST,
    cookieless_mode: "always",
    persistence: "memory",
    person_profiles: "never",
    disable_session_recording: true,
    enable_recording_console_log: false,
    autocapture: false,
    capture_heatmaps: false,          // a no-op in the slim bundle (no heatmaps in it); kept in case it ever has them
    disable_surveys: true,
    capture_pageview: true,
    capture_pageleave: false,
    capture_dead_clicks: false,
    capture_exceptions: false,
    capture_performance: false,
    rageclick: false,
    disable_external_dependency_loading: true,
    advanced_disable_flags: true,
    mask_personal_data_properties: true,   // PostHog masks the ad click IDs; before_send also drops them
    before_send: tagged,
  };
}

// PostHog's library, self-hosted (site/posthog-js/VENDORED.md): its slim core, which never loads other scripts
export async function loadPosthog() {
  return (await import("./posthog-js/module.slim.no-external.js")).default;
}

// every event PostHog sends, its own page view too, carries site: fm1-8p, and the page's URL with only the five
// standard utm_ parameters (no other query parameter, no fragment), and the referrer cut to its origin. The ad click IDs
// are dropped. $raw_user_agent stays: the browser.
const URLS = ["$current_url", "$session_entry_url", "$initial_current_url"];
// the campaign keys posthog-js 1.447.10 reads from the URL (its campaign list) besides the five utm_ ones: ad click
// IDs and the like, never sent (mask_personal_data_properties masks them too; this holds if that option changes)
export const CAMPAIGN_DROPPED = ["gad_source", "mc_cid", "gclid", "gclsrc", "dclid", "gbraid", "wbraid", "fbclid",
  "msclkid", "twclid", "li_fat_id", "igshid", "ttclid", "rdt_cid", "epik", "qclid", "sccid", "oppref", "bfcid", "irclid",
  "_kx"];
function tagged(event) {
  try {
    if (!event) return event;
    const props = { ...event.properties, site: SITE };
    for (const k of CAMPAIGN_DROPPED) delete props[k];
    for (const k of Object.keys(props)) if (k.startsWith("utm_") && !UTM.includes(k)) delete props[k];
    for (const k of URLS) if (k in props) props[k] = utmOnly(props[k]);
    for (const k of REFERRERS) if (k in props) props[k] = originOnly(props[k]);
    event.properties = props;
  } catch (_) { /* send it as it is */ }
  return event;
}

// the referrer: its origin only (the site it came from, not the page); PostHog's "$direct" as it is; undefined for
// one that cannot be read
const REFERRERS = ["$referrer", "$initial_referrer"];
function originOnly(ref) {
  if (typeof ref === "string" && ref.startsWith("$")) return ref;
  try { const o = new URL(ref).origin; return o && o !== "null" ? o : undefined; } catch (_) { return undefined; }
}

// a URL with only the five standard utm_ parameters and no fragment; undefined for one that cannot be read. The
// keys are compared as written, so an encoded lookalike (%75tm_source) is dropped, not decoded into one.
export const UTM = ["utm_source", "utm_medium", "utm_campaign", "utm_content", "utm_term"];
function utmOnly(url) {
  let u;
  try { u = new URL(url); } catch (_) { return undefined; }
  const keep = u.search.slice(1).split("&").filter((pair) => UTM.includes(pair.split("=", 1)[0]));
  u.search = keep.length ? "?" + keep.join("&") : "";
  u.hash = "";
  return u.href;
}

// our events: the only ones track() sends
export const EVENTS = ["emulator_start", "install_started", "install_succeeded", "install_failed",
  "dock_add_started", "dock_add_succeeded", "dock_add_failed"];
const FAILED = ["install_failed", "dock_add_failed"];

// what a failure may say: the installer's error codes (site/installer.js, fm1-dock's install/fm1install.js and
// install/fm1ota.js) and canceled. Anything else is "other": never a message, a serial or device bytes.
export const ERROR_CLASSES = ["canceled", "identity", "nocatalog", "denied", "notfound", "inuse", "model", "nosmk",
  "foreign", "stopped", "lost", "noloader", "noreturn", "mismatch", "badpkg", "badreq", "addonsota", "addons",
  "nodock", "abi", "space", "other"];

// an event's properties: the site tag, and for a failure its error class; nothing the caller passed besides
export function eventProps(event, props) {
  const out = { site: SITE };
  if (FAILED.includes(event)) {
    const e = props && props.error;
    out.error = typeof e === "string" && ERROR_CLASSES.includes(e) ? e : "other";
  }
  return out;
}

// the stats: load() gives the PostHog library (a promise); track(event, props) never throws, returns nothing and
// never makes its caller wait. Events tracked before the library is there are kept (at most MAX) and sent after.
// ready: a promise that settles (never rejects) once the library is loaded or has failed to.
const MAX = 50;
export function createAnalytics({ load = loadPosthog } = {}) {
  let ph = null, dead = false;
  const queue = [];
  const send = (event, props) => { try { ph.capture(event, eventProps(event, props)); } catch (_) { /* dropped */ } };
  const ready = (async () => {
    try {
      const lib = await load();
      lib.init(TOKEN, initConfig());
      ph = lib;
      for (const [event, props] of queue.splice(0)) send(event, props);
    } catch (_) {
      dead = true;
      queue.length = 0;
    }
  })();
  function track(event, props) {
    try {
      if (dead || !EVENTS.includes(event)) return;
      if (ph) send(event, props);
      else if (queue.length < MAX) queue.push([event, props]);
    } catch (_) { /* never the caller's problem */ }
  }
  return { track, ready };
}

// emulator_start: fm1-emu is a cross-origin iframe, so its Start button cannot be seen from here. The proxy: the
// first time the window loses focus to the iframe (a blur while document.activeElement is the iframe), which a
// click or tap into the emulator does. Once per page load; never throws.
export function watchEmulatorStart(win, iframe, track) {
  try {
    if (!win || !iframe || typeof track !== "function") return;
    const onBlur = () => {
      win.setTimeout(() => {                       // activeElement is the iframe once the blur has run
        try {
          if (win.document.activeElement !== iframe) return;
          win.removeEventListener("blur", onBlur);
          track("emulator_start");
        } catch (_) { /* never the page's problem */ }
      }, 0);
    };
    win.addEventListener("blur", onBlur);
  } catch (_) { /* never the page's problem */ }
}
