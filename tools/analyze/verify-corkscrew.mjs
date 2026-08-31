#!/usr/bin/env node
/**
 * Simulación del corkscrew (XYLimited) para verificar el port de xlimited.hpp.
 * Compara el port del engine (scroll_down/up/right/left) contra una réplica de
 * Scroller_XYLimited/main.c en el config parametrizado por env (mismos K que la
 * demo 107 / XlimitedConfig):
 *
 *   K_VIEWPORT_W/H (320/256 | 288/224)   ventana visible
 *   K_TILE_W / K_TILE_H (o K_TILE_WIDTH/SIZE)  tile (16|32)
 *   K_FETCH_MODE (0|1|2|3)               EXTRAWIDTH 32 (0) ó 64 (1/2/3)
 *   K_PLANES (4)                          profundidad (BLOCKPLANELINES)
 *   K_SCREENS_X/Y (16)                    pantallas virtuales (mapa = screens*viewport/tile)
 *
 * DESVIACIÓN deliberada (2026-08-31): block_videoposy se envuelve en
 * display_height (no bitmap_height) para que la fila entrante nunca caiga en
 * las filas extra del planeaddx walk (bug: tile obsoleto visible). El simulador
 * usa esa elección en ref y eng, de modo que valida el port con el invariante
 * corregido. TWOBLOCKSTEP = max(0, bitmap_blocks_per_row - tile_height) igual
 * que el engine (para tiles 32 el original daría negativo y no cubriría la fila).
 *
 * No toca hardware: solo comprueba que cada blit emitido (x, y_planeline,
 * mapx, mapy) coincide entre ref y eng para una secuencia de scroll.
 */
const K_VIEWPORT_W = parseInt(process.env.K_VIEWPORT_W ?? '320', 10);
const K_VIEWPORT_H = parseInt(process.env.K_VIEWPORT_H ?? '256', 10);
const K_TILE_W = parseInt(process.env.K_TILE_W ?? process.env.K_TILE_WIDTH ?? '16', 10);
const K_TILE_H = parseInt(process.env.K_TILE_H ?? process.env.K_TILE_SIZE ?? '16', 10);
const K_FETCH_MODE = parseInt(process.env.K_FETCH_MODE ?? '0', 10);
const K_PLANES = parseInt(process.env.K_PLANES ?? '4', 10);
const K_SCREENS_X = parseInt(process.env.K_SCREENS_X ?? '16', 10);
const K_SCREENS_Y = parseInt(process.env.K_SCREENS_Y ?? '16', 10);

const VH = K_VIEWPORT_H, VW = K_VIEWPORT_W, TH = K_TILE_H, TW = K_TILE_W, PLANES = K_PLANES;
const EXTRAWIDTH = K_FETCH_MODE === 0 ? 32 : 64;
const EXTRAHEIGHT = 2 * TH;
const BITMAPWIDTH = VW + EXTRAWIDTH;
const BPR = BITMAPWIDTH / 8;
const BITMAPBLOCKSPERROW = BITMAPWIDTH / TW;
const BITMAPHEIGHT = VH + EXTRAHEIGHT;        // bucle vertical de display
const BITMAPBLOCKSPERCOL = BITMAPHEIGHT / TH;
const BLOCKPLANELINES = TH * PLANES;
const TWOBLOCKSTEP = BITMAPBLOCKSPERROW > TH ? BITMAPBLOCKSPERROW - TH : 0;
const ROUND2BLOCKWIDTH = (x) => x & ~(TW - 1);

// Mapa no-wrapping y grande para que el límite del original (mapwidth*TW-VW-TW)
// nunca se alcance en la secuencia; así la comparación aísla el PORT (blits) de
// la política de wrap/límite (que en el engine depende de cfg.map.wrap_x/y).
const MAP_W = Math.max(K_SCREENS_X * (VW / TW), 1024);
const MAP_H = Math.max(K_SCREENS_Y * (VH / TH), 4096);
const mapdata = new Uint8Array(MAP_W * MAP_H);
let seed = 0x13579bd;
for (let i = 0; i < mapdata.length; ++i) {
  seed = (seed * 1103515245 + 12345) & 0x7fffffff;
  mapdata[i] = seed & 63;
}
const tileAt = (x, y) => mapdata[((y % MAP_H + MAP_H) % MAP_H) * MAP_W + ((x % MAP_W + MAP_W) % MAP_W)];

