#!/usr/bin/env node
// Frontend **SCUMM V3/V4 contenedores `.LFL`** (copias Amiga y FM-Towns): lee el índice
// (`00.LFL`) y las salas (`NNN.LFL`), lista/extrae sus bloques y descompila guiones con
// descumm (-3/-4). Complementa al frontend V5 (`frontends/scumm_v5`) para las copias que no
// usan `.000/.001`.
//
// Formato (oráculo `resource_v4.cpp`/`resource_v3.cpp`):
//  - Índice: bloques `[u32LE tamaño][u16LE tag]` (`tamaño` incluye la cabecera de 6 bytes);
//    tags del índice por par invertido: `0R`→tabla de salas (`R0`), `0S` scripts, `0N`
//    sonidos, `0C` costumes, `0O` objetos (`readIndexFile`, `readResTypeList`: u16LE `num` +
//    entradas `roomno u8`/`offset u32LE`).
//  - Salas: `[u32LE tamaño][tag 2 ASCII]`; la primera es `RO` (todo el fichero) y dentro, en
//    secuencia: `HD` (cabecera de sala), `BX` (walkboxes), `SC`/`LS` (guiones), `OC`
//    (objetos), `EX`/`EN`… Cada sala vive en su propio fichero y la tabla del índice da
//    `offset 0` (FM-Towns) o el offset dentro del fichero colector.
//  - Cifrado ligero (`getEncByte`, resource.cpp: `v4` sala ≠ 0 y < 900 → XOR 0x69`): se
//    detecta probando la cabecera del primer bloque.
//
// Uso:
//   node tools/adventure/frontends/scumm_v4_lfl/extract.mjs index <00.LFL> [--out f.json]
//   node tools/adventure/frontends/scumm_v4_lfl/extract.mjs rooms <00.LFL> [--out dir] [--room N]
//   node tools/adventure/frontends/scumm_v4_lfl/extract.mjs dump-game <00.LFL> --out <dir> [--version 3|4]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { writeDirReadme } from '../../lib/docs.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../../..');
const DESCUMM = process.env.DESCUMM || path.join(REPO, '..', 'SCUMM', 'scummvm-tools-2.9.0', process.platform === 'win32' ? 'descumm.exe' : 'descumm');

function parseArgs(argv) {
	const out = { _: [] };
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		if (a.startsWith('--')) out[a.slice(2)] = argv[++i];
		else out._.push(a);
	}
	return out;
}

// --- lectura de bloques ----------------------------------------------------------------

/** Itera bloques `[u32LE tamaño (incluye 6)][tag u16LE]`; tag en orden de fichero. */
function* blocks16(buf, start = 0, end = buf.length) {
	let off = start;
	while (off + 6 <= end) {
		const size = buf.readUInt32LE(off);
		const tag = String.fromCharCode(buf[off + 4]) + String.fromCharCode(buf[off + 5]);
		if (size < 6 || off + size > end) return;
		yield { tag, off, size, payload: buf.subarray(off + 6, off + size) };
		off += size;
	}
}

/** ¿Son válidas las cabeceras desde \p start? (para detectar cifrado XOR). */
const looksValid = (buf, start) => {
	if (buf.length < start + 6) return false;
	const size = buf.readUInt32LE(start);
	const tag = String.fromCharCode(buf[start + 4]) + String.fromCharCode(buf[start + 5]);
	return size >= 6 && start + size <= buf.length && /^[A-Za-z0-9]{2}$/.test(tag);
};

/** Lee un fichero `.LFL` descifrando con XOR si la cabecera no es válida (getEncByte). */
function readLfl(file, enc = 0) {
	let buf = fs.readFileSync(file);
	if (!looksValid(buf, 0) && enc !== 0) {
		buf = Buffer.from(buf);
		for (let i = 0; i < buf.length; i++) buf[i] ^= enc;
	}
	return buf;
}

