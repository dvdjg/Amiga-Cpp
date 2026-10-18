// Extractor del contenedor de audio digital de las ediciones *Talkie* (`.sof`, FLAC embebido).
// Formato (autoritativo: `engines/scumm/sound.cpp:1180-1200`, que es el que lo reproduce en
// ScummVM — `{ "sof", kFLACMode }` en sound.cpp:1106):
//
//   u32 BE  idxSize            // tamaño del índice en bytes
//   idxSize × [ org u32 BE, off u32 BE, tags u32 BE, size u32 BE ]   // N = idxSize/16 entradas
//   datos:   por entrada: [tags bytes] [size bytes de stream FLAC/MP3/Vorbis]
//            audio absoluto = 4 + idxSize + off + tags   (off es relativo al inicio de datos)
//
// La extracción es **sin pérdidas**: cada blob se escribe tal cual (`.flac` normalmente) y el
// índice se normaliza a JSON (`adv-sof/1`). Nada se recodifica.

import fs from 'node:fs';
import path from 'node:path';

const MAGIC_EXT = [
	[Buffer.from('fLaC'), 'flac'],
	[Buffer.from('OggS'), 'ogg'],
	[Buffer.from('ID3'), 'mp3'],
	[Buffer.from([0xff, 0xfb]), 'mp3'],
];

/** Adivina la extensión del stream por su cabecera (fLaC/OggS/ID3/MP3). */
function sniffExt(buf) {
	for (const [magic, ext] of MAGIC_EXT) {
		if (buf.length >= magic.length && magic.equals(buf.subarray(0, magic.length))) return ext;
	}
	return 'bin';
}

/**
 * Lee el índice del contenedor .sof.
 * \param filePath ruta del .sof.
 * \return { idxSize, entries: [{ i, org, off, tags, size, abs }] } con `abs` = offset absoluto del audio.
 */
export function readSofIndex(filePath) {
	const fd = fs.openSync(filePath, 'r');
	try {
		const head = Buffer.alloc(4);
		fs.readSync(fd, head, 0, 4, 0);
		const idxSize = head.readUInt32BE(0);
		if (idxSize % 16 !== 0) throw new Error(`índice no múltiplo de 16 (${idxSize})`);
		const idx = Buffer.alloc(idxSize);
		fs.readSync(fd, idx, 0, idxSize, 4);
		const entries = [];
		for (let o = 0; o < idxSize; o += 16) {
			const org = idx.readUInt32BE(o);
			const off = idx.readUInt32BE(o + 4);
			const tags = idx.readUInt32BE(o + 8);
			const size = idx.readUInt32BE(o + 12);
			entries.push({ i: entries.length, org, off, tags, size, abs: 4 + idxSize + off + tags });
		}
		return { idxSize, dataStart: 4 + idxSize, entries };
	} finally {
		fs.closeSync(fd);
	}
}

/**
 * Extrae todas las pistas embebidas a `<outDir>/NNNNN.<ext>` y devuelve el manifiesto JSON.
 * \param filePath ruta del .sof.
 * \param outDir directorio de salida (se crea).
 * \return manifiesto `adv-sof/1` (entradas + ficheros escritos).
 */
export function extractSof(filePath, outDir) {
	const { entries } = readSofIndex(filePath);
	fs.mkdirSync(outDir, { recursive: true });
	const manifest = { format: 'adv-sof/1', source: path.basename(filePath), entries: [] };
	const fd = fs.openSync(filePath, 'r');
	try {
		const probe = Buffer.alloc(4);
		for (const e of entries) {
			fs.readSync(fd, probe, 0, Math.min(4, e.size), e.abs);
			const ext = e.size > 0 ? sniffExt(probe) : 'bin';
			const name = `${String(e.i).padStart(5, '0')}.${ext}`;
			const buf = Buffer.alloc(e.size);
			fs.readSync(fd, buf, 0, e.size, e.abs);
			if (e.size > 0) fs.writeFileSync(path.join(outDir, name), buf);
			manifest.entries.push({ i: e.i, org: e.org, off: e.off, tags: e.tags, size: e.size, ext, file: e.size > 0 ? name : null });
		}
	} finally {
		fs.closeSync(fd);
	}
	return manifest;
}
