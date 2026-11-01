/* antirazer web — single-file configurator for the Naga LH 2020 and HP 300.
 *
 * No build, no modules: the whole app is this classic script. Sections:
 *   1. constants, keys, helpers        5. inspector + settings
 *   2. device descriptors + protocol   6. queue + JSON + push
 *   3. Naga driver                     7. boot + self-tests
 *   4. HP driver
 */
(() => {
'use strict';

/* ====================================================================== *
 * 1. constants, keys, helpers
 * ====================================================================== */

const VID_NAGA = 0x1532, PID_NAGA = 0x008d;
const VID_HP = 0x03f0, PID_HP = 0x2941;

const NAGA_LEN = 90, NAGA_TID = 0x1f;
const HP_SET_ADDR = 0xbe00, HP_SET_LEN = 0x33, HP_SET_PAYLOAD = 0x27;
const HP_SEC_BASE = 0xc000, HP_SEC_COUNT = 8;

const QUEUE_KEY = 'antirazer.queue.v1';
const BACKUP_KEY = 'antirazer.backup.v1';
const PROFILES_KEY = 'antirazer.profiles.v1';
const ACTIVE_KEY = 'antirazer.device';

/* HID usages, ported from src/naga_keys.c */
const KEYS = {
  enter: 0x28, esc: 0x29, backspace: 0x2a, tab: 0x2b, space: 0x2c, caps: 0x39,
  print: 0x46, scroll: 0x47, pause: 0x48, insert: 0x49, home: 0x4a, pgup: 0x4b,
  delete: 0x4c, end: 0x4d, pgdn: 0x4e, right: 0x4f, left: 0x50, down: 0x51,
  up: 0x52, minus: 0x2d, equal: 0x2e, lbracket: 0x2f, rbracket: 0x30,
  bslash: 0x31, hash: 0x32, semicolon: 0x33, quote: 0x34, grave: 0x35,
  comma: 0x36, dot: 0x37, slash: 0x38, numlock: 0x53, kpdiv: 0x54, kpmul: 0x55,
  kpsub: 0x56, kpadd: 0x57, kpenter: 0x58, kp1: 0x59, kp2: 0x5a, kp3: 0x5b,
  kp4: 0x5c, kp5: 0x5d, kp6: 0x5e, kp7: 0x5f, kp8: 0x60, kp9: 0x61, kp0: 0x62,
  kpdot: 0x63, kpequal: 0x67, kpcomma: 0x85, application: 0x65, power: 0x66,
  mute: 0x7f, volup: 0x80, voldown: 0x81, next: 0xb5, prev: 0xb6, stop: 0xb7,
  eject: 0xb8, playpause: 0xcd,
};
for (let i = 0; i < 26; i++) KEYS[String.fromCharCode(97 + i)] = 0x04 + i;
for (let i = 0; i < 10; i++) KEYS['' + ((i + 1) % 10)] = 0x1e + i;
for (let i = 0; i < 12; i++) KEYS['f' + (i + 1)] = 0x3a + i;
for (let i = 0; i < 12; i++) KEYS['f' + (i + 13)] = 0x68 + i;

const KEYN = {};
for (const [n, v] of Object.entries(KEYS)) if (!(v in KEYN)) KEYN[v] = n;

const MODN = ['ctrl', 'shift', 'alt', 'super', 'rctrl', 'rshift', 'ralt', 'rsuper'];
const HP_MODN = ['ctrl', 'shift', 'alt', 'win'];
const NAGA_MOUSE = { 1: 'left', 2: 'right', 3: 'middle', 4: 'back', 5: 'forward' };
const HP_MOUSE = { 1: 'left', 2: 'right', 4: 'middle', 8: 'back', 16: 'forward' };
const HP_MOUSE_ID = { left: 1, right: 2, middle: 4, back: 8, forward: 16 };

const hex = (n) => '0x' + (n >>> 0).toString(16).padStart(2, '0');
const clip = (s, n) => (s && s.length > n ? s.slice(0, n - 1) + '…' : s || '');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const $ = (id) => document.getElementById(id);

function debounce(fn, ms) {
  let timer;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), ms);
  };
}

function withTimeout(promise, ms, message) {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(message)), ms);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

function keyName(u) {
  if (u >= 0x04 && u <= 0x1d) return String.fromCharCode(97 + u - 4);
  if (u >= 0x1e && u <= 0x27) return '' + ((u - 0x1d) % 10);
  if (u >= 0x3a && u <= 0x45) return 'f' + (u - 0x39);
  if (u >= 0x68 && u <= 0x73) return 'f' + (u - 0x68 + 13);
  return KEYN[u] || hex(u);
}

function parseUsage(s) {
  s = String(s || '').trim().toLowerCase();
  if (/^0x[0-9a-f]+$/.test(s)) return parseInt(s, 16);
  if (s in KEYS) return KEYS[s];
  throw new Error('unknown key "' + s + '" (name or 0xNN)');
}

/* "ctrl+shift+t" -> { mod, usage }. `mask` limits the modifier names. */
function parseCombo(s, mask) {
  const parts = String(s || '').split('+').map((p) => p.trim()).filter(Boolean);
  if (!parts.length) throw new Error('empty key combination');
  const key = parts.pop();
  let mod = 0;
  for (const p of parts) {
    let i = -1;
    const lower = p.toLowerCase();
    if (lower === 'win') i = 3;
    else i = MODN.indexOf(lower);
    if (i < 0) throw new Error('unknown modifier "' + p + '"');
    const bit = 1 << i;
    if (mask != null && !(mask & bit)) throw new Error('modifier "' + p + '" is not supported here');
    mod |= bit;
  }
  return { mod, usage: parseUsage(key) };
}

function comboString(mod, usage, names) {
  names = names || MODN;
  let s = '';
  for (let i = 0; i < names.length; i++)
    if (mod & (1 << i)) s += names[i] + '+';
  return s + keyName(usage);
}

function h(tag, attrs, ...children) {
  const e = document.createElement(tag);
  if (attrs) for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'text') e.textContent = v;
    else if (k.startsWith('on') && typeof v === 'function') e.addEventListener(k.slice(2), v);
    else if (k === 'dataset') Object.assign(e.dataset, v);
    else if (k === 'value' || k === 'checked' || k === 'selected') e[k] = v;
    else e.setAttribute(k, v);
  }
  for (const c of children.flat()) {
    if (c == null || c === false) continue;
    e.append(c.nodeType ? c : document.createTextNode(String(c)));
  }
  return e;
}

const SVGNS = 'http://www.w3.org/2000/svg';
function sn(tag, attrs, text) {
  const e = document.createElementNS(SVGNS, tag);
  if (attrs) for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') e.setAttribute('class', v);
    else if (k === 'text') e.textContent = v;
    else e.setAttribute(k, v);
  }
  if (text != null) e.textContent = text;
  return e;
}

function bytesToHex(bytes) {
  return Array.from(bytes, (x) => x.toString(16).padStart(2, '0')).join('');
}
function hexToBytes(s) {
  const out = new Uint8Array(s.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(s.slice(2 * i, 2 * i + 2), 16);
  return out;
}

function readStore(key, fallback) {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : fallback;
  } catch (e) { return fallback; }
}
function writeStore(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); } catch (e) { /* quota/private mode */ }
}

function defaultQueue() {
  return { version: 1, naga: { readAt: 0, edits: [] }, hp: { readAt: 0, edits: [] } };
}

function defaultProfiles() {
  return { version: 1, naga: [], hp: [], selected: { naga: null, hp: null } };
}

function loadProfiles() {
  const p = readStore(PROFILES_KEY, null) || {};
  const out = defaultProfiles();
  for (const k of ['naga', 'hp']) {
    if (!Array.isArray(p[k])) continue;
    out[k] = p[k].filter((prof) =>
      prof && typeof prof.id === 'string' && typeof prof.name === 'string' &&
      prof.data && typeof prof.data === 'object');
  }
  if (p.selected && typeof p.selected === 'object') {
    for (const k of ['naga', 'hp'])
      if (typeof p.selected[k] === 'string' &&
          out[k].some((prof) => prof.id === p.selected[k]))
        out.selected[k] = p.selected[k];
  }
  return out;
}

function loadQueue() {
  const q = readStore(QUEUE_KEY, null) || {};
  const out = defaultQueue();
  for (const k of ['naga', 'hp']) {
    if (q[k] && Array.isArray(q[k].edits)) {
      out[k].readAt = q[k].readAt || 0;
      out[k].edits = q[k].edits;
    }
  }
  return out;
}

const state = {
  active: readStore(ACTIVE_KEY, null) || 'naga',
  connected: null,
  hid: { key: null, dev: null, listener: null },
  baseline: { naga: null, hp: null },
  layers: { naga: 0, hp: 0 },
  selected: { naga: '1', hp: 'sector-3' },
  dpi: { x: 1600, y: 1600 },
  queue: loadQueue(),
  backups: readStore(BACKUP_KEY, {}),
  profiles: loadProfiles(),
  profileSelected: { naga: null, hp: null },
  profileSig: null,
  selfTest: [],
  ready: false,
};
state.profileSelected = Object.assign({ naga: null, hp: null }, state.profiles.selected || {});
if (!state.queue[state.active]) state.active = 'naga';
if (!state.layers[state.active]) state.layers[state.active] = 0;

/* ---- device control descriptors -------------------------------------- */

const NAGA_THUMB_LABELS = ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '='];
const NAGA_THUMB_CTRS = [0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b];

const NAGA_TOP = [
  { id: 'left', name: 'left', ctr: 0x01, json: 'left', rect: [82, 10, 38, 58], label: 'L' },
  { id: 'right', name: 'right', ctr: 0x02, json: 'right', rect: [154, 10, 38, 58], label: 'R' },
  { id: 'wheel-up', name: 'wheel up', host: true, json: 'wheel-up', rect: [122, 10, 30, 16], label: '^' },
  { id: 'middle', name: 'middle', ctr: 0x03, json: 'middle', rect: [124, 28, 26, 24], label: 'M' },
  { id: 'wheel-down', name: 'wheel down', host: true, json: 'wheel-down', rect: [122, 54, 30, 14], label: 'v' },
  { id: 'dpi-up', name: 'DPI up', ctr: 0x0b, json: 'dpi-up', rect: [122, 74, 30, 16], label: 'DPI+' },
  { id: 'dpi-down', name: 'DPI down', ctr: 0x0c, json: 'dpi-down', rect: [122, 94, 30, 16], label: 'DPI-' },
];

const NAGA_EXTRA = [
  { id: 'mb9', name: 'mb9', ctr: 0x09, json: '09', list: true },
  { id: 'mb10', name: 'mb10', ctr: 0x0a, json: '0a', list: true },
  { id: 'profile', name: 'profile', ctr: 0x0e, json: '0e', list: true },
  { id: 'tilt-right', name: 'tilt right', ctr: 0x34, json: '34', list: true },
  { id: 'tilt-left', name: 'tilt left', ctr: 0x35, json: '35', list: true },
];

const NAGA_THUMB = NAGA_THUMB_LABELS.map((label, i) => ({
  id: label, name: label, ctr: NAGA_THUMB_CTRS[i], json: label, thumb: i,
  rect: [(i % 3) * 94, 20 + Math.floor(i / 3) * 60, 86, 52], label,
}));

const NAGA_CONTROLS = [...NAGA_TOP, ...NAGA_THUMB, ...NAGA_EXTRA];
const NAGA_BY_ID = Object.fromEntries(NAGA_CONTROLS.map((c) => [c.id, c]));
const NAGA_BY_JSON = Object.fromEntries(NAGA_CONTROLS.map((c) => [c.json, c]));

const HP_SECTORS = [
  { n: 0, physical: 'left?', known: false, note: 'assumed' },
  { n: 1, physical: 'right?', known: false, note: 'assumed' },
  { n: 2, physical: 'middle?', known: false, note: 'assumed' },
  { n: 3, physical: '1 (top left)', known: true, note: 'confirmed' },
  { n: 4, physical: 'side button?', known: false, note: 'unconfirmed' },
  { n: 5, physical: '4 / DPI (bottom right)', known: true, note: 'confirmed' },
  { n: 6, physical: 'side button?', known: false, note: 'unconfirmed' },
  { n: 7, physical: 'side button?', known: false, note: 'unconfirmed' },
];

