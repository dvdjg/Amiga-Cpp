#!/usr/bin/env node
/**
 * `next-number.mjs` — Siguiente número libre dentro del bloque reservado a la rama.
 *
 * La asignación de bloques vive en `docs/ai-dev-environment/NUMBERING.md` (fuente única). Este
 * script lee esa tabla, detecta (o recibe) la rama/workstream y, mirando los directorios
 * `tests/host/NNN_*` y `demos/<plataforma>/NNN_*`, imprime el **menor número libre** de cada
 * bloque de la rama. Sirve para no volver a colisionar números entre `master` y `feature/optimize`.
 *
 * Uso:
 *   node tools/check/next-number.mjs            # usa la rama git actual
 *   node tools/check/next-number.mjs master     # o forzar una rama/workstream
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

function currentBranch() {
	try {
		return execFileSync('git', ['branch', '--show-current'], { cwd: root, encoding: 'utf8' }).trim();
	} catch {
		return '';
	}
}

function parseBlocks() {
	const docPath = path.join(root, 'docs/ai-dev-environment/NUMBERING.md');
	const text = fs.readFileSync(docPath, 'utf8');
	const rows = [];
	for (const line of text.split('\n')) {
		const m = line.match(/^\|\s*([A-Z])\s*\|\s*(\d+)-(\d+)\s*\|\s*(.+?)\s*\|\s*(.+?)\s*\|\s*$/);
		if (!m) continue;
		rows.push({
			block: m[1],
			lo: parseInt(m[2], 10),
			hi: parseInt(m[3], 10),
			owner: m[4].replace(/`/g, '').trim(),
			state: m[5].trim(),
		});
	}
	return rows;
}

function usedNumbers() {
	const used = new Set();
	const collect = (dir) => {
		if (!fs.existsSync(dir)) return;
		for (const entry of fs.readdirSync(dir)) {
			const m = entry.match(/^(\d+)_/);
			if (m) used.add(parseInt(m[1], 10));
		}
	};
	collect(path.join(root, 'tests/host'));
	const demosRoot = path.join(root, 'demos');
	if (fs.existsSync(demosRoot)) {
		for (const platform of fs.readdirSync(demosRoot)) {
			collect(path.join(demosRoot, platform));
		}
	}
	return used;
}

function main() {
	const branch = process.argv[2] || currentBranch();
	if (!branch) {
		console.error('No se pudo detectar la rama (pasa el nombre como argumento).');
		process.exit(2);
	}
	const blocks = parseBlocks().filter((b) => !/cerrado/i.test(b.state));
	const mine = blocks.filter((b) => b.owner.toLowerCase().includes(branch.toLowerCase()));
	if (mine.length === 0) {
		console.error(`La rama '${branch}' no tiene bloque reservado en NUMBERING.md.`);
		console.error('Reserva uno (edita §Bloques) antes de crear demos/tests numerados.');
		process.exit(1);
	}
	const used = usedNumbers();
	let any = false;
	for (const b of mine) {
		// Regla del repo: el siguiente número es (máximo usado dentro del bloque) + 1; los
		// huecos no se reutilizan.
		let highest = b.lo - 1;
		for (const u of used) {
			if (u >= b.lo && u <= b.hi && u > highest) highest = u;
		}
		const next = highest + 1;
		const agotado = next > b.hi;
		const label = agotado ? 'AGOTADO' : String(next).padStart(3, '0');
		console.log(`[next-number] rama '${branch}' bloque ${b.block} (${b.lo}-${b.hi}): siguiente libre = ${label}`);
		if (!agotado) any = true;
	}
	process.exit(any ? 0 : 1);
}

main();
