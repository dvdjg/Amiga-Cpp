// Comprueba la higiene de los docs de hallazgos (investigaciones y rarezas): que esten
// indexados y que el nombre siga la convencion. Regla en `AGENTS.md` §1.3.
//
//   1. indice: todo `.md` de una carpeta indexada (salvo `README.md`) esta enlazado desde su README;
//   2. naming: el fichero es kebab-case, con prefijo `NNN_` solo si es de una demo.
//
// Carpetas: `docs/debugging/` y `docs/reference/emulators/` (recursiva: `winuae/...`), cuyo README
// actua de indice. Evita que un hallazgo quede huerfano o sin encontrar por tema.
//
// Uso: node tools/check/doc-index.mjs [--quiet]
// Falla (exit 1) si hay docs huerfanos o nombres fuera de la convencion.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

// Carpetas indexadas: su README debe enlazar todos los `.md` de debajo.
const GROUPS = [
	{ readme: 'docs/debugging/system/README.md', dir: 'docs/debugging/system', recurse: false },
	{ readme: 'docs/debugging/investigaciones/README.md', dir: 'docs/debugging/investigaciones', recurse: false },
	{ readme: 'docs/reference/emulators/README.md', dir: 'docs/reference/emulators', recurse: true },
];

// kebab-case; prefijo `NNN_` permitido si el doc es de una demo concreta.
const NAME_RE = /^(?:\d{3}_)?[a-z0-9]+(?:-[a-z0-9]+)*\.md$/;

const QUIET = process.argv.includes('--quiet');
const problems = [];

function walk(dir, recurse) {
	const out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) {
			if (recurse) out.push(...walk(p, recurse));
		} else if (e.isFile() && e.name.endsWith('.md')) {
			out.push(p);
		}
	}
	return out;
}

for (const g of GROUPS) {
	const readmePath = path.join(ROOT, g.readme);
	const dirPath = path.join(ROOT, g.dir);
	if (!fs.existsSync(readmePath) || !fs.existsSync(dirPath)) {
		problems.push(`${g.readme}: no existe (revisa la configuracion del check)`);
		continue;
	}
	const readme = fs.readFileSync(readmePath, 'utf8');
	const linked = new Set();
	for (const m of readme.matchAll(/\]\(([^)]+\.md)\)/g)) {
		linked.add(path.basename(m[1]));
	}
	for (const file of walk(dirPath, g.recurse)) {
		const base = path.basename(file);
		if (base === 'README.md') continue;
		const rel = path.relative(ROOT, file).replace(/\\/g, '/');
		if (!NAME_RE.test(base)) {
			problems.push(`${rel}: nombre fuera de la convencion (kebab-case; NNN_ solo si es de una demo)`);
		}
		if (!linked.has(base)) {
			problems.push(`${rel}: no esta indexado en ${g.readme}`);
		}
	}
}

if (problems.length) {
	for (const p of problems) console.error(`[doc-index] FAIL: ${p}`);
	console.error(`[doc-index] ${problems.length} problema(s) de indice/naming de docs.`);
	process.exit(1);
}
if (!QUIET) console.log('[doc-index] OK: docs de hallazgos indexados y con nombre valido.');
