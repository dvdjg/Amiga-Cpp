// Gate `api-positional-literals`: los agregados de API con presupuesto/plan no deben
// inicializarse posicionalmente con literales (`SpriteBudget {8u, 8u, 60000u}`).
// Usa initializers designados (`.channels = ...`) o una factoria con nombre.
// Uso: node tools/check/api-positional-literals.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SCAN = ['engine', 'tests', 'demos', 'games'];
const PATTERNS = [
	/\b(?:SpriteBudget|PlanConfig|BlitBudgetLimits|SpriteHint)\s*\{\s*(?:[-+]?\d|0x[0-9a-fA-F])/,
	/\bset_budget\s*\(\s*\{/,
];

function walk(dir, out) {
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) {
			if (e.name === 'out' || e.name === 'vision' || e.name === 'data' || e.name === '.git') continue;
			walk(p, out);
		} else if (/\.(cpp|hpp|h|cc)$/.test(e.name)) {
			out.push(p);
		}
	}
}

const files = [];
for (const top of SCAN) {
	const dir = path.join(ROOT, top);
	if (fs.existsSync(dir)) walk(dir, files);
}
const bad = [];
for (const abs of files) {
	const text = fs.readFileSync(abs, 'utf8');
	const lines = text.split(/\r?\n/);
	for (let i = 0; i < lines.length; ++i) {
		if (PATTERNS.some((re) => re.test(lines[i]))) {
			bad.push(`${path.relative(ROOT, abs).replace(/\\/g, '/')}:${i + 1}: ${lines[i].trim()}`);
		}
	}
}
if (bad.length) {
	for (const b of bad) console.error(`[api-positional-literals] FALLO: ${b}`);
	console.error('[api-positional-literals] usa designated initializers o una factoria con nombre.');
	process.exit(1);
}
console.log('[api-positional-literals] OK: sin inicializadores posicionales con literales.');
