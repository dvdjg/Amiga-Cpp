#!/usr/bin/env node
/**
 * verify-201-framebuffer.mjs — diagnóstico del FRAMEBUFFER del corkscrew 8-way
 * (demo 201, EHB 40x40).
 *
 * OBJETIVO: dado un OFFSET de cámara (mapposx, mapposy en píxeles de mapa),
 * decir QUÉ TILES (plaquetas) del mapa deberían ocupar cada celda de la imagen
 * que se monta en framebuffer, y por qué. Modela la parte DETERMINISTA del
 * corkscrew (el contrato del engine = qué "mundo" se ve en pantalla), sin
 * emular el Blitter.
 *
 * Mecánica del X-Limited (resumen, ver engine/include/eng/field/xlimited.hpp):
 *   - El chip lee un bitmap INTERLEAVED que es un anillo; la cámara se mueve
 *     con `mapposx/mapposy` y el engine garantiza el invariante de Steger
 *     `display_start == scroll_x` (la pantalla muestra el mundo exacto).
 *   - La ventana visible NO empieza en (mapposx, mapposy): el engine desfasó
 *     un bloque (BANDA DE STAGING) sobre la parte superior, de modo que la fila
 *     visible en la pantalla `sy` corresponde al mundo:
 *
 *         world_x = mapposx + sx                       (columnas)
 *         world_y = mapposy + sy                       (filas)
 *         bitmap_y = world_y % display_height                        (anillo físico)
 *
 *     (es el mismo mapeo `screen_to_world_x/y` que usan los objetos fijos).
 *
 * USO:
 *   node tools/analyze/verify-201-framebuffer.mjs --cam 304,0
 *       → imprime la rejilla 20x13 de índices de tile que DEBERÍAN estar en
 *         pantalla con la cámara en (304,0), y escribe `out/run/_tools/crop.png`
 *         (recorte de la imagen de referencia para ese offset).
 *   node tools/analyze/verify-201-framebuffer.mjs --cam 304,0 --screen <png>
 *       → además compara contra una captura y reporta las celdas que NO cuadran.
 *   node tools/analyze/verify-201-framebuffer.mjs --cam 100,100
 *   node tools/analyze/verify-201-framebuffer.mjs --origin 6,6
 *       → explica el RELLENO DE SETUP para un origen distinto de (0,0): qué
 *         bloques de mapa se pintan en el framebuffer antes de dar paso al
 *         scroll incremental de filas/columnas.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';

const root = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))));

// --- Args -------------------------------------------------------------------
const a = (k, d) => { const m = process.argv.find(x => x.startsWith('--')); return d; };
function arg(name, fallback) {
  const i = process.argv.indexOf(name);
  return i >= 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}
const camArg = arg('--cam', null);
const originArg = arg('--origin', null);
const screenArg = arg('--screen', null);
const outArg = arg('--out', path.join(root, 'out/run/_tools/expected-320x208-offset-320-0.png'));

// --- Geometría de la demo 201 (main.cpp) -------------------------------------
const TW = 16, TH = 16, PLANES = 6;
const VIEWPORT_W = 320, VIEWPORT_H = 256, HUD_H = 48;
const MAIN_H = VIEWPORT_H - HUD_H;   // 208
const MAP_W = 40, MAP_H = 40;
const DISPLAY_H = MAIN_H + 2 * TH;   // 240 (display_height del corkscrew)
const COLS_VIS = VIEWPORT_W / TW;    // 20
const ROWS_VIS = MAIN_H / TH;        // 13

// --- Leer kRenderMap (1600 u16) ---------------------------------------------
const hdr = fs.readFileSync(path.join(root, 'out/ehb/const_game_201.h'), 'utf8');
const m = hdr.match(/kRenderMap\[\d+\]\[\d+\]\s*=\s*\{([\s\S]*?)\};/);
const nums = (m[1].match(/\d+/g) || []).slice(0, MAP_W * MAP_H).map(Number);

const dmod = (v, mod) => ((v % mod) + mod) % mod;

function tileIndexAt(wx, wy) {
  const bx = Math.floor(wx / TW), by = Math.floor(wy / TH);
  const cc = dmod(bx, MAP_W);
  const rr = dmod(by, MAP_H);
  return nums[rr * MAP_W + cc];
}

// Rejilla visible 20x13 de índices de tile para un offset de cámara (px de mapa).
function gridFor(mapposx, mapposy) {
  const g = [];
  for (let sy = 0; sy < ROWS_VIS; sy++) {
    const row = [];
    for (let sx = 0; sx < COLS_VIS; sx++) {
      const wx = mapposx + sx * TW;
      const wy = mapposy + sy * TH;
      row.push(tileIndexAt(wx, wy));
    }
    g.push(row);
  }
  return g;
}

// Recorte de la imagen de referencia (reconstruct.png = mapa 640x640) para el
// offset de cámara, con el MISMO mapeo del engine. Escribe crop.png.
function writeCrop(mapposx, mapposy, outFile) {
  const ref = PNG.sync.read(fs.readFileSync(path.join(root, 'out/ehb/reconstruct.png')));
  const W = ref.width, H = ref.height;
  const cropW = VIEWPORT_W, cropH = MAIN_H;
  const out = new PNG({ width: cropW, height: cropH });
  for (let sy = 0; sy < cropH; sy++) {
    const wy = mapposy + sy;
    for (let sx = 0; sx < cropW; sx++) {
      const wx = mapposx + sx;
      const cx = dmod(wx, W);
      const cy = dmod(wy, H);
      const si = (cy * W + cx) * 4;
      const di = (sy * cropW + sx) * 4;
      out.data[di] = ref.data[si]; out.data[di + 1] = ref.data[si + 1];
      out.data[di + 2] = ref.data[si + 2]; out.data[di + 3] = 255;
    }
  }
  fs.mkdirSync(path.dirname(outFile), { recursive: true });
  fs.writeFileSync(outFile, PNG.sync.write(out));
  return [W, H];
}

function printGrid(label, g) {
  console.log(`\n=== ${label} ===`);
  for (const row of g) console.log('  ' + row.map(x => String(x).padStart(4)).join(' '));
}

function printFillOrigin(tx, ty) {
  // Relleno de SETUP para un mapa cuyo bloque (0,0) visible debe ser el bloque
  // (tx,ty) del mapa: el framebuffer (anillo de BPR bloques x BPC bloques) se
  // rellena con el mapa desplazado ese origen, ANTES de que el scroll incremente.
  const BPR = (VIEWPORT_W + 32) / TW;      // 22 (pista de staging)
  const BPC = DISPLAY_H / TH;              // 15 (alto del anillo)
  console.log(`\n=== Relleno de SETUP con origen de mapa (${tx},${ty}) bloques ===`);
  console.log(`El framebuffer es un anillo de BPR=${BPR} x BPC=${BPC} bloques.`);
  console.log(`Para arrancar mostrando el bloque (${tx},${ty}) arriba-izquierda, el`);
  console.log(`setup pinta ` + `map[${ty}+b][${tx}+a]` + ` en la celda del framebuffer (a,b):`);
  for (let b = 0; b < Math.min(3, BPC); b++) {
    const line = [];
    for (let a = 0; a < BPR; a++) {
      const mx = tx + a, my = ty + b;
      line.push(String(mx < MAP_W && my < MAP_H ? nums[my * MAP_W + mx] : 'X').padStart(4));
    }
    console.log(`  yblk ${String(b).padStart(2)}: ` + line.join(' '));
  }
  console.log('  (…) dibujadas todas las BPC filas; después, el scroll incremental');
  console.log('  pinta la COLUMNA/FILA entrante en cada sub-paso de 1 px.');
}

// --- Modo --cam --------------------------------------------------------------
if (camArg) {
  const [cx, cy] = camArg.split(',').map(Number);
  const g = gridFor(cx, cy);
  const [W, H] = writeCrop(cx, cy, outArg);
  console.log(`Cámara (mapposx,mapposy)=(${cx},${cy}). Ventana visible (contrato del engine):`);
  console.log(`  cols mundo x = mapposx + sx  (${cx}..${cx + VIEWPORT_W - 1})`);
  console.log(`  filas mundo y = mapposy + sy (wrap toroidal en X/Y)`);
  console.log(`  → la fila 0 del mapa (y 0..15) queda en la BANDA DE STAGING (oculta).`);
  printGrid(`tiles que DEBERÍAN verse (${COLS_VIS}x${ROWS_VIS})`, g);
  console.log(`Crop de referencia escrito: ${outArg} (ventana ${VIEWPORT_W}x${MAIN_H}, fuente ${W}x${H})`);
  console.log(`Rejilla: filas = mundo y [${cy},..], columnas = mundo x [${cx},..].`);
  console.log(`>>> Nota: la fila lógica visible empieza en y=${cy} (bloque ${Math.floor(cy / TH)}); la guarda superior es la fila anterior.`);
} else if (originArg) {
  const [tx, ty] = originArg.split(',').map(Number);
  printGrid(`rejilla visible para cámara (${tx * TW},${ty * TW}) [contrato]`, gridFor(tx * TW, ty * TW));
  printFillOrigin(tx, ty);
} else {
  console.log('Uso: --cam x,y  |  --origin tx,ty   (ver cabecera del fichero)');
}
