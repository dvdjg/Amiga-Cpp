#!/usr/bin/env node
// Indexa las pistas CD-Audio MP3 distribuidas junto a la copia FM-Towns de Indy3.
// No las recodifica ni duplica: registra metadatos ffprobe y la ruta relativa al juego fuente.

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { writeDirReadme } from '../lib/docs.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');

function collectTracks(folder, ffprobe = 'ffprobe') {
	const names = fs.readdirSync(folder).filter((n) => /^track\d+\.mp3$/i.test(n))
		.sort((a, b) => Number(/\d+/.exec(a)[0]) - Number(/\d+/.exec(b)[0]));
	return names.map((name) => {
		const result = spawnSync(ffprobe, ['-v', 'error', '-show_entries', 'stream=codec_name,sample_rate,channels',
			'-show_entries', 'format=duration', '-of', 'json', path.join(folder, name)], { encoding: 'utf8' });
		if (result.status !== 0) return { file: name, error: (result.stderr || `ffprobe salió ${result.status}`).trim() };
		const info = JSON.parse(result.stdout);
		const stream = info.streams?.[0] ?? {};
		return {
			track: Number(/\d+/.exec(name)[0]), file: name,
			codec: stream.codec_name ?? null,
			sampleRate: stream.sample_rate ? Number(stream.sample_rate) : null,
			channels: stream.channels ?? null,
			durationSeconds: info.format?.duration ? Number(info.format.duration) : null,
		};
	});
}

function main(target) {
	const ws = fs.existsSync(path.join(REPO, 'out', 'adventure', target)) ? path.join(REPO, 'out', 'adventure', target) : path.resolve(target);
	const game = JSON.parse(fs.readFileSync(path.join(ws, 'game.json'), 'utf8'));
	if (!game.engine.includes('v3') || !game.edition.toLowerCase().includes('towns')) throw new Error('Se requiere un workspace SCUMM V3 FM-Towns.');
	const sourceDir = path.dirname(path.resolve(game.source));
	const tracks = collectTracks(sourceDir);
	if (!tracks.length) throw new Error(`No se encontraron track<N>.mp3 en ${sourceDir}`);
	const outDir = path.join(ws, 'decoded', 'audio');
	fs.mkdirSync(outDir, { recursive: true });
	const manifest = {
		format: 'adv-cd-audio/1', game: game.id,
		tracks: tracks.map((t) => ({ ...t, source: path.relative(sourceDir, path.join(sourceDir, t.file)).split(path.sep).join('/') })),
	};
	fs.writeFileSync(path.join(outDir, 'tracks.json'), JSON.stringify(manifest, null, 2) + '\n');
	writeDirReadme(outDir, {
		title: 'CD-Audio de Indy3 FM-Towns',
		what: 'Inventario de las pistas MP3 de CD-Audio incluidas junto a la copia FM-Towns; los ficheros originales se referencian sin copiar ni recodificar.',
		producedBy: '`node tools/adventure/decode/scumm_v3_lfl-audio.mjs indy3` (metadatos consultados con ffprobe).',
		consumedBy: 'cocina de audio y mapeo posterior de señales `SO` a pistas de CD.',
		items: [{ name: 'tracks.json', desc: 'inventario `adv-cd-audio/1`: número, ruta fuente, codec, frecuencia, canales y duración.' }],
		related: [{ name: '`../../raw/`', desc: 'bloques SCUMM `.LFL` del mismo juego.' }],
	});
	const failures = tracks.filter((t) => t.error).length;
	console.log(`[v3-audio] ${game.id}: ${tracks.length} pistas MP3, ${failures} errores de lectura -> ${outDir}/tracks.json`);
}

const target = process.argv[2];
const isMain = process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isMain) {
	if (!target) {
		console.error('Uso: node tools/adventure/decode/scumm_v3_lfl-audio.mjs <juego|dir>');
		process.exit(2);
	}
	try { main(target); } catch (error) { console.error(`[v3-audio] ${error.message}`); process.exit(1); }
}
