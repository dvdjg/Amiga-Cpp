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
console.log(`[decode] ${GAME}: ${stats.rooms} salas, ${stats.walkboxes} walkboxes, ${stats.objects} objetos, ${stats.palettes} paletas${pngNote} -> ${outFile}`);
