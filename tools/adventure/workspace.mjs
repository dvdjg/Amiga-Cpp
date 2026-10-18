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
import { writeDirReadme, PIPELINE_RELATIONS } from './lib/docs.mjs';

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
	// Ficha local del workspace: qué hay en cada carpeta y cómo se relaciona (generada).
	writeDirReadme(dir, {
		title: `Workspace del juego «${descriptor.id}»`,
		what: `${descriptor.title}. Material de trabajo del pipeline de conversión (no versionado: se regenera). El flujo es **fuente → raw → decoded → edited → cook → engine**; los informes viven en reports/.`,
		producedBy: '`node tools/adventure/workspace.mjs init <juego>` (estructura y este README).',
		consumedBy: 'las herramientas de `tools/adventure/` y (previsto) el editor/previewer; el engine consume solo `cook/`.',
		items: [
			{ name: 'game.json', desc: 'descriptor: id, título, motor, edición y ruta de la fuente.' },
			{ name: 'inbox/', desc: 'copias locales del material original (normalmente vacío: se referencia la fuente).' },
			{ name: 'raw/', desc: 'extracción literal del contenedor (.000/.00N): bloques por sala y guiones descompilados.' },
			{ name: 'decoded/', desc: 'SIM v2 (semántica), PNG de salas/objetos, audio FLAC, walkboxes y AST de guiones.' },
			{ name: 'edited/', desc: 'FUENTE editable del port: SIM retocado y añadidos Amiga (capas, cues, transparencias).' },
			{ name: 'cook/', desc: 'salida cocinada para el engine (`cpp/<juego>_content.hpp`).' },
			{ name: 'preview/', desc: 'capturas e informes del previewer host (I3).' },
			{ name: 'reports/', desc: 'informes: uso de opcodes, cobertura de AST, presupuesto (I4).' },
		],
		related: PIPELINE_RELATIONS,
		notes: [
			'Regenerable: borrar `raw/`, `decoded/`, `cook/` o `reports/` no pierde trabajo humano; `edited/` es lo único irremplazable.',
			'Cada carpeta de artefactos incluye su propio `README.md` generado con su ficha y relaciones.',
			'Nada de estos datos entra en git (out/ está ignorado): el contenido original tiene derechos.',
		],
	});
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
		const countRooms = fs.existsSync(path.join(dir, 'raw', 'rooms'))
			? fs.readdirSync(path.join(dir, 'raw', 'rooms')).filter((n) => /^room_\d+$/.test(n)).length
			: 0;
		const scripts = [];
		const scriptsDir = path.join(dir, 'raw', 'scripts');
		try {
			for (const room of fs.readdirSync(scriptsDir)) {
				if (!/^room_\d+$/.test(room)) continue;
				scripts.push(...fs.readdirSync(path.join(scriptsDir, room)).filter((f) => f.endsWith('.txt')));
			}
		} catch {
			/* sin scripts */
		}
		let walkboxes = 0;
		try {
			walkboxes = fs.readdirSync(path.join(dir, 'decoded', 'walkboxes')).filter((f) => /^room_\d+\.json$/.test(f)).length;
		} catch {
			/* sin walkboxes */
		}
		console.log(`${game}: ${desc.title ?? '?'} [${desc.engine ?? '?'}] salas(raw)=${countRooms} guiones=${scripts.length} walkboxes=${walkboxes}`);
	}
} else {
	console.error(`Comando no soportado: ${cmd} (usa init | status)`);
	process.exit(2);
}
