"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { ELITE_ID, Pool, ensureContent, listBrowserBackups, persistBrowserBackup, readBrowserBackup, restoreContent, selectBrowserBackup, selectedBrowserBinding } from "./pool.mjs";
import { acceptsInstallerSource, assertUnambiguousPortPairs, checkStockPackage, installerLoaderFilter, prepareInstallerSession, prepareReturnSession, returnSourceFilter } from "./installer-policy.mjs";

// BB-37 uses the same two-stage MIDI update exchange as BB1.  The keyboard
// asks for each part of the package; the browser supplies it.  Keeping this
// here means the public installer is a web page, not a separate utility.
const HS_QUERY = [0xf0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7f, 0xf7];
const UPGRADE = [0xf0, 0x22, 0x24, 0x35, 0x7f, 0xf7];
const CHECK_DONE = 0xe0000000;
const WRITE_DONE = 0xf0000000;
const MAX_BLOCK = 512;

type Identity = { text: string; model: string; version: number };
type MidiPort = { id?: string; name?: string; state?: string; open: () => Promise<unknown>; onmidimessage: ((event: { data: Uint8Array }) => void) | null };
type MidiOutput = { id?: string; name?: string; state?: string; open: () => Promise<unknown>; send: (message: number[]) => void };
type MidiAccess = { inputs: Map<string, MidiPort>; outputs: Map<string, MidiOutput> };

const wait = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));
class UnitLost extends Error {}

function unpack7(source: number[]) {
  const out: number[] = [];
  let value = 0;
  let bits = 0;
  for (const byte of source) {
    value |= (byte & 0x7f) << bits;
    bits += 7;
    while (bits >= 8) {
      out.push(value & 0xff);
      value >>>= 8;
      bits -= 8;
    }
  }
  return out;
}

function pack7(source: number[]) {
  const out: number[] = [];
  let value = 0;
  let bits = 0;
  for (const byte of source) {
    value |= byte << bits;
    bits += 8;
    while (bits >= 7) {
      out.push(value & 0x7f);
      value >>>= 7;
      bits -= 7;
    }
  }
  if (bits) out.push(value & 0x7f);
  return out;
}

function identity(packet: number[]): Identity | null {
  if (packet[0] !== 0xf0 || packet.at(-1) !== 0xf7) return null;
  const decoded = unpack7(packet.slice(1, -1));
  if (decoded.length !== 34 || decoded[0] !== 0 || decoded[1] !== 0x59 || decoded[2] !== 0x11) return null;
  const text = String.fromCharCode(...decoded.slice(6, 33)).replace(/\0+$/, "");
  const found = /^(.+)_(\d+)$/.exec(text);
  return found ? { text, model: found[1], version: Number(found[2]) } : null;
}

function request(packet: number[]) {
  if (packet[0] !== 0xf0 || packet.at(-1) !== 0xf7) return null;
  const decoded = unpack7(packet.slice(1, -1));
  if (decoded.length !== 15 || decoded[0] !== 0 || decoded[1] !== 0x59 || decoded[2] !== 0x30) return null;
  let check = 0;
  for (let index = 6; index < 14; index++) check += decoded[index];
  if ((~check & 0xff) !== decoded[14]) return null;
  return {
    flags: decoded[6],
    address: (decoded[7] | (decoded[8] << 8) | (decoded[9] << 16) | (decoded[10] << 24)) >>> 0,
    length: decoded[11] | (decoded[12] << 8) | (decoded[13] << 16),
  };
}

function response(address: number, data: number[], flags = 0) {
  const length = data.length;
  const body = [0x00, 0x59, 0x30, (length + 8) & 0xff, ((length + 8) >>> 8) & 0xff, 0, flags,
    address & 0xff, (address >>> 8) & 0xff, (address >>> 16) & 0xff, (address >>> 24) & 0xff,
    length & 0xff, (length >>> 8) & 0xff, 0, ...data];
  let check = 0;
  for (let index = 6; index < body.length; index++) check += body[index];
  body.push(~check & 0xff);
  return [0xf0, ...pack7(body), 0xf7];
}

class Link {
  private queue: number[][] = [];
  private wake: (() => void) | null = null;

  constructor(readonly input: MidiPort, readonly output: MidiOutput) {
    input.onmidimessage = ({ data }) => {
      if (data[0] !== 0xf0) return;
      this.queue.push(Array.from(data));
      this.wake?.();
      this.wake = null;
    };
  }

