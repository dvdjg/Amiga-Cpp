#!/usr/bin/env node
// Construye el **SIM v2** (semántico) de un juego ya extraído: lee `raw/` del workspace
// (`out/adventure/<juego>/`) y escribe `decoded/sim.json` con lo que los guiones y el motor
// del port necesitan: tamaño de sala, walkboxes con adyacencia, paleta, objetos (con sus
// hotspots de imagen) y la lista de guiones por sala.
//
// Entrada: `raw/rooms/room_N/blocks.json` + los `.bin` (payloads sin cabecera) y
// `raw/index/rooms.json` (nombres). Decodificadores puros en `decoders.mjs` (citados al oráculo).
//
// Uso: node tools/adventure/decode/scumm_v5-decode.mjs <juego|dir> [--out <sim.json>] [--no-png]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { decodeWalkboxes, decodeObject, decodeObjectImage, decodePalette, decodeRoomHeader, findImageEntry } from './decoders.mjs';
import { decodeSmappedImage } from './image.mjs';
import { encodeIndexedPng, paletteTo8Bit } from '../lib/png.mjs';
import { writeDirReadme, PIPELINE_RELATIONS } from '../lib/docs.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');
const SCRIPT_TAGS = new Set(['SCRP', 'EXCD', 'ENCD', 'LSCR']);

function parseArgs(argv) {
	const out = { _: [] };
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		if (a.startsWith('--')) out[a.slice(2)] = argv[++i];
		else out._.push(a);
	}
	return out;
}

const args = parseArgs(process.argv.slice(2));
const target = args._[0];
if (!target) {
	console.error('Uso: node tools/adventure/decode/scumm_v5-decode.mjs <juego|dir> [--out <sim.json>]');
	process.exit(2);
}
const WS = fs.existsSync(path.join(REPO, 'out', 'adventure', target))
	? path.join(REPO, 'out', 'adventure', target)
	: path.resolve(target);
const GAME = path.basename(WS);

const inventory = JSON.parse(fs.readFileSync(path.join(WS, 'raw', 'index', 'rooms.json'), 'utf8'));
const names = new Map(Object.entries(inventory.roomNames ?? {}).map(([k, v]) => [Number(k), v]));

const roomsOut = [];
const stats = { rooms: 0, walkboxes: 0, objects: 0, palettes: 0 };
const png = !args['no-png'];
const pngDir = path.join(WS, 'decoded', 'png');
const imageManifest = { format: 'adv-images/1', game: GAME, rooms: [], objects: [], errors: [] };
if (png) {
	fs.mkdirSync(path.join(pngDir, 'rooms'), { recursive: true });
	fs.mkdirSync(path.join(pngDir, 'objects'), { recursive: true });
}