const DEVICES = {
  naga: {
    key: 'naga',
    label: 'Razer Naga LH 2020',
    short: 'Naga LH 2020',
    jsonDevice: 'naga-2020',
    match: { vendorId: '1532', productId: '008d' },
    filter: { vendorId: VID_NAGA, productId: PID_NAGA },
    controls: NAGA_CONTROLS,
    readAll: nagaReadAll,
    applyEdit: nagaApplyEdit,
    backupData: nagaBackupData,
    settingsForJson: nagaSettingsForJson,
    summary: (b) => b ? Object.keys(b.records).length + ' controls, 2 layers' : 'no data',
  },
  hp: {
    key: 'hp',
    label: 'HP Pavilion Gaming Mouse 300',
    short: 'HP Pavilion 300',
    jsonDevice: 'hp-300',
    match: { vendorId: '03f0', productId: '2941' },
    filter: { vendorId: VID_HP, productId: PID_HP, usagePage: 0xff32 },
    controls: HP_SECTORS,
    readAll: hpReadAll,
    applyEdit: hpApplyEdit,
    backupData: hpBackupData,
    settingsForJson: hpSettingsForJson,
    summary: (b) => b ? 'settings + ' + HP_SEC_COUNT + ' sectors' : 'no data',
  },
};

/* ====================================================================== *
 * 2. Naga + HP protocol and actions
 * ====================================================================== */

state.hp = { pending: null, status: null };

/* ---- Naga: 90-byte feature reports on interface 0 -------------------- */

function nagaFrame(cls, id, size, args) {
  const b = new Uint8Array(NAGA_LEN);
  b[1] = NAGA_TID;
  b[5] = size;
  b[6] = cls;
  b[7] = id == null ? 0 : id;
  b.set(args || [], 8);
  let crc = 0;
  for (let i = 2; i < 88; i++) crc ^= b[i];
  b[88] = crc;
  return b;
}

async function nagaTransact(cls, id, size, args) {
  const dev = state.hid.dev;
  if (!dev) throw new Error('Naga is not connected');
  await dev.sendFeatureReport(0, nagaFrame(cls, id, size, args));
  await sleep(50);
  const view = await withTimeout(dev.receiveFeatureReport(0), 5000,
    'no reply to ' + hex(cls) + '/' + hex(id));
  let r = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
  if (r.length === NAGA_LEN + 1 && r[0] === 0) r = r.subarray(1);
  if (r[0] !== 0x02)
    throw new Error('device answered ' + hex(r[0]) + ' to ' + hex(cls) + '/' + hex(id));
  return r;
}

function nagaParseRecord(a) {
  const n = Math.min(a[4], a.length - 5, 5);
  return { ctr: a[1], layer: a[2], type: a[3], data: Array.from(a.slice(5, 5 + n)) };
}

function nagaRecordBytes(ctr, layer, rec) {
  const b = new Uint8Array(10);
  b[0] = 1; b[1] = ctr; b[2] = layer; b[3] = rec.type; b[4] = rec.data.length;
  b.set(rec.data.slice(0, 5), 5);
  return b;
}

async function nagaGetRec(ctr, layer) {
  const req = [1, ctr, layer, 2];
  let r;
  try {
    r = await nagaTransact(0x02, 0x8c, 0x0a, req);
  } catch (e) {
    await sleep(1000);
    r = await nagaTransact(0x02, 0x8c, 0x0a, req);
  }
  return nagaParseRecord(r.slice(8, 18));
}

async function nagaSetRec(ctr, layer, rec) {
  await nagaTransact(0x02, 0x0c, 0x0a, nagaRecordBytes(ctr, layer, rec));
  await nagaTransact(0x00, 0xb3, 0x01, [0]);
}

async function nagaHello() {
  return nagaTransact(0x01, null, 0x00, []);
}

async function nagaSetDpi(x, y) {
  await nagaTransact(0x04, 0x05, 0x07,
    [0, x & 0xff, x >> 8, y & 0xff, y >> 8, 0, 0]);
  await nagaTransact(0x00, 0xb3, 0x01, [0]);
}

async function nagaReadAll() {
  await nagaHello();
  const records = {};
  let ok = 0;
  for (const c of NAGA_CONTROLS) {
    if (c.host) continue;
    records[c.ctr] = { 0: null, 1: null };
    for (let layer = 0; layer < 2; layer++) {
      try {
        records[c.ctr][layer] = await nagaGetRec(c.ctr, layer);
        ok++;
      } catch (e) { records[c.ctr][layer] = null; }
    }
  }
  if (!ok) throw new Error('no mapping records answered');
  return { records, dpi: { x: state.dpi.x, y: state.dpi.y }, readAt: Date.now() };
}

function nagaRecString(rec) {
  if (!rec) return null;
  const d = rec.data || [];
  switch (rec.type) {
    case 0: return 'default';
    case 1: return 'mouse:' + (NAGA_MOUSE[d[0]] || hex(d[0]));
    case 2: return comboString(d[0] || 0, d[1] || 0);
    case 6: return d[0] === 1 ? 'dpi:up' : d[0] === 2 ? 'dpi:down' : 'dpi:' + hex(d[0]);
    case 0x0b: return 'hypershift';
    case 0x0d: {
      const ms = ((d[2] << 8) | d[3]) ? Math.round(1000000 / ((d[2] << 8) | d[3])) : 0;
      return 'turbo:' + comboString(d[0] || 0, d[1] || 0) + '@' + ms;
    }
    case 0x07: return 'profile';
    case 0x0e: return 'tilt:' + (d[0] === 9 ? 'left' : d[0] === 10 ? 'right' : hex(d[0]));
    default: return 'type ' + hex(rec.type);
  }
}

function nagaParseActionText(text) {
  text = String(text || '').trim();
  const lower = text.toLowerCase();
  if (lower === 'default') return { type: 0, data: [] };
  if (lower === 'hypershift') return { type: 0x0b, data: [1] };
  if (lower.startsWith('mouse:')) {
    const name = lower.slice(6);
    const id = Object.keys(NAGA_MOUSE).find((k) => NAGA_MOUSE[k] === name);
    if (!id) throw new Error('unknown mouse button "' + name + '"');
    return { type: 1, data: [Number(id)] };
  }
  if (lower === 'dpi:up') return { type: 6, data: [1] };
  if (lower === 'dpi:down') return { type: 6, data: [2] };
  if (lower.startsWith('turbo:')) {
    const body = lower.slice(6);
    const at = body.lastIndexOf('@');
    if (at < 0) throw new Error('turbo needs turbo:<key>@<ms>');
    const combo = parseCombo(body.slice(0, at), 0x0f);
    const ms = Number(body.slice(at + 1));
    if (!Number.isFinite(ms) || ms < 1 || ms > 60000)
      throw new Error('turbo delay must be 1..60000 ms');
    const rate = Math.min(0xffff, Math.round(1000000 / ms));
    return { type: 0x0d, data: [combo.mod, combo.usage, rate >> 8, rate & 0xff] };
  }
  const combo = parseCombo(text, 0x0f);
  return { type: 2, data: [combo.mod, combo.usage] };
}

function nagaHostString(b) {
  if (!b) return null;
  if (b.action === 'mouse') return 'mouse:' + (NAGA_MOUSE[b.mouse] || hex(b.mouse));
  return comboString(b.mod || 0, b.usage || 0);
}

function findEdit(key) {
  const q = state.queue[state.active];
  return q ? q.edits.find((e) => e.key === key) || null : null;
}

/* Device values as last read; queue edits must keep these as "before" even
 * when the same control is edited repeatedly. */
function baselineNagaRec(ctr, layer) {
  const b = state.baseline.naga;
  return b && b.records[ctr] ? b.records[ctr][layer] : null;
}

function baselineNagaDpi() {
  const b = state.baseline.naga;
  return b ? { ...b.dpi } : { ...state.dpi };
}

function baselineHpLayer(n, layer) {
  const b = state.baseline.hp;
  return b ? hpParseLayer(b.sectors[n], layer ? 0xfe : 0) : null;
}

function baselineHpSettings() {
  return state.baseline.hp ? state.baseline.hp.settings : null;
}

function nagaEffectiveRec(ctr, layer) {
  const e = findEdit('naga:rec:' + ctr + '@' + layer);
  if (e) return e.after;
  const b = state.baseline.naga;
  return b && b.records[ctr] ? b.records[ctr][layer] : null;
}

function nagaEffectiveHost(id) {
  const e = findEdit('naga:host:' + id);
  if (e) return e.after;
  return null;
}

function nagaEffectiveDpi() {
  const e = findEdit('naga:dpi');
  if (e) return e.after;
  return state.baseline.naga ? state.baseline.naga.dpi : { ...state.dpi };
}

function nagaBindingText(control, layer) {
  if (control.host) return nagaHostString(nagaEffectiveHost(control.id)) || '—';
  return nagaRecString(nagaEffectiveRec(control.ctr, layer)) || '—';
}

async function nagaApplyEdit(dev, edit) {
  if (edit.target.kind === 'host') return;
  if (edit.target.kind === 'dpi') {
    await nagaSetDpi(edit.after.x, edit.after.y);
    state.dpi = { ...edit.after };
    if (state.baseline.naga) state.baseline.naga.dpi = { ...edit.after };
    return;
  }
  await nagaSetRec(edit.target.ctr, edit.target.layer, edit.after);
  const b = state.baseline.naga;
  if (b) b.records[edit.target.ctr][edit.target.layer] = edit.after;
}

function nagaBackupData(b) {
  const records = {};
  if (b) for (const c of NAGA_CONTROLS) {
    if (c.host || !b.records[c.ctr]) continue;
    for (const layer of [0, 1]) {
      const rec = b.records[c.ctr][layer];
      if (rec) records[c.ctr + '@' + layer] = bytesToHex(nagaRecordBytes(c.ctr, layer, rec));
    }
  }
  return { records, dpi: b ? b.dpi : { ...state.dpi } };
}

function nagaSettingsForJson() {
  return { dpi: { x: nagaEffectiveDpi().x, y: nagaEffectiveDpi().y } };
}

/* ---- HP: report 4 commands + 512-byte feature sectors ---------------- */

function hpAttach(dev) {
  hpDetach();
  state.hid.listener = (ev) => hpOnInput(ev);
  dev.addEventListener('inputreport', state.hid.listener);
}

function hpDetach() {
  if (state.hid.dev && state.hid.listener) {
    try { state.hid.dev.removeEventListener('inputreport', state.hid.listener); } catch (e) { /* closed */ }
  }
  state.hid.listener = null;
  state.hp.pending = null;
}

function hpOnInput({ reportId, data }) {
  if (reportId !== 4) return;
  const p = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  if (p[0] === 1) state.hp.status = p;
  const q = state.hp.pending;
  if (q && p[0] === q.cmd &&
      (q.addr === null ||
       (p[1] === (q.addr & 0xff) && p[2] === (q.addr >> 8) && p[3] === q.len))) {
    state.hp.pending = null;
    q.resolve(p);
  }
}

async function hpSendCmd(cmd, addr, len, payload) {
  const dev = state.hid.dev;
  if (!dev) throw new Error('HP mouse is not connected');
  const f = new Uint8Array(63);
  f[0] = cmd;
  if (addr !== null) { f[1] = addr & 0xff; f[2] = addr >> 8; }
  f[3] = len;
  if (payload) f.set(payload.slice(0, 59), 4);
  const reply = new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      if (state.hp.pending && state.hp.pending.reject === reject) {
        state.hp.pending = null;
        reject(new Error('no reply to cmd ' + cmd));
      }
    }, 3000);
    state.hp.pending = {
      cmd, addr, len, reject,
      resolve: (p) => { clearTimeout(timer); resolve(p); },
    };
  });
  await dev.sendReport(4, f);
  return reply;
}

