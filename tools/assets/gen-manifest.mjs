#!/usr/bin/env node
// Generador del manifiesto de assets de una demo/juego (ROADMAP_GAME_API.md §4).
//
// Lee un `assets.manifest.json` (fuente única: nombre, tipo, ruta y GEOMETRÍA de cada asset) y
// emite el `assets.manifest.hpp` que consume el código de juego: los `INCBIN` de los blobs + un
// namespace con un accesor por asset (`<name>_data()`/`<name>_size()`, `<name>_desc()` para
// sprites, `<name>_words()` para paletas) y un `register_assets(eng::Assets&)` que registra todo
// con su geometría en una llamada. Así la geometría deja de ser dato del juego: vive en el JSON.
//
// Uso:
//   node tools/assets/gen-manifest.mjs <assets.manifest.json> [--out <ruta.hpp>] [--check]
//
//   --check  regenera en memoria y compara con el fichero; falla (exit 1) si difieren. Es el gate
//            de "manifiesto sincronizado" (se puede correr en regresión sin tocar el fichero).
//
// Esquema del JSON:
//   {
//     "namespace": "abyss",
//     "include": "support/gcc8_c_support.h",       // opcional (por defecto ese)
//     "assets": [
//       { "name":"img", "kind":"bitmap",  "path":"assets/.../abyss.bpl",
//         "width":320, "height":256, "planes":5, "layout":"interleaved" },
//       { "name":"bob", "kind":"sprite",  "path":"assets/.../bob.bpl",
//         "width":32, "height":16, "planes":5, "frames":6, "frame_stride":640,
//         "layout":"interleaved", "draw":"cookie_cut", "mask_pack":"interleaved_pair" },
//       { "name":"mod", "kind":"music",   "path":"assets/amiga/audio/testmod.p61" },
//       { "name":"pal", "kind":"palette", "path":"assets/.../abyss.pal", "colors":32 }
//     ]
//   }
//
// `bytes` (opcional) fija el tamaño esperado del blob; si el fichero no cuadra, el generador falla.

import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

const args = process.argv.slice(2);
const jsonPath = args.find((a) => !a.startsWith('--'));
const check = args.includes('--check');
const outIdx = args.indexOf('--out');
if (!jsonPath) {
  console.error('Uso: node tools/assets/gen-manifest.mjs <assets.manifest.json> [--out <ruta.hpp>] [--check]');
  process.exit(2);
}

const absJson = path.resolve(process.cwd(), jsonPath);
const manifest = JSON.parse(fs.readFileSync(absJson, 'utf8'));
const ns = manifest.namespace;
if (!ns || !Array.isArray(manifest.assets) || manifest.assets.length === 0) {
  console.error('[gen-manifest] el manifiesto necesita "namespace" y "assets[]" no vacío.');
  process.exit(2);
}
const include = manifest.include || 'support/gcc8_c_support.h';
const outPath = outIdx >= 0 ? path.resolve(process.cwd(), args[outIdx + 1])
                            : path.join(path.dirname(absJson), 'assets.manifest.hpp');