/** Tabla del índice (`readResTypeList` v4): u16LE nº + entradas. Paso 5 (`roomno u8` +
 * `offset u32LE`) para salas/guiones/sonidos/costumes; la tabla de objetos (`O0`, solo
 * `offset u32LE`, 4 bytes) se detecta por encaje exacto del tamaño del payload. */
function readTable(payload) {
	const num = payload.readUInt16LE(0);
	const stride = 2 + num * 5 === payload.length ? 5 : 2 + num * 4 === payload.length ? 4 : 0;
	if (!stride) return { num, stride: 0, entries: [] };
	const entries = [];
	for (let i = 0; i < num; i++) {
		const o = 2 + i * stride;
		entries.push(stride === 5
			? { roomno: payload[o], off: payload.readUInt32LE(o + 1) }
			: { off: payload.readUInt32LE(o) });
	}
	return { num, stride, entries };
}

const INDEX_TABLES = { '0R': 'rooms', '0S': 'scripts', '0N': 'sounds', '0C': 'costumes', '0O': 'objects' };

function readIndex(file) {
	const buf = fs.readFileSync(file);
	const info = { format: 'adv-lfl-index/1', file: path.basename(file), enc: 0, counts: {}, tables: {}, names: {} };
	for (const b of blocks16(buf)) {
		if (b.tag === 'NR') {
			// Nombres de sala: byte de sala + 9 bytes XOR 0xFF, hasta sala 0 (resource_v4.cpp:573).
			let o = 0;
			while (o + 10 <= b.payload.length) {
				const room = b.payload[o];
				if (room === 0) break;
				let name = '';
				for (let i = 1; i <= 9; i++) name += String.fromCharCode(b.payload[o + i] ^ 0xff);
				info.names[room] = name.replace(/\0+$/, '');
				o += 10;
			}
			continue;
		}
		const table = INDEX_TABLES[b.tag];
		if (table) {
			const t = readTable(b.payload);
			info.tables[table] = t.entries;
			info.counts[table] = t.num;
			if (t.stride !== 5) info.strides = { ...info.strides, [table]: t.stride };
		}
	}
	return info;
}

// --- salas ------------------------------------------------------------------------------

/** Nombre de fichero de una sala: `%.3d.lfl` (v4) o el patrón presente (v3: `NN.LFL`). */
function roomFile(indexDir, room) {
	const candidates = [`${String(room).padStart(3, '0')}.LFL`, `${String(room).padStart(3, '0')}.lfl`,
		`${String(room).padStart(2, '0')}.LFL`, `${String(room).padStart(2, '0')}.lfl`];
	for (const c of candidates) {
		const p = path.join(indexDir, c);
		if (fs.existsSync(p)) return p;
	}
	return null;
}

/** Bloques de una sala (descifrada): [0] `RO` con la cabecera de sala dentro y, después del
 * `RO`, los bloques globales del fichero (`SC` guiones, `SO` sonidos, `CO` costumes; los
 * offsets del índice `S0`/`N0`/`C0` apuntan ahí). */
function readRoom(file, enc) {
	const buf = readLfl(file, enc);
	const first = blocks16(buf, 0).next();
	if (first.done || first.value.tag !== 'RO') return null;
	const header = [...blocks16(buf, first.value.off + 6, first.value.off + first.value.size)];
	const tail = [...blocks16(buf, first.value.off + first.value.size, buf.length)];
	return { header, tail };
}

// --- comandos ---------------------------------------------------------------------------

const args = parseArgs(process.argv.slice(2));
const cmd = args._[0];
const input = args._[1];
if (!cmd || !input) {
	console.error('Uso: node extract.mjs <index|rooms|dump-game> <00.LFL> [--out ...] [--room N] [--version 3|4]');
	process.exit(2);
}

