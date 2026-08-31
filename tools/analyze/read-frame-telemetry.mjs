#!/usr/bin/env node
/**
 * read-frame-telemetry.mjs — lee la telemetría por frame de una demo.
 *
 * La demo 107 (y las que usen `g_eng_frame_telemetry`) escribe cada frame el
 * coste de render en un bloque de 20 bytes (ver `engine/include/eng/debug/
 * run_status.hpp`, struct `FrameTelemetry`):
 *
 *   offset 0  u32 frame        frame_index
 *   offset 4  u16 blit_jobs    nº de blits encolados este frame
 *   offset 6  u16 blit_words   words de Blitter este frame (todos los planos)
 *   offset 8  u16 copper_words words de la copperlist emitida
 *   offset 10 u16 fillup_extra blits extra del ajuste de fillup (picos)
 *
 * Requiere WinUAE-DBG corriendo con la demo (`--keep-running`) y el `.map` de la
 * demo para resolver el símbolo. La reubicación es la misma que run-demo:
 * `runtime = sections[0] + (link - 0x400)`.
 *
 * Uso:
 *   node tools/analyze/read-frame-telemetry.mjs <demo> [samples] [--port 2346]
 *   # p. ej. con la demo viva:
 *   node tools/analyze/read-frame-telemetry.mjs demos/107_xlimited_corkscrew 20
 *
 * Salida: una línea por frame con frame, blit_jobs, blit_words, copper_words.
 * Permite comprobar que la carga es homogénea (blit_jobs ~constante salvo los
 * picos de fillup en los cruces de tile).
 */
import { readFileSync, existsSync } from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..', '..');
const { sideChannelCommand } = require(path.join(ROOT, '../../Amiga/mcp-winuae-emu/dist/side-channel.js'));

const demo = process.argv[2];
const samples = parseInt(process.argv[3] || '10', 10);
const port = parseInt(process.argv[process.argv.indexOf('--port') + 1] || '2346', 10);
if (!demo) { console.error('Uso: node tools/analyze/read-frame-telemetry.mjs <demo> [samples] [--port P]'); process.exit(2); }

const demoName = path.basename(demo);
const mapPath = path.join(ROOT, 'out/demos', demoName, `${demoName}.map`);
const reportPath = path.join(ROOT, 'out/run', demoName, 'run-report.json');
if (!existsSync(mapPath)) { console.error(`No existe ${mapPath}`); process.exit(1); }
if (!existsSync(reportPath)) { console.error(`No existe ${reportPath} (ejecuta run-demo --keep-running primero)`); process.exit(1); }

// Resolver el símbolo g_eng_frame_telemetry desde el .map (línea: 0xADDR  nombre).
let link = null;
for (const line of readFileSync(mapPath, 'utf8').split('\n')) {
  const m = line.match(/^\s*0x([0-9a-fA-F]+)\s+g_eng_frame_telemetry\s*$/);
  if (m) { link = parseInt(m[1], 16); break; }
}
if (link === null) { console.error('No se encontró g_eng_frame_telemetry en el .map'); process.exit(1); }

// Base runtime: sections[0] del side channel guardado en el run-report.
const report = JSON.parse(readFileSync(reportPath, 'utf8'));
const sections = report?.sideChannel?.state?.sections;
if (!sections || !sections.length) { console.error('Sin sections en el run-report; usa run-demo --keep-running'); process.exit(1); }
const base = parseInt(sections[0], 16);
const addr = base + (link - 0x400);
console.log(`g_eng_frame_telemetry link=0x${link.toString(16)} runtime=0x${addr.toString(16)} (base 0x${base.toString(16)})`);

for (let i = 0; i < samples; ++i) {
  const r = await sideChannelCommand(`mem 0x${addr.toString(16)} 20`, port);
  let b = null;
  if (r.ok && r.reply) {
    const hex = typeof r.reply === 'string' ? r.reply : JSON.stringify(r.reply);
    const m = hex.replace(/\s+/g, '').replace(/^.*?:/, '').match(/.{2}/g);
    if (m) b = m.map(h => parseInt(h, 16));
  }
  if (!b || b.length < 20) { console.error('mem falló:', r.error || r.raw || ''); process.exit(1); }
  const frame = ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]) >>> 0;
  const jobs = (b[4] << 8) | b[5];
  const words = (b[6] << 8) | b[7];
  const copper = (b[8] << 8) | b[9];
  const fill = (b[10] << 8) | b[11];
  console.log(`frame=${frame}  blit_jobs=${jobs}  blit_words=${words}  copper_words=${copper}  fillup_extra=${fill}`);
  await new Promise(r2 => setTimeout(r2, 60));
}
