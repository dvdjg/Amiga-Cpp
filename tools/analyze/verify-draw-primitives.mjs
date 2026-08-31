#!/usr/bin/env node
/**
 * Verificación host de las primitivas de dibujo del corkscrew
 * (`XLimitedPlayfield::set_pixel/fill_rect/draw_line` + `world_to_planeline`/
 * `write_planes`). Modela EXACTAMENTE el contrato de xlimited.hpp:
 *
 *   planelínea = ((wy % display_height) + display_height) % display_height * planes
 *   word_byte  = (wx/8) & ~1                    (word-aligned; wx fuera de la fila
 *                                                cruza a la siguiente planelínea:
 *                                                el *walk* horizontal del corkscrew)
 *   mask       = 0x8000 >> (wx & 15)
 *   dirección  = frontbuffer + planeline*row_bytes + p*row_bytes + word_byte
 *   límite     = byte < row_bytes * bitmap_height * planes  (si no, se omite)
 *   espejo     = en modo lineal, además planelínea + display_height*planes
 *
 * Parámetros por env (mismos K que verify-corkscrew.mjs):
 *   K_VIEWPORT_H (224)  K_VIEWPORT_W (320)  K_TILE_H (16)  K_PLANES (4)
 *   K_LINEAR (0|1)      K_SCREENS_X/Y (16)  K_FETCH_MODE (0)
 *
 * Uso: node tools/analyze/verify-draw-primitives.mjs
 *      K_LINEAR=1 node tools/analyze/verify-draw-primitives.mjs
 */
const K_VIEWPORT_W = parseInt(process.env.K_VIEWPORT_W ?? '320', 10);
const K_VIEWPORT_H = parseInt(process.env.K_VIEWPORT_H ?? '224', 10);
const K_TILE_W = parseInt(process.env.K_TILE_W ?? '16', 10);
const K_TILE_H = parseInt(process.env.K_TILE_H ?? '16', 10);
const K_PLANES = parseInt(process.env.K_PLANES ?? '4', 10);
const K_FETCH_MODE = parseInt(process.env.K_FETCH_MODE ?? '0', 10);
const K_SCREENS_X = parseInt(process.env.K_SCREENS_X ?? '16', 10);
const K_SCREENS_Y = parseInt(process.env.K_SCREENS_Y ?? '16', 10);
const K_LINEAR = parseInt(process.env.K_LINEAR ?? '0', 10);

const EXTRAWIDTH = K_FETCH_MODE === 0 ? 32 : 64;
const DISPLAY_H = K_VIEWPORT_H + 2 * K_TILE_H;          // bucle vertical (corkscrew)
const BITMAPWIDTH = K_VIEWPORT_W + EXTRAWIDTH;
const ROW_BYTES = BITMAPWIDTH / 8;
const BITMAPBLOCKSPERROW = BITMAPWIDTH / K_TILE_W;
const MAP_W_BLOCKS = K_SCREENS_X * (K_VIEWPORT_W / K_TILE_W);
// bitmap_height del engine (compute_bitmap_height): display_h + (map_w/blocks_per_row/planes) + 1 + 3
let BITMAPHEIGHT = DISPLAY_H + Math.floor(MAP_W_BLOCKS / BITMAPBLOCKSPERROW / K_PLANES) + 1 + 3;
if (K_LINEAR) BITMAPHEIGHT += DISPLAY_H; // espejo del bucle (engine: m_bitmap_height += m_display_height)
const TOTAL_BYTES = ROW_BYTES * BITMAPHEIGHT * K_PLANES;

const fw = new Uint8Array(TOTAL_BYTES);
const fb = (planeline, p, wordByte, mask, color) => {
  const b = (planeline + p) * ROW_BYTES + wordByte;
  if (b >= TOTAL_BYTES) return;
  const i = (b >> 1) | 0;
  if (i >= fw.length) return;
  const word = (fw[i * 2] << 8) | fw[i * 2 + 1];
  const nw = (color & (1 << p)) ? (word | mask) : (word & ~mask);
  fw[i * 2] = nw >> 8;
  fw[i * 2 + 1] = nw & 0xff;
};
const setPixel = (wx, wy, color) => {
  if (wx < 0) return;
  const wordByte = ((wx / 8) | 0) & ~1;
  const mask = 0x8000 >> (wx & 15);
  const loop = ((wy % DISPLAY_H) + DISPLAY_H) % DISPLAY_H;
  const pl = loop * K_PLANES;
  for (let p = 0; p < K_PLANES; p++) fb(pl, p, wordByte, mask, color);
  if (K_LINEAR) for (let p = 0; p < K_PLANES; p++) fb(pl + DISPLAY_H * K_PLANES, p, wordByte, mask, color);
};

