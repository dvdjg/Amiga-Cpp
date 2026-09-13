#!/usr/bin/env node
// F3-tools · Parser de mapas de Tiled (.tmx) → mapa virtual de la demo (índices).
//
// Lee el .tmx (XML) y extrae: dimensiones/tile, tilesets (firstgid + imagen) y los
// gids de cada layer, soportando las codificaciones de Tiled: CSV, XML (<tile gid=...>)
// y base64 (sin compresión, gzip o zlib). Soporta mapas finitos (matriz `gids`) e
// infinitos (`<chunk x y width height>`), limpiando los bits de flip (bit31..29).
// stdout: JSON compacto. Con --layers imprime también las capas.
// Uso: node tools/ehb/parse-tmx.mjs "ruta/Beginning Fields.tmx" [--layers] [--out fich]
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const argV = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 ? process.argv[i + 1] : d; };
const file = process.argv[2];
if (!file) { console.error('Uso: parse-tmx.mjs <mapa.tmx> [--layers] [--out f.json]'); process.exit(2); }
const xml = fs.readFileSync(file, 'utf8');

const attr = (tag, name) => { const m = xml.match(new RegExp('<.*?' + tag + '[^>]*\\s' + name + '="([^"]*)"')); return m ? m[1] : null; };
const attrs = (tag) => {
  const out = {};
  const m = xml.match(new RegExp('<' + tag + '([^>]*)>'));
  if (m) for (const mm of m[1].matchAll(/(\w+)="([^"]*)"/g)) out[mm[1]] = mm[2];
  return out;
};