const hpHello = () => hpSendCmd(1, null, 0, null);
async function hpReadMem(addr, len) {
  const p = await hpSendCmd(3, addr, len, null);
  return p.slice(4, 4 + len);
}
const hpBeginSession = () => hpSendCmd(2, null, 0, null);
const hpEndSession = () => hpSendCmd(7, null, 0, null);
const hpEraseSector = (n) => hpSendCmd(6, HP_SEC_BASE + n * 0x200, 0, null);
const hpWriteChunk = (addr, chunk) => hpSendCmd(4, addr, chunk.length, chunk);

async function hpWriteSettingsBlock(block) {
  await hpSendCmd(5, HP_SET_ADDR, block.length, block);
}

async function hpFeatureRead(rid) {
  const view = await state.hid.dev.receiveFeatureReport(rid);
  const skip = view.byteLength === 513 ? 1 : 0;
  return new Uint8Array(view.buffer, view.byteOffset + skip, view.byteLength - skip);
}

function hpChecksum(block) {
  let s = 0x8032;
  for (let i = 0; i < 0x25; i++) s += block[i];
  s &= 0xffff;
  block[0x25] = s & 0xff;
  block[0x26] = s >> 8;
  return s;
}

const HP_POLL_TO_CODE = { 1000: 1, 500: 2, 250: 4, 125: 8 };
const HP_CODE_TO_POLL = { 1: 1000, 2: 500, 4: 250, 8: 125 };

function hpParseSettings(raw) {
  const slotCount = Math.max(1, Math.min(raw[5] || 4, 8));
  const slots = [];
  for (let i = 0; i < slotCount; i++) {
    const o = 6 + 5 * i;
    slots.push({
      enabled: !!raw[o],
      level: raw[o + 1],
      color: '#' + [2, 3, 4].map((k) => raw[o + k].toString(16).padStart(2, '0')).join(''),
    });
  }
  return {
    polling: HP_CODE_TO_POLL[raw[0]] || 1000,
    activeSlot: Math.min(raw[1], slotCount - 1),
    slotCount,
    slots,
  };
}

function hpSettingsRaw(baseRaw, plain) {
  const raw = baseRaw ? baseRaw.slice() : new Uint8Array(HP_SET_LEN);
  raw[0] = HP_POLL_TO_CODE[plain.polling] || raw[0] || 1;
  raw[1] = plain.activeSlot | 0;
  raw[5] = plain.slotCount || (plain.slots ? plain.slots.length : 4);
  for (let i = 0; i < plain.slots.length && i < 8; i++) {
    const o = 6 + 5 * i, s = plain.slots[i];
    raw[o] = s.enabled ? 1 : 0;
    raw[o + 1] = s.level & 0xff;
    const v = parseInt(String(s.color || '#000000').replace('#', ''), 16) || 0;
    raw[o + 2] = (v >> 16) & 0xff;
    raw[o + 3] = (v >> 8) & 0xff;
    raw[o + 4] = v & 0xff;
  }
  hpChecksum(raw);
  return raw;
}

function hpParseLayer(sec, off) {
  if (!sec || off + 6 > sec.length) return null;
  const type = sec[off], label = sec[off + 1], mode = sec[off + 2], n = sec[off + 3];
  if (!n || n > 50 || off + 4 + 2 * n > sec.length) return null;
  const tokens = [];
  for (let i = 0; i < n; i++) tokens.push([sec[off + 4 + 2 * i], sec[off + 5 + 2 * i]]);
  return { type, label, mode, tokens };
}

function hpActionString(a) {
  if (!a || !a.tokens || !a.tokens.length) return null;
  const [v, kind] = a.tokens[0];
  if (a.type === 0 && kind === 0x20)
    return 'mouse:' + (HP_MOUSE[v] || hex(v));
  if (a.type === 9 && kind === 0x30)
    return v === 4 ? 'dpi:cycle' : 'device:' + hex(v);
  if (a.type === 6 && kind < 0x10)
    return comboString(kind, v, HP_MODN);
  if (a.type === 8) return 'macro';
  return null;
}

function hpParseActionText(text) {
  text = String(text || '').trim();
  const lower = text.toLowerCase();
  if (lower === 'default') return { type: 0, label: 0, mode: 0, tokens: [] };
  if (lower.startsWith('mouse:')) {
    const name = lower.slice(6);
    if (!(name in HP_MOUSE_ID)) throw new Error('unknown mouse button "' + name + '"');
    return { type: 0, label: 0, mode: 0, tokens: [[HP_MOUSE_ID[name], 0x20]] };
  }
  if (lower === 'dpi:cycle') return { type: 9, label: 0, mode: 0, tokens: [[4, 0x30]] };
  if (lower.startsWith('device:')) {
    const fn = parseInt(lower.slice(7), 16);
    if (!Number.isFinite(fn)) throw new Error('device:<hex> needs a hex function id');
    return { type: 9, label: 0, mode: 0, tokens: [[fn, 0x30]] };
  }
  const combo = parseCombo(text, 0x0f);
  return { type: 6, label: 0x13, mode: 0, tokens: [[combo.usage, combo.mod]] };
}

function hpWriteLayer(sec, off, a) {
  sec.fill(0, off, off + 0xfe);
  if (!a || !a.tokens || !a.tokens.length) return;
  sec[off] = a.type & 0xff;
  sec[off + 1] = a.label & 0xff;
  sec[off + 2] = a.mode & 0xff;
  sec[off + 3] = a.tokens.length;
  for (let i = 0; i < a.tokens.length && i < 60; i++) {
    sec[off + 4 + 2 * i] = a.tokens[i][0] & 0xff;
    sec[off + 5 + 2 * i] = a.tokens[i][1] & 0xff;
  }
}

function hpChecksumSector(sec) {
  let s = 0x8032;
  for (let i = 0; i < 0x1fe; i++) s += sec[i];
  s &= 0xffff;
  sec[0x1fe] = s & 0xff;
  sec[0x1ff] = s >> 8;
  return s;
}

function hpBuildSector(baseSec, layer, action) {
  const sec = baseSec ? baseSec.slice() : new Uint8Array(512);
  hpWriteLayer(sec, layer ? 0xfe : 0, action);
  hpChecksumSector(sec);
  return sec;
}

async function hpReadAll() {
  const status = await hpHello();
  const raw = await hpReadMem(HP_SET_ADDR, HP_SET_LEN);
  let s = 0x8032;
  for (let i = 0; i < 0x25; i++) s += raw[i];
  s &= 0xffff;
  const stored = raw[0x25] | (raw[0x26] << 8);
  if (stored !== s)
    throw new Error('settings checksum mismatch: ' + hex(stored) + ' vs ' + hex(s));
  const sectors = [];
  for (let n = 0; n < HP_SEC_COUNT; n++) sectors.push(await hpFeatureRead(0x10 + n));
  const dpiLevels = [];
  for (let i = 0; i < 8; i++) {
    const p = status || state.hp.status;
    dpiLevels.push(p ? (p[19 + 2 * i] | (p[20 + 2 * i] << 8)) : 0);
  }
  return {
    settings: hpParseSettings(raw),
    settingsRaw: raw,
    sectors,
    dpiLevels,
    readAt: Date.now(),
  };
}

function hpEffectiveSector(n) {
  const b = state.baseline.hp;
  const sec = b && b.sectors[n] ? b.sectors[n].slice() : new Uint8Array(512);
  for (const layer of [0, 1]) {
    const e = findEdit('hp:sector:' + n + '@' + layer);
    if (e) hpWriteLayer(sec, layer ? 0xfe : 0, e.after);
  }
  return sec;
}

function hpEffectiveSettings() {
  const e = findEdit('hp:settings');
  if (e) return e.after;
  return state.baseline.hp ? state.baseline.hp.settings : null;
}

function hpEffectiveLayer(n, layer) {
  return hpParseLayer(hpEffectiveSector(n), layer ? 0xfe : 0);
}

function hpBindingText(n, layer) {
  return hpActionString(hpEffectiveLayer(n, layer)) || '—';
}

async function hpApplyEdit(dev, edit) {
  if (edit.target.kind === 'settings') {
    const b = state.baseline.hp;
    const raw = hpSettingsRaw(b.settingsRaw, edit.after);
    await hpBeginSession();
    await hpWriteSettingsBlock(raw);
    await hpEndSession();
    if (b) { b.settingsRaw = raw; b.settings = hpParseSettings(raw); }
    return;
  }
  const b = state.baseline.hp;
  const sec = hpBuildSector(b.sectors[edit.target.n], edit.target.layer, edit.after);
  await hpBeginSession();
  await hpEraseSector(edit.target.n);
  for (let off = 0; off < 512; off += 59)
    await hpWriteChunk(HP_SEC_BASE + edit.target.n * 0x200 + off, sec.slice(off, off + 59));
  await hpEndSession();
  b.sectors[edit.target.n] = sec;
}

function hpBackupData(b) {
  if (!b) return null;
  return {
    settings: bytesToHex(b.settingsRaw),
    sectors: b.sectors.map(bytesToHex),
  };
}

function hpSettingsForJson() {
  const s = hpEffectiveSettings();
  if (!s) return null;
  return {
    polling: s.polling,
    activeSlot: s.activeSlot,
    slotCount: s.slotCount,
    slots: s.slots.map((x) => ({ enabled: x.enabled, level: x.level, color: x.color })),
  };
}

/* ====================================================================== *
 * 3. wireframes, settings panel, inspector
 * ====================================================================== */

function dev() { return DEVICES[state.active]; }
function activeBaseline() { return state.baseline[state.active]; }

function renderAll() {
  renderHeader();
  renderSketch();
  renderOtherControls();
  renderSettings();
  renderInspector();
  renderQueue();
  renderProfiles();
  updateButtons();
}

/* After an automatic stage: update the parts driven by the queue without
 * rebuilding the editor or the settings form (which would steal focus). */
function refreshAfterEdit() {
  renderSketch();
  renderOtherControls();
  renderQueue();
  updateButtons();
  updateInspectorStatus();
}

function updateInspectorStatus() {
  const cur = $('inspectorCurrent');
  if (!cur) return;
  if (state.active === 'naga') {
    const ctrl = NAGA_BY_ID[state.selected.naga] || NAGA_BY_ID['1'];
    if (!ctrl) return;
    const layer = ctrl.host ? 0 : state.layers.naga;
    const edit = stagedNaga(ctrl, layer);
    const current = ctrl.host ? nagaEffectiveHost(ctrl.id) :
      nagaEffectiveRec(ctrl.ctr, layer);
    const currentText = ctrl.host ? (nagaHostString(current) || '—') :
      (nagaRecString(current) || '—');
    const stagedText = edit ? (ctrl.host ? nagaHostString(edit.after) :
      nagaRecString(edit.after)) : null;
    cur.replaceChildren(
      h('div', null, h('span', { class: 'muted' }, 'now: '),
        h('span', { class: 'now' }, currentText)),
      stagedText != null
        ? h('div', null, h('span', { class: 'muted' }, 'staged: '),
            h('span', { class: 'staged' }, stagedText))
        : null);
    const rev = $('inspectorRevert');
    if (rev) rev.disabled = !edit;
  } else {
    const sel = String(state.selected.hp || 'sector-3');
    const n = Number((sel.match(/^sector-(\d+)$/) || [])[1] ?? 3);
    const edit = findEdit('hp:sector:' + n + '@0');
    const current = hpEffectiveLayer(n, 0);
    const stagedText = edit ? (hpActionString(edit.after) || 'default') : null;
    cur.replaceChildren(
      h('div', null, h('span', { class: 'muted' }, 'primary: '),
        h('span', { class: 'now' }, hpActionString(current) || '—')),
      h('div', null, h('span', { class: 'muted' }, 'shift: '),
        hpBindingText(n, 1)),
      stagedText != null
        ? h('div', null, h('span', { class: 'muted' }, 'staged: '),
            h('span', { class: 'staged' }, stagedText))
        : null);
    const rev = $('inspectorRevert');
    if (rev) rev.disabled = !edit;
  }
}

function renderHeader() {
  for (const b of document.querySelectorAll('#tabs .tab'))
    b.classList.toggle('active', b.dataset.dev === state.active);
  $('deviceTitle').textContent = '— ' + dev().label;
  const warn = state.active === 'naga'
    ? 'WebHID · Naga writes go to flash and persist · close naga-ui / the naga-remap daemon first · the onboard DPI button re-applies factory bindings until the next replug'
    : 'WebHID · HP settings and sectors apply at the next replug (the page cannot re-enumerate the USB device) · the onboard DPI button re-applies factory bindings · the LED is firmware-driven';
  $('warn').textContent = warn;
}

