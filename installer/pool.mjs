// SPDX-License-Identifier: GPL-3.0-only
// Browser transport for the checked BB-37 percussion owner.
export const ELITE_ID = "SMK-37 Elite_3737851";
export const APP_SHA256 = "1d1e9017357f50400c79604fed70230f7df95fee394c7a60710acb5aabd90027";
export const WINDOWS = Object.freeze([[0x9d000, 0xa0000], [0xa8000, 0xbb000], [0xc2000, 0xea000]]);
const HEADER = [0xf0, 0x7d, 0x45, 0x4c];
const STATUS = ["ok", "same", "range", "incomplete", "crc", "busy", "guard", "erase", "program", "verify", "bad"];
export const BACKUP_KEY = "bb37-727-backup-v1";
const INDEX_SCHEMA = "bb37-727-backup-index-v2";
const STORAGE_SCHEMA = "bb37-727-backup-storage-v2";
const backupKey = binding => BACKUP_KEY+":"+binding;
function encodeBackup(backup) {
  return JSON.stringify({storageSchema:STORAGE_SCHEMA,backup:{...backup,windows:backup.windows.map(window=>{
    const {data,...metadata}=window;
    let binary="";
    for(let at=0;at<data.length;at+=8192) binary+=String.fromCharCode(...data.slice(at,at+8192));
    return {...metadata,dataBase64:btoa(binary)};
  })}});
}
function decodeBackup(encoded) {
  const value=JSON.parse(encoded);
  if(value.storageSchema!==STORAGE_SCHEMA) return value; // Existing array-based exports remain valid.
  return {...value.backup,windows:value.backup.windows.map(window=>{
    const {dataBase64,...metadata}=window;
    if(typeof dataBase64!=="string" || dataBase64.length>400000) throw new Error("Invalid stored percussion backup.");
    return {...metadata,data:Array.from(atob(dataBase64),c=>c.charCodeAt(0))};
  })};
}
function retainedOriginal(storage,binding) {
  const direct=storage.getItem(backupKey(binding));
  if(direct) return direct;
  const legacy=storage.getItem(BACKUP_KEY);
  if(legacy){const value=JSON.parse(legacy);if(value.schema!==INDEX_SCHEMA && value.binding===binding)return legacy;}
  return null;
}
export function listBrowserBackups(storage) {
  const bindings=new Set(JSON.parse(storage.getItem(BACKUP_KEY+".index")??"[]"));
  // Recover older per-binding records even when the former global alias pointed elsewhere.
  for(let i=0;i<(storage.length??0);i++){
    const key=storage.key(i);
    if(!key?.startsWith(BACKUP_KEY+":"))continue;
    const value=JSON.parse(storage.getItem(key));
    const binding=value.backup?.binding??value.binding;
    if(typeof binding==="string" && key===backupKey(binding))bindings.add(binding);
  }
  const legacy=storage.getItem(BACKUP_KEY);
  if(legacy){const value=JSON.parse(legacy);if(value.schema!==INDEX_SCHEMA && typeof value.binding==="string")bindings.add(value.binding);}
  return [...bindings].filter(binding=>typeof binding==="string" && retainedOriginal(storage,binding));
}
export function selectedBrowserBinding(storage) {
  const encoded=storage.getItem(BACKUP_KEY);if(!encoded)return null;
  const value=JSON.parse(encoded);return typeof value.binding==="string"?value.binding:null;
}
export function selectBrowserBackup(binding,storage) {
  const original=retainedOriginal(storage,binding);if(!original)return;
  if(!storage.getItem(backupKey(binding))){
    const compact=encodeBackup(decodeBackup(original));
    storage.setItem(backupKey(binding),compact);
    if(storage.getItem(backupKey(binding))!==compact)throw new Error("The browser could not retain the percussion backup.");
  }
  const pointer=JSON.stringify({schema:INDEX_SCHEMA,binding,originalKey:backupKey(binding)});
  storage.setItem(BACKUP_KEY,pointer);
  if(storage.getItem(BACKUP_KEY)!==pointer)throw new Error("The browser could not select the percussion backup.");
}
export function readBrowserBackup(storage,binding,kind="original") {
  if(typeof binding!=="string")return null;
  let encoded=kind==="original"?retainedOriginal(storage,binding):storage.getItem(backupKey(binding)+":return");
  if(!encoded && kind==="return")encoded=storage.getItem(BACKUP_KEY+".return");
  if(!encoded)return null;
  const backup=decodeBackup(encoded);
  return backup.binding===binding?backup:null;
}
export function persistBrowserBackup(backup, storage) {
  const key=backupKey(backup.binding),original=retainedOriginal(storage,backup.binding);
  const prior=original?decodeBackup(original):backup;
  if(prior.binding!==backup.binding)throw new Error("The original percussion backup belongs to another connection.");
  const compactOriginal=encodeBackup(prior);
  // Migrate in place before removing the duplicate legacy alias. Original bytes stay durable.
  storage.setItem(key,compactOriginal);
  if(storage.getItem(key)!==compactOriginal)throw new Error("The browser could not retain the original percussion backup.");
  selectBrowserBackup(backup.binding,storage);
  const legacyReturn=storage.getItem(BACKUP_KEY+".return");
  if(legacyReturn){
    const value=decodeBackup(legacyReturn),destination=backupKey(value.binding)+":return",compact=encodeBackup(value);
    if(!storage.getItem(destination)){
      storage.setItem(destination,compact);
      if(storage.getItem(destination)!==compact)throw new Error("The browser could not retain the percussion return copy.");
    }
    storage.removeItem?.(BACKUP_KEY+".return");
  }
  if(original){
    const encoded=encodeBackup(backup),destination=key+":return";
    storage.setItem(destination,encoded);
    if(storage.getItem(destination)!==encoded)throw new Error("The browser could not save the percussion return copy. No percussion was written.");
  }
  const bindings=listBrowserBackups(storage);
  if(!bindings.includes(backup.binding))bindings.push(backup.binding);
  storage.setItem(BACKUP_KEY+".index",JSON.stringify(bindings));
}
const BANK_HASHES = {
  "compas727.bin": "fe24c5c074fad8a152a9db237c1e3bbe08045189cf6701c75582aa676eda8f05",
  "hanan-cumbia.bin": "9f4960e12f2a2805ebed4e0e84c92125601b09a2fcdd619ab1cacb5390e34276",
  "layer727.json": "9f6d8778b1f55afbddb3a51a93d99e6cb6ee1db5de69546480a8e5acb95e2b67",
};
export function requireElite(device) {
  if (device?.text !== ELITE_ID || device.model !== "SMK-37 Elite" || device.version !== 3737851)
    throw new Error("Percussion installation requires BB-37 Elite build 851.");
}
export function inWindow(offset, size) {
  return Number.isSafeInteger(offset) && Number.isSafeInteger(size) && size > 0 &&
    WINDOWS.some(([lo, hi]) => offset >= lo && offset + size <= hi);
}
export function crc32(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) {
    value ^= byte;
    for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  }
  return (value ^ 0xffffffff) >>> 0;
}
export async function sha256(bytes) {
  return Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)), b => b.toString(16).padStart(2, "0")).join("");
}
export async function verifyEliteApplication(bytes, product) {
  if (product !== ELITE_ID || await sha256(bytes) !== APP_SHA256)
    throw new Error("The Elite build 851 package checksum did not match.");
}
export function packPool(bytes) {
  const out = [];
  for (let at = 0; at < bytes.length; at += 7) {
    const group = Array.from(bytes.slice(at, at + 7));
    out.push(group.reduce((value, b, bit) => value | (((b >>> 7) & 1) << bit), 0), ...group.map(b => b & 127));
  }
  return out;
}
export function unpackPool(bytes) {
  const out = [];
  for (let at = 0; at < bytes.length; at += 8) {
    const high = bytes[at];
    if (!Number.isInteger(high) || high < 0 || high > 127) throw new Error("Invalid percussion reply encoding.");
    const group = bytes.slice(at + 1, at + 8);
    if (!group.length || group.some(b => b < 0 || b > 127)) throw new Error("Invalid percussion reply encoding.");
    group.forEach((b, bit) => out.push(b | (((high >>> bit) & 1) << 7)));
  }
  return Uint8Array.from(out);
}
const put21 = value => [value & 127, (value >>> 7) & 127, (value >>> 14) & 127];
const put35 = value => [0, 7, 14, 21, 28].map(shift => (value >>> shift) & 127);
const get21 = bytes => bytes[0] | (bytes[1] << 7) | (bytes[2] << 14);
const equal = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