const SCRIPTS = new Set(['SC']);
const version = args.version ?? '4';
// Prefijos de bytecode del original (v3): `SC` = 1 byte (id/relleno) + código; `LS` (LSCR) =
// id u8 + offset u16LE relativo a la cabecera de sala + código (room.cpp:794). Los scripts
// globales v4 y EX/EN se pasan tal cual a descumm.
const SCRIPT_SKIP = { SC: version === '3' ? 1 : 0, LS: 3, EX: 0, EN: 0 };

if (cmd === 'index') {
	const info = readIndex(input);
	const out = args.out ? String(args.out) : path.join(path.dirname(input), 'index.json');
	fs.writeFileSync(out, JSON.stringify(info, null, 2) + '\n');
	console.log(`[lfl] índice: ${Object.entries(info.counts).map(([k, v]) => `${k}=${v}`).join(' ')} -> ${out}`);
} else if (cmd === 'rooms') {
	const info = readIndex(input);
	const outDir = args.out ? String(args.out) : path.join(path.dirname(input), 'rooms');
	fs.mkdirSync(outDir, { recursive: true });
	const indexDir = path.dirname(input);
	const manifest = { format: 'adv-lfl-rooms/1', rooms: [] };
	for (let room = 1; room < (info.tables.rooms?.length ?? 0); room++) {
		if (args.room && Number(args.room) !== room) continue;
		const file = roomFile(indexDir, room);
		if (!file) continue;
		const r = readRoom(file, 0x69); // v4 sala ≠ 0: getEncByte (resource.cpp); no-op si no está cifrada
		if (!r) continue;
		manifest.rooms.push({
			room,
			name: info.names[room] ?? null,
			file: path.basename(file),
			blocks: r.header.map((b) => ({ tag: b.tag, size: b.size })),
			tail: r.tail.map((b) => ({ tag: b.tag, size: b.size })),
		});
	}
	fs.writeFileSync(path.join(outDir, 'rooms.json'), JSON.stringify(manifest, null, 2) + '\n');
	console.log(`[lfl] ${manifest.rooms.length} salas -> ${outDir}/rooms.json`);
} else if (cmd === 'dump-game') {
	const outDir = args.out ? path.resolve(String(args.out)) : path.resolve(path.join(path.dirname(input), 'lfl-workspace'));
	const indexDir = path.dirname(input);
	const info = readIndex(input);
	const rawDir = path.join(outDir, 'raw');
	fs.mkdirSync(path.join(rawDir, 'index'), { recursive: true });
	fs.writeFileSync(path.join(rawDir, 'index', 'lfl.json'), JSON.stringify(info, null, 2) + '\n');
	let rooms = 0;
	let scripts = 0;
	// id de guion global por (sala, offset) desde la tabla `S0` del índice.
	const scriptIdByPlace = new Map((info.tables.scripts ?? []).map((e, id) => [`${e.roomno}:${e.off}`, id]));
	for (let room = 1; room < (info.tables.rooms?.length ?? 0); room++) {
		const file = roomFile(indexDir, room);
		if (!file) continue;
		const r = readRoom(file, 0x69);
		if (!r) continue;
		rooms++;
		const dir = path.join(rawDir, 'rooms', `room_${room}`);
		fs.mkdirSync(dir, { recursive: true });
		const manifest = [];
		for (const b of [...r.header, ...r.tail]) {
			const base = `${b.tag}_${b.off}`;
			fs.writeFileSync(path.join(dir, `${base}.bin`), b.payload);
			manifest.push({ tag: b.tag, off: b.off, size: b.size, file: `${base}.bin` });
			if (b.tag in SCRIPT_SKIP) {
				const sdir = path.join(rawDir, 'scripts', `room_${room}`);
				fs.mkdirSync(sdir, { recursive: true });
				const id = scriptIdByPlace.get(`${room}:${b.off}`);
				const name = b.tag === 'SC' && id !== undefined ? `SCRP_${id}` : base;
				const tmp = path.join(sdir, '.tmp-script.bin');
				fs.writeFileSync(tmp, b.payload.subarray(SCRIPT_SKIP[b.tag]));
				// descumm sale con código ≠ 0 ante opcodes desconocidos pero imprime el texto
				// parcial: se acepta la salida y solo se marca error si no hay texto.
				const res = spawnSync(DESCUMM, [`-${version}`, '-u', tmp], { encoding: 'utf8' });
				const txt = (res.stdout ?? '').replace(/\r\n/g, '\n');
				if (txt.trim()) {
					fs.writeFileSync(path.join(sdir, `${name}.txt`), txt);
					scripts++;
				} else {
					fs.writeFileSync(path.join(sdir, `${name}.err.txt`), String(res.stdout ?? '') + String(res.stderr ?? ''));
				}
				fs.unlinkSync(tmp);
			}
		}
		fs.writeFileSync(path.join(dir, 'blocks.json'), JSON.stringify({ format: 'adv-lfl-blocks/1', room, file: path.basename(file), blocks: manifest }, null, 2) + '\n');
	}
	writeDirReadme(path.join(rawDir, 'index'), {
		title: 'Índice `.LFL` (V3/V4)',
		what: 'Tablas del índice del juego (`00.LFL`): salas (`0R`), guiones, sonidos, costumes y objetos, con el nombre de cada sala (`NR`).',
		producedBy: '`node tools/adventure/frontends/scumm_v4_lfl/extract.mjs index <00.LFL>`.',
		consumedBy: 'el pase `dump-game` y la comparación con las copias PC V5.',
		items: [{ name: 'lfl.json', desc: 'tablas del índice (`adv-lfl-index/1`).' }],
		related: [{ name: '`../rooms/`', desc: 'bloques por sala extraídos del mismo pase.' }],
		notes: ['Los offsets de `R0` son 0 en las copias FM-Towns (una sala por fichero `NNN.LFL`).'],
	});
	writeDirReadme(path.join(rawDir, 'rooms'), {
		title: 'Bloques de sala `.LFL` (V3/V4)',
		what: 'Bloques por sala tal cual están en el `.LFL` (`adv-lfl-blocks/1`): cabecera (`HD`), walkboxes (`BX`), paleta (`PA`), bitmap (`BM`), objetos (`OI`/`OC`), guiones de entrada/salida (`EX`/`EN`), lista local (`LC`/`LS`) y, tras el bloque `RO`, los globales del fichero (`SC` guiones, `SO` sonidos, `CO` costumes).',
		producedBy: '`node tools/adventure/frontends/scumm_v4_lfl/extract.mjs dump-game <00.LFL>`.',
		consumedBy: 'decodificadores V3/V4 (walkboxes/bitmaps/costumes) y comparación con las copias PC.',
		items: [{ name: 'room_<N>/', desc: 'bloques `<TAG>_<offset>.bin` + `blocks.json` de esa sala.' }],
		related: [{ name: '`../scripts/`', desc: 'guiones descompilados de esta misma pasada.' }],
	});
	writeDirReadme(path.join(rawDir, 'scripts'), {
		title: 'Guiones `.LFL` descompilados (V3/V4)',
		what: 'Guiones v3/v4 descompilados con `descumm` (-3/-4): globales `SCRP_<id>` (id desde la tabla `S0`), locales `LS_<offset>` y guiones de entrada/salida `EX`/`EN`.',
		producedBy: '`node tools/adventure/frontends/scumm_v4_lfl/extract.mjs dump-game <00.LFL>`.',
		consumedBy: 'análisis, traducción (I1) y comparación con las copias Amiga.',
		items: [{ name: 'room_<N>/<guion>.txt', desc: 'texto de descumm (LF).' }],
		notes: ['Prefijos de bytecode: `SC` v3 = 1 byte; `LS` = id u8 + offset u16LE (room.cpp:794); `EX`/`EN` tal cual.'],
	});
	console.log(`[lfl] dump: ${rooms} salas, ${scripts} guiones -> ${outDir}`);
} else {
	console.error(`comando no soportado: ${cmd}`);
	process.exit(2);
}
