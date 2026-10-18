#!/usr/bin/env node
// Extractor de contenedores SCUMM V5 (formato de los juegos PC floppy/CD con GF_USE_KEY).
//
// Formato (contrastado en `engines/scumm/resource.cpp` del oráculo `../SCUMM/scummvm-2026.4.0`
// y contra datos reales de MI2 Ultimate Talkie Edition):
//  - `.000`: índice con bloques `TAG(u32 ASCII)` + `size(u32 BE)` (`readIndexFile`,
//    resource.cpp:253). Los juegos con GF_USE_KEY van cifrados con XOR 0x69 byte a byte
//    (`getEncByte`, resource.cpp:200: v<=3->0xFF; v4: solo salas 1..899; v5: 0x69).
//    Bloques: RNAM (nombres de sala, 9 bytes XOR 0xFF), MAXS, DROO/DSCR/DCOS/DSOU/DCHR
//    (tabla: `num(u16 LE)` + `num` bytes de sala/disco + `num` offsets `u32 LE`;
//    `readResTypeList`, resource.cpp:480) y DOBJ.
//  - `.00N` (N>=1): datos, también XOR 0x69. Contenedor `LECF` + `LOFF` (tabla
//    `num(u8)` + `num` × `sala(u8)+offset(u32 LE)` — lo que lee `readRoomsOffsets`,
//    resource.cpp:156) + bloques `LFLF` contiguos; cada LFLF contiene un `ROOM` con
//    sub-bloques (RMHD, CYCL, TRNS, EPAL, BOXD, BOXM, OBIM, OBCD, SCRP, EXCD, ENCD...).
//    El offset de LOFF apunta al `ROOM` (payload del LFLF).
//
// Uso:
//   node tools/adventure/frontends/scumm_v5/extract.mjs index <juego.000>
//   node tools/adventure/frontends/scumm_v5/extract.mjs rooms <juego.000> [--room N]
//   node tools/adventure/frontends/scumm_v5/extract.mjs dump-room <juego.000> --room N [--out <dir>]
//   node tools/adventure/frontends/scumm_v5/extract.mjs dump-game <juego.000> [--out <dir>] [--descumm <exe>] [--no-descumm]
//   node tools/adventure/frontends/scumm_v5/extract.mjs scripts <juego.000> [--room N] [--out <dir>] [--descumm <exe>] [--no-descumm]
//   node tools/adventure/frontends/scumm_v5/extract.mjs decode-boxes <juego.000> [--room N] [--out <dir>]
//   node tools/adventure/frontends/scumm_v5/extract.mjs rooms-json <juego.000> [--out <file.json>]
//
// Salida por defecto: out/adventure/<base>/ con la estructura canónica del workspace (ver
// tools/adventure/WORKSPACE.md). Nada se escribe en git. Herramientas del pipeline:
// tools/adventure/README.md.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { decodeWalkboxes } from '../../decode/decoders.mjs';
import { writeDirReadme } from '../../lib/docs.mjs';

const ENC_KEYS = [0x69, 0x00]; // clave de cifrado probable primero (V5 con GF_USE_KEY = 0x69)
const INDEX_TAGS = new Set(['RNAM', 'MAXS', 'DROO', 'DSCR', 'DCOS', 'DSOU', 'DCHR', 'DOBJ', 'DIRF', 'DIRR', 'DIRS', 'DIRC', 'DIRN']);
const SCRIPT_TAGS = new Set(['SCRP', 'EXCD', 'ENCD', 'LSCR']); // `script_v5.cpp` loader: SCRP/EXCD/ENCD/LSCR
const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');

function die(msg) {
	console.error(`[extract] ${msg}`);
	process.exit(1);
}

function parseArgs(argv) {
	const out = { _: [] };
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		if (a === '--room' || a === '--out' || a === '--descumm') out[a.slice(2)] = argv[++i];
		else if (a.startsWith('--')) out[a.slice(2)] = true;
		else out._.push(a);
	}
	return out;
}

// --- lectura y descifrado -------------------------------------------------------------

function readEncrypted(filePath, key) {
	const buf = fs.readFileSync(filePath);
	if (key !== 0) for (let i = 0; i < buf.length; i++) buf[i] ^= key;
	return buf;
}

