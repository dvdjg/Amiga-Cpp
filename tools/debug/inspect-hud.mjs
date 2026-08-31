import { readFileSync } from 'fs';
import path from 'path';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const ROOT = process.cwd();
const { sideChannelCommand } = await import('file:///C:/Users/dvdjg/Documents/programa/AI/Amiga/mcp-winuae-emu/dist/side-channel.js');
const demoName = '107_xlimited_corkscrew';
const mapPath = path.join(ROOT, 'out/demos', demoName, `${demoName}.map`);
const reportPath = path.join(ROOT, 'out/run', demoName, 'run-report.json');

function symAddr(name) {
  for (const line of readFileSync(mapPath, 'utf8').split('\n')) {
    const m = line.match(new RegExp(`^\\s*0x([0-9a-fA-F]+)\\s+${name}\\s*$`));
    if (m) return parseInt(m[1], 16);
  }
  return null;
}
const report = JSON.parse(readFileSync(reportPath, 'utf8'));
// Base runtime EN VIVO: consultar el state del canal lateral (no el run-report,
// que puede estar obsoleto tras un relink).
const st = await sideChannelCommand('state', 2346, 4000);
const stSections = st?.reply?.sections;
const base = stSections && stSections.length ? parseInt(stSections[0], 16) : parseInt(report?.sideChannel?.state?.sections[0], 16);

async function readMem(addr, len) {
  const r = await sideChannelCommand(`mem 0x${addr.toString(16)} ${len}`, 2346, 4000);
  if (!r.ok || !r.reply || !r.reply.data) throw new Error('mem ' + (r.error || r.raw));
  const hex = String(r.reply.data).replace(/\s+/g, '');
  return hex.match(/.{2}/g).map(h => parseInt(h, 16));
}
async function readU32At(addr) {
  const b = await readMem(addr, 4);
  return ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]) >>> 0;
}
async function readU16At(addr) {
  const b = await readMem(addr, 2);
  return (b[0] << 8) | b[1];
}

const copper = await readU32At(base + (symAddr('g_dbg_copper') - 0x400));
const words = await readU16At(base + (symAddr('g_dbg_copper_words') - 0x400));
const hud = await readU32At(base + (symAddr('g_dbg_hud_base') - 0x400));
console.log(`base=0x${base.toString(16)} link_copper=0x${(symAddr('g_dbg_copper')).toString(16)} runtime_copper=0x${(base + (symAddr('g_dbg_copper') - 0x400)).toString(16)}`);
console.log(`copper=0x${copper.toString(16)} words=${words} hud_base=0x${hud.toString(16)}`);

// Decodificar el copper: buscar el WAIT de la zona HUD y los BPL moves siguientes.
const c = await readMem(copper, Math.min(words * 2, 400));
const W = [];
for (let i = 0; i < words; i++) W.push((c[i * 2] << 8) | c[i * 2 + 1]);
console.log('--- copper (words) ---');
console.log(W.map(w => '0x' + w.toString(16).padStart(4, '0')).join(' '));

// Buscar WAITs (segunda palabra de un par con & 0xff00 != 0 => instrucción WAIT)
console.log('--- análisis ---');
let i = 0;
while (i + 1 < W.length) {
  const w1 = W[i], w2 = W[i + 1];
  const isWait = (w2 & 0x00ff) === 0x00 && w1 !== 0xffff; // máscara con vpos mask
  const isMove = w1 < 0x100; // registro custom bajo = MOVE
  if (w1 === 0xffff && w2 === 0xfffe) { console.log(`[${i}] END`); break; }
  if (isWait && (w2 & 0xff00) === 0xff00) {
    console.log(`[${i}] WAIT vpos(high)=0x${((w1 >> 8) & 0xff).toString(16)} hpos=0x${(w1 & 0xff).toString(16)} mask=0x${w2.toString(16)}`);
    i += 2;
  } else if (isMove) {
    const reg = w1 & 0x1fe;
    const name = ['DMACON','BPLCON0','BPLCON1','BPLCON2','','','DIWSTRT','DIWSTOP','DDFSTRT','DDFSTOP', ...Array(22).fill('')][reg >> 1] || `reg$${reg.toString(16)}`;
    console.log(`[${i}] MOVE ${name} = 0x${w2.toString(16).padStart(4, '0')}`);
    i += 2;
  } else {
    // pointer move: registro alto + valor => 2 pairs
    const reg = w1 & 0x1fe;
    console.log(`[${i}] MOVE reg$${reg.toString(16)} = 0x${w2.toString(16).padStart(4, '0')} (parte alta de BPLxPT?)`);
    i += 2;
  }
}

// Canvas: primeros 64 bytes + fila 2 plano 0 (línea blanca x=2)
const m0 = await readMem(hud, 32);
console.log('canvas[0..31]:', m0.map(x => x.toString(16).padStart(2, '0')).join(' '));
const m2 = await readMem(hud + 320, 16); // fila 2 plano 0
console.log('canvas[320..335] (fila2 p0):', m2.map(x => x.toString(16).padStart(2, '0')).join(' '));

// BPLCON0 y BPL1PT vivos
const bplcon0 = await readU16At(0xdff100);
const bpl1pthi = await readU16At(0xdff120);
const bpl1ptlo = await readU16At(0xdff122);
const diwstop = await readU16At(0xdff108);
console.log(`BPLCON0=0x${bplcon0.toString(16)} BPL1PT=0x${((bpl1pthi << 16) | bpl1ptlo).toString(16)} DIWSTOP=0x${diwstop.toString(16)}`);
process.exit(0);