// --- Validación de los blobs (existencia y, si procede, tamaño) -------------------------------
function blobBytes(p) {
  const abs = path.resolve(ROOT, p);
  if (!fs.existsSync(abs)) {
    console.error(`[gen-manifest] falta el blob: ${p} (resuelto a ${abs})`);
    process.exit(1);
  }
  return fs.statSync(abs).size;
}
for (const a of manifest.assets) {
  const bytes = blobBytes(a.path);
  const expected = a.bytes;
  if (expected !== undefined && expected !== bytes) {
    console.error(`[gen-manifest] ${a.name}: ${a.path} mide ${bytes} B, el JSON dice ${expected} B.`);
    process.exit(1);
  }
  if (a.kind === 'bitmap') {
    const exp = (a.width / 8) * a.height * a.planes;
    if (a.width % 8 !== 0 || exp !== bytes) {
      console.error(`[gen-manifest] ${a.name}: bitmap ${a.width}x${a.height}x${a.planes} espera ${exp} B, tiene ${bytes}.`);
      process.exit(1);
    }
  } else if (a.kind === 'palette') {
    if (a.colors * 2 !== bytes) {
      console.error(`[gen-manifest] ${a.name}: paleta de ${a.colors} colores espera ${a.colors * 2} B, tiene ${bytes}.`);
      process.exit(1);
    }
  } else if (a.kind === 'sprite') {
    const frames = a.frames || 1;
    const stride = a.frame_stride || (bytes / frames);
    if (stride * frames !== bytes) {
      console.error(`[gen-manifest] ${a.name}: sprite ${frames} frames × ${stride} B ≠ ${bytes} B.`);
      process.exit(1);
    }
  } else if (a.kind !== 'music') {
    console.error(`[gen-manifest] ${a.name}: kind desconocido "${a.kind}".`);
    process.exit(2);
  }
}

// --- Mapeos de enumerados JSON -> C++ ---------------------------------------------------------
const LAYOUT = { interleaved: 'eng::graphics::PlaneLayout::Interleaved', contiguous: 'eng::graphics::PlaneLayout::Contiguous' };
const BOB_LAYOUT = { interleaved: 'eng::graphics::BobLayout::Interleaved', separate: 'eng::graphics::BobLayout::Separate' };
const BOB_DRAW = { cookie_cut: 'eng::graphics::BobDraw::CookieCut', copy: 'eng::graphics::BobDraw::Copy' };
const MASK_PACK = { interleaved_pair: 'eng::graphics::BobMaskPack::InterleavedPair', none: 'eng::graphics::BobMaskPack::None' };
const pick = (map, v, what, name) => {
  if (!(v in map)) { console.error(`[gen-manifest] ${name}: ${what} "${v}" no válido (${Object.keys(map).join(', ')}).`); process.exit(2); }
  return map[v];
};