for (const r of inventory.rooms ?? []) {
	const roomDir = path.join(WS, 'raw', 'rooms', `room_${r.room}`);
	const blocksJson = path.join(roomDir, 'blocks.json');
	if (!fs.existsSync(blocksJson)) continue;
	const manifest = JSON.parse(fs.readFileSync(blocksJson, 'utf8'));
	const load = (entry) => fs.readFileSync(path.join(roomDir, entry.file));

	const entryOf = (tag) => manifest.blocks.filter((b) => b.tag === tag);
	const loadOne = (tag) => {
		const e = entryOf(tag)[0];
		return e ? load(e) : null;
	};

	const header = decodeRoomHeader(loadOne('RMHD'));
	const walk = decodeWalkboxes(loadOne('BOXD'), loadOne('BOXM'));
	const palette = decodePalette(loadOne('CLUT'), loadOne('EPAL'));

	// Objetos: cada OBCD trae la definición (CDHD); cada OBIM la imagen (IMHD). Se emparejan por id.
	const images = new Map();
	for (const e of entryOf('OBIM')) {
		const img = decodeObjectImage(load(e));
		if (img) images.set(img.id, img);
	}
	const objects = [];
	for (const e of entryOf('OBCD')) {
		const obj = decodeObject(load(e));
		if (!obj) continue;
		if (images.has(obj.id)) obj.image = images.get(obj.id);
		objects.push(obj);
	}

	// Normalización a PNG (sin pérdidas): fondo de sala (RMIM) y primeros planos de objeto (IMxx).
	if (png && palette && palette.kind === 'clut' && header) {
		const pal8 = paletteTo8Bit(palette.colors);
		const rmim = loadOne('RMIM');
		if (rmim && header.width % 8 === 0) {
			try {
				const img = decodeSmappedImage(rmim, header.width, header.height);
				const file = `rooms/room_${r.room}.png`;
				fs.writeFileSync(path.join(pngDir, file), encodeIndexedPng(img.width, img.height, img.indices, pal8));
				imageManifest.rooms.push({ room: r.room, file, width: img.width, height: img.height, strips: img.strips, codes: [...new Set(img.codes)] });
			} catch (err) {
				imageManifest.errors.push({ kind: 'room', room: r.room, message: err.message });
			}
		}
		for (const e of entryOf('OBIM')) {
			const mimg = decodeObjectImage(load(e));
			if (!mimg) continue;
			const obj = objects.find((o) => o.id === mimg.id);
			if (!obj || obj.width % 8 !== 0 || obj.width === 0 || obj.height === 0) continue;
			const entry = findImageEntry(load(e), 1);
			if (!entry) continue;
			try {
				const img = decodeSmappedImage(entry, obj.width, obj.height, { transparentColor: 0 });
				const file = `objects/room_${r.room}_obj${obj.id}.png`;
				fs.writeFileSync(path.join(pngDir, file), encodeIndexedPng(img.width, img.height, img.indices, pal8, { transparentIndex: 0 }));
				imageManifest.objects.push({ room: r.room, obj: obj.id, file, width: img.width, height: img.height, strips: img.strips, codes: [...new Set(img.codes)] });
			} catch (err) {
				imageManifest.errors.push({ kind: 'object', room: r.room, obj: obj.id, message: err.message });
			}
		}
	}

	const scripts = manifest.blocks
		.filter((b) => SCRIPT_TAGS.has(b.tag))
		.map((b) => ({ tag: b.tag, size: b.size, file: `raw/scripts/room_${r.room}/${b.tag}_${b.off}.bin` }));

	roomsOut.push({
		room: r.room,
		name: names.get(r.room) ?? r.name ?? '',
		file: r.file,
		width: header ? header.width : r.room,
		height: header ? header.height : 0,
		walkboxes: walk ? walk.boxes : [],
		adjacency: walk ? walk.adjacency : null,
		palette,
		objects,
		scripts,
	});
	stats.rooms++;
	if (walk) stats.walkboxes += walk.boxes.length;
	stats.objects += objects.length;
	if (palette) stats.palettes++;
}

const sim = {
	format: 'sim/2',
	game: {
		id: GAME,
		engine: inventory.game?.source?.type ?? 'scumm-v5',
		source: inventory.game?.source ?? {},
		counts: inventory.game?.counts ?? {},
	},
	rooms: roomsOut,
};

const outFile = args.out ? String(args.out) : path.join(WS, 'decoded', 'sim.json');
fs.mkdirSync(path.dirname(outFile), { recursive: true });
fs.writeFileSync(outFile, JSON.stringify(sim, null, 2) + '\n');
let pngNote = '';
if (png) {
	const imgFile = path.join(WS, 'decoded', 'images.json');
	fs.writeFileSync(imgFile, JSON.stringify(imageManifest, null, 2) + '\n');
	pngNote = `, PNG: ${imageManifest.rooms.length} salas + ${imageManifest.objects.length} objetos (${imageManifest.errors.length} avisos)`;
}
writeDecodedDocs(WS, GAME, png, stats, imageManifest);
console.log(`[decode] ${GAME}: ${stats.rooms} salas, ${stats.walkboxes} walkboxes, ${stats.objects} objetos, ${stats.palettes} paletas${pngNote} -> ${outFile}`);