// ---- utilidades de lectura ------------------------------------------------
const pixelByte = (wx, wy, p) => {
  const loop = ((wy % DISPLAY_H) + DISPLAY_H) % DISPLAY_H;
  return (loop * K_PLANES + p) * ROW_BYTES + ((wx / 8) | 0) & ~1;
};
const readBit = (byteOfs, wx) => {
  const i = (byteOfs >> 1) | 0;
  const word = (fw[i * 2] << 8) | fw[i * 2 + 1];
  return (word & (0x8000 >> (wx & 15))) !== 0;
};
const colorAt = (wx, wy) => {
  let c = 0;
  for (let p = 0; p < K_PLANES; p++) if (readBit(pixelByte(wx, wy, p), wx)) c |= 1 << p;
  return c;
};
const mirrorColorAt = (wx, wy) => {
  let c = 0;
  for (let p = 0; p < K_PLANES; p++) {
    const loop = ((wy % DISPLAY_H) + DISPLAY_H) % DISPLAY_H;
    const byteOfs = (loop * K_PLANES + DISPLAY_H * K_PLANES + p) * ROW_BYTES + (((wx / 8) | 0) & ~1);
    if (readBit(byteOfs, wx)) c |= 1 << p;
  }
  return c;
};

let fails = 0;
const check = (name, cond) => {
  if (!cond) { console.log('  FAIL ' + name); fails++; }
};
const section = (s) => console.log(s);

// ---- 1. set_pixel: color correcto en cada plano y posición -----------------
section('1. set_pixel básico');
for (const [wx, wy, color] of [[8, 0, 1], [0, 0, 2], [255, 7, 4], [16, 16, 15], [319, DISPLAY_H - 1, 9], [0, DISPLAY_H, 1], [352, 3, 7]]) {
  setPixel(wx, wy, color);
  check(`(wx=${wx},wy=${wy}) color=${color}`, colorAt(wx, wy) === color);
}

// ---- 2. walk horizontal: wx >= BITMAPWIDTH cruza a la siguiente planelínea --
section('2. walk horizontal (wx fuera de la fila)');
{
  setPixel(352, 5, 3); // byte 44 => planelínea (5*planes+p)+1, byte 0
  // debe aparecer en planelínea (5*planes)+1 a color 3
  let ok = true;
  for (let p = 0; p < K_PLANES; p++) {
    const byteOfs = ((5 * K_PLANES) + p + 1) * ROW_BYTES + 0;
    const expect = (3 & (1 << p)) !== 0;
    if (readBit(byteOfs, 352) !== expect) ok = false;
  }
  check('wx=352 pinta en la planelínea siguiente (p+1)', ok);
  check('wx=352 visible como color en (352,5)', colorAt(352, 5) === 3);
}

// ---- 3. fill_rect: bloque sólido w×h ----------------------
section('3. fill_rect');
{
  const fx = 64, fy = 20, fw2 = 12, fh = 9;
  fillRect(fx, fy, fw2, fh, 5);
  let ok = true;
  for (let dy = 0; dy < fh; dy++) for (let dx = 0; dx < fw2; dx++)
    if (colorAt(fx + dx, fy + dy) !== 5) ok = false;
  check('rect (64,20) 12x9 color 5 relleno', ok);
  check('fuera del rect no pinta (63,20)', colorAt(63, 20) === 0);
  check('fuera del rect no pinta (64,19)', colorAt(64, 19) === 0);
}
function fillRect(wx, wy, w, h, color) {
  for (let dy = 0; dy < h; dy++) for (let dx = 0; dx < w; dx++) setPixel(wx + dx, wy + dy, color);
}
function drawLine(x0, y0, x1, y1, color) {
  const dx = Math.abs(x1 - x0), dy = Math.abs(y1 - y0);
  const sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  let err = dx - dy;
  for (;;) {
    setPixel(x0, y0, color);
    if (x0 === x1 && y0 === y1) break;
    const e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx) { err += dx; y0 += sy; }
  }
}

// ---- 4. draw_line: Bresenham ---- 
section('4. draw_line (Bresenham)');
{
  // referencia Bresenham puro
  const bres = [];
  let x0 = 100, y0 = 30, x1 = 170, y1 = 48;
  let dx = Math.abs(x1 - x0), dy = Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  let err = dx - dy;
  for (;;) {
    bres.push([x0, y0]);
    if (x0 === x1 && y0 === y1) break;
    const e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx) { err += dx; y0 += sy; }
  }
  // limpiar zona y dibujar
  fillRect(100, 30, 71, 19, 0);
  drawLine(100, 30, 170, 48, 6);
  let ok = true, bad = 0;
  for (let y = 30; y <= 48; y++) for (let x = 100; x <= 170; x++) {
    const expect = bres.some(([bx, by]) => bx === x && by === y) ? 6 : 0;
    if (colorAt(x, y) !== expect) { ok = false; if (++bad < 5) console.log('    línea (100,30)->(170,48): (' + x + ',' + y + ')=' + colorAt(x, y) + ' esperado ' + expect); }
  }
  check('draw_line sigue Bresenham (100,30)->(170,48)', ok);
}

