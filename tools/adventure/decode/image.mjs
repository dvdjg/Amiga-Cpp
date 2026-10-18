// Decodificador de **imágenes SCUMM V5** (paylaods de bloque con SMAP) a índices de paleta.
// Porta dos codecs del oráculo `engines/scumm/gfx.cpp`:
//   - **Major/Minor** (`MajMinCodec::decodeLine`, gfx.cpp:5040; usado por `drawStripComplex`):
//     ventana de bits de 16, color inicial, deltas de −4..+3 y repetidos. Códigos
//     `BMCOMP_MAJMIN_Hx` (64..68) = 8 px por fila × alto (`gfx.h:79`).
//   - **Zig-zag básico** (`drawStripBasicH`, gfx.cpp:4122): códigos `ZIGZAG_Hx`/`HTx` (24..28/44..48).
// El strip empieza con `code = *src++` y `shift = code % 10` (bits por píxel, `decompressBitmap`,
// gfx.cpp:2976). Las tiras (8 px de ancho) se direccionan con la tabla SMAP del propio bloque:
// offsets `u32 LE` en `smap + 8 + strip*4` (rama 256 colores de `drawStrip`, gfx.cpp:2460).

import { findBlock } from './decoders.mjs';

/** Códigos de compresión soportados (subconjunto de `gfx.h:38-138` que usan MI1/MI2). */
export const kMajMinH = new Set([64, 65, 66, 67, 68]); // MAJMIN_H4..H8
export const kMajMinHt = new Set([84, 85, 86, 87, 88]); // MAJMIN_HT4..HT8 (con transparencia)
export const kRaw256 = new Set([1]); // RAW256: píxeles directos (drawStripRaw)
export const kZigZagH = new Set([24, 25, 26, 27, 28]); // ZIGZAG_H4..H8
export const kZigZagHt = new Set([44, 45, 46, 47, 48]); // ZIGZAG_HT4..HT8 (con transparencia)
export const kZigZagV = new Set([14, 15, 16, 17, 18]); // ZIGZAG_V4..V8 (objetos: barrido vertical)
export const kZigZagVt = new Set([34, 35, 36, 37, 38]); // ZIGZAG_VT4..VT8 (vertical con transparencia)

/**
 * Busca un sub-bloque `tag` **a cualquier profundidad** (los SMAP van dentro de `IM00`/`IMxx`).
 * \param payload payload de un bloque contenedor.
 * \param tag etiqueta de 4 caracteres.
 * \param depth profundidad máxima de descenso (el propio contenedor cuenta).
 * \return payload del sub-bloque o null.
 */
export function findBlockDeep(payload, tag, depth = 3) {
	if (!payload || depth <= 0) return null;
	let off = 0;
	while (off + 8 <= payload.length) {
		const t = payload.toString('latin1', off, off + 4);
		const size = payload.readUInt32BE(off + 4);
		if (!/^[ -~]{4}$/.test(t) || size < 8 || off + size > payload.length) return null;
		if (t === tag) return payload.subarray(off + 8, off + size);
		const nested = findBlockDeep(payload.subarray(off + 8, off + size), tag, depth - 1);
		if (nested) return nested;
		off += size;
	}
	return null;
}

/** Lector de bits del codec Major/Minor (port de `MajMinCodec`, gfx.cpp:5010); con
 * `transparent` los píxeles del color transparente no se escriben (strips HT, transpCheck). */
function majminDecode(src, shift, width, height, out, outStride, transparent = false, transparentColor = 0) {
	let color = src[0];
	let bits = src[1] | (src[2] << 8);
	let numBits = 16;
	let ptr = 3;
	const read = (n) => {
		if (numBits <= 8) {
			bits = (bits | (((src[ptr++] ?? 0) & 0xff) << numBits)) & 0xffff; // ventana uint16 del original
			numBits += 8;
		}
		const v = bits & ((1 << n) - 1);
		bits = bits >>> n;
		numBits -= n;
		return v;
	};
	let repeatMode = false;
	let repeatCount = 0;
	for (let y = 0; y < height; y++) {
		for (let x = 0; x < width; x++) {
			if (!transparent || color !== transparentColor) out[y * outStride + x] = color;
			if (!repeatMode) {
				if (read(1)) {
					if (read(1)) {
						const diff = read(3) - 4;
						if (diff !== 0) {
							color = (color + diff) & 0xff;
						} else {
							repeatMode = true;
							repeatCount = read(8) - 1;
						}
					} else {
						color = read(shift) & 0xff;
					}
				}
			} else if (--repeatCount === 0) {
				repeatMode = false;
			}
		}
	}
}

