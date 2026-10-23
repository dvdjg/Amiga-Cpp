// Gate `magic-fail`: los códigos de `mark_failed`/`mark_ready` deben ser símbolos con
// nombre (enum/constante), no hexadecimales opacos. Baseline por fichero: el número de
// usos históricos solo puede bajar; cualquier uso nuevo en un fichero no listado o por
// encima de su cuenta falla.
//
// Uso: node tools/check/magic-fail.mjs [--list|--update-baseline]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const BASELINE = path.join(ROOT, 'tools/check/magic-fail-baseline.txt');
const SCAN = ['demos', 'games'];
const CODE = /mark_(?:failed|ready)\s*\([^;]*?\b0x[0-9a-fA-F]/g;

function walk(dir, out) {
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) {
			if (e.name === 'out' || e.name === 'vision' || e.name === 'data') continue;
			walk(p, out);
		} else if (/\.(cpp|hpp|h)$/.test(e.name)) {
			out.push(p);
		}
	}
}

const files = [];
for (const top of SCAN) walk(path.join(ROOT, top), files);
const counts = new Map();
for (const abs of files) {
	const text = fs.readFileSync(abs, 'utf8');
	const n = (text.match(CODE) ?? []).length;
	if (n !== 0) counts.set(path.relative(ROOT, abs).replace(/\\/g, '/'), n);
}

const args = process.argv.slice(2);
if (args.includes('--update-baseline')) {
	const lines = [
		'# Baseline de magic-fail: usos históricos de hexadecimal en mark_failed/mark_ready.',
		'# El conteo por fichero solo puede bajar. Los códigos nuevos usan enum/constante.',
		...[...counts.entries()].sort((a, b) => a[0].localeCompare(b[0])).map(([f, n]) => `${n} ${f}`),
	];
	fs.writeFileSync(BASELINE, lines.join('\n') + '\n', 'utf8');
	console.log(`[magic-fail] baseline actualizada (${counts.size} ficheros, ${[...counts.values()].reduce((a, b) => a + b, 0)} usos).`);
	process.exit(0);
}

const baseline = new Map();
if (fs.existsSync(BASELINE)) {
	for (const line of fs.readFileSync(BASELINE, 'utf8').split(/\r?\n/)) {
		if (!line || line.startsWith('#')) continue;
		const m = line.match(/^(\d+)\s+(.+)$/);
		if (m) baseline.set(m[2], Number(m[1]));
	}
}
const bad = [];
for (const [f, n] of counts) {
	const allowed = baseline.get(f) ?? 0;
	if (n > allowed) bad.push(`${f}: ${n} (baseline ${allowed})`);
}
if (args.includes('--list')) {
	for (const [f, n] of [...counts.entries()].sort()) console.log(`${n} ${f}`);
	console.log(`[magic-fail] ${counts.size} ficheros con hexadecimal histórico.`);
	process.exit(0);
}
if (bad.length) {
	for (const b of bad) console.error(`[magic-fail] FALLO: ${b}`);
	console.error('[magic-fail] usa un enum/constante documentado para el código de fallo.');
	process.exit(1);
}
console.log(`[magic-fail] OK: sin usos nuevos (${counts.size} ficheros históricos).`);
