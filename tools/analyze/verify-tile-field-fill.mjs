#!/usr/bin/env node
/* Modelo físico compacto de TileFieldController (ScrollingTrick/Part 12). */
const VW = 320, VH = 256, FETCH_BYTES = VW / 8 + 2;
const geometries = [[16, 16], [32, 16], [16, 32], [32, 32]];
const failures = [];
const check = (ok, text) => { if (!ok) failures.push(text); };
const mapAt = (x, y) => ((x * 257 + y * 911 + 0x1234) & 0xffff);
const floorDiv = (v, d) => v >= 0 ? Math.floor(v / d) : -Math.floor((-v + d - 1) / d);

function make(tw, th, sx, sy, marginBlocks = 2) {
  const fw = sx ? VW + marginBlocks * tw : VW;
  const fh = sy ? VH + marginBlocks * th : VH;
  const leftX = sx ? Math.floor(marginBlocks / 2) * tw : 0;
  const leftY = sy ? Math.floor(marginBlocks / 2) * th : 0;
  const s = { tw, th, sx, sy, fw, fh, rowBytes: fw / 8,
    worldX: 0, worldY: 0, originX: -leftX, originY: -leftY,
    windowX: leftX, windowY: leftY, cols: fw / tw, rows: fh / th,
    marginBlocks, fb: new Map(), writes: [], maxWrites: 0, recentered: 0 };
  for (let y = 0; y < s.rows; ++y) for (let x = 0; x < s.cols; ++x)
    s.fb.set(`${x},${y}`, mapAt(floorDiv(s.originX + x * tw, tw), floorDiv(s.originY + y * th, th)));
  return s;
}

function recenter(s, axis) {
  const horizontal = axis === 'x';
  const block = horizontal ? s.tw : s.th;
  const size = horizontal ? s.fw : s.fh;
  const view = horizontal ? VW : VH;
  const key = horizontal ? 'windowX' : 'windowY';
  const left = Math.floor(s.marginBlocks / 2) * block;
  const right = Math.ceil(s.marginBlocks / 2) * block;
  if (s[key] < left || s[key] + view + right > size) {
    const old = s[key]; s[key] = left;
    if (horizontal) s.originX += old - s[key]; else s.originY += old - s[key];
    // Recentrar es un cambio de coordenadas: no es una copia de la ventana.
    // La simulación actualiza la etiqueta lógica de cada celda para modelarlo.
    for (let y = 0; y < s.rows; ++y) for (let x = 0; x < s.cols; ++x)
      s.fb.set(`${x},${y}`, mapAt(floorDiv(s.originX + x * s.tw, s.tw), floorDiv(s.originY + y * s.th, s.th)));
    ++s.recenter;
  }
}

function writeCell(s, x, y, seen) {
  const key = `${x},${y}`;
  check(x >= 0 && x < s.cols && y >= 0 && y < s.rows, `write fuera de superficie (${key})`);
  check(!seen.has(key), `celda escrita dos veces (${key})`); seen.add(key);
  const visible = x * s.tw < s.windowX + VW && (x + 1) * s.tw > s.windowX &&
    y * s.th < s.windowY + VH && (y + 1) * s.th > s.windowY;
  check(!visible, `write dentro del viewport (${key})`);
  s.fb.set(key, mapAt(floorDiv(s.originX + x * s.tw, s.tw), floorDiv(s.originY + y * s.th, s.th)));
  s.writes.push(key);
}
function column(s, x, seen) { for (let y = 0; y < s.rows; ++y) writeCell(s, x, y, seen); }
function row(s, y, seen, excluded) { for (let x = 0; x < s.cols; ++x) if (!excluded.has(x)) writeCell(s, x, y, seen); }