// Fichas generadas de `decoded/` y sus subcarpetas de imágenes (qué es cada cosa y cómo encaja).
function writeDecodedDocs(ws, game, withPng, st, imgManifest) {
	const decoded = path.join(ws, 'decoded');
	writeDirReadme(decoded, {
		title: `Contenido decodificado (${game})`,
		what: 'Capa **semántica** del juego y formatos estándar: a partir de `raw/` se decodifica la estructura (SIM v2), las imágenes (PNG), el audio (FLAC, herramienta aparte) y los guiones (AST, etapa I1).',
		producedBy: '`node tools/adventure/decode/scumm_v5-decode.mjs <juego>` (SIM v2 + PNG); `decode/sof-extract.mjs` (audio) y `translate/parse.mjs` (AST).',
		consumedBy: '`codegen/` (tablas C++ para el engine), el previewer (I3), los informes y la revisión humana; `edited/` parte de aquí.',
		items: [
			{ name: 'sim.json', desc: 'SIM v2: salas con tamaño, walkboxes+adyacencia, paleta, objetos (id/x/y/w/h + imagen) y lista de guiones.' },
			{ name: 'walkboxes/room_<N>.json', desc: 'cajas y adyacencia por sala (misma información que `sim.json`, por comodidad).' },
			... (withPng ? [{ name: 'png/', desc: 'imágenes normalizadas sin pérdidas (ver png/README.md).' }, { name: 'images.json', desc: 'manifiesto `adv-images/1`: qué PNG existe, tamaño y códecs de tira usados.' }] : []),
			{ name: 'audio/', desc: 'pistas del contenedor .sof en FLAC + manifiesto `adv-sof/1` (otra herramienta).' },
			{ name: 'ast/', desc: 'guiones parseados a AST JSON (etapa I1, otra herramienta).' },
		],
		related: PIPELINE_RELATIONS,
	});
	if (!withPng) return;
	const pngDir = path.join(decoded, 'png');
	writeDirReadme(pngDir, {
		title: 'Imágenes normalizadas (PNG indexado, sin pérdidas)',
		what: 'Salas y objetos decodificados con los códecs del original y guardados como **PNG indexado 8bpp** (PLTE con la paleta del juego escalada de 6→8 bits). Los índices son exactamente los del original: sirve como verdad de referencia para la cocina y el diff visual.',
		producedBy: '`scumm_v5-decode.mjs` (`decode/image.mjs` + `lib/png.mjs`).',
		consumedBy: 'previewer (I3), cocina de assets (I2), validación por visión y comparación con ScummVM.',
		items: [
			{ name: 'rooms/room_<N>.png', desc: 'fondo completo de la sala N (decodificado del bloque RMIM; ancho/alto = RMHD).' },
			{ name: 'objects/room_<N>_obj<M>.png', desc: 'imagen del objeto M de la sala N (entrada IMxx de su OBIM); índice 0 transparente (tRNS).' },
			{ name: '../images.json', desc: 'manifiesto con dimensiones, nº de tiras y códigos de compresión por imagen.' },
		],
		related: [
			{ name: '`../sim.json`', desc: 'cada PNG corresponde a `rooms[N]` (fondo) o a `rooms[N].objects[id=M]` (objeto), con posición y tamaño en píxeles.' },
			{ name: '`../walkboxes/room_<N>.json`', desc: 'geometría de suelo dibujable sobre el mismo fondo.' },
			{ name: '`../../raw/rooms/room_<N>/RMIM_*.bin`, `OBIM_*.bin`', desc: 'bloques originales de los que se decodifica cada imagen.' },
		],
		notes: ['Índices de paleta = índices del juego: **no** se han reordenado ni cuantizado.', 'Un objeto sin PNG significa que su imagen no existe en la sala (hotspot decorativo pintado en el fondo).'],
	});
	writeDirReadme(path.join(pngDir, 'rooms'), {
		title: 'Fondos de sala (PNG)',
		what: 'Un PNG por sala con el fondo completo. Nombre: `room_<N>.png`, con N = id de sala del SIM.',
		producedBy: '`scumm_v5-decode.mjs` (bloque `RMIM` → tiras SMAP → códecs MajMin/zig-zag → PNG).',
		consumedBy: 'previewer, cocina (capas/parallax), visión.',
		items: [{ name: 'room_<N>.png', desc: 'índice de color 8bpp + PLTE del juego; tamaño = `sim.rooms[room].width × height`.' }],
		related: [{ name: '`../../sim.json`', desc: 'nombre de la sala, dimensiones y paleta (`rooms[room].palette`).' }],
	});
	writeDirReadme(path.join(pngDir, 'objects'), {
		title: 'Objetos de sala (PNG con transparencia)',
		what: 'Un PNG por objeto con imagen propia. Nombre: `room_<N>_obj<M>.png` (M = id de objeto del SIM). El índice 0 es transparente (tRNS) porque son sprites para componer sobre el fondo.',
		producedBy: '`scumm_v5-decode.mjs` (entrada `IMxx` del `OBIM` → tiras SMAP → PNG).',
		consumedBy: 'cocina (BOBs/sprites), previewer, visión.',
		items: [{ name: 'room_<N>_obj<M>.png', desc: 'índice de color 8bpp + PLTE; tamaño del objeto según CDHD (múltiplo de 8).' }],
		related: [
			{ name: '`../../sim.json`', desc: 'posición (`x`,`y`), tamaño y flags del objeto M en `rooms[N].objects`.' },
			{ name: '`../../raw/rooms/room_<N>/OBIM_*.bin`', desc: 'imagen original del objeto.' },
		],
		notes: ['El objeto puede no tener PNG si su `OBIM` no contiene entradas `IMxx` (solo `IMHD`): en ese caso su dibujo está pintado en el fondo.'],
	});
}