const theMap = attrs('map');
const result = {
  file,
  tile: { width: parseInt(theMap.tilewidth, 10), height: parseInt(theMap.tileheight, 10) },
  size: { width: parseInt(theMap.width, 10), height: parseInt(theMap.height, 10) },
  orientation: theMap.orientation || 'orthogonal',
  tilesets: [],
  layers: [],
};
// tilesets
const tmxDir = path.dirname(file);
const tsRe = /<tileset\b([^>]*)\/\s*>/g; let tm;
while ((tm = tsRe.exec(xml))) { const t = {}; for (const mm of tm[1].matchAll(/(\w+)="([^"]*)"/g)) t[mm[1]] = mm[2]; if (t.firstgid) result.tilesets.push({ firstgid: parseInt(t.firstgid, 10), source: t.source, image: t.image, name: t.name }); }
// tileset con <image> inline
try {
  const m2 = xml.match(/<tileset\b([^>]*)>/); if (m2) { const t = {}; for (const mm of m2[1].matchAll(/(\w+)="([^"]*)"/g)) t[mm[1]] = mm[2]; if (!result.tilesets.some((x) => x.firstgid === parseInt(t.firstgid, 10)) && t.firstgid) { const im = xml.match(/<image\b[^>]*source="([^"]*)"/); result.tilesets.push({ firstgid: parseInt(t.firstgid, 10), image: im ? im[1] : null, name: t.name }); } }
} catch { }
// resolver .tsx externos (--resolve-tsx): imagen real + tilecount/columns del tileset
if (process.argv.includes('--resolve-tsx')) {
  const resAx = (rel) => { const p = path.resolve(tmxDir, rel); return fs.existsSync(p) ? p : null; };
  for (const ts of result.tilesets) {
    if (!ts.source) continue;
    const tsx = resAx(ts.source);
    if (!tsx) { ts.resolved = false; continue; }
    const sx = fs.readFileSync(tsx, 'utf8');
    const img = (sx.match(/<image\b[^>]*source="([^"]*)"/) || [])[1];
    const tc = (sx.match(/tilecount="(\d+)"/) || [])[1];
    const cols = (sx.match(/columns="(\d+)"/) || [])[1];
    ts.resolved = true;
    ts.image = img;
    ts.imagePath = img ? resAx(img) : null;
    ts.tilecount = tc ? parseInt(tc, 10) : null;
    ts.columns = cols ? parseInt(cols, 10) : null;
    ts.name = (sx.match(/<tileset\b[^>]*name="([^"]*)"/) || [])[1] || ts.name;
  }
}
// layers
const clean = (g) => g & 0x0fffffff; // limpia flips H/V/DIAG (bits 29..31)
// Decodifica las celdas de un <data>/<chunk> según la codificación heredada del <data>.
// base64: enteros little-endian de 32 bits, con gzip/zlib opcionales (default de Tiled).
const decodeCells = (body, enc, comp) => {
  if (enc === 'base64') {
    const b64 = body.replace(/\s+/g, '');
    let buf = Buffer.from(b64, 'base64');
    if (comp === 'gzip') buf = zlib.gunzipSync(buf);
    else if (comp === 'zlib') buf = zlib.inflateSync(buf);
    const gids = [];
    for (let i = 0; i + 4 <= buf.length; i += 4) gids.push(buf.readUInt32LE(i));
    return gids;
  }
  if (enc === 'csv') return body.replace(/\s+/g, '').split(',').map((s) => parseInt(s, 10) || 0);
  const gids = []; // XML: <tile gid=".."/>
  for (const t of body.matchAll(/<tile\b[^>]*gid="(\d+)"/g)) gids.push(parseInt(t[1], 10) || 0);
  return gids;
};
const layerRe = /<layer\b([^>]*)>([\s\S]*?)<\/layer>/g; let lm;
while ((lm = layerRe.exec(xml))) {
  const l = {};
  for (const mm of lm[1].matchAll(/(\w+)="([^"]*)"/g)) l[mm[1]] = mm[2];
  const cellBody = lm[2];
  const num = (s) => parseInt(s, 10) || 0;
  const dm = cellBody.match(/<data\b([^>]*)>([\s\S]*?)<\/data>/);
  const da = { encoding: 'xml', compression: '' };
  if (dm) for (const mm of dm[1].matchAll(/(\w+)="([^"]*)"/g)) da[mm[1]] = mm[2];
  if (/<chunk\b/.test(cellBody)) {
    // Mapa INFINITO de Tiled: celdas por <chunk x y width height>, con la codificación del <data>.
    const chunks = [];
    const chunkRe = /<chunk\b([^>]*)>([\s\S]*?)<\/chunk>/g; let cm;
    while ((cm = chunkRe.exec(cellBody))) {
      const ca = {};
      for (const mm of cm[1].matchAll(/(\w+)="([^"]*)"/g)) ca[mm[1]] = mm[2];
      chunks.push({ x: num(ca.x), y: num(ca.y), width: num(ca.width), height: num(ca.height), gids: decodeCells(cm[2], da.encoding, da.compression).map(clean) });
    }
    result.layers.push({ name: l.name || 'layer', width: 0, height: 0, infinite: true, encoding: da.encoding, chunks });
  } else {
    const body = dm ? dm[2] : cellBody;
    result.layers.push({ name: l.name || 'layer', width: num(l.width), height: num(l.height), infinite: false, encoding: da.encoding, gids: decodeCells(body, da.encoding, da.compression).map(clean) });
  }
}
console.log(`[tmx] ${path.basename(file)}: ${result.size.width}x${result.size.height} tile ${result.tile.width}x${result.tile.height} | tilesets=${result.tilesets.map((t) => `gid${t.firstgid}`).join(',')} | layers=${result.layers.map((l) => l.infinite ? `${l.name}(${l.chunks.length} chunks)` : `${l.name}(${l.width}x${l.height})`).join(',')}`);

const out = argV('--out', '');
if (out) { fs.writeFileSync(out, JSON.stringify(result, null, 2), 'utf8'); console.log(`[tmx] -> ${out}`); }
if (!out || process.argv.includes('--layers')) {
  for (const l of result.layers) {
    const cells = l.infinite ? l.chunks.reduce((s, c) => s + c.gids.length, 0) : l.gids.length;
    const uniq = l.infinite
      ? new Set(l.chunks.flatMap((c) => c.gids.filter((g) => g > 0))).size
      : new Set(l.gids.filter((g) => g > 0)).size;
    console.log(`[tmx] capa '${l.name}': ${cells} celdas, únicos=${uniq}${l.infinite ? `, ${l.chunks.length} chunks` : ''}`);
  }
}