function buttonAttrs(selected, staged, extra) {
  return {
    tabindex: '0',
    role: 'button',
    'aria-pressed': selected ? 'true' : 'false',
    class: 'ctrl' + (selected ? ' selected' : '') + (staged ? ' staged' : '') + (extra ? ' ' + extra : ''),
  };
}

function bindSelect(g, id) {
  g.addEventListener('click', () => selectControl(id));
  g.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); selectControl(id); }
  });
}

function selectControl(id) {
  state.selected[state.active] = id;
  renderAll();
}

function nagaStaged(control) {
  if (control.host) return !!findEdit('naga:host:' + control.id);
  return !!findEdit('naga:rec:' + control.ctr + '@0') || !!findEdit('naga:rec:' + control.ctr + '@1');
}

function makeNagaCell(control, layer, x, y, w, hh) {
  const text = nagaBindingText(control, layer);
  const selected = state.selected.naga === control.id;
  const g = sn('g', buttonAttrs(selected, nagaStaged(control), control.host ? 'host' : ''));
  g.appendChild(sn('rect', { x, y, width: w, height: hh, rx: 5, class: 'face' }));
  const small = hh < 30;
  g.appendChild(sn('text', { x: x + w / 2, y: y + hh / 2 + (small ? 4 : -4) }, control.label));
  if (!small && text !== '—') {
    const t = sn('text', { x: x + w / 2, y: y + hh / 2 + 9, class: 'sub' });
    t.textContent = clip(text, Math.max(5, Math.floor(w / 6)));
    g.appendChild(t);
  }
  const title = sn('title');
  title.textContent = control.name + (control.ctr ? ' (ctr ' + hex(control.ctr) + ')' : '') +
    '\n' + text + (control.host ? '\nhost-side (JSON only)' : '');
  g.appendChild(title);
  if (nagaStaged(control)) g.appendChild(sn('circle', { cx: x + w - 5, cy: y + 5, r: 3, class: 'dot' }));
  bindSelect(g, control.id);
  return g;
}

function renderNagaSketch() {
  const svg = sn('svg', { viewBox: '0 0 548 252', role: 'group', 'aria-label': 'Naga LH 2020' });
  svg.appendChild(sn('text', { x: 0, y: 12, class: 'caption' }, 'thumb panel'));
  svg.appendChild(sn('text', { x: 298, y: 12, class: 'caption' }, 'top'));
  const layer = state.layers.naga;
  for (const c of NAGA_THUMB) {
    const [x, y, w, hh] = c.rect;
    svg.appendChild(makeNagaCell(c, layer, x, y, w, hh));
  }
  const top = sn('g', { transform: 'translate(298,20)' });
  top.appendChild(sn('rect', { x: 82, y: 8, width: 110, height: 174, rx: 26, class: 'body' }));
  for (let row = 0; row < 4; row++) for (let col = 0; col < 3; col++)
    top.appendChild(sn('rect', { x: 148 + col * 13, y: 112 + row * 15, width: 10, height: 11, rx: 2, class: 'hint' }));
  top.appendChild(sn('text', { x: 84, y: 155, class: 'hintText' }, 'thumb panel'));
  for (const c of NAGA_TOP) {
    const [x, y, w, hh] = c.rect;
    top.appendChild(makeNagaCell(c, layer, x, y, w, hh));
  }
  svg.appendChild(top);
  $('sketch').replaceChildren(svg);
}

function hpStaged(n) {
  return !!findEdit('hp:sector:' + n + '@0') || !!findEdit('hp:sector:' + n + '@1');
}

function makeHpCell(n, x, y, w, hh, label, sub) {
  const meta = HP_SECTORS[n];
  const selected = state.selected.hp === 'sector-' + n;
  const g = sn('g', buttonAttrs(selected, hpStaged(n)));
  g.appendChild(sn('rect', { x, y, width: w, height: hh, rx: 5, class: 'face' }));
  g.appendChild(sn('text', { x: x + w / 2, y: y + hh / 2 - 3 }, label));
  if (sub && sub !== '—') {
    const t = sn('text', { x: x + w / 2, y: y + hh / 2 + 9, class: 'sub' });
    t.textContent = clip(sub, Math.max(6, Math.floor(w / 6)));
    g.appendChild(t);
  }
  g.appendChild(sn('circle', {
    cx: x + w - 6, cy: y + 6, r: 2.6,
    class: meta.known ? 'known' : 'unknown',
  }));
  const title = sn('title');
  title.textContent = 'sector ' + n + ' · ' + meta.physical + ' · ' + meta.note +
    '\n' + (sub || '—');
  g.appendChild(title);
  if (hpStaged(n)) g.appendChild(sn('circle', { cx: x + 7, cy: y + 6, r: 3, class: 'dot' }));
  bindSelect(g, 'sector-' + n);
  return g;
}

function renderHpSketch() {
  const svg = sn('svg', { viewBox: '0 0 430 252', role: 'group', 'aria-label': 'HP Pavilion 300' });
  svg.appendChild(sn('rect', { x: 48, y: 12, width: 120, height: 200, rx: 24, class: 'body' }));
  svg.appendChild(makeHpCell(0, 54, 24, 38, 62, 'L', hpBindingText(0, 0)));
  svg.appendChild(makeHpCell(1, 128, 24, 38, 62, 'R', hpBindingText(1, 0)));
  svg.appendChild(makeHpCell(2, 97, 42, 26, 28, 'M', hpBindingText(2, 0)));
  svg.appendChild(sn('rect', { x: 96, y: 22, width: 28, height: 16, rx: 3, class: 'hint' }));
  svg.appendChild(sn('rect', { x: 96, y: 72, width: 28, height: 13, rx: 3, class: 'hint' }));
  svg.appendChild(sn('text', { x: 200, y: 12, class: 'caption' }, 'flash sectors'));
  for (const s of HP_SECTORS) {
    const col = s.n % 2, row = Math.floor(s.n / 2);
    svg.appendChild(makeHpCell(s.n, 200 + col * 76, 22 + row * 50, 68, 42,
      'S' + s.n, hpBindingText(s.n, 0)));
  }
  svg.appendChild(sn('text', { x: 200, y: 238, class: 'hintText' }, 'green = mapping confirmed · amber = unconfirmed'));
  svg.appendChild(sn('text', { x: 200, y: 250, class: 'hintText' }, 'S0–S2 assumed to be the main buttons'));
  $('sketch').replaceChildren(svg);
}

function renderSketch() {
  $('sketchTitle').textContent = dev().label;
  $('sketchHint').textContent = activeBaseline()
    ? 'click a control · amber = staged'
    : 'connect and read to populate bindings';
  if (state.active === 'naga') renderNagaSketch();
  else renderHpSketch();
}

function renderOtherControls() {
  const box = $('otherControls');
  box.replaceChildren();
  if (state.active !== 'naga') return;
  const layer = state.layers.naga;
  for (const c of NAGA_EXTRA) {
    const staged = nagaStaged(c);
    const chip = h('button', {
      type: 'button',
      class: 'chip' + (state.selected.naga === c.id ? ' selected' : '') + (staged ? ' staged' : ''),
      title: c.name + ' (ctr ' + hex(c.ctr) + ')',
      onclick: () => selectControl(c.id),
    }, h('span', null, c.name),
       h('span', { class: 'val' }, nagaBindingText(c, layer)),
       staged ? h('span', { class: 'muted' }, '•') : null);
    box.appendChild(chip);
  }
}

function selectEl(options, value) {
  const s = h('select');
  for (const [v, label] of options) {
    const o = h('option', { value: String(v), text: label });
    if (String(v) === String(value)) o.selected = true;
    s.appendChild(o);
  }
  return s;
}

function renderSettings() {
  const box = $('settingsPanel');
  box.replaceChildren();
  if (state.active === 'naga') {
    const dpi = nagaEffectiveDpi();
    const x = h('input', { type: 'number', min: '100', max: '16000', step: '50', value: dpi.x, id: 'dpiX' });
    const y = h('input', { type: 'number', min: '100', max: '16000', step: '50', value: dpi.y, id: 'dpiY' });
    const auto = debounce(() => stageNagaDpi(x, y), 250);
    x.addEventListener('input', auto);
    y.addEventListener('input', auto);
    x.addEventListener('change', () => stageNagaDpi(x, y));
    y.addEventListener('change', () => stageNagaDpi(x, y));
    box.appendChild(h('h3', null, 'DPI'));
    box.appendChild(h('div', { class: 'row' }, 'x', x, '×', y,
      h('span', { class: 'muted' }, 'queued automatically · not readable from the firmware')));
    return;
  }
  const b = state.baseline.hp;
  if (!b) {
    box.appendChild(h('h3', null, 'settings'));
    box.appendChild(h('div', { class: 'muted' }, 'connect and read the mouse to edit polling, DPI slots and colours'));
    return;
  }
  const s = hpEffectiveSettings();
  const poll = selectEl([[1000, '1000 Hz'], [500, '500 Hz'], [250, '250 Hz'], [125, '125 Hz']], s.polling);
  poll.dataset.role = 'poll';
  const active = selectEl(s.slots.map((_, i) => [i, 'slot ' + i]), s.activeSlot);
  active.dataset.role = 'active';
  box.appendChild(h('h3', null, 'settings'));
  box.appendChild(h('div', { class: 'row' }, h('label', null, 'polling'), poll,
    h('label', null, 'active DPI slot'), active));
  const tbl = h('table');
  tbl.appendChild(h('thead', null, h('tr', null,
    h('th', null, 'slot'), h('th', null, 'on'), h('th', null, 'level'),
    h('th', null, 'DPI'), h('th', null, 'colour'))));
  const tbody = h('tbody');
  s.slots.forEach((slot, i) => {
    const en = h('input', { type: 'checkbox', checked: slot.enabled, 'data-slot': i, 'data-f': 'en' });
    const lv = h('input', { type: 'number', min: '0', max: '8', value: slot.level, 'data-slot': i, 'data-f': 'level' });
    const co = h('input', { type: 'color', value: slot.color, 'data-slot': i, 'data-f': 'color' });
    tbody.appendChild(h('tr', null,
      h('td', null, i), h('td', null, en), h('td', null, lv),
      h('td', { 'data-dpi-slot': String(i) },
        String(b.dpiLevels && b.dpiLevels[slot.level] != null ? b.dpiLevels[slot.level] : '?')),
      h('td', null, co)));
  });
  tbl.appendChild(tbody);
  box.appendChild(tbl);
  const auto = () => stageHpSettings(box, true);
  poll.addEventListener('change', auto);
  active.addEventListener('change', auto);
  for (const inp of tbl.querySelectorAll('input')) {
    inp.addEventListener('change', auto);
    if (inp.type === 'number') inp.addEventListener('input', debounce(auto, 250));
  }
  box.appendChild(h('div', { class: 'row' },
    h('span', { class: 'muted' },
      'queued automatically · applies at the next replug · the LED is hardwired')));
  if (b.dpiLevels) {
    box.appendChild(h('div', { class: 'muted' },
      'DPI levels reported by the mouse: ' + b.dpiLevels.filter(Boolean).join(' / ')));
  }
}

function updateHpDpiCells(box, b) {
  if (!b || !b.dpiLevels) return;
  for (const cell of box.querySelectorAll('[data-dpi-slot]')) {
    const i = Number(cell.dataset.dpiSlot);
    const lv = box.querySelector('input[data-slot="' + i + '"][data-f=level]');
    const level = Number(lv ? lv.value : 0);
    const dpi = b.dpiLevels[level];
    cell.textContent = dpi != null ? String(dpi) : '?';
  }
}

/* ---- inspector ------------------------------------------------------- */

function modRow(devKey, mod) {
  const names = devKey === 'hp' ? HP_MODN : MODN.slice(0, 4);
  const row = h('div', { class: 'row mods' }, h('label', null, 'modifiers'));
  row.dataset.for = devKey === 'hp' ? 'key' : 'key turbo';
  names.forEach((name, i) => {
    const bit = 1 << i;
    row.appendChild(h('label', null,
      h('input', { type: 'checkbox', 'data-mod': String(bit), checked: !!(mod & bit) }), ' ' + name));
  });
  return row;
}

