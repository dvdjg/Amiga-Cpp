#!/usr/bin/env node
// Mapa de **audio real** del juego (paso 4 del pipeline): enlaza los ids de sonido del
// original y los códigos de habla incrustados en los textos con sus pistas digitales.
//
//  - **Música**: tabla `DSOU` del índice → bloque `SOUN` de la sala (payloads descifrados con
//    `resource.cpp:readResTypeList`/`sound.cpp:triggerSound`) → `type` en payload[7]
//    (`ptr += 2`, `*(ptr + 0x0D)` con `ptr` = tag) y, para `type == 2` (pista de CD), la
//    pista en payload[0x10] y sus frames de inicio/fin en [0x12..0x17]
//    (`soundcd.cpp:getCDTrackIdFromSoundId`, `+0x18` desde el tag) → fichero
//    `cd_music_flac/trackN.flac` (o `se_music_flac/`).
//  - **Habla**: el código de escape 10 de los textos incrusta el offset en `MONSTER.SOU`
//    (`string.cpp:handleNextCharsetCode`, caso 10: b0|b1<<8|b4<<16|b5<<24) y descumm lo
//    imprime como `sound(0xOFFSET, 0xVCTL)` (`descumm-common.cpp:313-330`); ese offset es el
//    `org` del índice `.sof` (`sound.cpp:1192-1200`) → pista `decoded/audio/NNNNN.flac`.
//
// Uso: node tools/adventure/analyze/audio-map.mjs <juego>

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { writeDirReadme } from '../lib/docs.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');

const target = process.argv[2];
if (!target) {
	console.error('Uso: node tools/adventure/analyze/audio-map.mjs <juego>');
	process.exit(2);
}
const wsCandidate = path.join(REPO, 'out', 'adventure', target);
const ws = fs.existsSync(wsCandidate) ? wsCandidate : path.resolve(target);
const game = JSON.parse(fs.readFileSync(path.join(ws, 'game.json'), 'utf8'));
const source = path.resolve(ws, game.source);
const gameDir = path.dirname(source);

// --- música: DSOU → SOUN → pista de CD -------------------------------------------------