/** Codec zig-zag básico horizontal (port de `drawStripBasicH`, gfx.cpp:4122). */
function zigzagHDecode(src, mask, shift, width, height, out, outStride, transparent, transparentColor = 0) {
	let color = src[0];
	let bits = src[1];
	let cl = 8;
	let ptr = 2;
	let bit = 0;
	const readBit = () => {
		cl--;
		bit = bits & 1;
		bits = bits >>> 1;
		return bit;
	};
	const fill = () => {
		if (cl <= 8) {
			bits = (bits | (((src[ptr++] ?? 0) & 0xff) << cl)) >>> 0;
			cl += 8;
		}
	};
	let inc = -1;
	for (let y = 0; y < height; y++) {
		let x = 0;
		do {
			fill();
			if (!transparent || color !== transparentColor) out[y * outStride + x] = color;
			if (!readBit()) {
				// sin cambio
			} else if (!readBit()) {
				fill();
				color = bits & mask;
				bits = bits >>> shift;
				cl -= shift;
				inc = -1;
			} else if (!readBit()) {
				color = (color + inc) & 0xff;
			} else {
				inc = -inc;
				color = (color + inc) & 0xff;
			}
		} while (++x < width);
	}
}

/** Codec zig-zag básico **vertical** (port de `drawStripBasicV`, gfx.cpp:4154): columnas de 8
 * píxeles de ancho recorridas de arriba a abajo dentro de la tira. */
function zigzagVDecode(src, mask, shift, width, height, out, outStride, transparent, transparentColor = 0) {
	let color = src[0];
	let bits = src[1];
	let cl = 8;
	let ptr = 2;
	let bit = 0;
	const readBit = () => {
		cl--;
		bit = bits & 1;
		bits = bits >>> 1;
		return bit;
	};
	const fill = () => {
		if (cl <= 8) {
			bits = (bits | (((src[ptr++] ?? 0) & 0xff) << cl)) >>> 0;
			cl += 8;
		}
	};
	let inc = -1;
	for (let x = 0; x < width; x++) {
		for (let y = 0; y < height; y++) {
			fill();
			if (!transparent || color !== transparentColor) out[y * outStride + x] = color;
			if (!readBit()) {
			} else if (!readBit()) {
				fill();
				color = bits & mask;
				bits = bits >>> shift;
				cl -= shift;
				inc = -1;
			} else if (!readBit()) {
				color = (color + inc) & 0xff;
			} else {
				inc = -inc;
				color = (color + inc) & 0xff;
			}
		}
	}
}

/**
 * Decodifica un bloque de imagen con SMAP (RMIM de sala o IMxx de objeto) a índices de paleta.
 * \param payload payload del bloque de imagen (sin cabecera tag+size).
 * \param width ancho total en píxeles (múltiplo de 8).
 * \param height alto total en píxeles.
 * \param opts.transparentColor índice transparente (0 por defecto; solo afecta a códigos VT/HT).
 * \return { indices, codes, strips, width, height } (lanza si no hay SMAP).
 */
export function decodeSmappedImage(payload, width, height, opts = {}) {
	const smap = findBlockDeep(payload, 'SMAP'); // puede ir anidado (p. ej. dentro de IM00)
	if (!smap) throw new Error('imagen: sin bloque SMAP');
	const strips = width / 8;
	const codes = [];
	const indices = new Uint8Array(width * height);
	// `drawStrip` direcciona sobre el TAG: offsets en tag+8+s*4 y tiras en tag+rel. Con `smap`
	// apuntando al payload (tag+8), los offsets están en smap+s*4 y las tiras en smap+rel-8.
	for (let s = 0; s < strips; s++) {
		const rel = smap.readUInt32LE(s * 4);
		const src = smap.subarray(rel - 8);
		const code = src[0];
		const shift = code % 10;
		const mask = 0xff >>> (8 - shift);
		const body = src.subarray(1);
		// Buffer temporal de la tira y copia al hueco (x = s*8).
		const tile = new Uint8Array(8 * height);
		if (kMajMinH.has(code)) majminDecode(body, shift, 8, height, tile, 8);
		else if (kMajMinHt.has(code)) majminDecode(body, shift, 8, height, tile, 8, true, opts.transparentColor ?? 0);
		else if (kRaw256.has(code)) {
			// drawStripRaw (gfx.cpp:4220): 8 píxeles directos por fila.
			for (let y = 0; y < height; y++) for (let x = 0; x < 8; x++) tile[y * 8 + x] = body[y * 8 + x] ?? 0;
		} else if (kZigZagH.has(code)) zigzagHDecode(body, mask, shift, 8, height, tile, 8, false);
		else if (kZigZagHt.has(code)) zigzagHDecode(body, mask, shift, 8, height, tile, 8, true, opts.transparentColor ?? 0);
		else if (kZigZagV.has(code)) zigzagVDecode(body, mask, shift, 8, height, tile, 8, false);
		else if (kZigZagVt.has(code)) zigzagVDecode(body, mask, shift, 8, height, tile, 8, true, opts.transparentColor ?? 0);
		else throw new Error(`imagen: código de tira ${code} no soportado (sala/tira ${s})`);
		for (let y = 0; y < height; y++) for (let x = 0; x < 8; x++) indices[y * width + s * 8 + x] = tile[y * 8 + x];
		codes.push(code);
	}
	return { indices, codes, strips, width, height };
}
