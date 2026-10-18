#!/usr/bin/env node
// Informe de USO DE OPCODES de una extracción descumm: cuenta los bytes de opcode vistos en
// `out/adventure/<juego>/raw/scripts/**/*.txt` y los mapea a nombres con la tabla de handlers
// del oráculo (`engines/scumm/script_v5.cpp`, líneas `OPCODE(0xXX, o5_handler)`).
//
// Sirve para dimensionar la conversión: qué subconjunto de la VM V5 usan los guiones reales.
//
// Uso:
//   node tools/adventure/analyze/opcode-usage.mjs <juego|dir> [--oracle <engines/scumm>] [--top N]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

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
	console.error('Uso: node tools/adventure/analyze/opcode-usage.mjs <juego|dir> [--oracle <engines/scumm>] [--top N]');
	process.exit(2);
}

const SCRIPTS_DIR = fs.existsSync(path.join(REPO, 'out', 'adventure', target, 'raw', 'scripts'))
	? path.join(REPO, 'out', 'adventure', target, 'raw', 'scripts')
	: path.resolve(target);
// id del juego: el argumento si es un workspace, o el directorio que contiene `raw/scripts`.
const GAME = fs.existsSync(path.join(REPO, 'out', 'adventure', target)) ? target : path.basename(path.dirname(path.dirname(SCRIPTS_DIR)));
const ORACLE = args.oracle ?? path.join(REPO, '..', 'SCUMM', 'scummvm-2026.4.0', 'engines', 'scumm');

// -- nombres de handler desde script_v5.cpp (OPCODE(0xXX, o5_nombre)) --------------------
// Primera aparición gana: el archivo registra variantes por plataforma más abajo.
const names = new Map();
const scriptV5 = path.join(ORACLE, 'script_v5.cpp');
if (fs.existsSync(scriptV5)) {
	const src = fs.readFileSync(scriptV5, 'utf8');
	for (const m of src.matchAll(/OPCODE\(0x([0-9a-f]{2}),\s*(\w+)\)/g)) {
		const b = parseInt(m[1], 16);
		if (!names.has(b)) names.set(b, m[2]);
	}
}

// -- conteo por byte de opcode ------------------------------------------------------------
const counts = new Map();
let total = 0, files = 0;
const walk = (dir) => {
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) walk(p);
		else if (e.isFile() && e.name.endsWith('.txt')) {
			files++;
			const text = fs.readFileSync(p, 'utf8');
			for (const m of text.matchAll(/^\[[0-9A-F]{4}\] \(([0-9A-Fa-f]{2}|\*\*)\)/gm)) {
				if (m[1] === '**') continue;
				const b = parseInt(m[1], 16);
				counts.set(b, (counts.get(b) ?? 0) + 1);
				total++;
			}
		}
	}
};
walk(SCRIPTS_DIR);

const opcodes = [...counts.entries()]
	.map(([byte, count]) => ({ byte, hex: `0x${byte.toString(16).padStart(2, '0')}`, name: names.get(byte) ?? '?', count }))
	.sort((a, b) => b.count - a.count);

const report = { format: 'adv-opcodes/1', game: GAME, engine: 'scumm-v5', files, total, distinct: opcodes.length, opcodes };
const outFile = path.join(REPO, 'out', 'adventure', GAME, 'reports', 'opcode-usage.json');
fs.mkdirSync(path.dirname(outFile), { recursive: true });
fs.writeFileSync(outFile, JSON.stringify(report, null, 2) + '\n');

const top = Number(args.top ?? 25);
console.log(`[opcodes] ${GAME}: ${files} ficheros, ${total} opcodes, ${opcodes.length} distintos (de 256) -> ${outFile}`);
for (const o of opcodes.slice(0, top)) {
	const pct = ((100 * o.count) / total).toFixed(2);
	console.log(`  ${o.hex} ${o.name.padEnd(22)} ${String(o.count).padStart(7)}  ${pct}%`);
}