function checkedMods(root) {
  let m = 0;
  for (const c of root.querySelectorAll('input[data-mod]:checked')) m |= Number(c.dataset.mod);
  return m;
}

function stagedNaga(ctrl, layer) {
  return ctrl.host ? findEdit('naga:host:' + ctrl.id) : findEdit('naga:rec:' + ctrl.ctr + '@' + layer);
}

function stageNagaDpi(xInput, yInput) {
  if (!activeBaseline()) return false;
  const x = Math.max(100, Math.min(16000, Number(xInput.value) || 1600));
  const y = Math.max(100, Math.min(16000, Number(yInput.value) || 1600));
  const existing = findEdit('naga:dpi');
  const before = existing ? existing.before : baselineNagaDpi();
  const after = { x, y };
  stageEdit({
    key: 'naga:dpi', kind: 'settings', target: { kind: 'dpi' }, label: 'DPI',
    before, after,
  }, true);
  refreshAfterEdit();
  setQueueMsg(same(before, after) ? 'queued: no DPI change' :
    'queued: DPI ' + x + '×' + y);
  return true;
}

function renderNagaInspector(box) {
  const ctrl = NAGA_BY_ID[state.selected.naga] || NAGA_BY_ID['1'];
  if (!ctrl) { box.appendChild(h('div', { class: 'empty' }, 'select a control')); return; }
  const layer = ctrl.host ? 0 : state.layers.naga;
  const edit = stagedNaga(ctrl, layer);
  const current = ctrl.host ? nagaEffectiveHost(ctrl.id) : nagaEffectiveRec(ctrl.ctr, layer);
  const currentText = ctrl.host ? (nagaHostString(current) || '—') : (nagaRecString(current) || '—');
  const stagedText = edit ? (ctrl.host ? nagaHostString(edit.after) : nagaRecString(edit.after)) : null;

  box.appendChild(h('div', { class: 'title' }, ctrl.name));
  box.appendChild(h('div', { class: 'sub' },
    ctrl.host ? 'host-side wheel · exported to JSON, applied by the daemon' :
    'ctr ' + hex(ctrl.ctr) + (ctrl.thumb != null ? ' · thumb grid' : '')));
  box.appendChild(h('div', { class: 'current', id: 'inspectorCurrent' },
    h('div', null, h('span', { class: 'muted' }, 'now: '), h('span', { class: 'now' }, currentText)),
    stagedText != null ? h('div', null, h('span', { class: 'muted' }, 'staged: '),
      h('span', { class: 'staged' }, stagedText)) : null));

  const layerSel = selectEl([['0', 'normal'], ['1', 'hypershift']], layer);
  layerSel.disabled = !!ctrl.host;
  layerSel.addEventListener('change', () => { state.layers.naga = Number(layerSel.value); renderAll(); });
  if (!ctrl.host) box.appendChild(h('div', { class: 'row' }, h('label', null, 'layer'), layerSel));

  const rec = current || { type: 2, data: [0, 0] };
  const host = current || null;
  const action = ctrl.host
    ? (host && host.action === 'mouse' ? 'mouse' : 'key')
    : rec.type === 1 ? 'mouse'
    : rec.type === 6 ? 'dpi'
    : rec.type === 0x0d ? 'turbo'
    : rec.type === 0x0b ? 'hypershift'
    : rec.type === 0 ? 'default'
    : 'key';

  const actionSel = selectEl(ctrl.host
    ? [['key', 'keyboard key'], ['mouse', 'mouse button']]
    : [['key', 'keyboard key'], ['mouse', 'mouse button'], ['turbo', 'autofire (turbo)'],
       ['dpi', 'DPI up/down'], ['hypershift', 'Hypershift switch'], ['default', 'device default']],
    action);
  actionSel.dataset.role = 'action';
  box.appendChild(h('div', { class: 'row' }, h('label', null, 'action'), actionSel));

  const usage = ctrl.host ? (host && host.usage) || 0 : rec.data[1] || 0;
  const mod = ctrl.host ? (host && host.mod) || 0 : rec.data[0] || 0;
  const keyInput = h('input', {
    type: 'text', value: usage ? keyName(usage) : '', list: 'keynames', placeholder: 'x / f5 / kp1 / 0x1a',
  });
  const keyRow = h('div', { class: 'row', 'data-for': 'key turbo' },
    h('label', null, 'key'), keyInput);
  box.appendChild(keyRow);
  const modsRow = modRow('naga', mod);
  box.appendChild(modsRow);

  const mouseSel = selectEl(Object.entries(NAGA_MOUSE).map(([id, n]) => [id, n]),
    ctrl.host ? (host && host.mouse) || 1 : rec.type === 1 ? rec.data[0] || 1 : 1);
  const mouseRow = h('div', { class: 'row', 'data-for': 'mouse' },
    h('label', null, 'button'), mouseSel);
  box.appendChild(mouseRow);

  const dpiSel = selectEl([[1, 'DPI up'], [2, 'DPI down']], rec.type === 6 ? rec.data[0] || 1 : 1);
  const dpiRow = h('div', { class: 'row', 'data-for': 'dpi' },
    h('label', null, 'direction'), dpiSel);
  box.appendChild(dpiRow);

  const ms = rec.type === 0x0d
    ? (rec.data[2] << 8 | rec.data[3] ? Math.round(1000000 / (rec.data[2] << 8 | rec.data[3])) : 500)
    : 500;
  const msInput = h('input', { type: 'number', min: '1', max: '60000', value: ms });
  const turboRow = h('div', { class: 'row', 'data-for': 'turbo' },
    h('label', null, 'repeat every'), msInput, h('span', { class: 'muted' }, 'ms'));
  box.appendChild(turboRow);

  const sync = () => {
    const a = actionSel.value;
    for (const row of box.querySelectorAll('[data-for]'))
      row.classList.toggle('hide', !row.dataset.for.split(' ').includes(a));
  };
  actionSel.addEventListener('change', sync);
  sync();

  const auto = (silent = true) => stageNagaForm(ctrl, layer, actionSel, keyInput,
    mouseSel, dpiSel, msInput, box, silent);
  actionSel.addEventListener('change', () => { sync(); auto(true); });
  keyInput.addEventListener('input', debounce(() => auto(true), 300));
  keyInput.addEventListener('change', () => auto(false));
  for (const m of modsRow.querySelectorAll('input[data-mod]'))
    m.addEventListener('change', () => auto(true));
  mouseSel.addEventListener('change', () => auto(true));
  dpiSel.addEventListener('change', () => auto(true));
  msInput.addEventListener('input', debounce(() => auto(true), 300));
  msInput.addEventListener('change', () => auto(false));

  const actions = h('div', { class: 'actions' },
    h('button', {
      type: 'button', id: 'inspectorRevert', disabled: !edit,
      onclick: () => {
        const e = stagedNaga(ctrl, layer);
        if (e) revertEdit(e.key);
      },
    }, 'Revert staged'));
  if (activeBaseline() && state.backups.naga) actions.appendChild(h('button', {
    type: 'button', onclick: () => restoreNaga(ctrl, layer),
  }, 'Restore backup'));
  box.appendChild(actions);
  box.appendChild(h('div', { class: 'notice' }, 'edits are queued automatically'));
  if (!activeBaseline()) box.appendChild(h('div', { class: 'notice' }, 'connect and read the mouse first'));
  if (ctrl.host) box.appendChild(h('div', { class: 'notice' },
    'wheel rotation is host-side; include it in the JSON and load it with naga-remap run/daemon'));
}

function stageNagaForm(ctrl, layer, actionSel, keyInput, mouseSel, dpiSel, msInput, root, silent) {
  if (!activeBaseline()) return false;
  try {
    const a = actionSel.value;
    const existing = stagedNaga(ctrl, layer);
    if (ctrl.host) {
      const before = existing ? existing.before : null;
      let after;
      if (a === 'mouse') after = { action: 'mouse', mouse: Number(mouseSel.value) };
      else {
        const combo = parseCombo(keyInput.value, 0x0f);
        after = { action: 'key', usage: combo.usage, mod: combo.mod | checkedMods(root) };
      }
      const edit = {
        key: 'naga:host:' + ctrl.id, kind: 'host',
        target: { kind: 'host', id: ctrl.id },
        label: ctrl.name + ' (host)', before, after,
      };
      stageEdit(edit, true);
      refreshAfterEdit();
      setQueueMsg(same(before, after) ? 'queued: no change' : 'queued: ' + edit.label);
      return true;
    }
    const before = existing ? existing.before : baselineNagaRec(ctrl.ctr, layer);
    let after;
    if (a === 'default') after = { type: 0, data: [] };
    else if (a === 'hypershift') after = { type: 0x0b, data: [1] };
    else if (a === 'mouse') after = { type: 1, data: [Number(mouseSel.value)] };
    else if (a === 'dpi') after = { type: 6, data: [Number(dpiSel.value)] };
    else if (a === 'turbo') {
      const combo = parseCombo(keyInput.value, 0x0f);
      const ms = Math.max(1, Math.min(60000, Number(msInput.value) || 500));
      const rate = Math.min(0xffff, Math.round(1000000 / ms));
      after = { type: 0x0d, data: [combo.mod | checkedMods(root), combo.usage, rate >> 8, rate & 0xff] };
    } else {
      const combo = parseCombo(keyInput.value, 0x0f);
      after = { type: 2, data: [combo.mod | checkedMods(root), combo.usage] };
    }
    const edit = {
      key: 'naga:rec:' + ctrl.ctr + '@' + layer, kind: 'onboard',
      target: { kind: 'rec', ctr: ctrl.ctr, layer },
      label: ctrl.name + (layer ? ' · hypershift' : ''), before, after,
    };
    stageEdit(edit, true);
    refreshAfterEdit();
    setQueueMsg(same(before, after) ? 'queued: no change' : 'queued: ' + edit.label);
    return true;
  } catch (e) {
    if (!silent) showError(e);
    return false;
  }
}

