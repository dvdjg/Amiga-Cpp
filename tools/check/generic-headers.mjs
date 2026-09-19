// Comprueba la "regla de oro" de diseño: las cabeceras GENÉRICAS del engine no deben fijar
// un escalar concreto. Sólo las cabeceras de una implementación concreta (`retro/`,
// `platform/`, `cpu/`) y las del propio escalar (`fixed*.hpp`, `minifloat*.hpp`) pueden
// nombrar representaciones concretas (`Fixed<s16,s32>`, `q0/q8/q12/q24`, `MiniFloat16`).
//
// Los infractores históricos (mientras se completa la reubicación de las especializaciones)
// se aceptan de forma explícita en `generic-headers-baseline.txt`. Cualquier fichero NUEVO
// con un tipo concreto falla.
//
// Uso: node tools/check/generic-headers.mjs [--quiet]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const ENG = path.join(ROOT, 'engine/include/eng');

const QUIET = process.argv.includes('--quiet');

// Directorios/cabeceras exentas (implementación concreta o del propio escalar).
const EXEMPT_DIR = [/(^|\/)(retro|platform|cpu)\//];
const EXEMPT_FILE = [/fixed(_math)?\.hpp$/, /minifloat(_math)?\.hpp$/, /scalar\.hpp$/, /scalar_fwd\.hpp$/];

// Patrones de tipo concreto (en código, no en comentarios).
const PATTERNS = [
	/Fixed<\s*(eng::)?s(8|16|32|64)/,
	/\bq(0|8|12|24)\b/,
	/\bMiniFloat16\b/,
];

const baseline = new Set(
	(fs.existsSync(path.join(__dirname, 'generic-headers-baseline.txt'))
		? fs.readFileSync(path.join(__dirname, 'generic-headers-baseline.txt'), 'utf8')
		: ''
	)
		.split(/\r?\n/)
		.map((l) => l.trim())
		.filter((l) => l && !l.startsWith('#')),
);

function walk(dir) {
	const out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) out.push(...walk(p));
		else if (e.name.endsWith('.hpp')) out.push(p);
	}
	return out;
}

const problems = [];
const exempted = [];
for (const abs of walk(ENG)) {
	const rel = path.relative(ENG, abs).replace(/\\/g, '/');
	if (EXEMPT_DIR.some((r) => r.test(rel))) continue;
	if (EXEMPT_FILE.some((r) => r.test(rel))) continue;
	const lines = fs.readFileSync(abs, 'utf8').split(/\r?\n/);
	let hit = false;
	lines.forEach((line, i) => {
		const t = line.trim();
		if (t.startsWith('//') || t.startsWith('*') || t.startsWith('/*')) return;
		const code = line.replace(/"[^"]*"/g, '""'); // ignora literales de cadena (mensajes)
		if (PATTERNS.some((re) => re.test(code))) {
			hit = true;
			if (!baseline.has(rel)) problems.push(`eng/${rel}:${i + 1}: tipo concreto en cabecera genérica`);
		}
	});
	if (hit && baseline.has(rel)) exempted.push(rel);
}

if (!QUIET) for (const f of exempted) console.log(`[generic-headers] aviso: ${f} (baseline; pendiente de reubicar)`);
if (problems.length) {
	for (const p of problems) console.error(`[generic-headers] FAIL: ${p}`);
	console.error(`[generic-headers] ${problems.length} cabecera(s) genérica(s) con tipo concreto.`);
	process.exit(1);
}
if (!QUIET) console.log('[generic-headers] OK: cabeceras genéricas sin tipos concretos (baseline aparte).');
