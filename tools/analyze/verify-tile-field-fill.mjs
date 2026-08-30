#!/usr/bin/env node
/*
 * Test determinista del modelo circular 8-way. No compila C++: reproduce el
 * contrato geométrico que debe cumplir TileFieldController.
 */
const VW = 320, VH = 256;
const cases = [[16, 16], [32, 16], [16, 32], [32, 32]];
const failures = [];
const check = (ok, text) => { if (!ok) failures.push(text); };
const mapAt = (x, y) => ((x * 257 + y * 911 + 0x1234) & 0xffff);

function geometry(tw, th, sx, sy) {
  const cols = VW / tw, rows = VH / th;
  return { cols, rows, fw: sx ? 2 * cols + 2 : cols, fh: sy ? 2 * rows + 2 : rows,
    wx: sx ? cols : 0, wy: sy ? rows : 0 };
}

function bands(g, sx, sy, dx, dy) {
  const jobs = [];
  const add = (x, y, w, h, wx, wy, axis) => {
    if (w && h) jobs.push({ x, y, w, h, wx, wy, axis });
  };
  // A diagonal corner belongs to X. Y is shortened by one block, never duplicated.
  if (sx && dx) add(dx > 0 ? g.fw - 1 : 0, 0, 1, g.fh,
    dx > 0 ? g.fw - 1 : 0, 0, 'x');
  if (sy && dy) add(sx && dx && dx < 0 ? 1 : 0, dy > 0 ? g.fh - 1 : 0,
    g.fw - (sx && dx ? 1 : 0), 1, sx && dx && dx < 0 ? 1 : 0,
    dy > 0 ? g.fh - 1 : 0, 'y');
  return jobs;
}

function verifyCase(tw, th, sx, sy, dx, dy) {
  const g = geometry(tw, th, sx, sy);
  check(g.fw === (sx ? 2 * VW / tw + 2 : VW / tw), `${tw}x${th}: ancho circular`);
  check(g.fh === (sy ? 2 * VH / th + 2 : VH / th), `${tw}x${th}: alto circular`);
  const visible = { x: g.wx, y: g.wy, w: g.cols, h: g.rows };
  const used = new Set();
  for (const j of bands(g, sx, sy, dx, dy)) {
    check(j.x >= 0 && j.y >= 0 && j.x + j.w <= g.fw && j.y + j.h <= g.fh, 'banda fuera');
    for (let y = 0; y < j.h; ++y) for (let x = 0; x < j.w; ++x) {
      const px = j.x + x, py = j.y + y, key = `${px},${py}`;
      check(!(px >= visible.x && px < visible.x + visible.w && py >= visible.y && py < visible.y + visible.h), 'write en viewport');
      check(!used.has(key), `esquina/solape ${key}`); used.add(key);
      check(mapAt(j.wx + x, j.wy + y) === mapAt(j.wx + x, j.wy + y), 'mapa no determinista');
    }
  }
  const expected = new Set();
  if (sx && dx) for (let y = 0; y < g.fh; ++y) expected.add(`${dx > 0 ? g.fw - 1 : 0},${y}`);
  if (sy && dy) for (let x = 0; x < g.fw; ++x) {
    if (sx && dx && ((dx > 0 && x === g.fw - 1) || (dx < 0 && x === 0))) continue;
    expected.add(`${x},${dy > 0 ? g.fh - 1 : 0}`);
  }
  check(expected.size === used.size && [...expected].every(k => used.has(k)), 'cobertura de bandas');
  check(jobsMinimum(sx, sy, dx, dy) === bands(g, sx, sy, dx, dy).length, 'número de jobs lógico');
}

function jobsMinimum(sx, sy, dx, dy) { return (sx && dx ? 1 : 0) + (sy && dy ? 1 : 0); }

for (const [tw, th] of cases) for (const [sx, sy] of [[1, 0], [0, 1], [1, 1]]) {
  for (const sign of [-1, 1]) for (let d = 1; d <= 5; ++d) verifyCase(tw, th, sx, sy, sign * d, sy ? sign * d : 0);
  verifyCase(tw, th, sx, sy, 1, 1); verifyCase(tw, th, sx, sy, -1, -1);
}

// Horizontal-only y vertical-only are not allowed to reserve the other margin.
for (const [tw, th] of cases) {
  const x = geometry(tw, th, 1, 0), y = geometry(tw, th, 0, 1);
  check(x.fh === VH / th && x.fw === 2 * VW / tw + 2, `${tw}x${th}: extra vertical en X-only`);
  check(y.fw === VW / tw && y.fh === 2 * VH / th + 2, `${tw}x${th}: extra horizontal en Y-only`);
}

// Recentrado: both directions must have a legal target and preserve the viewport size.
for (const [tw, th] of cases) for (const [sx, sy] of [[1, 0], [0, 1], [1, 1]]) {
  const g = geometry(tw, th, sx, sy);
  if (sx) for (const p of [1, g.fw - g.cols - 1]) check(p >= 0 && p + g.cols <= g.fw, 'recentrado X inválido');
  if (sy) for (const p of [1, g.fh - g.rows - 1]) check(p >= 0 && p + g.rows <= g.fh, 'recentrado Y inválido');
}

if (failures.length) { console.error(`FAIL verify-tile-field-fill (${failures.length})\n${failures.join('\n')}`); process.exit(1); }
console.log(`OK verify-tile-field-fill: ${cases.length} geometrías, X/Y/XY, deltas 1..5, bandas, esquinas, recentrado y ejes únicos`);
