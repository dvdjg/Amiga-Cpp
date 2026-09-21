// Genera el contenido de un "volumen" para las demos de sistema de archivos:
//   - un arbol de directorios en disco (el DH1: que monta el runner), y
//   - una imagen de disquete ADF (FFS) con el mismo contenido (DF0:).
// Contenido: texto, imagen, sonido y codigo relocatable en DOS formatos (`.englib` propio y
// **HUNK** nativo de AmigaOS) para la carga dinamica.
//
//   node tools/fs/make-volume.mjs [--out <dir>] [--adf <path>]
//
// Defectos: out/run/211_fs_test/A500_debug/dh1  y  out/fs/211_fs_test.adf
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { assemble68k } from './assemble.mjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const PYTHON = process.env.PYTHON || 'python';

// Codigo del stub (answer() -> 42), ENSAMBLADO con vasm (no bytes a mano).
const STUB = assemble68k(fs.readFileSync(path.join(__dirname, 'stub_answer.s'), 'ascii'));

function argValue(name, def) {
	const i = process.argv.indexOf(name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}

const outDir = path.resolve(argValue('--out', path.join(ROOT, 'out/run/211_fs_test/A500_debug/dh1')));
const adfPath = path.resolve(argValue('--adf', path.join(ROOT, 'out/fs/211_fs_test.adf')));
const contentDir = path.join(ROOT, 'out/fs/content');

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

// HUNK: ejecutable nativo de AmigaOS con 1 hunk de codigo (moveq #42,%d0 ; rts) y el
// simbolo "answer". Todo big-endian. Ver engine/include/eng/res/hunk.hpp y dos/doshunks.h.
function buildHunk() {
	const be32 = (v) => {
		const b = Buffer.alloc(4);
		b.writeUInt32BE(v >>> 0, 0);
		return b;
	};
	const parts = [];
	// HUNK_HEADER: magic, resident=0, num=1, first=0, last=0, size[0]=1 long.
	parts.push(be32(0x000003f3), be32(0), be32(1), be32(0), be32(0), be32(1));
	// HUNK_CODE: tag, 1 long, moveq #42,%d0 ; rts (ensamblado con vasm).
	parts.push(be32(1001), be32(1), Buffer.from(STUB));
	// HUNK_SYMBOL: tag, name_len=2 longs, "answer\0\0", value=0, terminador=0.
	const name = Buffer.alloc(8);
	name.write('answer', 0, 'ascii');
	parts.push(be32(1008), be32(2), name, be32(0), be32(0));
	// HUNK_END.
	parts.push(be32(1010));
	return Buffer.concat(parts);
}

// .englib: header(24) + code(8) + 1 reloc + 1 export ("answer" -> 0).
// code: 70 2a 4e 75 (moveq #42,%d0 ; rts) + 4 bytes de celda relocable.
function buildEngLib() {
	const code = Buffer.alloc(8);
	STUB.copy(code, 0); // moveq #42,%d0 ; rts (vasm); code[4..7] = celda relocable (0)

	const hdr = Buffer.alloc(24);
	hdr.writeUInt32BE(0x454e474c, 0); // 'ENGL'
	hdr.writeUInt16BE(1, 4);
	hdr.writeUInt16BE(8, 6);
	hdr.writeUInt32BE(0, 8);
	hdr.writeUInt32BE(0, 12);
	hdr.writeUInt32BE(0, 16);
	hdr.writeUInt16BE(1, 20);
	hdr.writeUInt16BE(1, 22);

	const relocs = Buffer.alloc(4);
	relocs.writeUInt32BE(4, 0);

	const exports = Buffer.alloc(8);
	exports.writeUInt32BE(hashName('answer'), 0);
	exports.writeUInt32BE(0, 4);

	return Buffer.concat([hdr, code, relocs, exports]);
}

function buildContent() {
	const img = Buffer.alloc(16 * 16);
	for (let y = 0; y < 16; ++y) {
		for (let x = 0; x < 16; ++x) {
			img[y * 16 + x] = ((x * 16 + y) & 0xff);
		}
	}
	const snd = Buffer.alloc(256);
	for (let i = 0; i < 256; ++i) {
		snd[i] = Math.round(127 * Math.sin((i / 256) * 2 * Math.PI)) & 0xff;
	}
	return {
		'data/text/hello.txt': Buffer.from('Hola desde el sistema de archivos del Amiga.\nLinea 2 con tilde: accion.\n', 'utf8'),
		'data/images/logo.raw': img,
		'data/audio/beep.raw': snd,
		'data/code/answer.englib': buildEngLib(),
		'data/code/answer.hunk': buildHunk(),
	};
}

const files = buildContent();

// 1) Volumen en disco (DH1:).
for (const [rel, buf] of Object.entries(files)) {
	const dst = path.join(outDir, rel);
	mkdirp(path.dirname(dst));
	fs.writeFileSync(dst, buf);
}
mkdirp(path.join(outDir, 'out'));

// 2) Ficheros fuente para xdftool.
for (const [rel, buf] of Object.entries(files)) {
	const dst = path.join(contentDir, rel);
	mkdirp(path.dirname(dst));
	fs.writeFileSync(dst, buf);
}

// 3) Imagen de disquete ADF (FFS) con el mismo contenido.
function runXdftool(args) {
	const r = spawnSync(PYTHON, ['-m', 'amitools.tools.xdftool', adfPath, ...args], { encoding: 'utf8' });
	if (r.status !== 0) {
		console.error(r.stdout || '');
		console.error(r.stderr || '');
		throw new Error('xdftool fallo: ' + args.join(' '));
	}
}

mkdirp(path.dirname(adfPath));
if (fs.existsSync(adfPath)) {
	fs.unlinkSync(adfPath);
}
runXdftool(['create', '+', 'format', 'AMG211', 'ffs']);
for (const dir of ['data', 'data/text', 'data/images', 'data/audio', 'data/code', 'out']) {
	runXdftool(['makedir', dir]);
}
for (const rel of Object.keys(files)) {
	runXdftool(['write', path.join(contentDir, rel), rel]);
}

// NOTA: el bootblock queda valido (arrancable). Para que la demo se ejecute, el runner
// monta la imagen y el harness sigue arrancando de DH0 (ver run-demo `--disk`).

console.log(`volumen generado en ${outDir}`);
console.log(`imagen de disquete generada en ${adfPath}`);