  close() { this.input.onmidimessage = null; }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }

  send(message: number[]) {
    try { this.output.send(message); return true; } catch { return false; }
  }

  async read(timeout: number) {
    if (!this.queue.length) {
      await Promise.race([
        new Promise<void>((resolve) => { this.wake = resolve; }),
        wait(timeout),
      ]);
      this.wake = null;
    }
    return this.queue.shift() ?? null;
  }
}

async function handshake(link: Link) {
  for (let tries = 0; tries < 3; tries++) {
    if (!link.send(HS_QUERY)) return null;
    const deadline = Date.now() + 1000;
    while (Date.now() < deadline) {
      const packet = await link.read(deadline - Date.now());
      const found = packet && identity(packet);
      if (found) return found;
    }
  }
  return null;
}

async function find(access: MidiAccess, accepts: (device: Identity) => boolean) {
  assertUnambiguousPortPairs(access);
  const matches: { link: Link; device: Identity }[] = [];
  for (const input of access.inputs.values()) {
    if (input.state === "disconnected") continue;
    const output = [...access.outputs.values()].find((candidate) => candidate.name === input.name && candidate.state !== "disconnected");
    if (!output) continue;
    try { await input.open(); await output.open(); } catch { continue; }
    const link = new Link(input, output);
    const found = await handshake(link);
    if (found && accepts(found)) matches.push({ link, device: found });
    else link.close();
  }
  if (matches.length > 1) {
    for (const match of matches) match.link.close();
    throw new Error("More than one SMK-37 matches this install stage. Choose one keyboard and retry.");
  }
  return matches[0] ?? null;
}

async function waitFor(access: MidiAccess, accepts: (device: Identity) => boolean, timeout: number) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const found = await find(access, accepts);
    if (found) return found;
    await wait(750);
  }
  return null;
}

async function serve(link: Link, image: Uint8Array, finish: number, timeout: number, update: (served: number) => void) {
  let served = 0;
  let last = Date.now();
  for (;;) {
    const packet = await link.read(1000);
    if (!packet) {
      if (link.lost) throw new UnitLost("The SMK-37 changed USB ports during the update.");
      if (Date.now() - last > timeout) throw new Error("The keyboard stopped answering. Disconnect and reconnect it, then press Install again.");
      continue;
    }
    const next = request(packet);
    if (!next) continue;
    last = Date.now();
    if (next.address === CHECK_DONE || next.address === WRITE_DONE) {
      if (next.address !== finish) throw new Error("The keyboard asked for the wrong update stage.");
      if (!link.send(response(next.address, Array.from(new TextEncoder().encode("success\0"))))) throw new UnitLost("Connection lost before completion.");
      return served;
    }
    if (next.length > MAX_BLOCK || next.address + next.length > image.length) throw new Error("The keyboard requested an invalid part of the update.");
    if (!link.send(response(next.address, Array.from(image.subarray(next.address, next.address + next.length)), next.flags))) {
      throw new UnitLost("The USB connection was lost during the update.");
    }
    served++;
    update(served);
  }
}

function binding(link: Link) {
  if (!link.input.id || !link.output.id) throw new Error("The browser cannot identify this Elite MIDI connection.");
  return JSON.stringify([link.input.id, link.output.id]);
}

function saveBackup(value: unknown) {
  persistBrowserBackup(value, localStorage);
}

