#!/usr/bin/env node
// Paso **I1** (1/2): convierte los guiones descompilados (descumm .txt) de un workspace a
// **AST JSON** (`adv-ast/1`) y calcula la cobertura del parser. Es la base del traductor a
// C++ de `eng::adv` (siguiente pieza de I1): el traductor consume el AST, no el texto.
//
// Uso: node tools/adventure/translate/parse.mjs <juego|dir> [--room N] [--out <dir>]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseDescummText } from './descumm.mjs';
import { writeDirReadme } from '../lib/docs.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');

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
	console.error('Uso: node tools/adventure/translate/parse.mjs <juego|dir> [--room N] [--out <dir>]');
	process.exit(2);
}
const ws = fs.existsSync(path.join(REPO, 'out', 'adventure', target)) ? path.join(REPO, 'out', 'adventure', target) : path.resolve(target);
const GAME = path.basename(ws);
const scriptsRoot = path.join(ws, 'raw', 'scripts');
if (!fs.existsSync(scriptsRoot)) {
	console.error(`[parse] no hay raw/scripts en ${ws} (extrae antes con dump-game)`);
	process.exit(1);
}
const outRoot = args.out ? String(args.out) : path.join(ws, 'decoded', 'ast');
const wantedRoom = args.room ? Number(args.room) : null;

const totals = { scripts: 0, lines: 0, parsed: 0, raw: 0, ifs: 0, gotos: 0 };
const index = [];
for (const roomDir of fs.readdirSync(scriptsRoot)) {
	const m = /^room_(\d+)$/.exec(roomDir);
	if (!m) continue;
	const room = Number(m[1]);
	if (wantedRoom !== null && room !== wantedRoom) continue;
	const srcDir = path.join(scriptsRoot, roomDir);
	const dstDir = path.join(outRoot, roomDir);
	fs.mkdirSync(dstDir, { recursive: true });
	const perRoom = { room, scripts: 0, lines: 0, parsed: 0, raw: 0 };
	for (const f of fs.readdirSync(srcDir)) {
		if (!f.endsWith('.txt') || f.endsWith('.err.txt')) continue;
		const ast = parseDescummText(fs.readFileSync(path.join(srcDir, f), 'utf8'), { script: f.replace(/\.txt$/, ''), room });
		const outFile = path.join(dstDir, f.replace(/\.txt$/, '.ast.json'));
		fs.writeFileSync(outFile, JSON.stringify(ast, null, 2) + '\n');
		perRoom.scripts++;
		perRoom.lines += ast.stats.lines;
		perRoom.parsed += ast.stats.parsed;
		perRoom.raw += ast.stats.raw;
		totals.scripts++;
		totals.lines += ast.stats.lines;
		totals.parsed += ast.stats.parsed;
		totals.raw += ast.stats.raw;
		totals.ifs += ast.stats.ifs;
		totals.gotos += ast.stats.gotos;
	}
	index.push(perRoom);
}

const coverage = totals.lines ? (100 * totals.parsed) / totals.lines : 0;
const coverageFile = path.join(ws, 'reports', 'parse-coverage.json');
fs.mkdirSync(path.dirname(coverageFile), { recursive: true });
fs.writeFileSync(
	coverageFile,
	JSON.stringify({ format: 'adv-ast-coverage/1', game: GAME, coverage, ...totals, rooms: index }, null, 2) + '\n',
);
fs.writeFileSync(path.join(outRoot, 'index.json'), JSON.stringify({ format: 'adv-ast-index/1', game: GAME, rooms: index }, null, 2) + '\n');

writeDirReadme(outRoot, {
	title: 'AST de guiones (descumm → JSON)',
	what: 'Cada guion descompilado convertido a **AST JSON** (`adv-ast/1`): sentencias tipadas (`call`, `assign`, `if`, `goto`, `raw`), expresiones y direcciones/opcodes originales. Es la entrada del traductor a C++ (I1, siguiente pieza).',
	producedBy: '`node tools/adventure/translate/parse.mjs <juego>` (parser en `descumm.mjs`).',
	consumedBy: 'el traductor `eng::adv` (I1), revisión humana y análisis de cobertura.',
	items: [
		{ name: 'room_<N>/<guion>.ast.json', desc: 'AST del guion: `statements` anidados + `stats` (líneas/parseadas/raw).' },
		{ name: 'index.json', desc: 'resumen por sala (nº de guiones y cobertura).' },
	],
	related: [
		{ name: '`../../raw/scripts/room_<N>/<guion>.txt`', desc: 'texto de origen (descumm), uno a uno con cada AST.' },
		{ name: '`../../reports/parse-coverage.json`', desc: 'cobertura global del parser (líneas parseadas vs `raw`).' },
	],
	notes: ['`kind:"raw"` marca lo que el parser aún no entiende: la cobertura debe subir con reglas nuevas, no ocultarse.'],
});
console.log(`[parse] ${GAME}: ${totals.scripts} guiones, ${totals.parsed}/${totals.lines} sentencias parseadas (${coverage.toFixed(1)}%), ${totals.raw} raw -> ${outRoot}`);
