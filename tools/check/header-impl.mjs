// Gate ADVISORY sobre la política de cabeceras (docs/engine/architecture/HEADER_POLICY.md).
// No falla por defecto: informa de candidatos a revisar.
//
//   1. Cabeceras de `engine/include/eng` por encima de un umbral de líneas
//      (candidatas a PARTIR POR TEMA, no a mover a `.cpp`).
//   2. Definiciones de función libre en cabecera SIN `inline`/`constexpr`/`template`
//      (posible problema de ODR/enlace). Heurística; revisar el reporte.
//
// Con `--strict` falla si hay hallazgos fuera de `header-impl-baseline.txt`.
//
// Uso: node tools/check/header-impl.mjs [--quiet] [--strict] [--lines N]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const ENG = path.join(ROOT, 'engine/include/eng');

const QUIET = process.argv.includes('--quiet');
const STRICT = process.argv.includes('--strict');
const linesArg = process.argv.indexOf('--lines');
const LINE_THRESHOLD = linesArg >= 0 ? Number(process.argv[linesArg + 1]) : 850;

const baselinePath = path.join(__dirname, 'header-impl-baseline.txt');
const baseline = new Set(
	(fs.existsSync(baselinePath) ? fs.readFileSync(baselinePath, 'utf8') : '')
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

const KEYWORDS = new Set(['if', 'for', 'while', 'switch', 'catch', 'do', 'else', 'class',
	'struct', 'enum', 'union', 'namespace', 'return', 'sizeof', 'alignas']);
const FUNC_DEF_RE = /^[A-Za-z_][\w:<>,*&\s]*\b([A-Za-z_]\w*)\s*\([^;]*\)\s*(const\s*)?\{$/;

const oversized = [];
const nonInline = [];
for (const abs of walk(ENG)) {
	const rel = path.relative(ENG, abs).replace(/\\/g, '/');
	const text = fs.readFileSync(abs, 'utf8');
	const lines = text.split(/\r?\n/);
	if (lines.length > LINE_THRESHOLD && !baseline.has(rel)) oversized.push({ rel, n: lines.length });

	let depth = 0;
	lines.forEach((line, i) => {
		const t = line.trim();
		// Solo definiciones a nivel de espacio de nombres (columna 0 y fuera de llaves):
		// las funciones miembro dentro de una clase son implícitamente inline.
		const atTopLevel = depth === 0 && line.length > 0 && !/^\s/.test(line);
		if (atTopLevel && t.endsWith('{') && !/\b(inline|constexpr|template|explicit|static)\b/.test(t)
			&& !t.startsWith('//') && !t.startsWith('*') && !t.startsWith('/*')) {
			const m = FUNC_DEF_RE.exec(t);
			if (m && !KEYWORDS.has(m[1])) {
				const key = `${rel}:${i + 1}`;
				if (!baseline.has(key)) nonInline.push(key);
			}
		}
		// Profundidad de llaves (ignora las de comentarios/literales de forma aproximada).
		if (!t.startsWith('//') && !t.startsWith('*')) {
			for (const ch of line) {
				if (ch === '{') depth++;
				else if (ch === '}') depth = Math.max(0, depth - 1);
			}
		}
	});
}

if (!QUIET) {
	for (const o of oversized) console.log(`[header-impl] cabecera grande (${o.n} líneas): eng/${o.rel} — partir por tema`);
	for (const n of nonInline) console.log(`[header-impl] función no-inline en cabecera: eng/${n}`);
}
if (STRICT && (oversized.length || nonInline.length)) {
	console.error(`[header-impl] ${oversized.length + nonInline.length} hallazgo(s) fuera de baseline.`);
	process.exit(1);
}
if (!QUIET) console.log(`[header-impl] OK${STRICT ? '' : ' (advisory)'}: ${oversized.length} cabecera(s) grande(s), ${nonInline.length} función(es) no-inline.`);
