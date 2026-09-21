// Genera el contenido de un "volumen" (el DH1: que monta el runner) para la demo de
// sistema de archivos: directorios + texto + imagen + sonido + un `.englib` (codigo
// relocatable) para probar la carga dinamica. Uso:
//
//   node tools/fs/make-volume.mjs [--out <dir>]
//
// Defecto: out/run/211_fs_test/A500_debug/dh1 (el dir que el runner monta como DH1:).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

function argValue(name, def) {
	const i = process.argv.indexOf(name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}

const outDir = path.resolve(argValue('--out', path.join(ROOT, 'out/run/211_fs_test/A500_debug/dh1')));

function mkdirp(p) {
	fs.mkdirSync(p, { recursive: true });
}

// FNV-1a 32 (igual que eng::res::DynLoader::hash_name).
function hashName(s) {
	let h = 2166136261 >>> 0;
	for (const ch of s) {
		h = (h ^ (ch.charCodeAt(0) & 0xff)) >>> 0;
		h = Math.imul(h, 16777619) >>> 0;
	}
	return h >>> 0;
}

// .englib: header(24) + code(8) + 1 reloc + 1 export ("answer" -> 0).
// code: 70 6a 4e 75 (moveq #42,d0 ; rts) + 4 bytes de celda relocable.
function buildEngLib() {
	const code = Buffer.alloc(8);
	code[0] = 0x70; code[1] = 0x2a; // moveq #42,%d0
	code[2] = 0x4e; code[3] = 0x75; // rts
	// code[4..7] = celda relocable (0)

	const hdr = Buffer.alloc(24);
	hdr.writeUInt32BE(0x454e474c, 0); // 'ENGL'
	hdr.writeUInt16BE(1, 4);          // version
	hdr.writeUInt16BE(8, 6);          // code_size
	hdr.writeUInt32BE(0, 8);          // data_size
	hdr.writeUInt32BE(0, 12);         // bss_size
	hdr.writeUInt32BE(0, 16);         // entry_offset
	hdr.writeUInt16BE(1, 20);         // reloc_count
	hdr.writeUInt16BE(1, 22);         // export_count

	const relocs = Buffer.alloc(4);
	relocs.writeUInt32BE(4, 0); // reloc en code+4

	const exports = Buffer.alloc(8);
	exports.writeUInt32BE(hashName('answer'), 0);
	exports.writeUInt32BE(0, 4); // offset 0 (la funcion)

	return Buffer.concat([hdr, code, relocs, exports]);
}

function writeVolume() {
	mkdirp(path.join(outDir, 'data/text'));
	mkdirp(path.join(outDir, 'data/images'));
	mkdirp(path.join(outDir, 'data/audio'));
	mkdirp(path.join(outDir, 'data/code'));
	mkdirp(path.join(outDir, 'out'));

	fs.writeFileSync(path.join(outDir, 'data/text/hello.txt'),
		'Hola desde el sistema de archivos del Amiga.\nLinea 2 con tilde: accion.\n', 'utf8');

	// Imagen 16x16 8-bit (gradiente).
	const img = Buffer.alloc(16 * 16);
	for (let y = 0; y < 16; ++y) {
		for (let x = 0; x < 16; ++x) {
			img[y * 16 + x] = ((x * 16 + y) & 0xff);
		}
	}
	fs.writeFileSync(path.join(outDir, 'data/images/logo.raw'), img);

	// Sonido: 256 muestras 8-bit (una sinusoide simple).
	const snd = Buffer.alloc(256);
	for (let i = 0; i < 256; ++i) {
		snd[i] = Math.round(127 * Math.sin((i / 256) * 2 * Math.PI)) & 0xff;
	}
	fs.writeFileSync(path.join(outDir, 'data/audio/beep.raw'), snd);

	// Codigo relocatable.
	fs.writeFileSync(path.join(outDir, 'data/code/answer.englib'), buildEngLib());

	console.log(`volumen generado en ${outDir}`);
	console.log('  data/text/hello.txt, data/images/logo.raw, data/audio/beep.raw, data/code/answer.englib');
}

writeVolume();