function renderHpInspector(box) {
  const sel = String(state.selected.hp || 'sector-3');
  const n = Number((sel.match(/^sector-(\d+)$/) || [])[1] ?? 3);
  const meta = HP_SECTORS[n];
  if (!meta) { box.appendChild(h('div', { class: 'empty' }, 'select a sector')); return; }
  const edit = findEdit('hp:sector:' + n + '@0');
  const current = hpEffectiveLayer(n, 0);
  const currentText = hpActionString(current) || '—';
  const stagedText = edit ? (hpActionString(edit.after) || 'default') : null;

  box.appendChild(h('div', { class: 'title' }, 'sector ' + n));
  box.appendChild(h('div', { class: 'sub' }, meta.physical + ' · ' + meta.note));
  box.appendChild(h('div', { class: 'current', id: 'inspectorCurrent' },
    h('div', null, h('span', { class: 'muted' }, 'primary: '), h('span', { class: 'now' }, currentText)),
    h('div', null, h('span', { class: 'muted' }, 'shift: '), hpBindingText(n, 1)),
    stagedText != null ? h('div', null, h('span', { class: 'muted' }, 'staged: '),
      h('span', { class: 'staged' }, stagedText)) : null));

  const action = current
    ? current.type === 9 ? (current.tokens[0][0] === 4 ? 'dpi' : 'device')
    : current.type === 0 ? 'mouse'
    : current.type === 6 ? 'key' : 'key'
    : 'key';
  const actionSel = selectEl([
    ['key', 'keyboard key'], ['mouse', 'mouse button'],
    ['dpi', 'DPI cycle'], ['device', 'device function (hex)'],
  ], action);
  actionSel.dataset.role = 'action';
  box.appendChild(h('div', { class: 'row' }, h('label', null, 'action'), actionSel));

  const usage = current && current.type === 6 ? current.tokens[0][0] : 0;
  const mod = current && current.type === 6 ? current.tokens[0][1] : 0;
  const keyInput = h('input', { type: 'text', value: usage ? keyName(usage) : '', list: 'keynames', placeholder: 'x / f5 / 0x1a' });
  box.appendChild(h('div', { class: 'row', 'data-for': 'key' }, h('label', null, 'key'), keyInput));
  const modsRow = modRow('hp', mod);
  box.appendChild(modsRow);

  const mouseSel = selectEl(Object.entries(HP_MOUSE).map(([id, name]) => [id, name]),
    current && current.type === 0 ? current.tokens[0][0] : 1);
  box.appendChild(h('div', { class: 'row', 'data-for': 'mouse' }, h('label', null, 'button'), mouseSel));

  const devInput = h('input', {
    type: 'text', value: current && current.type === 9 && current.tokens[0][0] !== 4
      ? hex(current.tokens[0][0]) : '0a',
    placeholder: '0a',
  });
  box.appendChild(h('div', { class: 'row', 'data-for': 'device' },
    h('label', null, 'function'), devInput));

  const sync = () => {
    const a = actionSel.value;
    for (const row of box.querySelectorAll('[data-for]'))
      row.classList.toggle('hide', !row.dataset.for.split(' ').includes(a));
  };
  actionSel.addEventListener('change', sync);
  sync();

  const auto = (silent = true) => stageHpForm(n, actionSel, keyInput, mouseSel,
    devInput, box, silent);
  actionSel.addEventListener('change', () => { sync(); auto(true); });
  keyInput.addEventListener('input', debounce(() => auto(true), 300));
  keyInput.addEventListener('change', () => auto(false));
  for (const m of modsRow.querySelectorAll('input[data-mod]'))
    m.addEventListener('change', () => auto(true));
  mouseSel.addEventListener('change', () => auto(true));
  devInput.addEventListener('input', debounce(() => auto(true), 300));
  devInput.addEventListener('change', () => auto(false));

  const actions = h('div', { class: 'actions' },
    h('button', {
      type: 'button', id: 'inspectorRevert', disabled: !edit,
      onclick: () => {
        const e = findEdit('hp:sector:' + n + '@0');
        if (e) revertEdit(e.key);
      },
    }, 'Revert staged'));
  if (activeBaseline() && state.backups.hp) actions.appendChild(h('button', {
    type: 'button', onclick: () => restoreHp(n),
  }, 'Restore backup'));
  box.appendChild(actions);
  box.appendChild(h('div', { class: 'notice' }, 'edits are queued automatically'));
  if (!activeBaseline()) box.appendChild(h('div', { class: 'notice' }, 'connect and read the mouse first'));
  box.appendChild(h('div', { class: 'notice' }, 'only the primary layer is editable; the shift layer is shown read-only'));
}

function stageHpForm(n, actionSel, keyInput, mouseSel, devInput, root, silent) {
  if (!activeBaseline()) return false;
  try {
    const a = actionSel.value;
    let after;
    if (a === 'mouse') after = hpParseActionText('mouse:' + Object.entries(HP_MOUSE)
      .find(([id]) => Number(id) === Number(mouseSel.value))[1]);
    else if (a === 'dpi') after = hpParseActionText('dpi:cycle');
    else if (a === 'device') after = hpParseActionText('device:' + devInput.value.trim());
    else {
      const combo = parseCombo(keyInput.value, 0x0f);
      after = {
        type: 6, label: 0x13, mode: 0,
        tokens: [[combo.usage, combo.mod | checkedMods(root)]],
      };
    }
    const existing = findEdit('hp:sector:' + n + '@0');
    const before = existing ? existing.before : baselineHpLayer(n, 0);
    const edit = {
      key: 'hp:sector:' + n + '@0', kind: 'onboard',
      target: { kind: 'sector', n, layer: 0 },
      label: 'sector ' + n, before, after,
    };
    stageEdit(edit, true);
    refreshAfterEdit();
    setQueueMsg(same(before, after) ? 'queued: no change' : 'queued: ' + edit.label);
    return true;
  } catch (e) {
    if (!silent) showError(e);
    return false;
  }
}

function stageHpSettings(box, silent) {
  const b = state.baseline.hp;
  if (!b) {
    if (!silent) showError(new Error('read the mouse first'));
    return false;
  }
  try {
    const s = hpEffectiveSettings();
    const polling = Number(box.querySelector('[data-role=poll]').value);
    const activeSlot = Number(box.querySelector('[data-role=active]').value);
    const slots = s.slots.map((slot, i) => {
      const en = box.querySelector('input[data-slot="' + i + '"][data-f=en]');
      const lv = box.querySelector('input[data-slot="' + i + '"][data-f=level]');
      const co = box.querySelector('input[data-slot="' + i + '"][data-f=color]');
      return {
        enabled: !!(en && en.checked),
        level: Number(lv ? lv.value : slot.level),
        color: co ? co.value : slot.color,
      };
    });
    const after = { polling, activeSlot, slotCount: slots.length, slots };
    const existing = findEdit('hp:settings');
    const before = existing ? existing.before : baselineHpSettings();
    const edit = {
      key: 'hp:settings', kind: 'settings', target: { kind: 'settings' },
      label: 'settings', before, after,
    };
    stageEdit(edit, true);
    updateHpDpiCells(box, b);
    refreshAfterEdit();
    setQueueMsg(same(before, after) ? 'queued: no change' : 'queued: settings');
    return true;
  } catch (e) {
    if (!silent) showError(e);
    return false;
  }
}

function renderInspector() {
  const box = $('inspector');
  box.replaceChildren();
  $('inspectorHint').textContent = state.connected === state.active
    ? 'connected' : activeBaseline() ? 'saved read' : 'not read';
  if (state.active === 'naga') renderNagaInspector(box);
  else renderHpInspector(box);
}

/* ====================================================================== *
 * 4. queue, JSON, connection, push, boot, self-tests
 * ====================================================================== */

function setStatus(text) { $('status').textContent = text; }
function ok(text) { $('err').textContent = ''; setStatus(text); }
function showError(e) { $('err').textContent = String(e && e.message ? e.message : e); }
function setQueueMsg(text) { $('queueMsg').textContent = text; }

function queueReplace(list, edit) {
  const i = list.findIndex((e) => e.key === edit.key);
  if (i >= 0) list.splice(i, 1);
  if (!same(edit.before, edit.after)) list.push(edit);
  return list;
}

function stageEdit(edit, quiet) {
  queueReplace(state.queue[state.active].edits, edit);
  saveQueue();
  if (!quiet) {
    renderAll();
    setQueueMsg(same(edit.before, edit.after) ? 'no change to stage' : 'staged: ' + edit.label);
  }
}

function revertEdit(key) {
  const q = state.queue[state.active];
  const i = q.edits.findIndex((e) => e.key === key);
  if (i >= 0) q.edits.splice(i, 1);
  saveQueue();
  renderAll();
  setQueueMsg('reverted ' + key);
}

function saveQueue() { writeStore(QUEUE_KEY, state.queue); }

function editStale(e) {
  const b = activeBaseline();
  if (!b) return false;
  let cur = null;
  if (e.target.kind === 'rec') cur = b.records[e.target.ctr] ? b.records[e.target.ctr][e.target.layer] : null;
  else if (e.target.kind === 'dpi') cur = b.dpi;
  else if (e.target.kind === 'host') cur = null;
  else if (e.target.kind === 'sector') cur = hpParseLayer(b.sectors[e.target.n], e.target.layer ? 0xfe : 0);
  else if (e.target.kind === 'settings') cur = b.settings;
  return !same(cur, e.before);
}

function valueText(target, value) {
  if (value == null) return '—';
  if (target.kind === 'rec') return nagaRecString(value) || '—';
  if (target.kind === 'host') return nagaHostString(value) || '—';
  if (target.kind === 'dpi') return value.x + '×' + value.y;
  if (target.kind === 'sector') return hpActionString(value) || '—';
  if (target.kind === 'settings') return value.polling + ' Hz, slot ' + value.activeSlot;
  return '—';
}

function settingsDiff(before, after) {
  const parts = [];
  if (!before || before.polling !== after.polling) parts.push('poll ' + after.polling + ' Hz');
  if (!before || before.activeSlot !== after.activeSlot) parts.push('active slot ' + after.activeSlot);
  if (!before || !same(before.slots, after.slots)) parts.push('slots / colours');
  return parts.join(', ') || 'settings';
}

function profileDate(ms) {
  const d = new Date(ms || 0);
  if (Number.isNaN(d.getTime())) return '';
  return d.toLocaleString(undefined, {
    year: '2-digit', month: '2-digit', day: '2-digit',
    hour: '2-digit', minute: '2-digit',
  });
}

function profileById(key, id) {
  return state.profiles[key].find((p) => p.id === id) || null;
}

function saveProfiles() {
  state.profiles.selected = { ...state.profileSelected };
  writeStore(PROFILES_KEY, state.profiles);
}

function renderProfiles() {
  const key = state.active;
  const list = state.profiles[key];
  const sel = $('profileSelect');
  const name = $('profileName');
  const want = state.profileSelected[key];

  sel.replaceChildren();
  if (!list.length) {
    sel.appendChild(new Option('no profiles saved', ''));
  } else {
    sel.appendChild(new Option('— select profile —', ''));
    for (const p of [...list].sort((a, b) => (b.savedAt || 0) - (a.savedAt || 0)))
      sel.appendChild(new Option(p.name + ' · ' + profileDate(p.savedAt), p.id));
  }
  if (want && list.some((p) => p.id === want)) sel.value = want;
  else {
    sel.value = '';
    if (want) state.profileSelected[key] = null;
  }

  const current = profileById(key, sel.value);
  const sig = key + ':' + sel.value;
  if (state.profileSig !== sig) {
    state.profileSig = sig;
    name.value = current ? current.name : '';
  }
  $('profileSave').disabled = !activeBaseline();
  $('profileLoad').disabled = !sel.value || !activeBaseline();
  $('profileDelete').disabled = !sel.value;
  $('profileMsg').textContent = !activeBaseline()
    ? 'read the device to save or load'
    : current ? 'saved ' + profileDate(current.savedAt) : '';
}

function renderQueue() {
  const list = $('queueList');
  list.replaceChildren();
  const q = state.queue[state.active];
  for (const e of q.edits) {
    const stale = editStale(e);
    const kind = e.kind === 'host' ? 'JSON only' : e.kind === 'settings' ? 'settings' : 'onboard';
    const change = e.target.kind === 'settings'
      ? h('div', { class: 'change' }, settingsDiff(e.before, e.after))
      : h('div', { class: 'change' },
          h('b', null, valueText(e.target, e.before)),
          h('span', { class: 'arrow' }, ' → '),
          h('b', null, valueText(e.target, e.after)));
    list.appendChild(h('div', { class: 'qitem' },
      h('div', { class: 'what', title: e.key }, e.label),
      change,
      h('div', null,
        h('span', { class: 'badge ' + (e.kind === 'host' ? 'host' : e.kind === 'settings' ? 'settings' : '') }, kind),
        stale ? h('span', { class: 'badge stale' }, 'device changed') : null),
      h('button', { type: 'button', onclick: () => revertEdit(e.key) }, 'revert')));
  }
  $('queueHint').textContent = q.edits.length
    ? 'edits are queued automatically — flush, export, save a profile, or reset'
    : 'no queued edits';
}

function updateButtons() {
  const connected = state.connected === state.active && !!state.hid.dev;
  const q = state.queue[state.active];
  const n = q.edits.length;
  const writable = q.edits.filter((e) => e.kind !== 'host').length;
  $('connect').textContent = connected ? 'Reconnect' : 'Connect';
  $('reRead').disabled = !connected;
  $('backupNow').disabled = !activeBaseline();
  $('discardAll').disabled = !n;
  $('downloadJson').disabled = !activeBaseline();
  $('push').disabled = !connected || !writable || !state.selfTestOk;
  $('push').textContent = 'Flush changes' + (writable ? ' (' + writable + ')' : '');
  $('queueCount').textContent = String(n);
}

/* ---- device connection ------------------------------------------------ */

async function closeHid() {
  hpDetach();
  if (state.hid.dev) {
    try { await state.hid.dev.close(); } catch (e) { /* already closed */ }
  }
  state.hid = { key: null, dev: null, listener: null };
  state.connected = null;
}

