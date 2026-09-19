// Comprueba la numeracion de las demos (`demos/<plataforma>/NNN_<tema>/`):
//   1. cada directorio de demo tiene prefijo `NNN_`;
//   2. no hay numeros duplicados dentro de una misma plataforma.
//
// Es el equivalente de `test-numbering.mjs` para demos. Regla y reparto en
// `docs/ai-dev-environment/NUMBERING.md` (bloques reservados por rama).
//
// Uso: node tools/check/demo-numbering.mjs [--quiet]
// Falla (exit 1) si hay duplicados o directorios sin prefijo.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const DEMOS = path.join(ROOT, 'demos');

const QUIET = process.argv.includes('--quiet');
const problems = [];
const infos = [];

// Duplicados historicos aceptados explicitamente (`<plataforma>/<NNN>` por linea).
const BASELINE = path.join(__dirname, 'demo-numbering-baseline.txt');
const baseline = new Set(
	(fs.existsSync(BASELINE) ? fs.readFileSync(BASELINE, 'utf8') : '')
		.split(/\r?\n/)
		.map((l) => l.trim())
		.filter((l) => l && !l.startsWith('#')),
);

if (fs.existsSync(DEMOS)) {
	for (const plat of fs.readdirSync(DEMOS, { withFileTypes: true })) {
		if (!plat.isDirectory()) continue;
		const dir = path.join(DEMOS, plat.name);
		const nums = new Map(); // numero -> [nombres]
		for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
			if (!e.isDirectory()) continue;
			const m = /^(\d{3})_(.+)$/.exec(e.name);
			if (!m) {
				problems.push(`${plat.name}/${e.name}: directorio sin prefijo NNN_`);
				continue;
			}
			const n = Number(m[1]);
			if (!nums.has(n)) nums.set(n, []);
			nums.get(n).push(e.name);
		}
		for (const [n, names] of nums) {
			if (names.length <= 1) continue;
			const key = `${plat.name}/${String(n).padStart(3, '0')}`;
			if (baseline.has(key)) {
				infos.push(`${key}: duplicado historico aceptado (${names.join(', ')})`);
				continue;
			}
			problems.push(
				`demos/${plat.name}: numero duplicado ${String(n).padStart(3, '0')}: ${names.join(', ')}`,
			);
		}
	}
}

if (!QUIET) for (const i of infos) console.log(`[demo-numbering] aviso: ${i}`);
if (problems.length) {
	for (const p of problems) console.error(`[demo-numbering] FAIL: ${p}`);
	console.error(`[demo-numbering] ${problems.length} problema(s) de numeracion de demos.`);
	process.exit(1);
}
if (!QUIET) console.log('[demo-numbering] OK: numeros de demo unicos por plataforma.');