const readEncrypted = (file, key) => {
	const buf = fs.readFileSync(file);
	if (key) for (let i = 0; i < buf.length; i++) buf[i] ^= key;
	return buf;
};
const roomsJson = JSON.parse(fs.readFileSync(path.join(ws, 'raw', 'index', 'rooms.json'), 'utf8'));
const encKey = roomsJson.game?.source?.enc && roomsJson.game.source.enc <= 0xff ? roomsJson.game.source.enc : null;
const indexBuf = readEncrypted(source, encKey);
const findBlock = (buf, name) => {
	let off = 0;
	while (off + 8 <= buf.length) {
		const tag = buf.toString('latin1', off, off + 4);
		const size = buf.readUInt32BE(off + 4);
		if (tag === name) return { off: off + 8, size: size - 8 };
		if (size < 8 || off + size > buf.length) return null;
		off += size;
	}
	return null;
};
const dsou = findBlock(indexBuf, 'DSOU');
const roomOf = new Map(roomsJson.rooms.map((r) => [r.room, r]));
const soundEntries = [];
if (dsou) {
	const num = indexBuf.readUInt16LE(dsou.off);
	for (let i = 0; i < num; i++) {
		soundEntries.push({
			id: i + 1,
			room: indexBuf[dsou.off + 2 + i],
			off: indexBuf.readUInt32LE(dsou.off + 2 + num + i * 4),
		});
	}
}
const dataCache = new Map();
const readData = (file) => {
	if (!dataCache.has(file)) dataCache.set(file, fs.readFileSync(file));
	return dataCache.get(file);
};
const music = [];
const musicKinds = {};
const swap32 = (v) => (((v & 0xff) << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | ((v >> 24) & 0xff)) >>> 0;
for (const e of soundEntries) {
	if (e.room === 0) {
		// DSOU con sala 0: id sin recurso cargable (no lo reproduce el original); se declara.
		music.push({ id: e.id, room: 0, kind: 'unmapped' });
		musicKinds.unmapped = (musicKinds.unmapped ?? 0) + 1;
		continue;
	}
	const room = roomOf.get(e.room);
	if (!room) { music.push({ id: e.id, room: e.room, error: 'sala sin entrada en rooms.json' }); continue; }
	const file = path.join(gameDir, room.file);
	const abs = room.off + e.off; // rooms.json `off` = payload de LFFL; DSOU off relativo a él
	let buf = readData(file);
	let tag = buf.toString('latin1', abs, abs + 4);
	if (tag !== 'SOUN') {
		buf = Buffer.from(readData(file)); // copia descifrada (no mutar la caché)
		const key = encKey ?? 0x69;
		for (let i = 0; i < buf.length; i++) buf[i] ^= key;
		tag = buf.toString('latin1', abs, abs + 4);
	}
	if (tag !== 'SOUN') { music.push({ id: e.id, room: e.room, off: e.off, error: `tag '${tag}' en ${room.file}+${abs}` }); continue; }
	const payload = buf.subarray(abs + 8);
	const blockSize = buf.readUInt32BE(abs + 4);
	const t0 = payload.readUInt32BE(0);
	// readSoundResource (sound.cpp:1269-1480): el bloque del juego es `SOUN` + tamaño + payload;
	// el payload real es `'SOU '`+sub-bloques por driver (SBL/ADL/ROL/…) o un bloque de pista
	// de CD (primer u32 en LE cuyo valor es el tamaño restante: SWAP_BYTES_32 == size-8).
	if (t0 === 0x534f5520) { // 'SOU '
		const inner = [];
		let o = 8;
		while (o + 8 <= payload.length) {
			const tag = payload.toString('latin1', o, o + 4);
			if (!/^[A-Za-z0-9!@#$%^&*()_+ ]{4}$/.test(tag)) break; // fin de los sub-bloques válidos
			const raw = payload.readUInt32BE(o + 4);
			if (raw <= 0 || o + raw + 8 > payload.length + 8) break;
			inner.push({ tag, size: raw });
			o += raw + 8;
		}
		music.push({ id: e.id, room: e.room, kind: 'wrap', inner });
		musicKinds.SOU = (musicKinds.SOU ?? 0) + 1;
	} else if (swap32(t0) === blockSize - 8) {
		const track = payload[0x10]; // soundcd.cpp:getCDTrackIdFromSoundId (+0x18 desde el tag)
		const loops = payload[0x11];
		const frames = (o) => (payload[o] * 60 + payload[o + 1]) * 75 + payload[o + 2];
		const start = frames(0x12);
		const end = frames(0x15);
		const cd = path.join(gameDir, 'cd_music_flac', `track${track}.flac`);
		const se = path.join(gameDir, 'se_music_flac', `track${track}.flac`);
		music.push({
			id: e.id, room: e.room, kind: 'cd', track, loops,
			startFrame: start, endFrame: end, startSeconds: +(start / 75).toFixed(3), endSeconds: +(end / 75).toFixed(3),
			file: fs.existsSync(cd) ? `cd_music_flac/track${track}.flac` : null,
			fileSE: fs.existsSync(se) ? `se_music_flac/track${track}.flac` : null,
		});
		musicKinds.cd = (musicKinds.cd ?? 0) + 1;
	} else {
		music.push({ id: e.id, room: e.room, kind: 'other', head: payload.subarray(0, 4).toString('hex') });
		musicKinds.other = (musicKinds.other ?? 0) + 1;
	}
}

// --- habla: códigos 10 de los textos → org del .sof → pista ------------------------------

const manifestFile = path.join(ws, 'decoded', 'audio', 'manifest.json');
const manifest = fs.existsSync(manifestFile) ? JSON.parse(fs.readFileSync(manifestFile, 'utf8')) : { entries: [] };
const byOrg = new Map(manifest.entries.map((e) => [e.org, e]));
const speech = [];
const SPEECH_RE = /sound\(0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\)/g;
const scriptsRoot = path.join(ws, 'raw', 'scripts');
for (const roomDir of fs.existsSync(scriptsRoot) ? fs.readdirSync(scriptsRoot) : []) {
	const m = /^room_(\d+)$/.exec(roomDir);
	if (!m) continue;
	const p = path.join(scriptsRoot, roomDir);
	if (!fs.statSync(p).isDirectory()) continue;
	for (const f of fs.readdirSync(p)) {
		if (!f.endsWith('.txt') || f.endsWith('.err.txt')) continue;
		const text = fs.readFileSync(path.join(p, f), 'utf8');
		for (const hit of text.matchAll(SPEECH_RE)) {
			const org = parseInt(hit[1], 16);
			const entry = byOrg.get(org);
			speech.push({
				room: Number(m[1]), script: f.replace(/\.txt$/, ''), org, vctl: parseInt(hit[2], 16),
				file: entry ? `decoded/audio/${entry.file}` : null,
			});
		}
	}
}

// --- cues de guion (si el informe previo existe) → enriquecer la música ------------------

const cuesFile = path.join(ws, 'reports', 'audio-cues.json');
if (fs.existsSync(cuesFile)) {
	const cues = JSON.parse(fs.readFileSync(cuesFile, 'utf8'));
	const byId = new Map();
	for (const s of cues.sites ?? []) {
		const id = (s.args ?? []).find((a) => a !== null);
		if (id === null || id === undefined) continue;
		if (!byId.has(id)) byId.set(id, []);
		byId.get(id).push({ room: s.room, script: s.script, prim: s.prim, addr: s.addr ?? null });
	}
	for (const m of music) {
		const cue = byId.get(m.id);
		if (cue) m.cuedBy = cue.slice(0, 12);
	}
}

const matched = speech.filter((s) => s.file).length;
const report = {
	format: 'adv-audio-map/1',
	game: target,
	music: { total: music.length, byKind: musicKinds, entries: music },
	speech: { total: speech.length, matched, entries: speech },
};
const outFile = path.join(ws, 'reports', 'audio-map.json');
fs.mkdirSync(path.dirname(outFile), { recursive: true });
fs.writeFileSync(outFile, JSON.stringify(report, null, 2) + '\n');
writeDirReadme(path.dirname(outFile), {
	title: `Informes del pipeline (${target})`,
	what: 'Informes de análisis (JSON, deterministas): uso de opcodes, cobertura del AST/traducción, **cues de audio** por guion y **mapa de audio** (id de sonido → pista de CD; código de habla → pista del `.sof`).',
	producedBy: '`analyze/opcode-usage.mjs` · `translate/parse.mjs` (cobertura) · `translate/emit.mjs` (traducción) · `analyze/audio-cues.mjs` · `analyze/audio-map.mjs`.',
	consumedBy: 'revisión humana, roadmap (prioridades) y la cocina de audio del port (elegir pistas por escena).',
	items: [
		{ name: 'opcode-usage.json', desc: '`adv-opcodes/1`: opcodes por byte con nombre de handler y frecuencia.' },
		{ name: 'parse-coverage.json', desc: '`adv-ast-coverage/1`: líneas de guion parseadas vs `raw`, por sala y global.' },
		{ name: 'translate-report.json', desc: '`adv-translate/1`: guiones traducidos/omitidos, llamadas mapeadas vs no soportadas.' },
		{ name: 'audio-cues.json', desc: '`adv-audio-cues/1`: llamadas de sonido/música con id, sala, guion y dirección del original.' },
		{ name: 'audio-map.json', desc: '`adv-audio-map/1`: música por id (pista de CD y tramo) y habla por código 10 (offset → pista FLAC del `.sof`).' },
	],
	related: [
		{ name: '`../raw/scripts/`', desc: 'corpus de entrada (descompilados).' },
		{ name: '`../decoded/ast/`', desc: 'AST producido (fuente de los informes).' },
		{ name: '`../decoded/audio/`', desc: 'pistas FLAC del `.sof` con su manifiesto `org`→fichero.' },
		{ name: '`../raw/index/rooms.json`', desc: 'offsets de sala (base de la resolución DSOU→SOUN).' },
	],
});
console.log(`[audio-map] ${target}: música ${music.length} ids (clases ${JSON.stringify(musicKinds)}), habla ${speech.length} códigos (${matched} con pista) -> ${outFile}`);
