#!/usr/bin/env node
// roof-quant-search.mjs — busca (con ollama local, visión) qué combinación de
// cuantización a 8 colores conserva mejor los detalles finos (p. ej. tejados de
// paja de las casitas) del mapa "Beginning Fields". Genera N variantes sobre un
// RECORTE del mapa (localizado por textura cálida+alta frecuencia), las monta en
// una sola imagen con la fuente como referencia y pregunta a un modelo de visión
// qué variante conserva mejor los detalles. Uso:
//   node tools/demo202/roof-quant-search.mjs [--tile 16|32] [--model qwen3-vl:8b-instruct-q8_0]
// Salida: imprime la variante ganadora (palette + dither) para re-cuantizar.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SRC = path.join(ROOT, 'tools/amiga-tiles/assets/Beginning Fields.png');
const OUT = path.join(ROOT, 'out/demo202/roofsearch');
fs.mkdirSync(OUT, { recursive: true });
const tile = process.argv.includes('--tile') ? parseInt(process.argv[process.argv.indexOf('--tile') + 1], 10) : 16;
const model = process.argv.includes('--model') ? process.argv[process.argv.indexOf('--model') + 1] : 'qwen3-vl:8b-instruct-q8_0';
const ollRaw = (process.env.OLLAMA_HOST || '').trim();
const OLLAMA = 'http://' + (/:[\d]+/.test(ollRaw) ? ollRaw : '127.0.0.1:11434');

const AT = path.join(ROOT, 'tools/amiga-tiles/amiga-tiles.mjs');
function runAmiga(crop, tag, palette, dither) {
  const dir = path.join(OUT, tag);
  fs.mkdirSync(dir, { recursive: true });
  const args = [AT, SRC, '--colors', '8', '--tile', String(tile),
    '--palette', palette, '--dither', dither,
    '--crop', crop.join(','), '--out', dir];
  execFileSync(process.execPath, args, { stdio: 'ignore' });
  const rec = fs.readdirSync(dir).find((f) => /^reconstruct_8c_.*\.png$/.test(f));
  if (!rec) throw new Error('no reconstruct en ' + dir);
  return path.join(dir, rec);
}

// 1) localizar recorte con tejados: tiles 16x16 con textura cálida + varianza.
const src = PNG.sync.read(fs.readFileSync(SRC));
const W = src.width, H = src.height;
function warmVar(px, py, ts) {
  let n = 0, rS = 0, gS = 0, bS = 0, warm = 0;
  for (let y = py; y < py + ts; y++) for (let x = px; x < px + ts; x++) {
    const o = (y * W + x) * 4, r = src.data[o], g = src.data[o + 1], b = src.data[o + 2];
    rS += r; gS += g; bS += b;
    if (r > 130 && g > 95 && b > 45 && r >= g && g >= b) warm++;
    n++;
  }
  const rm = rS / n, gm = gS / n, bm = bS / n;
  let v = 0;
  for (let y = py; y < py + ts; y++) for (let x = px; x < px + ts; x++) {
    const o = (y * W + x) * 4;
    v += Math.abs(src.data[o] - rm) + Math.abs(src.data[o + 1] - gm) + Math.abs(src.data[o + 2] - bm);
  }
  return { warmFrac: warm / n, var: v / n };
}
const ts = Math.min(16, tile); // muestreo de detalle
const score = [];
for (let ty = 0; ty + ts <= H; ty += ts) for (let tx = 0; tx + ts <= W; tx += ts) {
  const wv = warmVar(tx, ty, ts);
  // tejado: fracción cálida moderada + varianza alta
  score.push({ x: tx, y: ty, s: (wv.warmFrac > 0.15 && wv.warmFrac < 0.9) ? wv.var * wv.warmFrac : 0 });
}
score.sort((a, b) => b.s - a.s);
const top = score.slice(0, 12);
let x0 = Math.min(...top.map((t) => t.x)), x1 = Math.max(...top.map((t) => t.x + ts));
let y0 = Math.min(...top.map((t) => t.y)), y1 = Math.max(...top.map((t) => t.y + ts));
// ampliar margen y alinear a múltiplos de 32 (slicing limpio a 16/32)
const A = 32;
x0 = Math.max(0, Math.floor(x0 / A) * A); y0 = Math.max(0, Math.floor(y0 / A) * A);
x1 = Math.min(W, Math.ceil(x1 / A) * A); y1 = Math.min(H, Math.ceil(y1 / A) * A);
const crop = [x0, y0, x1 - x0, y1 - y0];
console.log('[roof] recorte detectado (tejados/textura): ' + crop.join(',') + ' (score ' + top.length + ' tiles)');
if (crop[2] < 32 || crop[3] < 32) { console.error('[roof] recorte demasiado pequeño'); process.exit(2); }

