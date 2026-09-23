// Comprueba la ESTRUCTURA TEMÁTICA del engine (docs/STRUCTURE.md §3,
// docs/engine/architecture/HEADER_POLICY.md):
//
//   1. `engine/include/eng/` solo contiene los módulos canónicos (nada de carpetas
//      ad-hoc nuevas de primer nivel).
//   2. `engine/include/eng/core/` está subdividido por tema (`math/`, `types/`,
//      `data/`, `util/`): no hay cabeceras sueltas en la raíz de `core/`.
//   3. `engine/src/platform/` solo contiene familias de backend conocidas
//      (`amiga/`, `atarist/`, `megadrive/`).
//
// La deuda histórica (mientras se completa el split temático) se acepta en
// `engine-tree-baseline.txt` (una entrada por línea: ruta relativa a la raíz del repo).
//
// Uso: node tools/check/engine-tree.mjs [--quiet] [--update-baseline]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const ENG = path.join(ROOT, 'engine/include/eng');
const CORE = path.join(ENG, 'core');
const SRC_PLATFORM = path.join(ROOT, 'engine/src/platform');

const QUIET = process.argv.includes('--quiet');
const UPDATE = process.argv.includes('--update-baseline');

const ALLOWED_TOP = new Set([
	'ai', 'api', 'assets', 'audio', 'board', 'cards', 'core', 'cpu', 'debug', 'field',
	'graphics', 'hw', 'input', 'memory', 'os', 'parallel', 'platform', 'res', 'retro',
	'scene', 'sim', 'task', 'ui', 'engine.hpp',
]);
const ALLOWED_CORE = new Set(['math', 'types', 'data', 'util']);
const ALLOWED_FAMILIES = new Set(['amiga', 'atarist', 'megadrive']);

const baselinePath = path.join(__dirname, 'engine-tree-baseline.txt');
const baseline = new Set(
	(fs.existsSync(baselinePath) ? fs.readFileSync(baselinePath, 'utf8') : '')
		.split(/\r?\n/)
		.map((l) => l.trim())
		.filter((l) => l && !l.startsWith('#')),
);

const problems = [];
const rel = (p) => path.relative(ROOT, p).replace(/\\/g, '/');
const flag = (p, msg) => {
	const r = rel(p);
	if (!baseline.has(r)) problems.push(`${r}: ${msg}`);
};

// 1. Primer nivel de eng/.
for (const e of fs.readdirSync(ENG, { withFileTypes: true })) {
	if (!ALLOWED_TOP.has(e.name)) flag(path.join(ENG, e.name), 'módulo no canónico en eng/');
}

// 2. Subdivisión temática de core/.
if (fs.existsSync(CORE)) {
	for (const e of fs.readdirSync(CORE, { withFileTypes: true })) {
		if (e.isDirectory()) {
			if (!ALLOWED_CORE.has(e.name)) flag(path.join(CORE, e.name), 'subcarpeta no canónica en core/');
		} else if (e.name.endsWith('.hpp')) {
			flag(path.join(CORE, e.name), 'cabecera suelta en core/ (debe ir a math/ types/ data/)');
		}
	}
}

// 3. Familias de backend.
if (fs.existsSync(SRC_PLATFORM)) {
	for (const e of fs.readdirSync(SRC_PLATFORM, { withFileTypes: true })) {
		if (e.isDirectory() && !ALLOWED_FAMILIES.has(e.name)) {
			flag(path.join(SRC_PLATFORM, e.name), 'familia de backend no canónica');
		}
	}
}

if (UPDATE) {
	const entries = [...new Set(problems.map((p) => p.split(':')[0]))].sort();
	const header = '# Deuda histórica aceptada por tools/check/engine-tree.mjs (mientras se\n' +
		'# completa el split temático). Una entrada por línea (ruta relativa al repo).\n' +
		'# Regenerar con --update-baseline solo si se decide aceptar deuda nueva.\n';
	fs.writeFileSync(baselinePath, header + entries.join('\n') + (entries.length ? '\n' : ''), 'utf8');
	if (!QUIET) console.log(`[engine-tree] baseline actualizado: ${entries.length} entrada(s).`);
	process.exit(0);
}

if (problems.length) {
	for (const p of problems) console.error(`[engine-tree] FAIL: ${p}`);
	console.error(`[engine-tree] ${problems.length} desviación(es) de la estructura temática.`);
	process.exit(1);
}
if (!QUIET) console.log('[engine-tree] OK: estructura temática de eng/ correcta.');
