// Genera el contenido de un "volumen" para las demos de sistema de archivos:
//   - un arbol de directorios en disco (el DH1: que monta el runner), y
//   - una imagen de disquete ADF (FFS) con el mismo contenido (DF0:).
// Contenido: texto, imagen, sonido y codigo relocatable en DOS formatos (`.englib` propio y
// **HUNK** nativo de AmigaOS) para la carga dinamica.
//
//   node tools/fs/make-volume.mjs [--out <dir>] [--adf <path>] [--add <f>[:<rel>]]... [--tar <t.tar>]
//
// Defectos: out/run/211_fs_test/A500_debug/dh1  y  out/fs/211_fs_test.adf
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { assemble68k } from './assemble.mjs';
import { extractTar } from './tar-extract.mjs';

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

// CRC-32 (IEEE, polinomio reflejado 0xEDB88320) — igual que `eng::crc32`.
const CRC_TABLE = (() => {
	const t = new Uint32Array(256);
	for (let n = 0; n < 256; ++n) {
		let c = n;
		for (let k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : c >>> 1;
		t[n] = c >>> 0;
	}
	return t;
})();
function crc32(buf) {
	let c = 0xffffffff >>> 0;
	for (let i = 0; i < buf.length; ++i) c = (CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8)) >>> 0;
	return (~c) >>> 0;
}

// `.engz`: contenedor (cabecera LE de 20 B + payload + CRC-32), codec 0 = Raw.
// Ver engine/include/eng/res/engz.hpp. El payload podría ir comprimido (ZX0); aquí Raw.
function buildEngz(payload, codec = 0) {
	const hdr = Buffer.alloc(20);
	hdr.writeUInt32LE(0x454e475a, 0); // 'ENGZ' (convención kEngzMagic)
	hdr.writeUInt16LE(1, 4); // version
	hdr.writeUInt8(codec, 6);
	hdr.writeUInt8(1, 7); // align_log2 (2 B)
	hdr.writeUInt32LE(payload.length, 8);
	hdr.writeUInt32LE(payload.length, 12); // uncompressed_size (Raw → igual)
	hdr.writeUInt32LE(crc32(payload), 16);
	return Buffer.concat([hdr, payload]);
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
	// SFX de ~1.5 s para demostrar el pin CPU de una voz y un bloque para presionar la caché.
	const leaseSfx = Buffer.alloc(16 * 1024);
	for (let i = 0; i < leaseSfx.length; ++i) {
		leaseSfx[i] = Math.round(24 * Math.sin((i / 11025) * 2 * Math.PI * 440)) & 0xff;
	}
	const leasePressure = Buffer.alloc(20 * 1024, 0x5a);
	// Archivo GRANDE para probar el streaming desde disquete: PCM 8-bit mono sin signo,
	// 512 KB (>= 500 kB), ~64 s de un tono de 440 Hz a 8 kHz. Se lee por rebanadas
	// (`file_read_async` + `ChunkStream`), no de una vez.
	const big = Buffer.alloc(512 * 1024);
	{
		const step = (2 * Math.PI * 440) / 8000;
		for (let i = 0; i < big.length; ++i) {
			big[i] = (Math.round(127 * Math.sin(i * step)) + 128) & 0xff;
		}
	}
	const files = {
		'data/text/hello.txt': Buffer.from('Hola desde el sistema de archivos del Amiga.\nLinea 2 con tilde: accion.\n', 'utf8'),
		'data/images/logo.raw': img,
		'data/audio/beep.raw': snd,
		'data/audio/lease_sfx.raw': leaseSfx,
		'data/audio/lease_pressure.raw': leasePressure,
		'data/audio/tone_8k_512k.raw': big,
		'data/code/answer.englib': buildEngLib(),
		'data/code/answer.hunk': buildHunk(),
		'data/code/answer.engz': buildEngz(buildHunk()), // HUNK envuelto en `.engz` (R6.4/R6.7)
	};
	// Modulos de musica reales (de assets/) para la demo 276: se cargan desde disco con
	// `file_open`/`file_read_sync` en vez de incrustarlos. Si no existen, se omiten.
	for (const [rel, src] of [
		['data/audio/jazzcat-boogie_town.mod', 'assets/amiga/audio/jazzcat-boogie_town.mod'],
		['data/audio/SneakyChick.mod', 'assets/amiga/audio/SneakyChick.mod'],
		['data/audio/testmod.p61', 'assets/amiga/audio/testmod.p61'],
	]) {
		const abs = path.join(ROOT, src);
		if (fs.existsSync(abs)) {
			files[rel] = fs.readFileSync(abs);
		}
	}
	// Melodia de dominio publico para la demo 278 (streaming AUZX): se genera con el pipeline de
	// audio (gen-melody + pack-auzx, port Node del packer de `host-tools/pack-pcm`). Si falla, se
	// omite sin romper el volumen.
	{
		const melodyRaw = path.join(ROOT, 'out/tmp/melody.raw');
		const melodyAuzx = path.join(ROOT, 'out/tmp/melody.auzx');
		mkdirp(path.dirname(melodyRaw));
		const gen = spawnSync('node', [path.join(ROOT, 'tools/audio/gen-melody.mjs'), melodyRaw], {
			stdio: 'ignore',
		});
		const pack =
			gen.status === 0
				? spawnSync(
						'node',
						[
							path.join(ROOT, 'tools/audio/pack-auzx.mjs'),
							melodyRaw,
							melodyAuzx,
							'1024',
							'8000',
						],
						{ stdio: 'ignore' },
					)
				: { status: 1 };
		if (pack.status === 0 && fs.existsSync(melodyAuzx)) {
			files['data/audio/melody.auzx'] = fs.readFileSync(melodyAuzx);
		}
	}
	return files;
}

