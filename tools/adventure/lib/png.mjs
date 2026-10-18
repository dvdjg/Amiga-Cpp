// Codificador de **PNG indexado** (color type 3, 8 bits) sin dependencias, para normalizar la
// salida del pipeline a un formato estándar **sin pérdidas**: los índices son exactamente los
// del juego original y la paleta va en PLTE (transparencia opcional por índice con tRNS).
// Determinista: mismo contenido → mismos bytes (deflate de node:zlib con nivel fijo).

import zlib from 'node:zlib';

function crc32(buf) {
	// zlib.crc32 existe en Node >= 20; fallback manual por si acaso.
	if (typeof zlib.crc32 === 'function') return zlib.crc32(buf) >>> 0;
	let crc = 0xffffffff;
	const table = [];
	for (let n = 0; n < 256; n++) {
		let c = n;
		for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
		table[n] = c >>> 0;
	}
	for (const b of buf) crc = table[(crc ^ b) & 0xff] ^ (crc >>> 8);
	return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
	const out = Buffer.alloc(12 + data.length);
	out.writeUInt32BE(data.length, 0);
	out.write(type, 4, 4, 'latin1');
	data.copy(out, 8);
	out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
	return out;
}

/**
 * Codifica una imagen indexada a PNG (color type 3, 8 bpp, sin filtros).
 * \param width ancho en píxeles.
 * \param height alto en píxeles.
 * \param indices Uint8Array/Buffer de width*height con índices de paleta (0..255).
 * \param palette array de [r,g,b] (0..255) hasta 256 entradas.
 * \param opts.transparentIndex índice marcado transparente en tRNS (opcional).
 * \return Buffer con el PNG completo.
 */
export function encodeIndexedPng(width, height, indices, palette, opts = {}) {
	if (indices.length !== width * height) throw new Error(`png: ${indices.length} != ${width}x${height}`);
	const ihdr = Buffer.alloc(13);
	ihdr.writeUInt32BE(width, 0);
	ihdr.writeUInt32BE(height, 4);
	ihdr[8] = 8; // bit depth
	ihdr[9] = 3; // color type: indexed
	ihdr[10] = 0; // deflate
	ihdr[11] = 0; // adaptive filtering
	ihdr[12] = 0; // no interlace

	const plte = Buffer.alloc(palette.length * 3);
	palette.forEach((c, i) => {
		plte[i * 3] = c[0] & 0xff;
		plte[i * 3 + 1] = c[1] & 0xff;
		plte[i * 3 + 2] = c[2] & 0xff;
	});

	// Scanlines con filtro 0 (None): [0x00][fila…]
	const raw = Buffer.alloc((1 + width) * height);
	for (let y = 0; y < height; y++) {
		raw[y * (1 + width)] = 0;
		Buffer.from(indices.buffer ?? indices, indices.byteOffset ?? 0, indices.length).copy(raw, y * (1 + width) + 1, y * width, (y + 1) * width);
	}
	const idat = zlib.deflateSync(raw, { level: 9 });

	const parts = [Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr), chunk('PLTE', plte)];
	if (opts.transparentIndex !== undefined) {
		const t = Buffer.alloc(opts.transparentIndex + 1, 255);
		t[opts.transparentIndex] = 0;
		parts.push(chunk('tRNS', t));
	}
	parts.push(chunk('IDAT', idat), chunk('IEND', Buffer.alloc(0)));
	return Buffer.concat(parts);
}

/**
 * Convierte la paleta de un juego SCUMM (CLUT de 6 bits por canal) a 8 bits/ canal para PNG.
 * \param colors array de [r,g,b] con los valores crudos del juego.
 * \return nueva paleta escalada (×4 si todos los canales son ≤ 63, como en VGA).
 */
export function paletteTo8Bit(colors) {
	const sixBit = colors.every((c) => c[0] <= 63 && c[1] <= 63 && c[2] <= 63);
	return colors.map((c) => (sixBit ? [c[0] << 2, c[1] << 2, c[2] << 2] : [c[0], c[1], c[2]]));
}
