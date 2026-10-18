#!/usr/bin/env node
// Scaffold del **workspace por juego** del pipeline de aventuras (ver WORKSPACE.md):
// crea `out/adventure/<juego>/` con la estructura canónica y su descriptor `game.json`.
//
// Uso:
//   node tools/adventure/workspace.mjs init <juego> [--source <ruta>] [--edition <texto>] [--engine scumm-v5] [--title <texto>]
//   node tools/adventure/workspace.mjs status

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../..');
const ROOT = path.join(REPO, 'out', 'adventure');

const AREAS = ['inbox', 'raw/index', 'raw/rooms', 'raw/scripts', 'decoded', 'edited', 'cook', 'preview', 'reports'];

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
const cmd = args._[0];

if (cmd === 'init') {
	const game = args._[1];
	if (!game) {
		console.error('Uso: node tools/adventure/workspace.mjs init <juego> [--source <ruta>] [--edition <texto>] [--engine scumm-v5] [--title <texto>]');
		process.exit(2);
	}
	const dir = path.join(ROOT, game);
	for (const area of AREAS) fs.mkdirSync(path.join(dir, area), { recursive: true });
	const descriptorPath = path.join(dir, 'game.json');
	const prev = fs.existsSync(descriptorPath) ? JSON.parse(fs.readFileSync(descriptorPath, 'utf8')) : {};
	const descriptor = {
		ws: 'adv-workspace/1',
		id: game,
		title: args.title ?? prev.title ?? game,
		engine: args.engine ?? prev.engine ?? 'scumm-v5',
		edition: args.edition ?? prev.edition ?? '',
		source: args.source ?? prev.source ?? '',
		notes: prev.notes ?? '',
	};
	fs.writeFileSync(descriptorPath, JSON.stringify(descriptor, null, 2) + '\n');
	console.log(`[workspace] ${descriptorPath}`);
} else if (cmd === 'status' || cmd === undefined) {
	if (!fs.existsSync(ROOT)) {
		console.log('[workspace] sin juegos (out/adventure/ vacío).');
		process.exit(0);
	}
	for (const game of fs.readdirSync(ROOT)) {
		const dir = path.join(ROOT, game);
		if (!fs.statSync(dir).isDirectory()) continue;
		const desc = fs.existsSync(path.join(dir, 'game.json')) ? JSON.parse(fs.readFileSync(path.join(dir, 'game.json'), 'utf8')) : {};
		const count = (p) => {
			try {
				return fs.readdirSync(path.join(dir, p)).length;
			} catch {
				return 0;
			}
		};
		const scripts = [];
		const scriptsDir = path.join(dir, 'raw', 'scripts');
		try {
			for (const room of fs.readdirSync(scriptsDir)) scripts.push(...fs.readdirSync(path.join(scriptsDir, room)).filter((f) => f.endsWith('.txt')));
		} catch {
			/* sin scripts */
		}
		let walkboxes = 0;
		try {
			walkboxes = fs.readdirSync(path.join(dir, 'decoded', 'walkboxes')).filter((f) => /^room_\d+\.json$/.test(f)).length;
		} catch {
			/* sin walkboxes */
		}
		console.log(`${game}: ${desc.title ?? '?'} [${desc.engine ?? '?'}] salas(raw)=${count('raw/rooms')} guiones=${scripts.length} walkboxes=${walkboxes}`);
	}
} else {
	console.error(`Comando no soportado: ${cmd} (usa init | status)`);
	process.exit(2);
}
