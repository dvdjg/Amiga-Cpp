#!/usr/bin/env node
// ---------------------------------------------------------------------------
// tools/assets/hud-atlas.mjs - atlas de iconos del HUD (`eng::hud::IconAtlas`).
//
// Lee una hoja de iconos PNG de `assets/amiga/hud/`, cuantiza a N colores
// (N <= 2^planos), convierte a **planar interleaved por filas** y emite el atlas
// Chip + la tabla de frames + la paleta. Formato canonico:
// `docs/engine/architecture/HUD_SYSTEM.md` §8.2.
//
//   words_per_row = frame_w / 16
//   stride_words  = words_per_row * frame_h          (palabras por plano y frame)
//   offset(f)     = f * planes * stride_words
//   palabra(f,r,p,w) = offset(f) + (r * planes + p) * words_per_row + w
//   bit 15 = pixel 0 (MSB primero); palabras big-endian en el .bin.
//
// Uso:
//   node tools/assets/hud-atlas.mjs <sheet.png> [--frame 16x16] [--planes 2]
//        [--masked|--no-masked] [--colors N] [--out out/assets/hud/<nombre>]
//        [--check] [--selftest] [--help]
// ---------------------------------------------------------------------------
import * as fs from 'node:fs';
import * as path from 'node:path';
import * as crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { PNG } from 'pngjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

const has = (n) => process.argv.includes(n);
const arg = (n, d) => {
	const i = process.argv.indexOf(n);
	return i >= 0 && i + 1 < process.argv.length && !process.argv[i + 1].startsWith('--')
		? process.argv[i + 1]
		: d;
};
const fail = (msg, code = 1) => {
	console.error(`[hud-atlas] ${msg}`);
	process.exit(code);
};

// --- utilidades de color (paleta Amiga RGB444) ------------------------------

/// Color RGB444 a entero `0x0RGB` (12 bits) desde componentes 0..255.
export function rgb444(r, g, b) {
	return ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
}

/// Componentes 0..255 desde un color RGB444.
export function rgb444_parts(c) {
	return [((c >> 8) & 0xf) * 17, ((c >> 4) & 0xf) * 17, (c & 0xf) * 17];
}

// --- cuantizacion determinista (exacta o median-cut) ------------------------

/// Cuantiza la lista de colores `{c, n}` (RGB444 + cuenta) a `slots` entradas.
/// Exacta si caben (ordenadas por valor); si no, median-cut por canal dominante
/// con mediana ponderada. Devuelve la paleta (array de RGB444, ordenada).
export function quantizePalette(counts, slots) {
	if (slots < 1) {
		fail('slots de paleta < 1');
	}
	const entries = [...counts.entries()].sort((a, b) => a[0] - b[0]).map(([c, n]) => ({ c, n }));
	if (entries.length <= slots) {
		return entries.map((e) => e.c);
	}
	let boxes = [entries];
	while (boxes.length < slots) {
		let best = -1;
		let best_span = -1;
		for (let i = 0; i < boxes.length; i++) {
			const box = boxes[i];
			if (box.length < 2) continue;
			const span = channelSpan(box);
			if (span.size > best_span) {
				best_span = span.size;
				best = i;
			}
		}
		if (best < 0) break; // no hay caja divisible
		const box = boxes[best];
		const span = channelSpan(box);
		const ch = span.channel;
		const comp = (c) => (ch === 0 ? (c >> 8) & 0xf : ch === 1 ? (c >> 4) & 0xf : c & 0xf);
		box.sort((a, b) => comp(a.c) - comp(b.c));
		let total = 0;
		for (const e of box) total += e.n;
		let acc = 0;
		let cut = 1;
		for (let i = 0; i < box.length - 1; i++) {
			acc += box[i].n;
			if (acc * 2 >= total) {
				cut = i + 1;
				break;
			}
		}
		boxes.splice(best, 1, box.slice(0, cut), box.slice(cut));
	}
	return boxes.map((box) => {
		let r = 0, g = 0, b = 0, w = 0;
		for (const e of box) {
			const [pr, pg, pb] = rgb444_parts(e.c);
			r += pr * e.n;
			g += pg * e.n;
			b += pb * e.n;
			w += e.n;
		}
		return rgb444(Math.round(r / w), Math.round(g / w), Math.round(b / w));
	}).sort((a, b) => a - b);
}

