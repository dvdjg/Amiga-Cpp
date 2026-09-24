// Comprueba la numeracion de las demos (`demos/<clase>/<...>/NNN_<tema>/`):
//   1. cada directorio de demo tiene prefijo `NNN_`;
//   2. no hay numeros duplicados dentro de un mismo **ambito** (el directorio que
//      contiene las demos, p. ej. `techniques/amiga/copper` o `features/ui/amiga`);
//   3. no se mezclan demos y subdirectorios de categoria en el mismo nivel;
//   4. el nombre de directorio (leaf) es unico en todo `demos/` (el build/out usa el
//      basename; evita pisarse hasta que el id derive de la ruta).
//
// Regla y reparto en `docs/ai-dev-environment/NUMBERING.md`. Los duplicados historicos
// aceptados van en `demo-numbering-baseline.txt` (`<ambito>/<NNN>` por linea).
//
// Uso: node tools/check/demo-numbering.mjs [--quiet]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const DEMOS = path.join(ROOT, 'demos');

const QUIET = process.argv.includes('--quiet');
const problems = [];
const infos = [];
let scopes = 0;

const BASELINE = path.join(__dirname, 'demo-numbering-baseline.txt');
const baseline = new Set(
	(fs.existsSync(BASELINE) ? fs.readFileSync(BASELINE, 'utf8') : '')
		.split(/\r?\n/)
		.map((l) => l.trim())
		.filter((l) => l && !l.startsWith('#')),
);

const rel = (p) => path.relative(DEMOS, p).replace(/\\/g, '/');
const leafSeen = new Map(); // leaf -> ruta

function walk(dir) {
	const demos = [];
	const cats = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		if (!e.isDirectory()) continue;
		if (/^\d{3}_/.test(e.name)) demos.push(e.name);
		else cats.push(e.name);
	}
	if (demos.length > 0 && cats.length > 0) {
		problems.push(`${rel(dir)}: mezcla demos (${demos.join(', ')}) y categorias (${cats.join(', ')})`);
	}
	if (demos.length > 0) {
		// Ambito: los numeros deben ser unicos aqui.
		scopes++;
		const nums = new Map();
		for (const name of demos) {
			const n = Number(name.slice(0, 3));
			if (!nums.has(n)) nums.set(n, []);
			nums.get(n).push(name);
			if (leafSeen.has(name)) {
				problems.push(`leaf duplicado en demos/: ${name} (${leafSeen.get(name)} y ${rel(dir)}/${name})`);
			} else {
				leafSeen.set(name, rel(dir));
			}
		}
		for (const [n, names] of nums) {
			if (names.length <= 1) continue;
			const key = `${rel(dir)}/${String(n).padStart(3, '0')}`;
			if (baseline.has(key)) {
				infos.push(`${key}: duplicado historico aceptado (${names.join(', ')})`);
				continue;
			}
			problems.push(`${rel(dir)}: numero duplicado ${String(n).padStart(3, '0')}: ${names.join(', ')}`);
		}
	} else {
		// Categoria: bajar a sus subdirectorios.
		for (const c of cats) walk(path.join(dir, c));
	}
}

if (fs.existsSync(DEMOS)) walk(DEMOS);

if (!QUIET) for (const i of infos) console.log(`[demo-numbering] aviso: ${i}`);
if (problems.length) {
	for (const p of problems) console.error(`[demo-numbering] FAIL: ${p}`);
	console.error(`[demo-numbering] ${problems.length} problema(s) de numeracion de demos.`);
	process.exit(1);
}
if (!QUIET) console.log(`[demo-numbering] OK: ${scopes} ambito(s), numeros unicos y sin leaf duplicados.`);