const files = buildContent();

// Contenido extra: `--add <fichero>[:<ruta-en-el-volumen>]` (repetible) y `--tar <archivo.tar>`
// (se extrae y se vuelca al volumen; el "archivo original" puede contener un sistema de archivos
// completo, preparado con la orden `tar` estandar).
const extras = {};
for (let i = 0; i < process.argv.length; ++i) {
	if (process.argv[i] === '--add' && i + 1 < process.argv.length) {
		const spec = process.argv[i + 1];
		const sep = spec.indexOf(':');
		const src = sep >= 0 ? spec.slice(0, sep) : spec;
		const rel = sep >= 0 ? spec.slice(sep + 1) : path.basename(spec);
		extras[rel] = fs.readFileSync(path.resolve(src));
	}
}
const tarArg = argValue('--tar', '');
if (tarArg !== '') {
	const tmp = path.join(ROOT, 'out/tmp/make-volume-tar');
	fs.rmSync(tmp, { recursive: true, force: true });
	extractTar(path.resolve(tarArg), tmp);
	const walk = (dir) => {
		for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
			const abs = path.join(dir, e.name);
			if (e.isDirectory()) {
				walk(abs);
			} else {
				extras[path.relative(tmp, abs).split(path.sep).join('/')] = fs.readFileSync(abs);
			}
		}
	};
	walk(tmp);
}
Object.assign(files, extras);

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

// 3) Imagen de disquete ADF (FFS) con el mismo contenido. Se omite con `--no-adf` (no necesita
//    Python/amitools; el runner de demos monta DH1: el arbol de disco).
const noAdf = process.argv.includes('--no-adf');
function runXdftool(args) {
	const r = spawnSync(PYTHON, ['-m', 'amitools.tools.xdftool', adfPath, ...args], { encoding: 'utf8' });
	if (r.status !== 0) {
		console.error(r.stdout || '');
		console.error(r.stderr || '');
		throw new Error('xdftool fallo: ' + args.join(' '));
	}
}

mkdirp(path.dirname(adfPath));
if (noAdf) {
	console.log(`volumen generado en ${outDir} (sin ADF)`);
	process.exit(0);
}
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