/// Canal de mayor rango de una caja (r/g/b) y su tamaño.
function channelSpan(box) {
	let rmin = 15, rmax = 0, gmin = 15, gmax = 0, bmin = 15, bmax = 0;
	for (const e of box) {
		const r = (e.c >> 8) & 0xf, g = (e.c >> 4) & 0xf, b = e.c & 0xf;
		if (r < rmin) rmin = r;
		if (r > rmax) rmax = r;
		if (g < gmin) gmin = g;
		if (g > gmax) gmax = g;
		if (b < bmin) bmin = b;
		if (b > bmax) bmax = b;
	}
	const dr = rmax - rmin, dg = gmax - gmin, db = bmax - bmin;
	const channel = dr >= dg && dr >= db ? 0 : dg >= db ? 1 : 2;
	return { channel, size: Math.max(dr, dg, db) };
}

/// Indice de la entrada mas cercana a `c` en `palette` (distancia RGB444).
function nearestIndex(palette, c, from = 0) {
	const [r, g, b] = rgb444_parts(c);
	let best = from;
	let best_d = Infinity;
	for (let i = from; i < palette.length; i++) {
		const [pr, pg, pb] = rgb444_parts(palette[i]);
		const d = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb);
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	return best;
}

// --- hojas de frames ---------------------------------------------------------

/// Corta la hoja PNG en frames `fw x fh` en orden lectura (izq->der, arriba->abajo).
/// Devuelve `{ frames, fw, fh }` con `frames[i].idx` ya en indices de paleta.
export function sliceQuantized(png, fw, fh, planes, masked, colors) {
	if (png.width % fw !== 0 || png.height % fh !== 0) {
		fail(`la hoja ${png.width}x${png.height} no es multiplo de frame ${fw}x${fh}`);
	}
	if (fw % 16 !== 0) {
		fail(`frame_w ${fw} no es multiplo de 16 (una palabra por fila de palabra)`);
	}
	const count = (png.width / fw) * (png.height / fh);
	const color_slots = masked ? colors - 1 : colors;
	if (color_slots < 1) {
		fail(`sin entradas de color: colors=${colors} masked=${masked}`);
	}
	const counts = new Map();
	for (let y = 0; y < png.height; y++) {
		for (let x = 0; x < png.width; x++) {
			const o = (y * png.width + x) * 4;
			if (masked && png.data[o + 3] < 128) continue;
			const c = rgb444(png.data[o], png.data[o + 1], png.data[o + 2]);
			counts.set(c, (counts.get(c) ?? 0) + 1);
		}
	}
	if (counts.size === 0) {
		fail('la hoja no tiene pixeles opacos');
	}
	const quant = quantizePalette(counts, color_slots);
	const palette = masked ? [0x000, ...quant] : quant;
	const frames = [];
	for (let f = 0; f < count; f++) {
		const fx = (f % (png.width / fw)) * fw;
		const fy = Math.floor(f / (png.width / fw)) * fh;
		const idx = new Uint8Array(fw * fh);
		for (let y = 0; y < fh; y++) {
			for (let x = 0; x < fw; x++) {
				const o = ((fy + y) * png.width + (fx + x)) * 4;
				if (masked && png.data[o + 3] < 128) {
					idx[y * fw + x] = 0;
					continue;
				}
				const c = rgb444(png.data[o], png.data[o + 1], png.data[o + 2]);
				idx[y * fw + x] = nearestIndex(palette, c, masked ? 1 : 0);
			}
		}
		frames.push({ idx });
	}
	return { frames, palette, fw, fh };
}

// --- empaquetado planar interleaved por filas --------------------------------

/// Empaqueta los frames en palabras planares interleaved por filas (bit 15 = pixel 0).
export function packAtlas(frames, planes, fw, fh, masked) {
	const wordsPerRow = fw >> 4;
	const strideWords = wordsPerRow * fh;
	const out = Buffer.alloc(frames.length * planes * strideWords * 2);
	for (let f = 0; f < frames.length; f++) {
		for (let r = 0; r < fh; r++) {
			for (let p = 0; p < planes; p++) {
				for (let w = 0; w < wordsPerRow; w++) {
					let word = 0;
					for (let b = 0; b < 16; b++) {
						const x = (w << 4) + b;
						if (x >= fw) continue;
						const v = frames[f].idx[r * fw + x];
						const bit = masked
							? (p === 0 ? (v !== 0 ? 1 : 0) : (v >> (p - 1)) & 1)
							: (v >> p) & 1;
						if (bit) word |= 0x8000 >> b;
					}
					const off = (f * planes * strideWords + (r * planes + p) * wordsPerRow + w) * 2;
					out.writeUInt16BE(word, off);
				}
			}
		}
	}
	return { words: out, strideWords, wordCount: frames.length * planes * strideWords };
}