function tagAscii(buf, off) {
	return buf.toString('latin1', off, off + 4);
}

function isPrintableTag(buf, off) {
	for (let i = 0; i < 4; i++) {
		const c = buf[off + i];
		if (c < 0x20 || c > 0x7e) return false;
	}
	return true;
}

// Detecta la clave de cifrado probando a leer el primer bloque como tag+sizes plausibles.
function detectKey(filePath) {
	const raw = fs.readFileSync(filePath);
	for (const key of ENC_KEYS) {
		if (key !== 0) {
			const head = Buffer.from(raw.subarray(0, 8));
			for (let i = 0; i < head.length; i++) head[i] ^= key;
			const t = head.toString('latin1', 0, 4);
			if ((INDEX_TAGS.has(t) || t === 'LECF') && head.readUInt32BE(4) <= raw.length) return key;
		} else if (isPrintableTag(raw, 0) && raw.readUInt32BE(4) <= raw.length) {
			return 0;
		}
	}
	die(`no se reconoce el contenedor de ${filePath} (¿SCUMM V5? ¿clave distinta de 0x69?)`);
}

// Itera bloques TAG(asii)+size(BE) en [start,end). size incluye la cabecera de 8 bytes.
function* blocks(buf, start = 0, end = buf.length) {
	let off = start;
	while (off + 8 <= end) {
		const tag = tagAscii(buf, off);
		const size = buf.readUInt32BE(off + 4);
		if (!/^[A-Z0-9!@#$%^&*()_+]{4}$/.test(tag) || size < 8 || off + size > end) {
			yield { tag, off, size, payload: buf.subarray(off + 8, Math.min(off + size, end)), invalid: true };
			return;
		}
		yield { tag, off, size, payload: buf.subarray(off + 8, off + size) };
		off += size;
	}
}

// --- índice (.000) ---------------------------------------------------------------------

function readIndex(filePath) {
	const key = detectKey(filePath);
	const buf = readEncrypted(filePath, key);
	const info = { path: filePath, enc: key, blocks: [], counts: {}, roomNames: new Map(), roomDisks: [], scripts: [], costumes: [], sounds: [], charsets: [] };
	for (const b of blocks(buf, 0, buf.length)) {
		if (b.invalid) break;
		info.blocks.push({ tag: b.tag, size: b.size });
		const p = b.payload;
		switch (b.tag) {
		case 'RNAM': { // resource.cpp:436: entries hasta sala 0; nombre 9 bytes XOR 0xFF
			let o = 0;
			while (o < p.length) {
				const room = p[o];
				if (room === 0 || o + 10 > p.length) break;
				let name = '';
				for (let i = 1; i <= 9; i++) name += String.fromCharCode(p[o + i] ^ 0xff);
				info.roomNames.set(room, name.replace(/\0+$/, ''));
				o += 10;
			}
			break;
		}
		case 'DROO': case 'DSCR': case 'DCOS': case 'DSOU': case 'DCHR': {
			const num = p.readUInt16LE(0);
			info.counts[b.tag] = num;
			const entries = [];
			let o = 2 + num;
			for (let i = 0; i < num && o + 4 <= p.length; i++, o += 4) {
				entries.push({ n: p[2 + i], off: p.readUInt32LE(o) });
			}
			if (b.tag === 'DROO') info.roomDisks = entries;
			if (b.tag === 'DSCR') info.scripts = entries;
			if (b.tag === 'DCOS') info.costumes = entries;
			if (b.tag === 'DSOU') info.sounds = entries;
			if (b.tag === 'DCHR') info.charsets = entries;
			break;
		}
		case 'DOBJ': {
			info.counts.DOBJ = p.readUInt16LE(0); // readGlobalObjects: nº de objetos globales
			break;
		}
		case 'MAXS': {
			info.maxs = p.toString('hex');
			break;
		}
		default:
			break;
		}
	}
	return info;
}

// --- datos (.00N) ----------------------------------------------------------------------

function dataFiles(basePath) {
	const dir = path.dirname(basePath);
	const base = path.basename(basePath).replace(/\.[^.]*$/, '');
	const out = [];
	for (let n = 0; n < 10; n++) {
		const f = path.join(dir, `${base}.${String(n).padStart(3, '0')}`);
		if (n >= 1 && fs.existsSync(f)) out.push(f);
	}
	return out;
}

// Devuelve [{ file, key, rooms: [{room, roomOff, lflfOff, blocks, lflf_blocks}] }]
// `blocks` = hijos del ROOM (RMHD/CYCL/TRNS/EPAL/BOXD/BOXM/CLUT/SCAL/RMIM/...);
// `lflf_blocks` = hijos del LFLF fuera del ROOM (aquí viven los SCRP/EXCD/ENCD de la sala).
function readData(files) {
	const out = [];
	for (const file of files) {
		const key = detectKey(file);
		const buf = readEncrypted(file, key);
		if (tagAscii(buf, 0) !== 'LECF') continue; // contenedor esperado en V5
		let loff = null;
		const lflfs = [];
		for (const b of blocks(buf, 8, buf.length)) {
			if (b.invalid) break;
			if (b.tag === 'LOFF') {
				loff = [];
				let o = 0;
				const num = b.payload[o++];
				for (let i = 0; i < num; i++) {
					const room = b.payload[o++];
					const off = b.payload.readUInt32LE(o); o += 4;
					loff.push({ room, off });
				}
			} else if (b.tag === 'LFLF') {
				lflfs.push({ off: b.off, size: b.size });
			}
		}
		const rooms = [];
		if (loff) {
			for (const { room, off } of loff) {
				// El offset de LOFF apunta al bloque ROOM (payload del LFLF que lo contiene).
				const roomBlock = blocks(buf, off, off + 8 + buf.readUInt32BE(off + 4)).next().value;
				const sub = [];
				for (const c of blocks(buf, roomBlock.off + 8, roomBlock.off + roomBlock.size)) {
					if (c.invalid) break;
					sub.push({ tag: c.tag, off: c.off, size: c.size });
				}
				// LFLF contenedor que posee este ROOM; sus hijos fuera del ROOM son la
				// sección de scripts de la sala (SCRP/EXCD/ENCD/LSCR).
				const host = lflfs.find((l) => off >= l.off && off < l.off + l.size);
				const extra = [];
				if (host) {
					for (const c of blocks(buf, host.off + 8, host.off + host.size)) {
						if (c.invalid) break;
						if (c.tag === 'ROOM') continue;
						extra.push({ tag: c.tag, off: c.off, size: c.size });
					}
				}
				rooms.push({ room, roomOff: off, lflfOff: host ? host.off : -1, blocks: sub, lflf_blocks: extra });
			}
		}
		out.push({ file, key, rooms, buf });
	}
	return out;
}

function loadGame(indexPath) {
	if (!fs.existsSync(indexPath)) die(`no existe ${indexPath}`);
	const index = readIndex(indexPath);
	const files = dataFiles(indexPath);
	if (files.length === 0) die(`no hay ficheros de datos .00N junto a ${indexPath}`);
	const data = readData(files);
	return { indexPath, index, data };
}

function findRoom(game, roomNo) {
	for (const d of game.data) {
		const r = d.rooms.find((x) => x.room === roomNo);
		if (r) return { data: d, room: r };
	}
	return null;
}

// --- sub-comandos -----------------------------------------------------------------------

function cmdIndex(args) {
	const g = loadGame(args._[1]);
	console.log(`índice: ${g.indexPath} (xor 0x${g.index.enc.toString(16)})`);
	for (const b of g.index.blocks) console.log(`  ${b.tag} ${b.size}`);
	console.log(`salas: ${g.index.roomDisks.length}  scripts: ${g.index.scripts.length}  costumes: ${g.index.costumes.length}  sonidos: ${g.index.sounds.length}  charsets: ${g.index.charsets.length}  objetos globales: ${g.index.counts.DOBJ ?? '?'}`);
	const names = [...g.index.roomNames.entries()].slice(0, 12).map(([r, n]) => `${r}:${n}`);
	console.log(`nombres de sala (primeros): ${names.join('  ')}`);
	for (const d of g.data) {
		console.log(`datos: ${path.basename(d.file)} (xor 0x${d.key.toString(16)})  salas en LOFF: ${d.rooms.length}`);
	}
}

function cmdRooms(args) {
	const g = loadGame(args._[1]);
	for (const d of g.data) {
		const list = args.room ? d.rooms.filter((r) => r.room === Number(args.room)) : d.rooms;
		for (const r of list) {
			const tags = [...r.blocks, ...r.lflf_blocks].map((b) => `${b.tag}(${b.size})`).join(' ');
			console.log(`sala ${r.room} @${r.roomOff} en ${path.basename(d.file)}: ${tags}`);
		}
	}
}

function outDir(args, base) {
	// Raíz del workspace del juego (ver tools/adventure/WORKSPACE.md): out/adventure/<juego>/.
	const dir = args.out ? String(args.out) : path.join(REPO_ROOT, 'out', 'adventure', base);
	fs.mkdirSync(dir, { recursive: true });
	return dir;
}

// Vuelca los bloques de una sala (ROOM + sección de scripts del LFLF) a raw/rooms/room_N/.
// Los bloques se guardan como payload (sin cabecera tag+size); la cabecera queda en blocks.json.
function dumpRoomBlocks(work, data, room) {
	const dir = path.join(work, 'raw', 'rooms', `room_${room.room}`);
	fs.mkdirSync(dir, { recursive: true });
	const manifest = { game: path.basename(data.file), room: room.room, file: path.basename(data.file), blocks: [] };
	for (const b of [...room.blocks, ...room.lflf_blocks]) {
		const payload = data.buf.subarray(b.off + 8, b.off + b.size);
		const name = `${b.tag}_${b.off}.bin`;
		fs.writeFileSync(path.join(dir, name), payload);
		manifest.blocks.push({ tag: b.tag, size: b.size, off: b.off, file: name });
	}
	fs.writeFileSync(path.join(dir, 'blocks.json'), JSON.stringify(manifest, null, 2) + '\n');
	return manifest.blocks.length;
}

// Vuelca los bloques de guion de una sala (con cabecera, para descumm) a raw/scripts/room_N/
// y los descompila si `descumm` está disponible.
function dumpRoomScripts(work, data, room, descumm) {
	const dir = path.join(work, 'raw', 'scripts', `room_${room.room}`);
	fs.mkdirSync(dir, { recursive: true });
	let total = 0, ok = 0;
	for (const b of [...room.blocks, ...room.lflf_blocks]) {
		if (!SCRIPT_TAGS.has(b.tag)) continue;
		total++;
		const bin = path.join(dir, `${b.tag}_${b.off}.bin`);
		fs.writeFileSync(bin, data.buf.subarray(b.off, b.off + b.size));
		if (!descumm) continue;
		const out = spawnSync(descumm, ['-5', bin], { encoding: 'latin1', maxBuffer: 64 * 1024 * 1024 });
		if (out.status === 0 && out.stdout && !/Unknown|ERROR/i.test(out.stdout.slice(0, 120))) {
			fs.writeFileSync(bin.replace(/\.bin$/, '.txt'), out.stdout.replace(/\r\n/g, '\n')); // LF en artefactos
			ok++;
		} else {
			fs.writeFileSync(bin.replace(/\.bin$/, '.err.txt'), (out.stdout || '') + (out.stderr || ''));
		}
	}
	return { total, ok };
}

function cmdDumpRoom(args) {
	const g = loadGame(args._[1]);
	const roomNo = Number(args.room);
	if (!Number.isInteger(roomNo)) die('--room N requerido');
	const found = findRoom(g, roomNo);
	if (!found) die(`sala ${roomNo} no encontrada`);
	const base = path.basename(args._[1]).replace(/\.[^.]*$/, '');
	const work = outDir(args, base);
	const n = dumpRoomBlocks(work, found.data, found.room);
	console.log(`[extract] sala ${roomNo}: ${n} bloques -> ${path.join(work, 'raw', 'rooms', `room_${roomNo}`)}`);
}

// Vuelca el juego entero: bloques por sala + guiones descompilados + SIM (raw/index/rooms.json).
function cmdDumpGame(args) {
	const g = loadGame(args._[1]);
	const base = path.basename(args._[1]).replace(/\.[^.]*$/, '');
	const work = outDir(args, base);
	const descumm = args['no-descumm'] ? null : descummPath(args);
	let rooms = 0, blockCount = 0, total = 0, ok = 0;
	for (const d of g.data) {
		for (const r of d.rooms) {
			rooms++;
			blockCount += dumpRoomBlocks(work, d, r);
			const s = dumpRoomScripts(work, d, r, descumm);
			total += s.total;
			ok += s.ok;
		}
	}
	cmdRoomsJson({ ...args, out: path.join(work, 'raw', 'index', 'rooms.json') });
	writeRawDocs(work, base);
	console.log(`[extract] dump-game: ${rooms} salas, ${blockCount} bloques, ${total} guiones (descumm OK: ${ok}) -> ${work}`);
}

// Fichas generadas de `raw/` (qué contiene cada subcarpeta y cómo se relaciona con decoded/).
function writeRawDocs(work, base) {
	const raw = path.join(work, 'raw');
	writeDirReadme(raw, {
		title: `Extracción literal (${base})`,
		what: 'Volcado **literal** del contenedor SCUMM V5: índice, bloques por sala (payload sin cabecera) y guiones descompilados. Es la fuente de la que se deriva todo lo demás; no se edita a mano.',
		producedBy: '`node tools/adventure/frontends/scumm_v5/extract.mjs dump-game <juego.000> --out <workspace>`.',
		consumedBy: '`decode/` (SIM v2, PNG, audio), `translate/` (AST) y las herramientas de análisis.',
		items: [
			{ name: 'index/rooms.json', desc: 'SIM v1: inventario de salas y bloques (con tamaños) del índice del juego.' },
			{ name: 'rooms/room_<N>/', desc: 'payload de cada bloque de la sala (`<TAG>_<offset>.bin`) + `blocks.json` con offset/tamaño.' },
			{ name: 'scripts/room_<N>/', desc: 'guiones (`SCRP/EXCD/ENCD/LSCR`) con cabecera (.bin) y su descompilado descumm (.txt).' },
		],
	});
	writeDirReadme(path.join(raw, 'index'), {
		title: 'Índice del juego (SIM v1)',
		what: 'Manifiesto de extracción: salas con sus nombres y la lista de bloques de cada una, más contadores globales del contenedor.',
		producedBy: '`extract.mjs rooms-json` (invocado por `dump-game`).',
		consumedBy: '`decode/scumm_v5-decode.mjs` (para saber qué salas existen y qué bloques leer).',
		items: [{ name: 'rooms.json', desc: 'formato `sim/1`: `rooms[]` con `blocks` (tag/size), `lflfBlocks` y `scripts`.' }],
		related: [{ name: '`../rooms/room_<N>/blocks.json`', desc: 'detalle por sala con offsets y ficheros de bloque.' }],
	});
	writeDirReadme(path.join(raw, 'rooms'), {
		title: 'Bloques por sala (payload literal)',
		what: 'Un directorio por sala con el payload de cada bloque del `ROOM`/`LFLF` (RMHD, paletas, BOXD/BOXM, RMIM, OBIM/OBCD, SCRP, COST…). Aquí NO se interpreta nada: los datos están tal cual.',
		producedBy: '`extract.mjs dump-game` (o `dump-room` para una sola sala).',
		consumedBy: '`decode/scumm_v5-decode.mjs` (walkboxes/paleta/objetos, imágenes y SIM v2).',
		items: [{ name: 'room_<N>/<TAG>_<offset>.bin', desc: 'payload del bloque (sin cabecera tag+size; ver `blocks.json`).' }, { name: 'room_<N>/blocks.json', desc: 'tabla [{tag,size,off,file}] de la sala.' }],
		related: [{ name: '`decoded/sim.json`', desc: 'cada entrada `rooms[].blocks` proviene de este directorio.' }, { name: '`decoded/png/rooms/room_<N>.png`', desc: 'imagen decodificada del bloque `RMIM`.' }],
	});
	writeDirReadme(path.join(raw, 'scripts'), {
		title: 'Guiones descompilados (descumm)',
		what: 'Un directorio por sala con los bloques de guion originales (.bin, con cabecera) y su texto descompilado (.txt, sintaxis descumm).',
		producedBy: '`extract.mjs dump-game/scripts` (ejecuta `descumm -5`).',
		consumedBy: '`translate/parse.mjs` (AST) y revisión humana; el texto NO es la fuente del port (lo es el C++ traducido).',
		items: [{ name: 'room_<N>/<TAG>_<offset>.bin', desc: 'bloque de guion íntegro (cabecera incluida), lo que consume descumm.' }, { name: 'room_<N>/<TAG>_<offset>.txt', desc: 'descompilado legible con direcciones `[addr] (opcode)`.' }],
		related: [{ name: '`decoded/ast/room_<N>/<guion>.ast.json`', desc: 'representación estructurada del mismo guion (I1).' }],
		notes: ['Los .bin de guion viven aquí y no en `raw/rooms/` porque descumm necesita la cabecera completa.'],
	});
}

// Decodifica las walkboxes (BOXD + BOXM) a JSON semántico en decoded/walkboxes/.
// El formato y los citados viven en `../decode/decoders.mjs` (única implementación).
function cmdDecodeBoxes(args) {
	const g = loadGame(args._[1]);
	const base = path.basename(args._[1]).replace(/\.[^.]*$/, '');
	const work = outDir(args, base);
	const dir = path.join(work, 'decoded', 'walkboxes');
	fs.mkdirSync(dir, { recursive: true });
	const index = [];
	for (const d of g.data) {
		const list = args.room ? d.rooms.filter((r) => r.room === Number(args.room)) : d.rooms;
		for (const r of list) {
			const all = [...r.blocks, ...r.lflf_blocks];
			const boxd = all.find((b) => b.tag === 'BOXD');
			const boxm = all.find((b) => b.tag === 'BOXM');
			if (!boxd) continue;
			const decoded = decodeWalkboxes(
				d.buf.subarray(boxd.off + 8, boxd.off + boxd.size),
				boxm ? d.buf.subarray(boxm.off + 8, boxm.off + boxm.size) : null,
			);
			if (!decoded) continue;
			const json = {
				room: r.room,
				numBoxes: decoded.numBoxes,
				boxes: decoded.boxes,
				adjacency: decoded.adjacency,
				source: { file: path.basename(d.file), boxdOff: boxd.off, boxmOff: boxm ? boxm.off : -1 },
			};
			fs.writeFileSync(path.join(dir, `room_${r.room}.json`), JSON.stringify(json, null, 2) + '\n');
			index.push({ room: r.room, boxes: decoded.numBoxes, adjacency: decoded.adjacency ? decoded.adjacency.length : 0 });
		}
	}
	fs.writeFileSync(path.join(dir, 'index.json'), JSON.stringify({ format: 'adv-walkboxes/1', game: base, rooms: index }, null, 2) + '\n');
	writeDirReadme(dir, {
		title: 'Walkboxes decodificadas (BOXD + BOXM)',
		what: 'Geometría de suelo por sala: cajas convexas (esquina ul/ur/lr/ll, `scale`, flags) y la matriz de adyacencia del bloque `BOXM`. Es la fuente del pathfinding del runtime (A0).',
		producedBy: '`extract.mjs decode-boxes <juego.000>` (decodificador en `decode/decoders.mjs`).',
		consumedBy: 'revisión humana, `decode/` (SIM v2) y (previsto) el walkbox system del engine; `codegen/` emite estas cajas como tablas C++.',
		items: [{ name: 'room_<N>.json', desc: 'cajas + `adjacency` + offset de origen en el contenedor.' }, { name: 'index.json', desc: 'resumen por sala (nº de cajas y de entradas de adyacencia).' }],
		related: [{ name: '`../sim.json`', desc: 'incluye las mismas cajas en `rooms[].walkboxes` + `adjacency`.' }, { name: '`../png/rooms/room_<N>.png`', desc: 'fondo de la sala donde se aplican esas cajas.' }],
	});
	console.log(`[extract] walkboxes: ${index.length} salas -> ${dir}`);
}

function cmdRoomsJson(args) {
	const g = loadGame(args._[1]);
	const base = path.basename(args._[1]).replace(/\.[^.]*$/, '');
	const json = {
		format: 'sim/1',
		game: {
			id: base,
			source: {
				type: 'scumm-v5',
				index: path.basename(args._[1]),
				data: g.data.map((d) => path.basename(d.file)),
				enc: g.index.enc,
			},
			counts: {
				rooms: g.index.roomDisks.length,
				scripts: g.index.scripts.length,
				costumes: g.index.costumes.length,
				sounds: g.index.sounds.length,
				charsets: g.index.charsets.length,
				globalObjects: g.index.counts.DOBJ ?? 0,
			},
		},
		roomNames: Object.fromEntries([...g.index.roomNames.entries()]),
		rooms: g.data.flatMap((d) =>
			d.rooms.map((r) => ({
				room: r.room,
				name: g.index.roomNames.get(r.room) ?? '',
				file: path.basename(d.file),
				off: r.roomOff,
				lflfOff: r.lflfOff,
				blocks: r.blocks.map((b) => ({ tag: b.tag, size: b.size })),
				lflfBlocks: r.lflf_blocks.map((b) => ({ tag: b.tag, size: b.size })),
				scripts: [...r.blocks, ...r.lflf_blocks]
					.filter((b) => SCRIPT_TAGS.has(b.tag))
					.map((b) => ({ tag: b.tag, off: b.off, size: b.size })),
			})),
		),
	};
	const file = args.out ? String(args.out) : path.join(outDir(args, base), 'raw', 'index', 'rooms.json');
	fs.mkdirSync(path.dirname(file), { recursive: true });
	fs.writeFileSync(file, JSON.stringify(json, null, 2) + '\n');
	console.log(`[extract] rooms.json -> ${file}`);
}

function descummPath(args) {
	if (args.descumm) return String(args.descumm);
	if (process.env.DESCUMM) return process.env.DESCUMM;
	const p = path.join(REPO_ROOT, '..', 'SCUMM', 'scummvm-tools-2.9.0', process.platform === 'win32' ? 'descumm.exe' : 'descumm');
	return fs.existsSync(p) ? p : null;
}

function cmdScripts(args) {
	const g = loadGame(args._[1]);
	const base = path.basename(args._[1]).replace(/\.[^.]*$/, '');
	const work = outDir(args, base);
	const descumm = args['no-descumm'] ? null : descummPath(args);
	let total = 0, ok = 0;
	for (const d of g.data) {
		const list = args.room ? d.rooms.filter((r) => r.room === Number(args.room)) : d.rooms;
		for (const r of list) {
			const s = dumpRoomScripts(work, d, r, descumm);
			total += s.total;
			ok += s.ok;
		}
	}
	console.log(`[extract] scripts: ${total} bloques -> ${path.join(work, 'raw', 'scripts')} (descumm OK: ${ok}${descumm ? '' : '; desactivado'})`);
}

// --- main -------------------------------------------------------------------------------

const args = parseArgs(process.argv.slice(2));
const cmd = args._[0];
switch (cmd) {
case 'index': cmdIndex(args); break;
case 'rooms': cmdRooms(args); break;
case 'dump-room': cmdDumpRoom(args); break;
case 'dump-game': cmdDumpGame(args); break;
case 'scripts': cmdScripts(args); break;
case 'decode-boxes': cmdDecodeBoxes(args); break;
case 'rooms-json': cmdRoomsJson(args); break;
default:
	console.log(`Uso: node ${path.relative(process.cwd(), fileURLToPath(import.meta.url))} <index|rooms|dump-room|dump-game|scripts|decode-boxes|rooms-json> <juego.000> [opciones]

  index         tabla del índice (bloques, cuentas, nombres de sala)
  rooms         salas con sus bloques internos (digest)
  dump-room     vuelca los bloques de una sala a <out>/raw/rooms/room_N/
  dump-game     vuelca el juego entero (bloques + guiones + SIM) al workspace
  scripts       vuelca los bloques SCRP/EXCD/ENCD y los descompila con descumm
  decode-boxes  decodifica walkboxes (BOXD/BOXM) a <out>/decoded/walkboxes/
  rooms-json    manifiesto SIM v1 (rooms.json)

Opciones: --room N  --out <dir|file>  --descumm <exe>  --no-descumm
Workspace canónico: tools/adventure/WORKSPACE.md (defecto out/adventure/<juego>/).`);
	process.exit(cmd ? 1 : 0);
}
