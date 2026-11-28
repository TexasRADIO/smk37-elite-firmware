import {readFile, writeFile} from "node:fs/promises";
import {resolve} from "node:path";

const [elite, pro, mke] = process.argv.slice(2);
if (!elite || !pro || !mke) {
  throw new Error("usage: node installer/generate-assets.mjs ELITE.fwsc PRO.fwsc MKE.fwsc");
}

const root = resolve(import.meta.dirname, "..");
const files = {
  "/elite/content/BB-37-Elite-Build-856.fwsc": elite,
  "/elite/firmware/bb37-pro.fwsc": pro,
  "/elite/firmware/bb37-mke-p37.fwsc": mke,
  "/elite/content/compas727.bin": resolve(root, "assets/layer727/compas727.bin"),
  "/elite/content/hanan-cumbia.bin": resolve(root, "assets/layer727/hanan-cumbia.bin"),
  "/elite/content/layer727.json": resolve(root, "assets/layer727/layer727.json"),
};

const assets = {};
for (const [url, path] of Object.entries(files)) assets[url] = (await readFile(path)).toString("base64");
const source = `const assets = ${JSON.stringify(assets)};\n` +
  "export async function readAsset(key){return Object.hasOwn(assets,key)?new Response(Uint8Array.from(atob(assets[key]), c=>c.charCodeAt(0))):new Response(null,{status:404});}\n";
await writeFile(new URL("./assets.mjs", import.meta.url), source);
