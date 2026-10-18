#!/usr/bin/env node
// Genera los **andamios de sala `adv-stage/1`** (directivas de hardware Amiga) a partir del
// SIM v2 de un workspace, en `edited/stages/room_N.stage.json`. No pisa trabajo humano salvo
// `--force`. Validación: `stage.mjs` (+ test); diseño: `STAGE_FORMAT.md`.
//
// Uso: node tools/adventure/formats/scaffold-stages.mjs <juego> [--force] [--room N]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { STAGE_FORMAT, scaffoldStage, validateStage } from './stage.mjs';
import { writeDirReadme } from '../lib/docs.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');

function parseArgs(argv) {
	const out = { _: [] };
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		if (a.startsWith('--')) out[a.slice(2)] = a === '--force' ? true : argv[++i];
		else out._.push(a);
	}
	return out;
}

const args = parseArgs(process.argv.slice(2));
const target = args._[0];
if (!target) {
	console.error('Uso: node tools/adventure/formats/scaffold-stages.mjs <juego> [--force] [--room N]');
	process.exit(2);
}
const ws = path.join(REPO, 'out', 'adventure', target);
const simFile = path.join(ws, 'decoded', 'sim.json');
if (!fs.existsSync(simFile)) {
	console.error(`[stage] falta ${simFile} (decodifica antes)`);
	process.exit(1);
}
const sim = JSON.parse(fs.readFileSync(simFile, 'utf8'));
const outDir = path.join(ws, 'edited', 'stages');
fs.mkdirSync(outDir, { recursive: true });

let made = 0, kept = 0, invalid = 0;
const wanted = args.room ? Number(args.room) : null;
const index = [];
for (const room of sim.rooms ?? []) {
	if (wanted !== null && room.room !== wanted) continue;
	const file = path.join(outDir, `room_${room.room}.stage.json`);
	if (fs.existsSync(file) && !args.force) {
		kept++;
	} else {
		const stage = scaffoldStage(room, sim.game?.id ?? target);
		const errs = validateStage(stage);
		if (errs.length) {
			invalid++;
			console.error(`[stage] sala ${room.room}: andamio inválido:`, errs.slice(0, 3));
			continue;
		}
		fs.writeFileSync(file, JSON.stringify(stage, null, 2) + '\n');
		made++;
	}
	index.push({ room: room.room, file: `room_${room.room}.stage.json` });
}
fs.writeFileSync(path.join(outDir, 'index.json'), JSON.stringify({ format: 'adv-stage-index/1', game: sim.game?.id ?? target, stageFormat: STAGE_FORMAT, rooms: index }, null, 2) + '\n');

writeDirReadme(outDir, {
	title: 'Salas Amiga (`adv-stage/1`) — fuente editable del port',
	what: 'Descripción de cada sala **orientada al hardware Amiga** (A500): display (single/DPF/EHB/HAM) y planos, capas con scroll por hardware, parallax, transparencias por truco de color, **Copper Score** (cues con disparadores y keyframes), canales de sprite, BOBs, HUD, streaming y presupuesto declarado. Es la FUENTE que el compositor (A3) baja a `eng::scene`/`copper::Plan`.',
	producedBy: '`node tools/adventure/formats/scaffold-stages.mjs <juego>` genera el andamio desde el SIM v2 (este README incluido); el enriquecimiento se hace a mano/editor sobre estos ficheros.',
	consumedBy: 'el compositor de la capa de aventura (A3) y el validador de presupuesto (I4).',
	items: [
		{ name: 'room_<N>.stage.json', desc: 'documento `adv-stage/1`: display+paleid, capas, copper.cues, sprites, bobs, hud, stream y budget.' },
		{ name: 'index.json', desc: 'lista de salas con su fichero de stage.' },
	],
	related: [
		{ name: '`../../decoded/sim.json`', desc: 'geometría/paleta de origen (los niveles de partida).' },
		{ name: '`../../decoded/png/`', desc: 'contenido visual referenciado por `layers[].content.ref`.' },
		{ name: '`../../../docs/engine/architecture/ADVENTURE_LAYER.md`', desc: 'cómo lo consume el compositor (capas y presupuesto).' },
	],
	notes: ['No se pisa trabajo humano: el andamio solo se escribe si falta (o con --force).', 'Validación estructural: `stage.mjs`; las cotas de hardware son política A500 documentada en STAGE_FORMAT.md.'],
});
console.log(`[stage] ${sim.game?.id ?? target}: ${made} nuevos, ${kept} conservados, ${invalid} inválidos -> ${outDir}`);
