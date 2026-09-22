// Comprueba la higiene de los docs de hallazgos (investigaciones y rarezas). Regla en
// `AGENTS.md` §1.3.
//
// Areas declaradas en `doc-index-areas.txt`. En cada una, **recursivamente**:
//   1. indice: cada carpeta con `.md` tiene un `README.md` que enlaza todos sus `.md` hermanos;
//   2. naming: el fichero es kebab-case, con prefijo `NNN_` solo si es de una demo.
//
// Anadir un emulador o una subcarpeta nueva no requiere tocar este codigo: basta con que su
// README exista y los indexe. Evita que un hallazgo quede huerfano.
//
// Uso: node tools/check/doc-index.mjs [--quiet]
// Falla (exit 1) si falta un README, hay docs huerfanos o nombres fuera de la convencion.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const AREAS_FILE = path.join(__dirname, 'doc-index-areas.txt');

// kebab-case; prefijo `NNN_` permitido si el doc es de una demo concreta.
const NAME_RE = /^(?:\d{3}_)?[a-z0-9]+(?:-[a-z0-9]+)*\.md$/;

const QUIET = process.argv.includes('--quiet');
const problems = [];

function readAreas() {
	if (!fs.existsSync(AREAS_FILE)) {
		return [];
	}
	return fs
		.readFileSync(AREAS_FILE, 'utf8')
		.split(/\r?\n/)
		.map((l) => l.trim())
		.filter((l) => l && !l.startsWith('#'));
}

// Carpetas (recursivo) que contienen al menos un `.md`.
function dirsWithMarkdown(dir) {
	const out = [];
	let hasMd = false;
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		if (e.isDirectory()) {
			out.push(...dirsWithMarkdown(path.join(dir, e.name)));
		} else if (e.isFile() && e.name.endsWith('.md')) {
			hasMd = true;
		}
	}
	if (hasMd) out.unshift(dir);
	return out;
}

for (const area of readAreas()) {
	const areaPath = path.join(ROOT, area);
	if (!fs.existsSync(areaPath)) {
		problems.push(`${area}: no existe (revisa doc-index-areas.txt)`);
		continue;
	}
	for (const dir of dirsWithMarkdown(areaPath)) {
		const rel = path.relative(ROOT, dir).replace(/\\/g, '/');
		const files = fs.readdirSync(dir).filter((f) => f.endsWith('.md'));
		for (const f of files) {
			if (f !== 'README.md' && !NAME_RE.test(f)) {
				problems.push(`${rel}/${f}: nombre fuera de la convencion (kebab-case; NNN_ solo si es de una demo)`);
			}
		}
		const readme = path.join(dir, 'README.md');
		if (!fs.existsSync(readme)) {
			problems.push(`${rel}: falta README.md (debe indexar sus .md)`);
			continue;
		}
		const linked = new Set();
		for (const m of fs.readFileSync(readme, 'utf8').matchAll(/\]\(([^)]+\.md)\)/g)) {
			linked.add(path.basename(m[1]));
		}
		for (const f of files) {
			if (f !== 'README.md' && !linked.has(f)) {
				problems.push(`${rel}/${f}: no esta indexado en ${rel}/README.md`);
			}
		}
	}
}

if (problems.length) {
	for (const p of problems) console.error(`[doc-index] FAIL: ${p}`);
	console.error(`[doc-index] ${problems.length} problema(s) de indice/naming de docs.`);
	process.exit(1);
}
if (!QUIET) console.log('[doc-index] OK: docs de hallazgos indexados y con nombre valido.');