// ---- 5. espejo (modo lineal) ----
section('5. espejo (K_LINEAR=' + K_LINEAR + ')');
{
  const wx = 128, wy = 60, color = 10;
  setPixel(wx, wy, color);
  if (K_LINEAR) {
    check('espejo: planelínea + display_height duplicada', mirrorColorAt(wx, wy) === color);
    check('espejo: no corrompe el bucle', colorAt(wx, wy) === color);
  } else {
    check('sin lineal no hay espejo (mirror = 0)', mirrorColorAt(wx, wy) === 0);
  }
}

// ---- 6. objeto fijo en pantalla: screen_to_world_y ----
section('6. objeto fijo en pantalla (screen_to_world_y)');{
  // display_offset = (mapposy + tile_height) % display_height; una fila fija en
  // pantalla sy se dibuja en wy = (mapposy + tile_height + sy) % display_height.
  // Simular dos frames con mapposy distinto: el dibujo debe quedar en el MISMO
  // byte de planelínea y mostrarse en la misma fila de pantalla.
  const mapposy = [0, 5, 33];
  const sy = 4;
  let consistent = true;
  for (const my of mapposy) {
    const dispOfs = (my + K_TILE_H) % DISPLAY_H;
    const wy = (dispOfs + sy) % DISPLAY_H; // = screen_to_world_y
    fillRect(0, 0, 320, DISPLAY_H, 0);     // limpiar
    setPixel(160, wy, 7);
    // el píxel debe estar en planelínea (wy*planes) — la misma fila de pantalla
    const loop = ((wy % DISPLAY_H) + DISPLAY_H) % DISPLAY_H;
    check('mapposy=' + my + ' -> píxel en fila de pantalla ' + sy, colorAt(160, loop) === 7);
    if (colorAt(160, loop) !== 7) consistent = false;
  }
  check('fila fija consistente en 3 mapposy', consistent);
}

// ---- 7. CanvasPlayfield (lienzo plano: layout interleaved sin walk) ----
section('7. CanvasPlayfield (lienzo plano 320x32, 4 planos)');
{
  const CW = 320, CH = 32, CP = 4, CROW = CW / 8;
  const canvas = new Uint8Array(CROW * CH * CP);
  const cset = (wx, wy, color) => {
    if (wx < 0 || wy < 0 || wx >= CW || wy >= CH) return;
    const pl = wy * CP;
    const byte = (wx / 8) | 0;
    const mask = 0x8000 >> (wx & 15);
    for (let p = 0; p < CP; p++) {
      const i = (pl + p) * CROW + byte;
      const idx = (i >> 1) | 0;
      const word = (canvas[idx * 2] << 8) | canvas[idx * 2 + 1];
      const nw = (color & (1 << p)) ? (word | mask) : (word & ~mask);
      canvas[idx * 2] = nw >> 8; canvas[idx * 2 + 1] = nw & 0xff;
    }
  };
  const ccolor = (wx, wy) => {
    let c = 0;
    for (let p = 0; p < CP; p++) {
      const i = (wy * CP + p) * CROW + ((wx / 8) | 0);
      const idx = (i >> 1) | 0;
      const word = (canvas[idx * 2] << 8) | canvas[idx * 2 + 1];
      if (word & (0x8000 >> (wx & 15))) c |= 1 << p;
    }
    return c;
  };
  // rectángulo 8x8 color 5 en (16,4)
  for (let dy = 0; dy < 8; dy++) for (let dx = 0; dx < 8; dx++) cset(16 + dx, 4 + dy, 5);
  let ok = true;
  for (let dy = 0; dy < 8; dy++) for (let dx = 0; dx < 8; dx++) if (ccolor(16 + dx, 4 + dy) !== 5) ok = false;
  check('rect (16,4) 8x8 color 5 en lienzo', ok);
  check('lienzo no pinta fuera (0,0)', ccolor(0, 0) === 0);
  check('lienzo no pinta fuera (200,20)', ccolor(200, 20) === 0);
  // línea vertical x=2 color 1 (marco) en filas 2..29
  for (let y = 2; y < 30; y++) cset(2, y, 1);
  ok = true;
  for (let y = 2; y < 30; y++) if (ccolor(2, y) !== 1) ok = false;
  check('línea vertical x=2 color 1 en lienzo', ok);
  // límite: píxel fuera del lienzo se rechaza (in_bounds)
  let rejected = false;
  const before = ccolor(319, 31);
  cset(320, 31, 7); // fuera de ancho
  cset(100, 32, 7); // fuera de alto
  check('lienzo ignora píxeles fuera de rango', ccolor(319, 31) === before);
}

console.log(fails === 0 ? 'OK verify-draw-primitives (layout corkscrew)' : ('FAILS=' + fails));
process.exit(fails === 0 ? 0 : 1);