function downloadBackup(value: string) {
  const url = URL.createObjectURL(new Blob([value], { type: "application/json" }));
  const anchor = document.createElement("a");
  anchor.href = url; anchor.download = "BB-37-percussion-backup.json"; anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export { find as findEliteInstallerDevice };

export default function EliteInstaller() {
  const [status, setStatus] = useState("BB-37 is ready to install.");
  const [busy, setBusy] = useState(false);
  const [hasBackup, setHasBackup] = useState(false);
  const [backupBinding, setBackupBinding] = useState<string | null>(null);
  const [savedBindings, setSavedBindings] = useState<string[]>([]);
  const inFlight = useRef(false);
  useEffect(() => {
    try {
      setSavedBindings(listBrowserBackups(localStorage));
      setBackupBinding(selectedBrowserBinding(localStorage));
    } catch { setStatus("The saved percussion backups could not be read. Keep any downloaded copies."); }
  }, []);

  const install = useCallback(async () => {
    if (busy || inFlight.current) return;
    inFlight.current = true;
    setBusy(true);
    let activeLink: Link | undefined;
    try {
      const getMidi = (navigator as Navigator & { requestMIDIAccess?: (options: { sysex: boolean }) => Promise<MidiAccess> }).requestMIDIAccess;
      if (!getMidi) throw new Error("Use the latest Chrome or Edge to install BB-37.");
      setStatus("Choose your SMK-37 in the browser’s MIDI prompt.");
      const midi = await getMidi.call(navigator, { sysex: true });
      setStatus("Finding your SMK-37…");
      const app = await find(midi, id => acceptsInstallerSource({...id,model:id.model.replace(/^ota-/, "")}));
      if (!app) throw new Error("No SMK-37 answered. Check the USB data cable, close other MIDI apps, and try again.");
      activeLink = app.link;
      setStatus("Getting the matching BB-37 package…");
      let complete = app;
      const {elite,bank} = await prepareInstallerSession({...app.device,model:app.device.model.replace(/^ota-/, "")}, {startUpgrade: async ({model,product,image}: {model: string; product: string; image: Uint8Array}) => {
      const resuming = app.device.model === "ota-"+model;
      if (!resuming) {
      setStatus("Preparing BB-37…");
      if (!app.link.send(UPGRADE)) throw new Error("The selected keyboard disconnected.");
      await serve(app.link, image, CHECK_DONE, 10_000, () => setStatus("Preparing BB-37…"));
      app.link.close();
      }
      setStatus("Installing BB-37…");
      let loader = resuming ? app : await waitFor(midi, installerLoaderFilter(model), 30_000);
      if (!loader) throw new Error("The installer could not reconnect to your SMK-37. Keep it connected and press Install again.");
      activeLink = loader.link;
      let written = false;
      for (let attempt = 0; attempt < 3 && !written; attempt++) {
        try {
          if (!loader.link.send(UPGRADE)) throw new UnitLost("The SMK-37 loader changed USB ports.");
          await serve(loader.link, image, WRITE_DONE, 180_000, () => setStatus("Installing BB-37…"));
          written = true;
        } catch (error) {
          if (!(error instanceof UnitLost) || attempt === 2) throw error;
          setStatus("Reconnecting to the BB-37 loader…");
        } finally {
          loader.link.close();
        }
        if (!written) {
          const reconnected = await waitFor(midi, installerLoaderFilter(model), 15_000);
          if (!reconnected) throw new Error("The installer could not reconnect to your SMK-37 loader. Keep it connected and press Install again.");
          loader = reconnected;
          activeLink = loader.link;
        }
      }
      setStatus("Restarting your SMK-37…");
      const returned = await waitFor(midi, (device) => device.text === product, 40_000);
      if (!returned) throw new Error("BB-37 did not return after the update. Restart your SMK-37, then press Install again.");
      complete = returned;
      }});
      activeLink = complete.link;
      try {
        if (elite && bank) {
          const connectedBinding = binding(complete.link);
          setBackupBinding(connectedBinding);
          selectBrowserBackup(connectedBinding, localStorage);
          setStatus("Checking the Elite percussion content…");
          const pool = new Pool(complete.link, complete.device);
          await ensureContent(pool, connectedBinding, bank, (backup: unknown) => {
            saveBackup(backup); setHasBackup(true); setSavedBindings(listBrowserBackups(localStorage));
          }, (done: number, total: number) => setStatus(total ? `Installing percussion: ${done} of ${total}…` : "Saving two checked percussion backup reads…"));
        }
        setStatus(elite ? "BB-37 Elite build 852 and its percussion content are ready. Play the synths." : "BB-37 is installed. Play the synths.");
      } finally { complete.link.close(); }
    } catch (error) {
      setStatus(error instanceof Error ? error.message : "The install did not finish.");
    } finally {
      activeLink?.close();
      inFlight.current = false;
      setBusy(false);
    }
  }, [busy]);

  const restore = useCallback(async (file: File) => {
    if (busy || inFlight.current) return;
    inFlight.current = true;
    setBusy(true);
    let link: Link | undefined;
    try {
      if (file.size > 2_000_000) throw new Error("That percussion backup is too large.");
      const backup = JSON.parse(await file.text());
      const getMidi = (navigator as Navigator & { requestMIDIAccess?: (options: { sysex: boolean }) => Promise<MidiAccess> }).requestMIDIAccess;
      if (!getMidi) throw new Error("Use Chrome or Edge to restore percussion.");
      const midi = await getMidi.call(navigator, { sysex: true });
      const app = await find(midi, device => device.text === ELITE_ID);
      if (!app) throw new Error("Connect the same Elite with BB-37 build 852 to restore this backup.");
      link = app.link;
      const connectedBinding = binding(link);
      setBackupBinding(connectedBinding);
      selectBrowserBackup(connectedBinding, localStorage);
      setStatus("Checking the percussion backup and saving a return copy…");
      const result = await restoreContent(new Pool(link, app.device), backup, connectedBinding, (current: unknown) => {
        saveBackup(current); setHasBackup(true); setSavedBindings(listBrowserBackups(localStorage));
      }, (done: number, total: number) => setStatus(`Restoring percussion: ${done} of ${total}…`));
      setStatus(result.mounted ? "Percussion backup restored and verified." : "Previous percussion bytes restored and verified. Install BB-37 to load its matching percussion.");
    } catch (error) { setStatus(error instanceof Error ? error.message : "Percussion restore did not finish."); }
    finally { link?.close(); inFlight.current = false; setBusy(false); }
  }, [busy]);

  return <div className="fm1-actions" aria-live="polite">
    <button className="fm1-button" type="button" disabled={busy} onClick={() => void install()}>
      {busy ? "Installing BB-37…" : "Install BB-37"} <span aria-hidden="true">→</span>
    </button>
    <label className="fm1-caption">Saved Elite percussion backup <select disabled={busy} value={backupBinding ?? ""} onChange={event => {
      setBackupBinding(event.target.value || null);
      if (event.target.value) selectBrowserBackup(event.target.value, localStorage);
    }}>
      <option value="">Choose a saved Elite</option>
      {savedBindings.map((value, index) => <option key={value} value={value}>Elite backup {index + 1}</option>)}
      {backupBinding && !savedBindings.includes(backupBinding) && <option value={backupBinding}>Connected Elite — no backup needed yet</option>}
    </select></label>
    <button className="fm1-button" type="button" disabled={busy} onClick={() => {
      const value = readBrowserBackup(localStorage, backupBinding);
      if (value) downloadBackup(JSON.stringify(value)); else setStatus("No original percussion backup is stored for the selected Elite.");
    }}>Download Elite percussion backup {hasBackup ? "→" : ""}</button>
    <button className="fm1-button" type="button" disabled={busy} onClick={() => {
      const value = readBrowserBackup(localStorage, backupBinding, "return");
      if (value) downloadBackup(JSON.stringify(value)); else setStatus("No additional percussion return copy is stored for the selected Elite.");
    }}>Download latest return copy →</button>
    <label className="fm1-caption">Restore an Elite percussion backup <input type="file" accept="application/json,.json" disabled={busy} onChange={event => {
      const file = event.target.files?.[0]; event.target.value = ""; if (file) void restore(file);
    }} /></label>
    <p className="fm1-caption">{status}</p>
  </div>;
}

type StockPackage = { model: string; product: string; sha256: string; pinned: boolean; image: Uint8Array };

async function sha256Hex(bytes: Uint8Array) {
  const digest = await crypto.subtle.digest("SHA-256", new Uint8Array(bytes));
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("");
}

// Return the keyboard to M-VAVE's firmware with the user's own copy of
// M-VAVE's version 016 package, through the same exchange as an install.
export function ReturnToMvave() {
  const [stock, setStock] = useState<StockPackage | null>(null);
  const [confirmed, setConfirmed] = useState(false);
  const [status, setStatus] = useState("Choose M-VAVE’s version 016 file for your keyboard.");
  const [busy, setBusy] = useState(false);
  const inFlight = useRef(false);

  const choose = useCallback(async (file: File) => {
    setStock(null);
    setConfirmed(false);
    try {
      if (file.size > 4_000_000) throw new Error("That file is too large to be an SMK-37 firmware package.");
      const bytes = new Uint8Array(await file.arrayBuffer());
      const checked = checkStockPackage(bytes, await sha256Hex(bytes)) as StockPackage;
      setStock(checked);
      setStatus(checked.pinned
        ? `M-VAVE’s ${checked.product} is verified. Connect the ${checked.model} and press Return to M-VAVE firmware.`
        : `${checked.product} read. Its SHA-256 is ${checked.sha256}. Use only the file from m-vave.com.`);
    } catch (error) {
      setStatus(error instanceof Error ? error.message : "That file could not be read.");
    }
  }, []);

  const restore = useCallback(async () => {
    if (!stock || busy || inFlight.current || (!stock.pinned && !confirmed)) return;
    inFlight.current = true;
    setBusy(true);
    let activeLink: Link | undefined;
    try {
      const getMidi = (navigator as Navigator & { requestMIDIAccess?: (options: { sysex: boolean }) => Promise<MidiAccess> }).requestMIDIAccess;
      if (!getMidi) throw new Error("Use the latest Chrome or Edge to return to M-VAVE firmware.");
      setStatus("Choose your SMK-37 in the browser’s MIDI prompt.");
      const midi = await getMidi.call(navigator, { sysex: true });
      setStatus(`Finding your ${stock.model}…`);
      const found = await find(midi, returnSourceFilter(stock.model));
      if (!found) throw new Error(`No ${stock.model} answered. Check the USB data cable, close other MIDI apps, and try again.`);
      activeLink = found.link;
      const write = async (loader: { link: Link }) => {
        let current = loader;
        let written = false;
        for (let attempt = 0; attempt < 3 && !written; attempt++) {
          activeLink = current.link;
          try {
            if (!current.link.send(UPGRADE)) throw new UnitLost("The SMK-37 loader changed USB ports.");
            await serve(current.link, stock.image, WRITE_DONE, 180_000, () => setStatus("Installing M-VAVE firmware… keep the cable connected."));
            written = true;
          } catch (error) {
            if (!(error instanceof UnitLost) || attempt === 2) throw error;
            setStatus("Reconnecting to the update loader…");
          } finally {
            current.link.close();
          }
          if (!written) {
            const reconnected = await waitFor(midi, installerLoaderFilter(stock.model), 15_000);
            if (!reconnected) throw new Error("The page could not reconnect to the update loader. Keep the keyboard connected and press Return again.");
            current = reconnected;
          }
        }
      };
      const prepared = await prepareReturnSession(found.device, stock, {
        startUpgrade: async () => {
          setStatus("Preparing M-VAVE firmware…");
          found.link.send(UPGRADE);
          await serve(found.link, stock.image, CHECK_DONE, 10_000, () => setStatus("Preparing M-VAVE firmware…"));
          found.link.close();
          const loader = await waitFor(midi, installerLoaderFilter(stock.model), 30_000);
          if (!loader) throw new Error(`The page could not reconnect to your ${stock.model}. Keep it connected and press Return again.`);
          await write(loader);
        },
        resumeLoader: async () => { await write(found); },
      });
      if (prepared.firmwareRequired) {
        setStatus(`Restarting your ${stock.model}…`);
        const returned = await waitFor(midi, (device) => device.text === stock.product, 40_000);
        if (!returned) throw new Error(`M-VAVE’s firmware did not report back. Restart your ${stock.model}, then check its screen.`);
        returned.link.close();
      }
      setStatus(`Your ${stock.model} is on M-VAVE’s firmware (version 016). Other SMK-37 installers will recognise it again.`);
    } catch (error) {
      setStatus(error instanceof Error ? error.message : "The return did not finish.");
    } finally {
      activeLink?.close();
      inFlight.current = false;
      setBusy(false);
    }
  }, [busy, confirmed, stock]);

  return <div className="fm1-actions" aria-live="polite">
    <label className="fm1-caption">M-VAVE firmware file (.fwsc) <input type="file" accept=".fwsc" disabled={busy} onChange={event => {
      const file = event.target.files?.[0]; event.target.value = ""; if (file) void choose(file);
    }} /></label>
    {stock && !stock.pinned ? <label className="fm1-caption">
      <input type="checkbox" checked={confirmed} disabled={busy} onChange={event => setConfirmed(event.target.checked)} /> This is M-VAVE’s unmodified file, downloaded from m-vave.com.
    </label> : null}
    <button className="fm1-button" type="button" disabled={!stock || busy || (!stock.pinned && !confirmed)} onClick={() => void restore()}>
      {busy ? "Returning to M-VAVE firmware…" : "Return to M-VAVE firmware"} <span aria-hidden="true">→</span>
    </button>
    <p className="fm1-caption">{status}</p>
  </div>;
}