async function connect() {
  if (!('hid' in navigator)) {
    showError(new Error('This browser has no WebHID. Use Chrome/Chromium/Brave/Edge.'));
    return;
  }
  const key = state.active;
  const d = DEVICES[key];
  await closeHid();
  try {
    const devices = await navigator.hid.requestDevice({ filters: [d.filter] });
    if (!devices.length) { setStatus('no device selected'); return; }
    let found = null;
    for (const handle of devices) {
      try {
        await handle.open();
        state.hid.dev = handle;
        if (key === 'hp') hpAttach(handle);
        if (key === 'naga') await nagaGetRec(0x40, 0);
        else await hpHello();
        found = handle;
        break;
      } catch (e) {
        hpDetach();
        state.hid.dev = null;
        try { await handle.close(); } catch (_) { /* ignore */ }
      }
    }
    if (!found) {
      showError(new Error('No interface answered. Pick every "' + d.short +
        '" entry in the chooser; only one is the config channel.'));
      return;
    }
    state.hid.key = key;
    state.connected = key;
    ok(d.label + ' — connected');
    renderAll();
    await readAll();
  } catch (e) { showError(e); }
}

async function readAll() {
  const key = state.connected;
  if (!key || state.hid.key !== key) { showError(new Error('connect the active device first')); return; }
  const d = DEVICES[key];
  setStatus('reading ' + d.label + '…');
  try {
    const base = await d.readAll(state.hid.dev);
    state.baseline[key] = base;
    if (key === 'naga') state.dpi = { ...base.dpi };
    if (!state.backups[key]) saveBackup(key, base);
    state.queue[key].readAt = Date.now();
    saveQueue();
    renderAll();
    ok(d.label + ': read ' + d.summary(base));
  } catch (e) { showError(e); }
}

function saveBackup(key, base) {
  base = base || state.baseline[key];
  if (!base) return;
  state.backups[key] = { savedAt: Date.now(), data: DEVICES[key].backupData(base) };
  writeStore(BACKUP_KEY, state.backups);
}

function saveBackupNow() {
  const key = state.active;
  if (!state.baseline[key]) return showError(new Error('read the device first'));
  saveBackup(key, state.baseline[key]);
  renderAll();
  setQueueMsg('saved a raw backup of ' + DEVICES[key].label);
}

/* ---- restore from the saved raw backup -------------------------------- */

function restoreNaga(ctrl, layer) {
  if (ctrl.host) return showError(new Error('the wheel is host-side; there is no raw backup'));
  const backup = state.backups.naga && state.backups.naga.data;
  const hexs = backup && backup.records[ctrl.ctr + '@' + layer];
  if (!hexs) return showError(new Error('no backup record for ' + ctrl.name));
  const rec = nagaParseRecord(hexToBytes(hexs));
  stageEdit({
    key: 'naga:rec:' + ctrl.ctr + '@' + layer, kind: 'onboard',
    target: { kind: 'rec', ctr: ctrl.ctr, layer },
    label: ctrl.name + (layer ? ' · hypershift' : '') + ' (backup)',
    before: nagaEffectiveRec(ctrl.ctr, layer), after: rec,
  });
}

function restoreHp(n) {
  const backup = state.backups.hp && state.backups.hp.data;
  const hexs = backup && backup.sectors[n];
  if (!hexs) return showError(new Error('no backup sector ' + n));
  const action = hpParseLayer(hexToBytes(hexs), 0);
  stageEdit({
    key: 'hp:sector:' + n + '@0', kind: 'onboard',
    target: { kind: 'sector', n, layer: 0 },
    label: 'sector ' + n + ' (backup)',
    before: hpParseLayer(activeBaseline().sectors[n], 0), after: action,
  });
}

/* ---- JSON ------------------------------------------------------------- */

function nagaHostFromText(text) {
  const lower = String(text || '').trim().toLowerCase();
  if (lower.startsWith('mouse:')) {
    const name = lower.slice(6);
    const id = Object.keys(NAGA_MOUSE).find((k) => NAGA_MOUSE[k] === name);
    if (!id) throw new Error('unknown mouse button "' + name + '"');
    return { action: 'mouse', mouse: Number(id) };
  }
  const combo = parseCombo(text, 0x0f);
  return { action: 'key', usage: combo.usage, mod: combo.mod };
}

function buildJson(key) {
  const b = state.baseline[key];
  if (!b) throw new Error('read the device before exporting');
  const d = DEVICES[key];
  const out = {
    version: 1,
    device: d.jsonDevice,
    match: d.match,
    bindings: { normal: {}, hypershift: {} },
  };
  if (key === 'naga') {
    for (const c of NAGA_CONTROLS) {
      if (c.host) {
        const s = nagaHostString(nagaEffectiveHost(c.id));
        if (s) out.bindings.normal[c.json] = s;
        continue;
      }
      for (const layer of [0, 1]) {
        const rec = nagaEffectiveRec(c.ctr, layer);
        const s = nagaRecString(rec);
        if (!s || s === 'profile' || s.startsWith('tilt:') || s.startsWith('type ')) continue;
        out.bindings[layer ? 'hypershift' : 'normal'][c.json] = s;
      }
    }
    out.settings = nagaSettingsForJson();
  } else {
    for (const s of HP_SECTORS) for (const layer of [0, 1]) {
      const action = hpActionString(hpEffectiveLayer(s.n, layer));
      if (action) out.bindings[layer ? 'hypershift' : 'normal']['sector-' + s.n] = action;
    }
    const settings = hpSettingsForJson();
    if (settings) out.settings = settings;
  }
  const backup = (state.backups[key] && state.backups[key].data) || d.backupData(b);
  if (backup) out.backup = backup;
  return out;
}

function downloadJson() {
  try {
    const key = state.active;
    const obj = buildJson(key);
    const blob = new Blob([JSON.stringify(obj, null, 2) + '\n'], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = h('a', { href: url, download: DEVICES[key].jsonDevice + '.json' });
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    setQueueMsg('downloaded ' + DEVICES[key].jsonDevice + '.json');
  } catch (e) { showError(e); }
}

/* ---- named profiles --------------------------------------------------- */

function newProfileId() {
  return 'p' + Date.now().toString(36) + Math.random().toString(36).slice(2, 8);
}

function saveProfile() {
  const key = state.active;
  if (!state.baseline[key]) return showError(new Error('read the device before saving a profile'));
  const list = state.profiles[key];
  let name = $('profileName').value.trim().replace(/\s+/g, ' ').slice(0, 48);
  if (!name) name = 'profile ' + (list.length + 1);
  let data;
  try { data = buildJson(key); } catch (e) { return showError(e); }
  delete data.backup; /* a profile is a config, not a raw backup */
  let prof = list.find((p) => p.name.toLowerCase() === name.toLowerCase());
  const existed = !!prof;
  if (!prof) {
    prof = { id: newProfileId(), name, savedAt: Date.now(), data };
    list.push(prof);
  } else {
    prof.name = name;
    prof.data = data;
    prof.savedAt = Date.now();
  }
  state.profileSelected[key] = prof.id;
  state.profileSig = null;
  saveProfiles();
  renderAll();
  setQueueMsg((existed ? 'updated' : 'saved') + ' profile "' + name + '"');
}

function loadProfile() {
  const key = state.active;
  const id = $('profileSelect').value;
  const prof = profileById(key, id);
  if (!prof) return showError(new Error('select a profile first'));
  if (!state.baseline[key])
    return showError(new Error('connect and read ' + DEVICES[key].label + ' first'));
  try {
    applyProfileData(key, prof.data);
    state.profileSelected[key] = prof.id;
    state.profileSig = null;
    saveProfiles();
    renderAll();
    setQueueMsg('loaded profile "' + prof.name + '" — review, then Flush changes');
  } catch (e) { showError(e); }
}

function deleteProfile() {
  const key = state.active;
  const id = $('profileSelect').value;
  const list = state.profiles[key];
  const i = list.findIndex((p) => p.id === id);
  if (i < 0) return showError(new Error('select a profile first'));
  const prof = list.splice(i, 1)[0];
  if (state.profileSelected[key] === id) state.profileSelected[key] = null;
  state.profileSig = null;
  saveProfiles();
  renderAll();
  setQueueMsg('deleted profile "' + prof.name + '"');
}

/* Stage every difference between a saved profile and the current read.
 * Nothing is written to the mouse; the normal Push path applies it. */
function applyProfileData(key, data) {
  if (!data || data.version != null && data.version !== 1)
    throw new Error('unsupported profile version');
  if (data.device !== DEVICES[key].jsonDevice)
    throw new Error('profile is for "' + data.device + '", not "' +
      DEVICES[key].jsonDevice + '"');
  if (key !== state.active) throw new Error('switch to the profile device first');
  const q = state.queue[key];
  const old = q.edits;
  q.edits = [];
  try {
    if (key === 'naga') applyNagaProfile(data);
    else applyHpProfile(data);
  } catch (e) {
    q.edits = old;
    saveQueue();
    throw e;
  }
  saveQueue();
}

function applyNagaProfile(data) {
  const bindings = data.bindings || {};
  const sections = [bindings.normal || {}, bindings.hypershift || {}];

  for (const c of NAGA_CONTROLS) {
    if (c.host) {
      const value = sections[0][c.json];
      if (value == null) continue;
      stageEdit({
        key: 'naga:host:' + c.id, kind: 'host',
        target: { kind: 'host', id: c.id },
        label: c.name + ' (host)',
        before: nagaEffectiveHost(c.id),
        after: nagaHostFromText(value),
      }, true);
      continue;
    }
    for (const layer of [0, 1]) {
      const value = sections[layer][c.json];
      if (value == null) continue;
      stageEdit({
        key: 'naga:rec:' + c.ctr + '@' + layer, kind: 'onboard',
        target: { kind: 'rec', ctr: c.ctr, layer },
        label: c.name + (layer ? ' · hypershift' : ''),
        before: nagaEffectiveRec(c.ctr, layer),
        after: nagaParseActionText(value),
      }, true);
    }
  }

  if (data.settings && data.settings.dpi) {
    const before = nagaEffectiveDpi();
    const x = Math.max(100, Math.min(16000,
      Number(data.settings.dpi.x) || before.x));
    const y = Math.max(100, Math.min(16000,
      Number(data.settings.dpi.y) || before.y));
    stageEdit({
      key: 'naga:dpi', kind: 'settings', target: { kind: 'dpi' },
      label: 'DPI', before: { x: before.x, y: before.y }, after: { x, y },
    }, true);
  }
}

function applyHpProfile(data) {
  const bindings = data.bindings || {};
  const sections = [bindings.normal || {}, bindings.hypershift || {}];
  const base = state.baseline.hp;

  for (const s of HP_SECTORS) {
    for (const layer of [0, 1]) {
      const value = sections[layer]['sector-' + s.n];
      const before = hpParseLayer(base.sectors[s.n], layer ? 0xfe : 0);
      if (value == null && !before) continue;
      const after = value == null ? hpParseActionText('default') :
        hpParseActionText(value);
      stageEdit({
        key: 'hp:sector:' + s.n + '@' + layer, kind: 'onboard',
        target: { kind: 'sector', n: s.n, layer },
        label: 'sector ' + s.n + (layer ? ' · shift' : ''),
        before, after,
      }, true);
    }
  }

  if (data.settings) {
    const cur = hpEffectiveSettings();
    const slots = Array.isArray(data.settings.slots)
      ? data.settings.slots.map((s, i) => ({
          enabled: !!s.enabled,
          level: Math.max(0, Math.min(8, Number(s.level) || 0)),
          color: /^#[0-9a-f]{6}$/i.test(s.color || '') ? s.color :
            (cur.slots[i] ? cur.slots[i].color : '#000000'),
        }))
      : cur.slots;
    const after = {
      polling: [125, 250, 500, 1000].includes(Number(data.settings.polling))
        ? Number(data.settings.polling) : cur.polling,
      activeSlot: Number(data.settings.activeSlot),
      slotCount: slots.length,
      slots,
    };
    if (!Number.isInteger(after.activeSlot) || after.activeSlot < 0 ||
        after.activeSlot >= after.slotCount)
      after.activeSlot = Math.max(0, after.slotCount - 1);
    stageEdit({
      key: 'hp:settings', kind: 'settings', target: { kind: 'settings' },
      label: 'settings', before: state.baseline.hp.settings, after,
    }, true);
  }
}

function importMap(obj) {
  if (obj.version != null && obj.version !== 1)
    throw new Error('unsupported JSON version ' + obj.version);
  const key = Object.keys(DEVICES).find((k) => DEVICES[k].jsonDevice === obj.device);
  if (!key) throw new Error('unknown device "' + obj.device + '"');
  if (obj.bindings && typeof obj.bindings !== 'object') throw new Error('bindings must be an object');
  if (key !== state.active) {
    state.active = key;
    writeStore(ACTIVE_KEY, key);
  }
  if (!state.baseline[key]) throw new Error('connect and read ' + DEVICES[key].label + ' first');
  const warnings = [];
  let staged = 0;
  const bindings = obj.bindings || {};
  for (const layerName of ['normal', 'hypershift']) {
    const layer = layerName === 'hypershift' ? 1 : 0;
    const section = bindings[layerName];
    if (!section || typeof section !== 'object') continue;
    for (const [id, value] of Object.entries(section)) {
      try {
        if (key === 'naga') {
          const c = NAGA_BY_JSON[id];
          if (!c) { warnings.push('unknown id "' + id + '"'); continue; }
          if (c.host) {
            if (layer) { warnings.push(id + ' has no hypershift layer'); continue; }
            const after = nagaHostFromText(value);
            stageEdit({
              key: 'naga:host:' + c.id, kind: 'host', target: { kind: 'host', id: c.id },
              label: c.name + ' (host)', before: nagaEffectiveHost(c.id), after,
            }, true);
          } else {
            const after = nagaParseActionText(value);
            stageEdit({
              key: 'naga:rec:' + c.ctr + '@' + layer, kind: 'onboard',
              target: { kind: 'rec', ctr: c.ctr, layer },
              label: c.name + (layer ? ' · hypershift' : ''),
              before: nagaEffectiveRec(c.ctr, layer), after,
            }, true);
          }
          staged++;
        } else {
          const m = /^sector-(\d+)$/.exec(id);
          const n = m ? Number(m[1]) : NaN;
          if (!Number.isInteger(n) || n < 0 || n >= HP_SEC_COUNT) {
            warnings.push('unknown id "' + id + '"');
            continue;
          }
          const after = hpParseActionText(value);
          stageEdit({
            key: 'hp:sector:' + n + '@' + layer, kind: 'onboard',
            target: { kind: 'sector', n, layer },
            label: 'sector ' + n + (layer ? ' · shift' : ''),
            before: hpParseLayer(state.baseline.hp.sectors[n], layer ? 0xfe : 0),
            after,
          }, true);
          staged++;
        }
      } catch (e) { warnings.push(id + ': ' + e.message); }
    }
  }
  if (key === 'naga' && obj.settings && obj.settings.dpi) {
    const x = Math.max(100, Math.min(16000, Number(obj.settings.dpi.x) || 1600));
    const y = Math.max(100, Math.min(16000, Number(obj.settings.dpi.y) || 1600));
    const before = nagaEffectiveDpi();
    stageEdit({
      key: 'naga:dpi', kind: 'settings', target: { kind: 'dpi' }, label: 'DPI',
      before: { x: before.x, y: before.y }, after: { x, y },
    }, true);
    staged++;
  }
  if (key === 'hp' && obj.settings) {
    const cur = hpEffectiveSettings();
    const after = {
      polling: Number(obj.settings.polling) || cur.polling,
      activeSlot: Number(obj.settings.activeSlot) || 0,
      slotCount: cur.slotCount,
      slots: Array.isArray(obj.settings.slots)
        ? obj.settings.slots.map((s, i) => ({
            enabled: !!s.enabled,
            level: Number(s.level) || 0,
            color: /^#[0-9a-f]{6}$/i.test(s.color || '') ? s.color : (cur.slots[i] ? cur.slots[i].color : '#000000'),
          }))
        : cur.slots,
    };
    after.slotCount = after.slots.length;
    after.activeSlot = Math.min(after.activeSlot, after.slotCount - 1);
    stageEdit({
      key: 'hp:settings', kind: 'settings', target: { kind: 'settings' },
      label: 'settings', before: state.baseline.hp.settings, after,
    }, true);
    staged++;
  }
  if (obj.backup) warnings.push('backup section ignored (not imported)');
  saveQueue();
  renderAll();
  setQueueMsg('imported ' + staged + ' staged change(s)' +
    (warnings.length ? ' · ' + warnings.join(' · ') : ''));
}

function readImportFile(file) {
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    try { importMap(JSON.parse(String(reader.result))); }
    catch (e) { showError(e); }
  };
  reader.onerror = () => showError(new Error('could not read ' + file.name));
  reader.readAsText(file);
}