export class Pool {
  constructor(link, device, timeout = 3000) { requireElite(device); this.link = link; this.timeout = timeout; }
  async ask(body, accepts, timeout = this.timeout, tries = 3) {
    for (let attempt = 0; attempt < tries; attempt++) {
      if (!this.link.send([...HEADER, ...body, 0xf7])) throw new Error("The Elite percussion connection was lost.");
      const deadline = Date.now() + timeout;
      while (Date.now() < deadline) {
        const packet = await this.link.read(deadline - Date.now());
        if (!packet) break;
        if (!equal(packet.slice(0, 4), HEADER) || packet[4] !== body[0] + 0x40 || packet.at(-1) !== 0xf7) continue;
        const reply = packet.slice(5, -1);
        if (reply.some(v => v < 0 || v > 127)) throw new Error("Invalid percussion response.");
        if (accepts(reply)) return reply;
      }
    }
    throw new Error("The Elite did not confirm the percussion operation. Keep your backup and reconnect before retrying.");
  }
  async info() {
    const reply = await this.ask([0x14], b => b.length === 4 || b.length === 7);
    const info = { build: reply[0] * 100 + reply[1], mounted: !!reply[2], busy: !!reply[3], shedBlocks: reply.length === 7 ? get21(reply.slice(4)) : 0 };
    if (info.build !== 851) throw new Error("The percussion reply came from a different firmware build.");
    return info;
  }
  async ready() { const info = await this.info(); if (info.busy) throw new Error("Stop playback before installing percussion."); return info; }
  async read(offset, size) {
    if (!inWindow(offset, size) || size > 256) throw new Error("Percussion read outside the allowed windows.");
    const reply = await this.ask([0x10, ...put21(offset), size & 127, size >>> 7], b => b.length >= 4 && get21(b) === offset);
    if (reply[3]) throw new Error(`Percussion read refused: ${STATUS[reply[3]] ?? "unknown"}.`);
    const data = unpackPool(reply.slice(4));
    if (data.length !== size) throw new Error("Incomplete percussion read.");
    return data;
  }
  async readWindow(lo, hi, progress = () => {}) {
    if (!inWindow(lo, hi - lo)) throw new Error("Invalid percussion backup window.");
    const data = new Uint8Array(hi - lo);
    for (let at = lo; at < hi; at += 256) { data.set(await this.read(at, Math.min(256, hi - at)), at - lo); progress(); }
    return data;
  }
  async sector(offset, bytes, prior) {
    guardSector(offset, bytes, prior);
    for (let chunk = 0; chunk < 16; chunk++) {
      const reply = await this.ask([0x11, ...put21(offset), chunk, ...packPool(bytes.slice(chunk * 256, chunk * 256 + 256))],
        b => b.length === 5 && get21(b) === offset && b[3] === chunk);
      if (reply[4]) throw new Error(`Percussion staging refused: ${STATUS[reply[4]] ?? "unknown"}.`);
    }
    // COMMIT has an erase/program side effect; never blindly retry an unanswered commit.
    const reply = await this.ask([0x12, ...put21(offset), ...put35(crc32(bytes))],
      b => (b.length === 4 || b.length === 5) && get21(b) === offset, 8000, 1);
    if (reply[3] !== 0 && reply[3] !== 1) throw new Error(`Percussion write refused: ${STATUS[reply[3]] ?? "unknown"}.`);
    for (let at = 0; at < 4096; at += 256)
      if (!equal(await this.read(offset + at, 256), bytes.slice(at, at + 256))) throw new Error("Percussion sector readback mismatch.");
  }
  async mount() { const reply = await this.ask([0x13], b => b.length === 1, 8000); return !!reply[0]; }
}