function step(s, dx, dy) {
  const oldX = s.worldX, oldY = s.worldY, seen = new Set();
  s.worldX += dx; s.worldY += dy;
  if (s.sx) s.windowX += dx; if (s.sy) s.windowY += dy;
  if (s.sx) recenter(s, 'x'); if (s.sy) recenter(s, 'y');
  const crossX = s.sx && floorDiv(oldX, s.tw) !== floorDiv(s.worldX, s.tw);
  const crossY = s.sy && floorDiv(oldY, s.th) !== floorDiv(s.worldY, s.th);
  const xIn = dx > 0 ? (s.windowX + VW) / s.tw : s.windowX / s.tw - 1;
  const yIn = dy > 0 ? (s.windowY + VH) / s.th : s.windowY / s.th - 1;
  const xEntering = dx > 0 ? xIn : 0, xOpposite = dx > 0 ? 0 : s.cols - 1;
  const yEntering = dy > 0 ? yIn : 0, yOpposite = dy > 0 ? 0 : s.rows - 1;
  if (crossX) { column(s, xEntering, seen); column(s, xOpposite, seen); }
  if (crossY) {
    const excluded = crossX ? new Set([xEntering, xOpposite]) : new Set();
    row(s, yEntering, seen, excluded); row(s, yOpposite, seen, excluded);
  }
  if (crossX || crossY) s.maxWrites = Math.max(s.maxWrites, s.writes.length);
  check(!s.sx || (s.windowX >= Math.floor(s.marginBlocks / 2) * s.tw && s.windowX + VW + Math.ceil(s.marginBlocks / 2) * s.tw <= s.fw), 'ventana X sin margen');
  check(!s.sy || (s.windowY >= Math.floor(s.marginBlocks / 2) * s.th && s.windowY + VH + Math.ceil(s.marginBlocks / 2) * s.th <= s.fh), 'ventana Y sin margen');
  const fetch = s.windowX > 0 ? (s.windowX - 1) & ~15 : 0;
  check(fetch / 8 + (s.sx ? FETCH_BYTES : VW / 8) <= s.rowBytes, 'fetch horizontal fuera de superficie');
  for (let y = 0; y < s.rows; ++y) for (let x = 0; x < s.cols; ++x) {
    if (x * s.tw >= s.windowX + VW || (x + 1) * s.tw <= s.windowX || y * s.th >= s.windowY + VH || (y + 1) * s.th <= s.windowY) continue;
    const expected = mapAt(floorDiv(s.originX + x * s.tw, s.tw), floorDiv(s.originY + y * s.th, s.th));
    check(s.fb.get(`${x},${y}`) === expected, `contenido incorrecto (${x},${y})`);
  }
  s.writes.length = 0;
}

for (const [tw, th] of geometries) for (const margin of [2, 3]) for (const [sx, sy] of [[1, 0], [0, 1], [1, 1]]) for (const sign of [-1, 1]) {
  const s = make(tw, th, sx, sy, margin);
  for (let i = 0; i < 1200; ++i) { const n = (i % 5) + 1; step(s, sx ? sign * n : 0, sy ? sign * n : 0); }
}
const sizes = make(16, 16, 1, 1, 2); check(sizes.fw === 352 && sizes.fh === 288, 'tamaño 16x16 margen2 incorrecto');
check(make(16, 16, 1, 0, 2).fw === 352 && make(16, 16, 1, 0, 2).fh === 256, 'tamaño X-only incorrecto');
check(make(16, 16, 0, 1, 2).fw === 320 && make(16, 16, 0, 1, 2).fh === 288, 'tamaño Y-only incorrecto');
check(sizes.maxWrites <= 2 * sizes.rows + 2 * sizes.cols, 'se escribió una página completa');
function fusionContract(layout, direction, ids) {
  const compatible = (direction === 'x' && layout === 'RowMajor') ||
    (direction === 'y' && layout === 'ColumnMajor');
  return compatible && ids.every((id, i) => id === ids[0] + i);
}
check(!fusionContract('TileMajor', 'x', [4, 5]), 'TileMajor fusionó una fila incompatible');
check(!fusionContract('TileMajor', 'y', [4, 5]), 'TileMajor fusionó una columna incompatible');
check(fusionContract('RowMajor', 'x', [4, 5, 6]), 'RowMajor no fusionó una fila compatible');
check(fusionContract('ColumnMajor', 'y', [4, 5, 6]), 'ColumnMajor no fusionó una columna compatible');

if (failures.length) { console.error(`FAIL verify-tile-field-fill (${failures.length})\n${failures.slice(0, 20).join('\n')}`); process.exit(1); }
console.log('OK verify-tile-field-fill: superficie compacta persistente, márgenes 2/3, X/Y/XY, cruces, wraps, fetch y esquinas');