// Nota: el port envuelve block_videoposy en el BUCLE de display
// (BITMAPHEIGHT = display_height), no en bitmap_height (304), para que la fila
// entrante nunca caiga en las filas extra del planeaddx walk (bug 2026-08-31:
// tile visible en el área de pantalla cada 304 px de scroll vertical).
// El simulador usa la MISMA elección en ref y eng para validar el port.

// ---------------------------------------------------------------------------
// Réplica fiel del original (Scroller_XYLimited/main.c)
// ---------------------------------------------------------------------------
function refState() {
  return { mapposx: 0, mapposy: 0, videoposx: 0, videoposy: 0,
    block_videoposy: 0, mapblockx: 0, mapblocky: 0, stepx: 0, stepy: 0,
    previous_xdirection: 0, savewordpointer: null, saveword: 0 };
}
function refDraw(s, blits, x, y, mapx, mapy) {
  // x en px, y en planeline
  blits.push({ x, y, mapx, mapy, tile: tileAt(mapx, mapy) });
}
function refScrollDown(s, blits) {
  if (s.mapposy >= MAP_H * TH - VH - TH) return;
  let mapx, mapy, x, y;
  mapx = s.stepy; mapy = s.mapblocky + BITMAPBLOCKSPERCOL; y = s.block_videoposy * PLANES;
  if (mapx >= TWOBLOCKSTEP) {
    mapx += TWOBLOCKSTEP; x = mapx * TW + ROUND2BLOCKWIDTH(s.videoposx); mapx += s.mapblockx;
    refDraw(s, blits, x, y, mapx, mapy);
  } else {
    mapx *= 2; x = mapx * TW + ROUND2BLOCKWIDTH(s.videoposx); mapx += s.mapblockx;
    refDraw(s, blits, x, y, mapx, mapy); x += TW; refDraw(s, blits, x, y, mapx + 1, mapy);
  }
  s.mapposy++; s.mapblocky = Math.floor(s.mapposy / TH); s.stepy = s.mapposy & (TH - 1);
  s.videoposy++; if (s.videoposy >= BITMAPHEIGHT) s.videoposy -= BITMAPHEIGHT;
  if (!s.stepy) { s.block_videoposy += TH; if (s.block_videoposy >= BITMAPHEIGHT) s.block_videoposy -= BITMAPHEIGHT; }
  if (s.stepy === 0) {
    if (s.stepx) {
      mapx = s.mapblockx; mapy = s.mapblocky; x = ROUND2BLOCKWIDTH(s.videoposx); y = s.block_videoposy * PLANES;
      refDraw(s, blits, x, y, mapx, mapy);
      if (s.previous_xdirection === 1) { /* restore */ }
      mapy = s.stepx + 1; x += BITMAPWIDTH; y = ((s.block_videoposy + mapy * TH) % BITMAPHEIGHT) * PLANES;
      let y2 = (y + BLOCKPLANELINES - 1) % (BITMAPHEIGHT * PLANES);
      s.savewordpointer = y2 * BPR + (x / 8); s.saveword = 0;
      mapx += BITMAPBLOCKSPERROW; mapy += s.mapblocky;
      refDraw(s, blits, x, y, mapx, mapy);
      s.previous_xdirection = 2;
    }
  }
}
function refScrollUp(s, blits) {
  if (s.mapposy < 1) return;
  s.mapposy--; s.mapblocky = Math.floor(s.mapposy / TH); s.stepy = s.mapposy & (TH - 1);
  s.videoposy--; if (s.videoposy < 0) s.videoposy += BITMAPHEIGHT;
  if (s.stepy === TH - 1) { s.block_videoposy -= TH; if (s.block_videoposy < 0) s.block_videoposy += BITMAPHEIGHT; }
  if (s.stepy === TH - 1) {
    if (s.stepx) {
      let mapx = s.mapblockx + BITMAPBLOCKSPERROW, mapy = s.mapblocky + 1;
      let x = ROUND2BLOCKWIDTH(s.videoposx);
      let y = ((s.block_videoposy + TH) % BITMAPHEIGHT) * PLANES;
      refDraw(s, blits, x + BITMAPWIDTH, y, mapx, mapy);
      if (s.previous_xdirection === 2) { /* restore */ }
      mapy = s.stepx + 2; y = ((s.block_videoposy + mapy * TH) % BITMAPHEIGHT) * PLANES;
      s.savewordpointer = y * BPR + (x / 8); s.saveword = 0;
      mapx -= BITMAPBLOCKSPERROW; mapy += s.mapblocky;
      refDraw(s, blits, x, y, mapx, mapy);
      s.previous_xdirection = 1;
    }
  }
  let mapx = s.stepy, mapy = s.mapblocky, x, y = s.block_videoposy * PLANES;
  if (mapx >= TWOBLOCKSTEP) {
    mapx += TWOBLOCKSTEP; x = mapx * TW + ROUND2BLOCKWIDTH(s.videoposx); mapx += s.mapblockx;
    refDraw(s, blits, x, y, mapx, mapy);
  } else {
    mapx *= 2; x = mapx * TW + ROUND2BLOCKWIDTH(s.videoposx); mapx += s.mapblockx;
    refDraw(s, blits, x, y, mapx, mapy); x += TW; refDraw(s, blits, x, y, mapx + 1, mapy);
  }
}
function refScrollRight(s, blits) {
  if (s.mapposx >= MAP_W * TW - VW - TW) return;
  let mapx = s.mapblockx + BITMAPBLOCKSPERROW;
  let mapy = s.stepx + 1;
  let x = ROUND2BLOCKWIDTH(s.videoposx);
  if (s.previous_xdirection === 1) { /* restore */ }
  if (mapy === 1) {
    mapy += s.mapblocky;
    let y = ((s.block_videoposy + TH) % BITMAPHEIGHT) * PLANES;
    refDraw(s, blits, x + BITMAPWIDTH, y, mapx, mapy);
    y = (y + BLOCKPLANELINES) % (BITMAPHEIGHT * PLANES);
    let y2 = (y + BLOCKPLANELINES - 1) % (BITMAPHEIGHT * PLANES);
    s.savewordpointer = y2 * BPR + ((x + BITMAPWIDTH) / 8); s.saveword = 0;
    refDraw(s, blits, x + BITMAPWIDTH, y, mapx, mapy + 1);
  } else {
    mapy++;
    let y = ((s.block_videoposy + mapy * TH) % BITMAPHEIGHT) * PLANES;
    let y2 = (y + BLOCKPLANELINES - 1) % (BITMAPHEIGHT * PLANES);
    mapy += s.mapblocky;
    s.savewordpointer = y2 * BPR + ((x + BITMAPWIDTH) / 8); s.saveword = 0;
    refDraw(s, blits, x + BITMAPWIDTH, y, mapx, mapy);
  }
  s.mapposx++; s.mapblockx = Math.floor(s.mapposx / TW); s.stepx = s.mapposx & (TW - 1);
  s.videoposx++;
  if (s.stepx === 0) {
    mapx = s.mapblockx + BITMAPBLOCKSPERROW - 1; mapy = s.mapblocky;
    x = ROUND2BLOCKWIDTH(s.videoposx) + (BITMAPBLOCKSPERROW - 1) * TW;
    let y = s.block_videoposy * PLANES;
    refDraw(s, blits, x, y, mapx, mapy);
    mapx = s.stepy;
    if (mapx) {
      if (mapx >= TWOBLOCKSTEP) mapx += (TWOBLOCKSTEP - 1); else mapx = mapx * 2 - 1;
      x = ROUND2BLOCKWIDTH(s.videoposx) + mapx * TW;
      y = s.block_videoposy * PLANES;
      mapx += s.mapblockx;
      refDraw(s, blits, x, y, mapx, mapy + BITMAPBLOCKSPERCOL);
    }
  }
  s.previous_xdirection = s.stepx ? 2 : 0;
}
function refScrollLeft(s, blits) {
  if (s.mapposx < 1) return;
  s.mapposx--; s.mapblockx = Math.floor(s.mapposx / TW); s.stepx = s.mapposx & (TW - 1);
  s.videoposx--;
  if (s.stepx === TW - 1) {
    let mapx = s.mapblockx; let mapy = s.mapblocky;
    if (s.stepy) mapy += BITMAPBLOCKSPERCOL;
    let x = ROUND2BLOCKWIDTH(s.videoposx);
    let y = s.block_videoposy * PLANES;
    refDraw(s, blits, x, y, mapx, mapy);
    mapx = s.stepy;
    if (mapx) {
      if (mapx >= TWOBLOCKSTEP) mapx += TWOBLOCKSTEP; else mapx *= 2;
      x = ROUND2BLOCKWIDTH(s.videoposx) + mapx * TW;
      y = s.block_videoposy * PLANES;
      mapx += s.mapblockx; mapy -= BITMAPBLOCKSPERCOL;
      refDraw(s, blits, x, y, mapx, mapy);
    }
  }
  let mapx = s.mapblockx;
  let mapy = s.stepx + 1;
  let x = ROUND2BLOCKWIDTH(s.videoposx);
  if (s.previous_xdirection === 2) { /* restore */ }
  if (mapy === 1) {
    mapy += s.mapblocky;
    let y = ((s.block_videoposy + TH) % BITMAPHEIGHT) * PLANES;
    s.savewordpointer = y * BPR + (x / 8); s.saveword = 0;
    refDraw(s, blits, x, y, mapx, mapy);
    y = (y + BLOCKPLANELINES) % (BITMAPHEIGHT * PLANES);
    refDraw(s, blits, x, y, mapx, mapy + 1);
  } else {
    mapy++;
    let y = ((s.block_videoposy + mapy * TH) % BITMAPHEIGHT) * PLANES;
    mapy += s.mapblocky;
    s.savewordpointer = y * BPR + (x / 8); s.saveword = 0;
    refDraw(s, blits, x, y, mapx, mapy);
  }
  s.previous_xdirection = s.stepx ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Port del engine (xlimited.hpp) — traducido literal
// ---------------------------------------------------------------------------
function engState() { return { mapposx: 0, mapposy: 0, videoposx: 0, videoposy: 0, prevDir: 0 }; }
function twoblockstep() { return TWOBLOCKSTEP; }
function engBlockVideoposy(s) { return (Math.floor(s.mapposy / TH) * TH) % BITMAPHEIGHT; }
function engScrollDown(s, blits) {
  const mapblockx = Math.floor(s.mapposx / TW), mapblocky = Math.floor(s.mapposy / TH);
  const stepx = s.mapposx & (TW - 1), stepy = s.mapposy & (TH - 1);
  const bvpos = engBlockVideoposy(s);
  const x0 = s.videoposx & ~(TW - 1);
  const y_pl = bvpos * PLANES;
  const mapy = mapblocky + BITMAPBLOCKSPERCOL;
  if (stepy >= twoblockstep()) {
    const mx = stepy + twoblockstep() + mapblockx;
    const x = (stepy + twoblockstep()) * TW + x0;
    refDraw(s, blits, x, y_pl, mx, mapy);
  } else {
    const mx = stepy * 2 + mapblockx;
    const x = stepy * 2 * TW + x0;
    refDraw(s, blits, x, y_pl, mx, mapy);
    refDraw(s, blits, x + TW, y_pl, mx + 1, mapy);
  }
  s.mapposy++; s.videoposy = s.mapposy % BITMAPHEIGHT;
  if (stepy === TH - 1 && stepx) {
    const nvpos = engBlockVideoposy(s);
    const nmapblocky = mapblocky + 1;
    refDraw(s, blits, x0, nvpos * PLANES, mapblockx, nmapblocky);
    if (s.prevDir === 1) { /* restore */ }
    const my = stepx + 1;
    const y2 = ((nvpos + my * TH) % BITMAPHEIGHT) * PLANES;
    const y2b = (y2 + BLOCKPLANELINES - 1) % (BITMAPHEIGHT * PLANES);
    refDraw(s, blits, x0 + BITMAPWIDTH, y2, mapblockx + BITMAPBLOCKSPERROW, my + nmapblocky);
    s.prevDir = 2;
  }
}
function engScrollUp(s, blits) {
  if (s.mapposy < 1) return;
  s.mapposy--; s.videoposy = s.mapposy % BITMAPHEIGHT;
  const mapblockx = Math.floor(s.mapposx / TW), mapblocky = Math.floor(s.mapposy / TH);
  const stepx = s.mapposx & (TW - 1), stepy = s.mapposy & (TH - 1);
  const bvpos = engBlockVideoposy(s);
  const x0 = s.videoposx & ~(TW - 1);
  const y_pl = bvpos * PLANES;
  if (stepy === TH - 1 && stepx) {
    const mx1 = mapblockx + BITMAPBLOCKSPERROW;
    const y1 = ((bvpos + TH) % BITMAPHEIGHT) * PLANES;
    refDraw(s, blits, x0 + BITMAPWIDTH, y1, mx1, mapblocky + 1);
    if (s.prevDir === 2) { /* restore */ }
    const my2 = stepx + 2;
    const y2 = ((bvpos + my2 * TH) % BITMAPHEIGHT) * PLANES;
    refDraw(s, blits, x0, y2, mx1 - BITMAPBLOCKSPERROW, my2 + mapblocky);
    s.prevDir = 1;
  }
  if (stepy >= twoblockstep()) {
    const mx = stepy + twoblockstep() + mapblockx;
    const x = (stepy + twoblockstep()) * TW + x0;
    refDraw(s, blits, x, y_pl, mx, mapblocky);
  } else {
    const mx = stepy * 2 + mapblockx;
    const x = stepy * 2 * TW + x0;
    refDraw(s, blits, x, y_pl, mx, mapblocky);
    refDraw(s, blits, x + TW, y_pl, mx + 1, mapblocky);
  }
}
function engScrollRight(s, blits) {
  if (s.mapposx >= MAP_W * TW - VW - TW) return;
  const mapblockx = Math.floor(s.mapposx / TW), mapblocky = Math.floor(s.mapposy / TH);
  const stepx = s.mapposx & (TW - 1), stepy = s.mapposy & (TH - 1);
  const bvpos = engBlockVideoposy(s);
  const x0 = s.videoposx & ~(TW - 1);
  const mapx = mapblockx + BITMAPBLOCKSPERROW;
  if (s.prevDir === 1) { /* restore */ }
  let mapy = stepx + 1;
  if (mapy === 1) {
    mapy += mapblocky;
    const y = ((bvpos + TH) % BITMAPHEIGHT) * PLANES;
    refDraw(s, blits, x0 + BITMAPWIDTH, y, mapx, mapy);
    const y2 = (y + BLOCKPLANELINES) % (BITMAPHEIGHT * PLANES);
    refDraw(s, blits, x0 + BITMAPWIDTH, y2, mapx, mapy + 1);
  } else {
    mapy++;
    const y = ((bvpos + mapy * TH) % BITMAPHEIGHT) * PLANES;
    mapy += mapblocky;
    refDraw(s, blits, x0 + BITMAPWIDTH, y, mapx, mapy);
  }
  s.mapposx++; s.videoposx = s.mapposx;
  const new_stepx = s.mapposx & (TW - 1);
  if (new_stepx === 0) {
    const nx0 = x0 + TW;
    const nmapblockx = mapblockx + 1;
    refDraw(s, blits, nx0 + (BITMAPBLOCKSPERROW - 1) * TW, bvpos * PLANES, nmapblockx + BITMAPBLOCKSPERROW - 1, mapblocky);
    if (stepy) {
      const mx = stepy >= twoblockstep() ? stepy + (twoblockstep() - 1) : stepy * 2 - 1;
      refDraw(s, blits, nx0 + mx * TW, bvpos * PLANES, mx + nmapblockx, mapblocky + BITMAPBLOCKSPERCOL);
    }
  }
  s.prevDir = new_stepx ? 2 : 0;
}
function engScrollLeft(s, blits) {
  if (s.mapposx < 1) return;
  s.mapposx--; s.videoposx = s.mapposx;
  const mapblockx = Math.floor(s.mapposx / TW), mapblocky = Math.floor(s.mapposy / TH);
  const stepx = s.mapposx & (TW - 1), stepy = s.mapposy & (TH - 1);
  const bvpos = engBlockVideoposy(s);
  const x0 = s.videoposx & ~(TW - 1);
  if (stepx === TW - 1) {
    let mx = mapblockx, my = mapblocky;
    if (stepy) my += BITMAPBLOCKSPERCOL;
    refDraw(s, blits, x0, bvpos * PLANES, mx, my);
    mx = stepy;
    if (mx) {
      mx = mx >= twoblockstep() ? mx + twoblockstep() : mx * 2;
      refDraw(s, blits, x0 + mx * TW, bvpos * PLANES, mx + mapblockx, my - BITMAPBLOCKSPERCOL);
    }
  }
  const mapx = mapblockx;
  let mapy = stepx + 1;
  if (s.prevDir === 2) { /* restore */ }
  if (mapy === 1) {
    mapy += mapblocky;
    const y = ((bvpos + TH) % BITMAPHEIGHT) * PLANES;
    refDraw(s, blits, x0, y, mapx, mapy);
    const y2 = (y + BLOCKPLANELINES) % (BITMAPHEIGHT * PLANES);
    refDraw(s, blits, x0, y2, mapx, mapy + 1);
  } else {
    mapy++;
    const y = ((bvpos + mapy * TH) % BITMAPHEIGHT) * PLANES;
    mapy += mapblocky;
    refDraw(s, blits, x0, y, mapx, mapy);
  }
  s.prevDir = stepx ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Comparación
// ---------------------------------------------------------------------------
function fmt(b) { return `x=${b.x} y=${b.y} mapx=${b.mapx} mapy=${b.mapy} tile=${b.tile}`; }
let failures = 0;
function cmp(seq) {
  const r = refState(), e = engState();
  const rb = [], eb = [];
  for (const dir of seq) {
    rb.length = 0; eb.length = 0;
    if (dir === 'D') { refScrollDown(r, rb); engScrollDown(e, eb); }
    else if (dir === 'U') { refScrollUp(r, rb); engScrollUp(e, eb); }
    else if (dir === 'R') { refScrollRight(r, rb); engScrollRight(e, eb); }
    else if (dir === 'L') { refScrollLeft(r, rb); engScrollLeft(e, eb); }
    if (rb.length !== eb.length) { failures++; console.log(`F: ${dir} blits ref=${rb.length} eng=${eb.length} [mapposx=${e.mapposx} mapposy=${e.mapposy}]`); continue; }
    for (let i = 0; i < rb.length; ++i) {
      const a = rb[i], b = eb[i];
      if (a.x !== b.x || a.y !== b.y || a.mapx !== b.mapx || a.mapy !== b.mapy) {
        failures++;
        console.log(`F ${dir}[${i}]: ref(${fmt(a)}) vs eng(${fmt(b)}) [mapposx=${e.mapposx} mapposy=${e.mapposy}]`);
      }
    }
  }
}
// Secuencia: abajo 300 px, luego arriba 100 px, luego derecha 300, izquierda 100, y XY mezclado
const seq = [];
for (let i = 0; i < 300; ++i) seq.push('D');
for (let i = 0; i < 100; ++i) seq.push('U');
for (let i = 0; i < 300; ++i) seq.push('R');
for (let i = 0; i < 100; ++i) seq.push('L');
const xySeq = [];
for (let i = 0; i < 600; ++i) xySeq.push(i % 2 === 0 ? 'D' : 'R');
for (let i = 0; i < 600; ++i) xySeq.push(i % 2 === 0 ? 'U' : 'L');
cmp(seq);
cmp(xySeq);
// Secuencia aleatoria larga (regresión: cualquier cambio en el port debe seguir
// coincidiendo bloque a bloque con el original en scroll 8-way arbitrario).
let rng = 42;
function rnd(n) { rng = (rng * 1103515245 + 12345) & 0x7fffffff; return rng % n; }
const dirs = ['D', 'U', 'R', 'L'];
const randSeq = [];
for (let i = 0; i < 5000; ++i) randSeq.push(dirs[rnd(4)]);
cmp(randSeq);
console.log(failures === 0
  ? `OK verify-corkscrew [viewport ${VW}×${VH} tile ${TW}×${TH} planes ${PLANES} fetch ${K_FETCH_MODE}]: port coincide con XYLimited (incl. secuencia aleatoria 5000 pasos, BITMAPWIDTH=${BITMAPWIDTH}, TWOBLOCKSTEP=${TWOBLOCKSTEP})`
  : `FAIL: ${failures} discrepancias`);
process.exit(failures === 0 ? 0 : 1);