export function guardSector(offset, bytes, prior) {
  if (offset % 4096 || !inWindow(offset, 4096) || bytes.length !== 4096 || prior.length !== 4096)
    throw new Error("Percussion sector outside the allowed windows.");
  if (offset < 0xbb000) for (const data of [bytes, prior]) {
    const tag = data[0xf06] | (data[0xf07] << 8);
    if (!(tag & (~0x5441 & 0xffff))) throw new Error("Percussion sector could form an updater record.");
  }
}

export async function loadBank(fetcher = fetch) {
  const files = {};
  for (const [name, digest] of Object.entries(BANK_HASHES)) {
    const reply = await fetcher(`/elite/content/${name}`, { cache: "no-store" });
    if (!reply.ok) throw new Error("The matching percussion content is unavailable.");
    const bytes = new Uint8Array(await reply.arrayBuffer());
    if (await sha256(bytes) !== digest) throw new Error("Percussion content checksum mismatch.");
    files[name] = bytes;
  }
  const meta = JSON.parse(new TextDecoder().decode(files["layer727.json"]));
  // Flash destinations are bound to the release's independently checked plan.
  const offsets = [757855,643072,647152,762349,736212,927071,942795,723896,702372,710082,753207,717573,688128,862370,794624,742174,650172,730157,747918];
  if (meta.clips.length !== offsets.length) throw new Error("Invalid percussion clip count.");
  const clips = meta.clips.map((item, i) => {
    const bank = files[i < 15 ? "compas727.bin" : "hanan-cumbia.bin"];
    const size = i < 15 ? item.packed_bytes : item.frames;
    const data = bank.slice(item.offset, item.offset + size);
    if (data.length !== size || !inWindow(offsets[i], size)) throw new Error("Invalid percussion clip range.");
    return { offset: offsets[i], data };
  });
  for (let i = 0; i < clips.length; i++) for (let j = 0; j < i; j++)
    if (clips[i].offset < clips[j].offset + clips[j].data.length && clips[j].offset < clips[i].offset + clips[i].data.length)
      throw new Error("Overlapping percussion clips.");
  return clips;
}

