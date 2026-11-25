import {readAsset} from "./assets.mjs";
const pins = {"SMK-37 Elite": {"product": "SMK-37 Elite_3737852", "sha256": "cca38af3451ef5ce111a37d70bd604d39815b9c1f1134f806c4fe179f392db79", "file": "Firmware/BB-37-Elite-852.fwsc"}, "SMK-37 Pro": {"product": "SMK-37 Pro_3737852", "sha256": "e214f4c3b6c679187c37a1e10733c31cb2e543e95ddc1f07cc06fa8b78e8a50e", "file": "Firmware/BB-37-Pro-852.fwsc"}, "MKE-P37": {"product": "MKE-P37_3737852", "sha256": "efab8199c331c71146896bed8beacb377dbd5a0a66e4615dd2f8e1158cf983c0", "file": "Firmware/BB-37-MKE-P37-852.fwsc"}};
import {loadBank,verifyEliteApplication,sha256 as digest} from "./pool.mjs";

const MODELS=Object.freeze({
  "SMK-37 Pro":"/elite/firmware/bb37-pro.fwsc",
  "SMK-37 Elite":"/elite/content/BB-37-Elite-Build-852.fwsc",
  "MKE-P37":"/elite/firmware/bb37-mke-p37.fwsc",
});

export function acceptsInstallerSource(device){return Object.hasOwn(MODELS,device.model);}
export function installerLoaderFilter(model){return device=>device.model===`ota-${model}`;}

export function assertUnambiguousPortPairs(access){
  const inputs=[...access.inputs.values()].filter(port=>port.state!=="disconnected");
  const outputs=[...access.outputs.values()].filter(port=>port.state!=="disconnected");
  for(const input of inputs){
    const matchingOutputs=outputs.filter(port=>port.name===input.name);
    if(matchingOutputs.length>1 || (matchingOutputs.length && inputs.filter(port=>port.name===input.name).length>1))
      throw new Error("More than one MIDI port shares the same name. Connect one SMK-37 and close duplicate MIDI ports before installing.");
  }
}

function productOf(packageBytes){
  const blocks=20,blockSize=0x30,marker=0x2f;
  if(packageBytes.length<blocks*blockSize)throw new Error("The BB-37 package is incomplete.");
  let product="";
  for(let index=0;index<blocks;index++){
    const value=packageBytes[index*blockSize+marker];
    if(value!==0x7d)product+=String.fromCharCode((value-index-1)&0xff);
  }
  return product;
}
function logicalImage(packageBytes){
  const blocks=20,blockSize=0x30,keep=0x2f;
  const image=new Uint8Array(packageBytes.length-blocks);
  for(let index=0;index<blocks;index++)image.set(packageBytes.subarray(index*blockSize,index*blockSize+keep),index*keep);
  image.set(packageBytes.subarray(blocks*blockSize),blocks*keep);
  return image;
}

// The real page supplies the upgrade operation. Admission and all package/content
// checks complete here before that operation can send an UPGRADE command.
export async function prepareInstallerSession(device,{fetcher=readAsset,startUpgrade}){
  if(!acceptsInstallerSource(device))throw new Error("No matching SMK-37 application answered.");
  const model=device.model;
  const loaded=await fetcher(MODELS[model],{cache:"no-store"});
  if(!loaded.ok)throw new Error("The matching BB-37 package is not ready yet.");
  const bytes=new Uint8Array(await loaded.arrayBuffer());
  const product=productOf(bytes);
  if(!new RegExp(`^${model.replace(/[.*+?^${}()|[\]\\]/g,"\\$&")}_\\d+$`).test(product))
    throw new Error("The matching BB-37 package could not be verified.");
  if(product!==pins[model].product || await digest(bytes)!==pins[model].sha256)
    throw new Error("The firmware failed its identity or checksum check.");
  const elite=model==="SMK-37 Elite";
  if(elite)await verifyEliteApplication(bytes,product);
  const bank=elite?await loadBank(fetcher):null;
  const prepared={model,product,image:logicalImage(bytes),elite,bank,firmwareRequired:elite || device.text!==product};
  if(prepared.firmwareRequired)await startUpgrade(prepared);
  return prepared;
}

// Returning to M-VAVE's firmware. The user supplies M-VAVE's own version 016
// package; it goes through the same two-stage exchange as an install. A unit
// keeps its own model identity throughout: only the connected model's 016
// file is accepted, and M-VAVE's unmodified file is pinned where its checksum
// is known.
export const STOCK_VERSION=16;
const STOCK_SHA256=Object.freeze({
  "SMK-37 Pro":null,
  "SMK-37 Elite":"b2ee38abfef3ae922e4eecdb8a551e14345300ae6f270265feacd33b6ef99cb6",
  "MKE-P37":null,
});

export function checkStockPackage(bytes,sha256){
  const product=productOf(bytes);
  const found=/^(.+)_(\d+)$/.exec(product);
  if(!found||!Object.hasOwn(MODELS,found[1]))
    throw new Error("This is not an M-VAVE firmware file for an SMK-37 Pro, SMK-37 Elite or MKE-P37.");
  const model=found[1],version=Number(found[2]);
  if(version!==STOCK_VERSION)
    throw new Error(`This file is version ${found[2]}. Choose M-VAVE's version 016 file for the ${model}.`);
  const pinned=STOCK_SHA256[model];
  if(pinned&&pinned!==sha256)
    throw new Error(`This file is not M-VAVE's unmodified ${product}.fwsc. Download it again from m-vave.com.`);
  return{model,product,sha256,pinned:Boolean(pinned),image:logicalImage(bytes)};
}

// The connected keyboard's running app, or the update loader an interrupted
// transfer left waiting, for the same model as the chosen file.
export function returnSourceFilter(model){return device=>device.model===model||device.model===`ota-${model}`;}

export async function prepareReturnSession(device,stock,{startUpgrade,resumeLoader}){
  if(!returnSourceFilter(stock.model)(device))
    throw new Error(`Connect the ${stock.model} that this file is for.`);
  if(device.text===stock.product)return{...stock,firmwareRequired:false};
  if(device.model===`ota-${stock.model}`){await resumeLoader(stock);return{...stock,firmwareRequired:true};}
  await startUpgrade(stock);
  return{...stock,firmwareRequired:true};
}