/// Decodifica el atlas (inverso exacto de `packAtlas`) a indices por pixel.
export function decodeAtlas(words, planes, fw, fh, count, masked) {
	const wordsPerRow = fw >> 4;
	const strideWords = wordsPerRow * fh;
	const frames = [];
	for (let f = 0; f < count; f++) {
		const idx = new Uint8Array(fw * fh);
		for (let r = 0; r < fh; r++) {
			for (let x = 0; x < fw; x++) {
				const w = x >> 4;
				const b = x & 15;
				let v = 0;
				for (let p = 0; p < planes; p++) {
					const off = (f * planes * strideWords + (r * planes + p) * wordsPerRow + w) * 2;
					const bit = (words.readUInt16BE(off) >> (15 - b)) & 1;
					if (masked) {
						if (p === 0) {
							if (!bit) v = 0;
						} else if (bit) {
							v |= 1 << (p - 1);
						}
					} else if (bit) {
						v |= 1 << p;
					}
				}
				idx[r * fw + x] = v;
			}
		}
		frames.push({ idx });
	}
	return frames;
}

/// Compara dos juegos de frames por indice; devuelve el porcentaje de igualdad.
export function roundTripPct(a, b) {
	let eq = 0;
	let total = 0;
	for (let f = 0; f < a.length; f++) {
		for (let i = 0; i < a[f].idx.length; i++) {
			++total;
			if (a[f].idx[i] === b[f].idx[i]) ++eq;
		}
	}
	return total === 0 ? 100 : (100 * eq) / total;
}

// --- salidas -----------------------------------------------------------------

/// Genera el header C++ con el `INCBIN_CHIP`, la tabla de frames y el accesor.
export function emitHeader(ns, srcRel, binRel, geo) {
	const frames = geo.frameNames
		.map((n, i) => `\tk${n} = ${i}u`)
		.join(',\n');
	return `#pragma once

/// \\file ${ns}.atlas.hpp
/// **GENERADO** por \`tools/assets/hud-atlas.mjs\` desde \`${srcRel}\`. NO EDITAR A MANO:
/// cambia la hoja fuente y regenera (\`node tools/assets/hud-atlas.mjs ${srcRel}\`).
/// Formato canonico: HUD_SYSTEM.md §8.2 (palabras interleaved por filas; bit 15 = pixel 0).

#include "support/gcc8_c_support.h"

#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/hud/theme.hpp>

// En \`.INCBIN.MEMF_CHIP\` (elf2hunk -> HUNKF_CHIP): procedencia Chip auditada.
INCBIN_CHIP(${ns}_atlas_words, "${binRel}");

namespace ${ns} {

constexpr eng::u16 kFrameW = ${geo.frame_w}u;
constexpr eng::u16 kFrameH = ${geo.frame_h}u;
constexpr eng::u8 kPlanes = ${geo.planes}u;
constexpr bool kMasked = ${geo.masked ? 'true' : 'false'};
constexpr eng::u16 kCount = ${geo.count}u;
constexpr eng::u16 kStrideWords = static_cast<eng::u16>((kFrameW / 16u) * kFrameH);
constexpr eng::u32 kWordCount = static_cast<eng::u32>(kCount) * kPlanes * kStrideWords;

/// Indice de cada frame en el orden del pipeline (izquierda a derecha, arriba a abajo).
enum Frame : eng::u16 {
${frames},
};

/// Vista \`ChipView<HudAtlasTag>\` sobre el blob incbinado + descriptor para el tema.
[[nodiscard]] inline eng::hud::IconAtlas make_icon_atlas() noexcept {
	const eng::ByteView<eng::hud::HudAtlasTag> bytes = eng::ByteView<eng::hud::HudAtlasTag>::from(
		eng::Span<const eng::u8> {static_cast<const eng::u8*>(${ns}_atlas_words),
					  static_cast<eng::usize>(kWordCount) * 2u});
	return eng::hud::IconAtlas {eng::as_chip(bytes), kFrameW, kFrameH, kCount,
				    kStrideWords, kPlanes, kMasked};
}

} // namespace ${ns}
`;
}

/// Genera el PNG de preview (RGBA) desde los indices y la paleta.
export function emitPreview(frames, palette, fw, fh, masked) {
	const count = frames.length;
	const png = new PNG({ width: fw * count, height: fh });
	for (let f = 0; f < count; f++) {
		for (let y = 0; y < fh; y++) {
			for (let x = 0; x < fw; x++) {
				const v = frames[f].idx[y * fw + x];
				const [r, g, b] = rgb444_parts(palette[v] ?? 0);
				const o = (y * png.width + f * fw + x) * 4;
				png.data[o] = r;
				png.data[o + 1] = g;
				png.data[o + 2] = b;
				png.data[o + 3] = masked && v === 0 ? 0 : 255;
			}
		}
	}
	return PNG.sync.write(png);
}