// 2) variantes a 8 colores
const combos = [
  ['adaptive', 'floyd', 'A adaptive/floyd (actual)'],
  ['mediancut', 'atkinson', 'B mediancut/atkinson'],
  ['adaptive', 'none', 'C adaptive/none'],
];
const rowPaths = combos.map(([p, d]) => runAmiga(crop, `v_${p}_${d}`, p, d));
const srcCrop = new PNG({ width: crop[2], height: crop[3] });
for (let y = 0; y < crop[3]; y++) for (let x = 0; x < crop[2]; x++) {
  const o = ((y0 + y) * W + x0 + x) * 4, d = (y * crop[2] + x) * 4;
  srcCrop.data[d] = src.data[o]; srcCrop.data[d + 1] = src.data[o + 1];
  srcCrop.data[d + 2] = src.data[o + 2]; srcCrop.data[d + 3] = 255;
}
// montaje: referencia arriba + una fila por variante, escalado 2x (nearest)
const rows = [srcCrop, ...rowPaths.map((p) => PNG.sync.read(fs.readFileSync(p)))];
const sc = 2, cw = crop[2], ch = crop[3];
const mont = new PNG({ width: cw * sc, height: rows.length * ch * sc });
const put = (rowImg, ry) => {
  for (let y = 0; y < ch; y++) for (let x = 0; x < cw; x++) {
    const o = (y * cw + x) * 4;
    for (let sy = 0; sy < sc; sy++) for (let sx = 0; sx < sc; sx++) {
      const d = (((ry * ch + y) * sc + sy) * mont.width + x * sc + sx) * 4;
      mont.data[d] = rowImg.data[o]; mont.data[d + 1] = rowImg.data[o + 1];
      mont.data[d + 2] = rowImg.data[o + 2]; mont.data[d + 3] = 255;
    }
  }
};
put(srcCrop, 0);
combos.forEach((c, i) => put(PNG.sync.read(fs.readFileSync(rowPaths[i])), i + 1));
const montPath = path.join(OUT, 'montage.png');
fs.writeFileSync(montPath, PNG.sync.write(mont));
console.log('[roof] montaje -> ' + montPath);

// 3) preguntar a ollama (visión)
const b64 = fs.readFileSync(montPath).toString('base64');
const prompt = 'La fila superior es la IMAGEN ORIGINAL. Debajo hay tres filas, cada una es la ' +
  'misma escena cuantizada a 8 colores con un algoritmo distinto (A, B, C en orden). ' +
  'Quiero conservar los DETALLES FINOS (tejados, texturas pequeñas) al reducir a 8 colores. ' +
  'Responde SOLO con la letra (A, B o C) de la fila que conserva mejor esos detalles.';
const body = JSON.stringify({ model, temperature: 0.1, max_tokens: 40, stream: false,
  messages: [{ role: 'user', content: [{ type: 'text', text: prompt },
    { type: 'image_url', image_url: { url: 'data:image/png;base64,' + b64 } }] }] });
const r = await fetch(OLLAMA + '/v1/chat/completions', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
if (!r.ok) { console.error('ollama ' + r.status + ': ' + (await r.text()).slice(0, 300)); process.exit(1); }
const j = await r.json();
const ans = (j.choices?.[0]?.message?.content || '').trim();
console.log('[roof] ollama -> ' + ans);
const m = ans.match(/\b([ABC])\b/i);
const idx = m ? { A: 0, B: 1, C: 2 }[m[1].toUpperCase()] : -1;
if (idx < 0) { console.error('[roof] respuesta ininterpretable'); process.exit(1); }
console.log('[roof] GANADOR: palette=' + combos[idx][0] + ' dither=' + combos[idx][1]);