/* ---- push ------------------------------------------------------------- */

async function pushAll() {
  const key = state.connected;
  if (!key || state.hid.key !== key) return showError(new Error('connect the active device first'));
  if (!state.selfTestOk) return showError(new Error('self-test failed; writes are disabled'));
  const d = DEVICES[key];
  if (!state.baseline[key]) return showError(new Error('read the device first'));
  const onboard = state.queue[key].edits.filter((e) => e.kind !== 'host');
  if (!onboard.length) return showError(new Error('only host-side entries are queued; download the JSON instead'));
  setStatus('pushing ' + onboard.length + ' change(s)…');
  let done = 0;
  try {
    for (const e of onboard) {
      await d.applyEdit(state.hid.dev, e, state.baseline[key]);
      const q = state.queue[key];
      const i = q.edits.findIndex((x) => x.key === e.key);
      if (i >= 0) q.edits.splice(i, 1);
      done++;
    }
    saveQueue();
    try { await readAll(); } catch (e) { showError(e); }
    const left = state.queue[key].edits.length;
    ok('flushed ' + done + ' change(s)' +
      (left ? '; ' + left + ' host-side entr' + (left === 1 ? 'y' : 'ies') + ' stay queued for the JSON' : ''));
    renderAll();
  } catch (e) {
    saveQueue();
    renderAll();
    showError(e);
  }
}

/* ---- boot ------------------------------------------------------------- */

function switchDevice(key) {
  if (!DEVICES[key]) return;
  state.active = key;
  writeStore(ACTIVE_KEY, key);
  renderAll();
}

function renderSelfTest(results) {
  const box = $('selftestPanel');
  box.replaceChildren(h('h2', null, 'self-test'));
  const ul = h('ul');
  for (const r of results) {
    ul.appendChild(h('li', { class: r.ok ? 'ok' : 'fail' },
      (r.ok ? 'ok · ' : 'FAIL · ') + r.name + (r.error ? ': ' + r.error : '')));
  }
  box.appendChild(ul);
  box.classList.remove('hide');
}

function runSelfTests() {
  const results = [];
  const check = (name, fn) => {
    try { fn(); results.push({ name, ok: true }); }
    catch (e) { results.push({ name, ok: false, error: e.message }); }
  };
  check('naga frame CRC', () => {
    const b = nagaFrame(0x00, 0xb3, 0x01, [0]);
    let crc = 0;
    for (let i = 2; i < 88; i++) crc ^= b[i];
    if (b[88] !== crc || b[88] !== 0xb2) throw new Error('CRC ' + hex(b[88]));
  });
  check('naga record round-trip', () => {
    const rec = { type: 2, data: [0x02, 0x1e] };
    const back = nagaParseRecord(nagaRecordBytes(0x40, 0, rec));
    if (!same(back, { ctr: 0x40, layer: 0, type: 2, data: [0x02, 0x1e] }))
      throw new Error(JSON.stringify(back));
  });
  check('naga action grammar', () => {
    if (nagaRecString(nagaParseActionText('ctrl+shift+t')) !== 'ctrl+shift+t')
      throw new Error('key combo');
    if (nagaParseActionText('turbo:f5@3000').type !== 0x0d) throw new Error('turbo');
    if (nagaParseActionText('dpi:up').type !== 6) throw new Error('dpi');
    if (nagaRecString(nagaParseActionText('mouse:back')) !== 'mouse:back') throw new Error('mouse');
  });
  check('hp settings checksum', () => {
    const raw = new Uint8Array(HP_SET_LEN);
    raw[0] = 1;
    const sum = hpChecksum(raw);
    if ((raw[0x25] | (raw[0x26] << 8)) !== sum) throw new Error('checksum');
  });
  check('hp key round-trip', () => {
    const sec = new Uint8Array(512);
    hpWriteLayer(sec, 0, hpParseActionText('shift+x'));
    hpChecksumSector(sec);
    if (hpActionString(hpParseLayer(sec, 0)) !== 'shift+x') throw new Error('key');
  });
  check('hp mouse / device grammar', () => {
    if (hpActionString(hpParseActionText('mouse:back')) !== 'mouse:back') throw new Error('mouse');
    if (hpActionString(hpParseActionText('dpi:cycle')) !== 'dpi:cycle') throw new Error('dpi');
    if (hpActionString(hpParseActionText('device:0a')) !== 'device:0x0a') throw new Error('device');
  });
  check('key table', () => {
    if (parseUsage('f5') !== 0x3e || parseUsage('0x1a') !== 0x1a) throw new Error('usage');
    if (keyName(0x1e) !== '1') throw new Error('name');
  });
  check('queue replace / drop', () => {
    const list = [];
    const e = { key: 'x', before: 1, after: 2 };
    queueReplace(list, e);
    if (list.length !== 1) throw new Error('push');
    queueReplace(list, { key: 'x', before: 1, after: 1 });
    if (list.length !== 0) throw new Error('drop');
    queueReplace(list, { key: 'y', before: null, after: 3 });
    queueReplace(list, { key: 'y', before: null, after: 4 });
    if (list.length !== 1 || list[0].after !== 4) throw new Error('replace');
  });
  return results;
}

function init() {
  const params = new URLSearchParams(location.search);
  const wanted = params.get('device') ||
    (location.hash === '#hp' ? 'hp' : location.hash === '#naga' ? 'naga' : null);
  if (wanted && DEVICES[wanted]) state.active = wanted;
  if (!DEVICES[state.active]) state.active = 'naga';

  const dl = h('datalist', { id: 'keynames' });
  for (const n of Object.keys(KEYS)) dl.appendChild(h('option', { value: n }));
  document.body.appendChild(dl);

  for (const b of document.querySelectorAll('#tabs .tab'))
    b.addEventListener('click', () => switchDevice(b.dataset.dev));
  $('connect').addEventListener('click', connect);
  $('reRead').addEventListener('click', readAll);
  $('backupNow').addEventListener('click', saveBackupNow);
  $('discardAll').addEventListener('click', () => {
    state.queue[state.active].edits = [];
    saveQueue();
    renderAll();
    setQueueMsg('queue reset');
  });
  $('downloadJson').addEventListener('click', downloadJson);
  $('push').addEventListener('click', pushAll);
  $('profileSave').addEventListener('click', saveProfile);
  $('profileLoad').addEventListener('click', loadProfile);
  $('profileDelete').addEventListener('click', deleteProfile);
  $('profileSelect').addEventListener('change', () => {
    state.profileSelected[state.active] = $('profileSelect').value || null;
    state.profileSig = null;
    saveProfiles();
    renderProfiles();
  });
  $('importJson').addEventListener('click', () => $('importFile').click());
  $('importFile').addEventListener('change', (e) => {
    readImportFile(e.target.files && e.target.files[0]);
    e.target.value = '';
  });

  const results = runSelfTests();
  state.selfTest = results;
  state.selfTestOk = results.every((r) => r.ok);
  if (!state.selfTestOk) {
    showError(new Error('self-test failed; writes are disabled'));
    renderSelfTest(results);
  } else if (params.get('selftest') === '1') {
    renderSelfTest(results);
  }

  renderAll();
  window.antirazer = {
    state, DEVICES, buildJson, importMap, runSelfTests,
    saveProfile, loadProfile, deleteProfile, applyProfileData,
    naga: { frame: nagaFrame, parseAction: nagaParseActionText, recString: nagaRecString },
    hp: { parseAction: hpParseActionText, actionString: hpActionString, settings: hpParseSettings },
    queueReplace,
  };
  setStatus('disconnected');
}

init();

})();
