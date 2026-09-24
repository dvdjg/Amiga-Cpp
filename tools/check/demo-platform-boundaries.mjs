// Comprueba la FRONTERA de las demos de FEATURE (`demos/features/**`): una feature es
// **portable** y no usa hardware directo. Falla si:
//   1. referencia registros de chipset (`0xdff…`, `$dff…`, `DFF0…`) en código;
//   2. incluye vocabulario de plataforma (`eng/platform/<familia>/X`) que no sea el
//      adaptador mínimo permitido (`backend.hpp` e `input_poll.hpp`).
//
// El vocabulario de chipset vive en `demos/techniques/**` (y en `engine/src/platform/`).
// Ver `docs/guides/roadmap/PLAN_ORGANIZACION_DEMOS.md`.
//
// Uso: node tools/check/demo-platform-boundaries.mjs [--quiet]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const FEATURES = path.join(ROOT, 'demos/features');

const QUIET = process.argv.includes('--quiet');
const ALLOWED_PLATFORM_INCLUDE = /^eng\/platform\/[^/]+\/(backend|input_poll)\.hpp$/;
const INCLUDE_RE = /#\s*include\s*[<"]([^>"]+)[>"]/;
const REGISTER_RE = /0x[dD][fF][fF]|\$[dD][fF][fF]|\bDFF0\b/;

function walk(dir, out = []) {
	if (!fs.existsSync(dir)) return out;
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) walk(p, out);
		else if (/\.(cpp|hpp|h)$/.test(e.name)) out.push(p);
	}
	return out;
}

const problems = [];
for (const abs of walk(FEATURES)) {
	const rel = path.relative(ROOT, abs).replace(/\\/g, '/');
	const lines = fs.readFileSync(abs, 'utf8').split(/\r?\n/);
	lines.forEach((line, i) => {
		const t = line.trim();
		if (t.startsWith('//') || t.startsWith('*') || t.startsWith('/*')) return;
		const code = line.replace(/"[^"]*"/g, '""');
		const m = INCLUDE_RE.exec(code);
		if (m && /^eng\/platform\//.test(m[1]) && !ALLOWED_PLATFORM_INCLUDE.test(m[1])) {
			problems.push(`${rel}:${i + 1}: feature incluye vocabulario de plataforma '${m[1]}'`);
		}
		if (REGISTER_RE.test(code)) {
			problems.push(`${rel}:${i + 1}: feature referencia registro de chipset`);
		}
	});
}

if (problems.length) {
	for (const p of problems) console.error(`[demo-platform-boundaries] FAIL: ${p}`);
	console.error(`[demo-platform-boundaries] ${problems.length} violación(es): una feature debe ser portable.`);
	process.exit(1);
}
if (!QUIET) console.log('[demo-platform-boundaries] OK: las features no usan hardware directo.');
