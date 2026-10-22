// ---------------------------------------------------------------------------
// Gate `raw-text-buffer`: parejas `char buf[N]` + `char* p = buf` en demos/juegos.
//
// Es el patrón que desbordó la pila del HUD de la 212 (la línea de contadores no
// cabía en `char line[64]` y `append(p, …)` escribía fuera). El texto de HUD debe
// usar `eng::util::StaticString<N>` + `to_chars`/`to_chars_hex` (CODING_STYLE
// §Texto formateado). Este gate impide introducir el patrón crudo de nuevo.
//
// Baseline histórico: `tools/check/raw-text-buffer-baseline.txt` (solo puede
// encoger). Uso: node tools/check/raw-text-buffer.mjs [--list|--update-baseline]
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const BASELINE_PATH = path.join(ROOT, 'tools/check/raw-text-buffer-baseline.txt');
const SCAN_DIRS = ['demos', 'games'];
const PATTERN =
	/\bchar\s+(\w+)\s*\[[^\]]*\]\s*;[^\n]*\n(?:[^\n]*\n){0,5}?[^\n]*\bchar\s*\*\s*(\w+)\s*=\s*\1\s*;/g;

function listSources(dir) {
	const out = [];
	const walk = (d) => {
		let entries;
		try {
			entries = fs.readdirSync(d, { withFileTypes: true });
		} catch {
			return;
		}
		for (const e of entries) {
			const p = path.join(d, e.name);
			if (e.isDirectory()) {
				if (e.name === 'out' || e.name === 'vision' || e.name === 'data') continue;
				walk(p);
			} else if (/\.(cpp|hpp|h)$/.test(e.name)) {
				out.push(p);
			}
		}
	};
	walk(dir);
	return out;
}

const rel = (p) => path.relative(ROOT, p).replace(/\\/g, '/');
const offenders = new Set();
for (const top of SCAN_DIRS) {
	for (const file of listSources(path.join(ROOT, top))) {
		const text = fs.readFileSync(file, 'utf8');
		for (const m of text.matchAll(PATTERN)) {
			offenders.add(rel(file));
			break;
		}
	}
}
const files = [...offenders].sort();

if (process.argv.includes('--list')) {
	for (const f of files) console.log(f);
	process.exit(0);
}

if (process.argv.includes('--update-baseline')) {
	fs.writeFileSync(BASELINE_PATH, `${files.join('\n')}\n`, 'utf8');
	console.log(`[raw-text-buffer] baseline: ${files.length} fichero(s) histórico(s).`);
	process.exit(0);
}

let baseline = [];
try {
	baseline = fs
		.readFileSync(BASELINE_PATH, 'utf8')
		.split(/\r?\n/)
		.map((s) => s.trim())
		.filter(Boolean);
} catch {
	// Sin baseline: cualquier pareja es nueva.
}
const allowed = new Set(baseline);
const added = files.filter((f) => !allowed.has(f));
const removed = baseline.filter((f) => !offenders.has(f));

if (removed.length) {
	console.log(
		`[raw-text-buffer] baseline contiene ${removed.length} fichero(s) ya corregido(s); actualízalo con --update-baseline.`
	);
}
if (added.length) {
	console.log('[raw-text-buffer] FALLO: nuevo texto crudo `char buf[N]` + `char* p = buf`;');
	for (const f of added) console.log(`  ${f}`);
	console.log('Usa eng::util::StaticString<N> + to_chars/to_chars_hex (CODING_STYLE).');
	process.exit(1);
}
console.log(`[raw-text-buffer] OK: ${files.length} histórico(s), sin parejas nuevas.`);