const MACRO = `${ns.toUpperCase()}_BLOB_SIZE`;
const L = [];
L.push('#pragma once', '');
L.push('/// \\file assets.manifest.hpp');
L.push(`/// **GENERADO** por \`tools/assets/gen-manifest.mjs\` desde \`${path.basename(absJson)}\`. NO EDITAR A`);
L.push('/// MANO: cambia el JSON y regenera (`node tools/assets/gen-manifest.mjs <json>`). Declara los');
L.push('/// `INCBIN` de los blobs y expone un accesor por asset más `register_assets(Assets&)`, que');
L.push('/// registra cada recurso con su **geometría** (incrustada aquí, no en el código de juego).');
L.push('');
L.push(`#include "${include}"`);
L.push('');
L.push('#include <eng/core/types/types.hpp>');
L.push('#include <eng/graphics/sprite_asset.hpp>');
L.push('');
for (const a of manifest.assets) L.push(`INCBIN(${ns}_${a.name}, "${a.path}");`);
L.push('');
L.push(`namespace ${ns} {`, '');
L.push('/// Tamaño del blob `sym` (diferencia de los símbolos `incbin_*_start`/`_end` de `INCBIN`).');
L.push(`#define ${MACRO}(sym)                                                                          \\`);
L.push('	static_cast<eng::u32>(reinterpret_cast<const char*>(&incbin_##sym##_end) -             \\');
L.push('			      incbin_##sym##_start)');
L.push('');
for (const a of manifest.assets) {
  const sym = `${ns}_${a.name}`;
  // `INCBIN(ns_name, path)` define la variable `const void* <sym>` (el puntero a los datos) y los
  // símbolos `incbin_<sym>_start`/`_end` (para el tamaño); se usan ambos.
  if (a.kind === 'palette') {
    L.push(`[[nodiscard]] inline const eng::u16* ${a.name}_words() noexcept { return reinterpret_cast<const eng::u16*>(${sym}); }`);
    L.push(`[[nodiscard]] inline eng::u32 ${a.name}_size() noexcept { return ${MACRO}(${sym}); }`);
  } else {
    L.push(`[[nodiscard]] inline const eng::u8* ${a.name}_data() noexcept { return reinterpret_cast<const eng::u8*>(${sym}); }`);
    L.push(`[[nodiscard]] inline eng::u32 ${a.name}_size() noexcept { return ${MACRO}(${sym}); }`);
  }
  if (a.kind === 'sprite') {
    L.push(`[[nodiscard]] inline eng::graphics::Bob ${a.name}_desc() noexcept {`);
    L.push('\teng::graphics::Bob d {};');
    L.push(`\td.width = ${a.width}u;`);
    L.push(`\td.height = ${a.height}u;`);
    L.push(`\td.planes = ${a.planes}u;`);
    L.push(`\td.frame_count = ${a.frames || 1}u;`);
    L.push(`\td.frame_stride = ${a.frame_stride || 0}u;`);
    L.push(`\td.layout = ${pick(BOB_LAYOUT, a.layout || 'interleaved', 'layout', a.name)};`);
    L.push(`\td.draw = ${pick(BOB_DRAW, a.draw || 'cookie_cut', 'draw', a.name)};`);
    L.push(`\td.mask_pack = ${pick(MASK_PACK, a.mask_pack || 'none', 'mask_pack', a.name)};`);
    L.push('\treturn d;');
    L.push('}');
  }
  L.push('');
}
L.push('/// Registra **todos** los assets del manifiesto en el `Assets` del juego, con su geometría.');
L.push('/// `Assets` es plantilla para no acoplar el header a \`eng/api/assets.hpp\` (lo incluye el juego).');
L.push('template <class Assets>');
L.push('[[nodiscard]] inline bool register_assets(Assets& a) noexcept {');
for (const a of manifest.assets) {
  if (a.kind === 'bitmap') {
    L.push(`\tif (!a.add_bitmap("${a.name}", ${a.name}_data(), ${a.name}_size(), ${a.width}u, ${a.height}u, ${a.planes}u, ${pick(LAYOUT, a.layout || 'interleaved', 'layout', a.name)})) return false;`);
  } else if (a.kind === 'sprite') {
    L.push(`\tif (!a.add_sprite("${a.name}", ${a.name}_data(), ${a.name}_size(), ${a.name}_desc())) return false;`);
  } else if (a.kind === 'music') {
    L.push(`\tif (!a.template add<eng::MusicTag>("${a.name}", ${a.name}_data(), ${a.name}_size())) return false;`);
  } else if (a.kind === 'palette') {
    L.push(`\tif (!a.template add<eng::PaletteTag>("${a.name}", reinterpret_cast<const eng::u8*>(${a.name}_words()), ${a.name}_size())) return false;`);
  }
}
L.push('\treturn true;');
L.push('}');
L.push('');
L.push(`#undef ${MACRO}`);
L.push('');
L.push(`} // namespace ${ns}`);
L.push('');

const text = L.join('\n');

if (check) {
  const current = fs.existsSync(outPath) ? fs.readFileSync(outPath, 'utf8') : null;
  if (current !== text) {
    console.error(`[gen-manifest] DESINCRONIZADO: ${path.relative(ROOT, outPath)} no coincide con ${path.basename(absJson)}.`);
    console.error('  Regenera: node tools/assets/gen-manifest.mjs ' + path.relative(ROOT, absJson));
    process.exit(1);
  }
  console.log(`[gen-manifest] OK: ${path.relative(ROOT, outPath)} sincronizado (${manifest.assets.length} assets).`);
} else {
  fs.writeFileSync(outPath, text);
  console.log(`[gen-manifest] escrito ${path.relative(ROOT, outPath)} (${manifest.assets.length} assets).`);
}
