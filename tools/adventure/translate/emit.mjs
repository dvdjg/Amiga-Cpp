#!/usr/bin/env node
// Paso **I1 (2/2)**: traduce los AST de guion (`decoded/ast/`) a **C++ de `eng::adv`**
// (`cook/cpp/scripts/room_N_scripts.hpp`) con el contrato de `eng/adv/script_hooks.hpp`.
// Los guiones con control de flujo no estructurado (goto/unless-goto) o sentencias `raw`
// se OMITEN con motivo (informe `reports/translate-report.json`): cobertura honesta.
//
// Uso: node tools/adventure/translate/emit.mjs <juego> [--room N] [--out <dir>]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { translateScript, TranslateError } from './cpp.mjs';
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
	console.error('Uso: node tools/adventure/translate/emit.mjs <juego> [--room N] [--out <dir>]');
	process.exit(2);
}
const ws = fs.existsSync(path.join(REPO, 'out', 'adventure', target)) ? path.join(REPO, 'out', 'adventure', target) : path.resolve(target);
const GAME = path.basename(ws);
const astRoot = path.join(ws, 'decoded', 'ast');
if (!fs.existsSync(astRoot)) {
	console.error(`[emit] no hay decoded/ast en ${ws} (ejecuta parse.mjs antes)`);
	process.exit(1);
}
const outRoot = args.out ? String(args.out) : path.join(ws, 'cook', 'cpp', 'scripts');
fs.mkdirSync(outRoot, { recursive: true });
const wantedRoom = args.room ? Number(args.room) : null;

const totals = { scripts: 0, translated: 0, skipped: 0, callsMapped: 0, callsUnsupported: 0, sayLines: 0 };
const rooms = [];
const roomHeaders = [];
for (const roomDir of fs.readdirSync(astRoot)) {
	const m = /^room_(\d+)$/.exec(roomDir);
	if (!m) continue;
	const room = Number(m[1]);
	if (wantedRoom !== null && room !== wantedRoom) continue;
	const srcDir = path.join(astRoot, roomDir);
	const parts = [`// GENERADO por tools/adventure/translate/emit.mjs — sala ${room} de ${GAME}.`, '#pragma once', ''];
	const roomReport = { room, scripts: 0, translated: 0, skipped: 0, reasons: [] };
	for (const f of fs.readdirSync(srcDir)) {
		if (!f.endsWith('.ast.json')) continue;
		const ast = JSON.parse(fs.readFileSync(path.join(srcDir, f), 'utf8'));
		if (!ast.statements) continue;
		roomReport.scripts++;
		totals.scripts++;
		try {
			const { code, stats } = translateScript(ast);
			parts.push(code);
			roomReport.translated++;
			totals.translated++;
			totals.callsMapped += stats.mapped;
			totals.callsUnsupported += stats.unsupported;
			totals.sayLines += stats.sayLines;
		} catch (err) {
			if (!(err instanceof TranslateError)) throw err;
			roomReport.skipped++;
			totals.skipped++;
			roomReport.reasons.push({ script: ast.script, reason: err.reason });
		}
	}
	const file = path.join(outRoot, `room_${room}_scripts.hpp`);
	if (roomReport.translated > 0) {
		fs.writeFileSync(file, parts.join('\n'));
		roomHeaders.push(`#include "room_${room}_scripts.hpp"`);
	}
	rooms.push(roomReport);
}
fs.writeFileSync(path.join(outRoot, 'scripts.hpp'), ['// GENERADO — todos los guiones traducidos de ' + GAME + '.', '#pragma once', '', ...roomHeaders, ''].join('\n'));

const coverage = totals.scripts ? (100 * totals.translated) / totals.scripts : 0;
const reportFile = path.join(ws, 'reports', 'translate-report.json');
fs.mkdirSync(path.dirname(reportFile), { recursive: true });
fs.writeFileSync(reportFile, JSON.stringify({ format: 'adv-translate/1', game: GAME, coverage, ...totals, rooms }, null, 2) + '\n');

writeDirReadme(outRoot, {
	title: `Guiones C++ traducidos (${GAME})`,
	what: 'Funciones de guion generadas desde el AST del original: `template <class W> void scrp_<sala>_<script>(W& w)` con el vocabulario de `engine/include/eng/adv/script_hooks.hpp` (variables, bits y primitivas de aventura). `scripts.hpp` incluye todas las salas.',
	producedBy: '`node tools/adventure/translate/emit.mjs <juego>` (reglas en `translate/cpp.mjs`).',
	consumedBy: 'el juego/demo en el engine: instancia su mundo de guion (`W`) y llama las funciones desde los eventos de sala/objeto.',
	items: [
		{ name: 'room_<N>_scripts.hpp', desc: 'funciones `scrp_<sala>_<guion>` de esa sala (solo guiones traducidos).' },
		{ name: 'scripts.hpp', desc: 'incluye los headers de todas las salas con guiones traducidos.' },
	],
	related: [
		{ name: '`../../decoded/ast/`', desc: 'AST de origen (uno por guion).' },
		{ name: '`../../../reports/translate-report.json`', desc: 'cobertura: traducidos/omitidos con motivo, llamadas mapeadas vs no soportadas.' },
		{ name: '`engine/include/eng/adv/script_hooks.hpp`', desc: 'contrato del mundo de guion (`NullScriptWorld` para compilar/validar en host).' },
	],
	notes: ['Los guiones con `goto`/`unless-goto` o sentencias `raw` se omiten (no se genera código inseguro); ver el informe por guion.'],
});
console.log(`[emit] ${GAME}: ${totals.translated}/${totals.scripts} guiones traducidos (${coverage.toFixed(1)}%), ${totals.callsMapped} llamadas, ${totals.callsUnsupported} no soportadas, ${totals.sayLines} réplicas -> ${outRoot}`);