/// Genera el JSON de procedencia (determinista; sin fechas).
export function emitJson(srcRel, geo, palette, sha) {
	return (
		JSON.stringify(
			{
				source: srcRel,
				frame_w: geo.frame_w,
				frame_h: geo.frame_h,
				planes: geo.planes,
				masked: geo.masked,
				count: geo.count,
				stride_words: geo.strideWords,
				words: geo.wordCount,
				palette: palette.map((c) => '0x' + c.toString(16).padStart(3, '0')),
				frames: geo.frameNames,
				bin_sha256: sha,
			},
			null,
			2,
		) + '\n'
	);
}

// --- pipeline completo (en memoria) -----------------------------------------

/// Ejecuta el pipeline completo sobre una hoja ya decodificada. Devuelve las
/// salidas en memoria (bin/header/json/preview) para escribir o comparar.
export function buildOutputs(png, srcRel, opts) {
	const { frames, palette, fw, fh } = sliceQuantized(
		png,
		opts.frame_w,
		opts.frame_h,
		opts.planes,
		opts.masked,
		opts.colors,
	);
	const packed = packAtlas(frames, opts.planes, fw, fh, opts.masked);
	const decoded = decodeAtlas(packed.words, opts.planes, fw, fh, frames.length, opts.masked);
	const pct = roundTripPct(frames, decoded);
	if (pct !== 100) {
		fail(`round-trip ${pct.toFixed(2)}% (debe ser 100.00%)`);
	}
	const ns = opts.ns;
	const binName = `${ns}.atlas.bin`;
	const geo = {
		frame_w: fw,
		frame_h: fh,
		planes: opts.planes,
		masked: opts.masked,
		count: frames.length,
		strideWords: packed.strideWords,
		wordCount: packed.wordCount,
		frameNames: frames.map((_, i) => `Icon${i}`),
	};
	const sha = crypto.createHash('sha256').update(packed.words).digest('hex');
	return {
		binName,
		bin: packed.words,
		header: emitHeader(ns, srcRel, opts.binRel, geo),
		json: emitJson(srcRel, geo, palette, sha),
		preview: emitPreview(frames, palette, fw, fh, opts.masked),
		palette,
		frames,
		geo,
	};
}

// --- selftest ----------------------------------------------------------------

/// Autotest sin assets externos: PNG sintetico, geometria, bit order y round-trip.
function selftest() {
	const fw = 16, fh = 16, planes = 2, masked = true;
	const sheet = new PNG({ width: fw * 2, height: fh });
	for (let i = 0; i < sheet.data.length; i += 4) {
		sheet.data[i] = 0;
		sheet.data[i + 1] = 0;
		sheet.data[i + 2] = 0;
		sheet.data[i + 3] = 0;
	}
	const set = (x, y, r, g, b, a) => {
		const o = (y * sheet.width + x) * 4;
		sheet.data[o] = r;
		sheet.data[o + 1] = g;
		sheet.data[o + 2] = b;
		sheet.data[o + 3] = a;
	};
	set(0, 0, 255, 0, 0, 255); // frame 0: pixel (0,0) rojo
	for (let x = 0; x < fw; x++) set(fw + x, 0, 0, 255, 0, 255); // frame 1: fila 0 verde
	const out = buildOutputs(sheet, 'synthetic', {
		frame_w: fw,
		frame_h: fh,
		planes,
		masked,
		colors: 1 << (planes - 1),
		ns: 'selftest',
		binRel: 'selftest.atlas.bin',
	});
	let bad = 0;
	const check = (ok, msg) => {
		if (!ok) {
			console.error(`[hud-atlas] selftest FAIL: ${msg}`);
			++bad;
		}
	};
	check(out.geo.strideWords === 16, 'stride_words = words_per_row * frame_h');
	check(out.bin.length === 2 * planes * 16 * 2, 'tamano del .bin');
	check(out.bin.readUInt16BE(0) === 0x8000, 'frame0 plano 0 (mascara): pixel 0 opaco');
	check(out.bin.readUInt16BE(2) === 0x8000, 'frame0 plano 1 (color): pixel 0');
	check(out.bin.readUInt16BE(4) === 0, 'frame0 fila 1 vacia');
	const f1 = planes * 16 * 2;
	check(out.bin.readUInt16BE(f1) === 0xffff, 'frame1 fila 0 completa (mascara)');
	check(out.bin.readUInt16BE(f1 + 2) === 0xffff, 'frame1 fila 0 completa (color)');
	check(out.header.includes('kStrideWords = static_cast<eng::u16>((kFrameW / 16u) * kFrameH)'), 'header: stride');
	check(out.header.includes('INCBIN_CHIP'), 'header: procedencia Chip');
	check(out.header.includes('kMasked = true'), 'header: mascara');
	check(out.preview.length > 0, 'preview generado');
	if (bad === 0) {
		console.log('[hud-atlas] selftest OK: geometria, bit order MSB-first y round-trip 100%.');
		return 0;
	}
	return 1;
}

