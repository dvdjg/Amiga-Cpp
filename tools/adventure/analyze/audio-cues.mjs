#!/usr/bin/env node
// Informe de **cues de audio** por guion (paso 4 del pipeline): recorre el AST y extrae las
// llamadas que disparan sonido/música (`startSound`, `stopSound`, `soundKludge`,
// `startMusic`) con sus ids y situación (sala/guion/dirección). Sirve para enlazar después
// cada cue con las pistas FLAC del contenedor (`.sof`, `org`) y con las réplicas del diálogo.
//
// Limitación declarada: el id de sonido del original → pista digital necesita el parser de
// `SOUN` (iMUSE/VOC) que aún no existe; este informe deja el lado del guion resuelto.
//
// Uso: node tools/adventure/analyze/audio-cues.mjs <juego>

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { writeDirReadme } from '../lib/docs.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');

const target = process.argv[2];
if (!target) {
	console.error('Uso: node tools/adventure/analyze/audio-cues.mjs <juego>');
	process.exit(2);
}
const ws = path.join(REPO, 'out', 'adventure', target);
const astRoot = path.join(ws, 'decoded', 'ast');
if (!fs.existsSync(astRoot)) {
	console.error(`[audio] no hay decoded/ast en ${ws}`);
	process.exit(1);
}
const CUES = new Set(['startSound', 'stopSound', 'soundKludge', 'startMusic']);
const sites = [];
const ids = new Map();

const argsOf = (n) => n.args.map((a) => (a.k === 'num' ? a.v : null));
const walkExpr = (n, meta) => {
	if (!n || typeof n !== 'object') return;
	if (n.k === 'call' && CUES.has(n.name)) record(n.name, argsOf(n), meta);
	if (n.args) n.args.forEach((a) => walkExpr(a, meta));
	if (n.items) n.items.forEach((a) => walkExpr(a, meta));
	if (n.a) walkExpr(n.a, meta);
	if (n.b) walkExpr(n.b, meta);
	if (n.stmt) walkExpr(n.stmt, meta);
	if (n.target) walkExpr(n.target, meta);
	if (n.value) walkExpr(n.value, meta);
	if (n.index) walkExpr(n.index, meta);
};
const record = (prim, args, meta) => {
	const id = args.find((a) => a !== null) ?? null;
	if (id !== null) ids.set(id, (ids.get(id) ?? 0) + 1);
	sites.push({ ...meta, prim, args });
};
const walkStmts = (list, meta) => {
	for (const s of list) {
		if (s.kind === 'call' && CUES.has(s.name)) record(s.name, argsOf(s), { ...meta, addr: s.addr });
		if (s.kind === 'call') s.args.forEach((a) => walkExpr(a, meta));
		if (s.kind === 'assign') { walkExpr(s.target, meta); walkExpr(s.value, meta); }
		if (s.kind === 'expr') walkExpr(s.expr, meta);
		if (s.kind === 'incdec') walkExpr(s.target, meta);
		if (s.cond) walkExpr(s.cond, meta);
		if (s.then) walkStmts(s.then, meta);
		if (s.else) walkStmts(s.else, meta);
	}
};

for (const roomDir of fs.readdirSync(astRoot)) {
	const m = /^room_(\d+)$/.exec(roomDir);
	if (!m) continue;
	const p = path.join(astRoot, roomDir);
	if (!fs.statSync(p).isDirectory()) continue;
	for (const f of fs.readdirSync(p)) {
		if (!f.endsWith('.ast.json')) continue;
		const ast = JSON.parse(fs.readFileSync(path.join(p, f), 'utf8'));
		walkStmts(ast.statements ?? [], { room: Number(m[1]), script: ast.script });
	}
}
const report = {
	format: 'adv-audio-cues/1',
	game: target,
	total: sites.length,
	ids: [...ids.entries()].sort((a, b) => b[1] - a[1]).map(([id, count]) => ({ id, count })),
	sites,
};
const outFile = path.join(ws, 'reports', 'audio-cues.json');
fs.mkdirSync(path.dirname(outFile), { recursive: true });
fs.writeFileSync(outFile, JSON.stringify(report, null, 2) + '\n');
writeDirReadme(path.dirname(outFile), {
	title: `Informes del pipeline (${target})`,
	what: 'Informes de análisis (JSON, deterministas): uso de opcodes, cobertura del AST/traducción y **cues de audio** por guion (qué id de sonido dispara cada guion y dónde).',
	producedBy: '`analyze/opcode-usage.mjs` · `translate/parse.mjs` (cobertura) · `translate/emit.mjs` (traducción) · `analyze/audio-cues.mjs` (audio).',
	consumedBy: 'revisión humana, roadmap (prioridades) y el mapeo audio↔diálogo del port.',
	items: [
		{ name: 'opcode-usage.json', desc: '`adv-opcodes/1`: opcodes por byte con nombre de handler y frecuencia.' },
		{ name: 'parse-coverage.json', desc: '`adv-ast-coverage/1`: líneas de guion parseadas vs `raw`, por sala y global.' },
		{ name: 'translate-report.json', desc: '`adv-translate/1`: guiones traducidos/omitidos, llamadas mapeadas vs no soportadas.' },
		{ name: 'audio-cues.json', desc: '`adv-audio-cues/1`: llamadas de sonido/música con id, sala, guion y dirección del original.' },
	],
	related: [
		{ name: '`../raw/scripts/`', desc: 'corpus de entrada (descompilados).' },
		{ name: '`../decoded/ast/`', desc: 'AST producido (fuente de los informes).' },
		{ name: '`../decoded/audio/`', desc: 'pistas FLAC del `.sof`; el enlace id↔pista queda pendiente del parser SOUN.' },
	],
});
console.log(`[audio] ${target}: ${sites.length} cues, ${ids.size} ids distintos -> ${outFile}`);
