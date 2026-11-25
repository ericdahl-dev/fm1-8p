// installFirmware with fm1-dock's real checkPackage (site/vendor/fm1-dock) on small synthetic packages: a package of
// another identity never reaches installOn, ours does (#362 review M3).
import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { checkPackage, productOf } from "../../site/vendor/fm1-dock/install/fm1install.js";
import * as I from "../../site/installer.js";

const BASE = "https://example.test/fm1-8p/";

function crc16(d) {                              // CRC-16/XMODEM, as the package and the FM-1 use
  let crc = 0;
  for (const b of d) {
    crc ^= b << 8;
    for (let k = 0; k < 8; k++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}

function enc(b) {                                // JieLi's ENC stream cipher (its own inverse)
  const out = Uint8Array.from(b);
  let key = 0xffff;
  for (let i = 0; i < out.length; i++) {
    out[i] ^= key & 0xff;
    key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xffff;
  }
  return out;
}

const put16 = (b, o, v) => { b[o] = v & 0xff; b[o + 1] = (v >> 8) & 0xff; };
const put32 = (b, o, v) => { for (let i = 0; i < 4; i++) b[o + i] = (v >>> (8 * i)) & 0xff; };

// a package-shaped file: header, one file (with the update loader's mark) and its CRCs, and the identity markers
export function fakePackage(identity) {
  const file = new Uint8Array(1200);
  file.set(new TextEncoder().encode("FELUCCA-LOADER-1"), 100);
  const entry = new Uint8Array(0x50);
  put16(entry, 0, 1);
  put16(entry, 4, crc16(file));
  put32(entry, 8, 0x40 + 0x50);
  put32(entry, 12, file.length);
  entry.set(new TextEncoder().encode("app.bin"), 0x40);
  const list = enc(entry);
  const hdr = new Uint8Array(0x40);
  put16(hdr, 2, crc16(list));
  put16(hdr, 8, 1);
  put16(hdr, 0, crc16(hdr.subarray(2)));
  const image = new Uint8Array([...enc(hdr), ...list, ...file]);
  const raw = [];
  for (let i = 0; i < 20; i++) {
    raw.push(...image.subarray(i * 0x2f, (i + 1) * 0x2f));
    raw.push(i < identity.length ? (identity.charCodeAt(i) + i + 1) & 0xff : 0x7d);
  }
  raw.push(...image.subarray(20 * 0x2f));
  return Uint8Array.from(raw);
}

function setup(identity) {
  const raw = fakePackage(identity);
  const entry = { name: "FM1-8P", version: "abc1234", file: "fm1-8p.fwsc", size: raw.length, product: "FM-1_948",
    sha256: createHash("sha256").update(raw).digest("hex") };
  const files = { "fw/index.json": { firmwares: { "fm1-8p": entry } }, "fw/fm1-8p.fwsc": raw };
  const fetch = async (url) => {
    const body = files[String(url).slice(BASE.length)];
    return body ? { ok: true, status: 200, json: async () => body, arrayBuffer: async () => body.buffer } : { ok: false, status: 404 };
  };
  const calls = [];
  const lib = { entryFor: async (midi, e) => e, checkPackage,
    installOn: async (midi, pkg, name) => { calls.push({ product: pkg.product, name }); return pkg.product; } };
  return { fetch, lib, calls };
}

test("the synthetic package passes fm1-dock's own check (identity, loader, CRCs)", async () => {
  const raw = fakePackage("FM-1_948");
  assert.equal(productOf(raw), "FM-1_948");
  assert.equal((await checkPackage(raw, { file: "x.fwsc", product: "FM-1_948" })).product, "FM-1_948");
});

test("an FM-1_948 package reaches installOn, named with its identity", async () => {
  const { fetch, lib, calls } = setup("FM-1_948");
  const got = await I.installFirmware({ midi: {}, fetch, base: BASE, lib, ask: async () => true, say: () => {}, step: () => {} });
  assert.equal(got, "FM-1_948");
  assert.deepEqual(calls, [{ product: "FM-1_948", name: "FM1-8P abc1234 (firmware identity FM-1_948)" }]);
});

test("an FM-1_921 package never reaches installOn", async () => {
  const { fetch, lib, calls } = setup("FM-1_921");
  await assert.rejects(I.installFirmware({ midi: {}, fetch, base: BASE, lib, ask: async () => true, say: () => {}, step: () => {} }),
    (e) => e.code === "badpkg" && /FM-1_921/.test(e.message));
  assert.deepEqual(calls, []);
});