// --- CLI ---------------------------------------------------------------------

/// Punto de entrada: valida argumentos, ejecuta el pipeline y escribe salidas.
function main() {
	if (has('--selftest')) {
		process.exit(selftest());
	}
	const src = process.argv.slice(2).find((a) => !a.startsWith('--'));
	if (src === undefined || has('--help') || has('-h')) {
		console.log('Uso: node tools/assets/hud-atlas.mjs <sheet.png> [--frame 16x16] [--planes 2]');
		console.log('     [--masked|--no-masked] [--colors N] [--out out/assets/hud/<nombre>]');
		console.log('     [--check] [--selftest]');
		console.log('  Hoja de frames en orden lectura; salida planar interleaved (HUD_SYSTEM.md §8.2).');
		process.exit(src === undefined ? 2 : 0);
	}
	const frame = (arg('--frame', '16x16') || '16x16').split('x').map(Number);
	if (!Number.isInteger(frame[0]) || !Number.isInteger(frame[1]) || frame[0] < 16 || frame[1] < 1) {
		fail('--frame debe ser WxH con W multiplo de 16 y H >= 1', 2);
	}
	const planes = Number(arg('--planes', '2'));
	if (!Number.isInteger(planes) || planes < 1 || planes > 6) fail('--planes debe ser 1..6', 2);
	const masked = has('--masked') || (!has('--no-masked') && planes <= 3);
	const colors = Number(arg('--colors', String(masked ? 1 << (planes - 1) : 1 << planes)));
	if (!Number.isInteger(colors) || colors < 1 || colors > 1 << planes) {
		fail('--colors fuera de rango (1..2^planes)', 2);
	}
	const srcRel = path.relative(ROOT, path.resolve(src)).replace(/\\/g, '/');
	const ns = path.basename(src, path.extname(src)).replace(/[^A-Za-z0-9_]/g, '_');
	const outDir = path.resolve(ROOT, arg('--out', `out/assets/hud/${ns}`));
	const binRel = path.relative(ROOT, path.join(outDir, `${ns}.atlas.bin`)).replace(/\\/g, '/');
	if (!fs.existsSync(path.resolve(ROOT, src))) fail(`no existe la hoja: ${src}`, 2);
	const png = PNG.sync.read(fs.readFileSync(path.resolve(ROOT, src)));
	const out = buildOutputs(png, srcRel, {
		frame_w: frame[0],
		frame_h: frame[1],
		planes,
		masked,
		colors,
		ns,
		binRel,
	});
	const files = [
		[`${ns}.atlas.bin`, out.bin],
		[`${ns}.atlas.hpp`, out.header],
		[`${ns}.atlas.json`, out.json],
	];
	if (has('--check')) {
		let bad = 0;
		for (const [name, data] of files) {
			const p = path.join(outDir, name);
			const cur = fs.existsSync(p) ? fs.readFileSync(p) : null;
			if (cur === null || !cur.equals(Buffer.isBuffer(data) ? data : Buffer.from(data))) {
				console.error(`[hud-atlas] DESINCRONIZADO: ${path.relative(ROOT, p)}`);
				++bad;
			}
		}
		if (bad > 0) {
			console.error(`  Regenera: node tools/assets/hud-atlas.mjs ${srcRel}`);
			process.exit(1);
		}
		console.log(`[hud-atlas] OK: ${ns} sincronizado (${out.geo.count} frames, ${planes} planos).`);
		process.exit(0);
	}
	fs.mkdirSync(outDir, { recursive: true });
	for (const [name, data] of files) {
		fs.writeFileSync(path.join(outDir, name), data);
	}
	fs.writeFileSync(path.join(outDir, `${ns}.preview.png`), out.preview);
	console.log(
		`[hud-atlas] ${ns}: ${out.geo.count} frames ${frame[0]}x${frame[1]}, ${planes} planos, ` +
			`stride ${out.geo.strideWords} palabras -> ${path.relative(ROOT, outDir)}`,
	);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
	main();
}