export async function makeBackup(pool, binding, progress = () => {}) {
  const windows = [];
  for (const [lo, hi] of WINDOWS) {
    const data = await pool.readWindow(lo, hi, progress);
    if (!equal(data, await pool.readWindow(lo, hi, progress))) throw new Error("The two percussion backup reads disagree.");
    windows.push({ offset: lo, data: Array.from(data), sha256: await sha256(data) });
  }
  return { schema: "bb37-727-backup-v1", model: "SMK-37 Elite", identity: ELITE_ID, binding, windows };
}
export async function validateBackup(backup, binding) {
  if (backup?.schema !== "bb37-727-backup-v1" || backup.model !== "SMK-37 Elite" || ![ELITE_ID, "SMK-37 Elite_3737826"].includes(backup.identity) ||
      backup.binding !== binding || !Array.isArray(backup.windows) || backup.windows.length !== WINDOWS.length)
    throw new Error("This percussion backup does not match this Elite connection.");
  for (let i = 0; i < WINDOWS.length; i++) {
    const [lo, hi] = WINDOWS[i], item = backup.windows[i];
    if (item.offset !== lo || !Array.isArray(item.data) || item.data.length !== hi - lo ||
        item.data.some(b => !Number.isInteger(b) || b < 0 || b > 255) ||
        await sha256(Uint8Array.from(item.data)) !== item.sha256) throw new Error("Invalid percussion backup checksum or range.");
  }
  return backup.windows.map(w => ({ offset: w.offset, data: Uint8Array.from(w.data) }));
}
export function overlay(windows, clips) {
  const target = windows.map(w => ({ offset: w.offset, data: w.data.slice() }));
  for (const clip of clips) {
    if (!inWindow(clip.offset, clip.data.length)) throw new Error("Invalid percussion overlay range.");
    const window = target.find(w => clip.offset >= w.offset && clip.offset + clip.data.length <= w.offset + w.data.length);
    if (!window) throw new Error("Percussion overlay outside backup.");
    window.data.set(clip.data, clip.offset - window.offset);
  }
  return target;
}
export async function applyWindows(pool, before, after, progress = (done, total) => {}) {
  if (before.length !== WINDOWS.length || after.length !== WINDOWS.length) throw new Error("Incomplete percussion windows.");
  const changes = [];
  for (let i = 0; i < WINDOWS.length; i++) {
    const [lo, hi] = WINDOWS[i];
    if (before[i].offset !== lo || after[i].offset !== lo || before[i].data.length !== hi-lo || after[i].data.length !== hi-lo)
      throw new Error("Invalid percussion write windows.");
    for (let at = 0; at < hi-lo; at += 4096) {
      const old = before[i].data.slice(at, at+4096), data = after[i].data.slice(at, at+4096);
      if (!equal(old, data)) { guardSector(lo+at, data, old); changes.push({ offset:lo+at, data, old }); }
    }
  }
  // Preflight every changed sector before the first stage/commit.
  for (let i = 0; i < changes.length; i++) {
    const {offset,data,old}=changes[i]; await pool.ready(); await pool.sector(offset,data,old); progress(i+1,changes.length);
  }
  for (const target of after)
    if (!equal(await pool.readWindow(target.offset,target.offset+target.data.length),target.data)) throw new Error("Percussion window readback mismatch.");
  return changes.length;
}
export async function ensureContent(pool, binding, clips, persistBackup, progress = (done, total) => {}) {
  const info = await pool.ready();
  if (info.mounted) return { written: 0, alreadyMounted: true };
  const backup = await makeBackup(pool,binding,() => progress(0,0));
  const before = await validateBackup(backup,binding), after = overlay(before,clips);
  // Durable storage must succeed before the first flash operation.
  await persistBackup(backup);
  const written = await applyWindows(pool,before,after,progress);
  if (!await pool.mount() || !(await pool.info()).mounted) throw new Error("The percussion content did not mount. Keep the backup for restoration.");
  return { written, alreadyMounted: false };
}
export async function restoreContent(pool, backup, binding, persistReturn, progress = (done, total) => {}) {
  const after=await validateBackup(backup,binding); await pool.ready();
  const current=await makeBackup(pool,binding);
  await persistReturn(current);
  const before=await validateBackup(current,binding);
  await applyWindows(pool,before,after,progress);
  const mounted=await pool.mount();
  return { mounted };
}
