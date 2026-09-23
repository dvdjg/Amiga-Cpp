// Comprueba la numeracion de los tests host (`tests/host/<categoría>/NNN_<nombre>/`):
//   1. sin prefijos `NNN` duplicados entre directorios (unicos en TODO tests/host);
//   2. cada categoría tiene un `README.md` de catálogo y los directorios de la
//      categoría coinciden 1:1 con sus filas;
//   3. en cada fila, el `HOST-NNN` y el directorio enlazado usan el MISMO numero.
//
// Regla (ver `docs/testing/TAXONOMY.md`): el numero es unico y no se reutiliza; agrupar
// por categoría NO renumera. Ante una colision se renumera el test mas nuevo. Los huecos
// (numeros nunca usados) NO son fallo: se informan como aviso.
//
// Uso: node tools/check/test-numbering.mjs [--quiet] [--json]
// Falla (exit 1) si hay duplicados o un catalogo de categoría no cuadra.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const HOST_DIR = path.join(ROOT, 'tests/host');

const QUIET = process.argv.includes('--quiet');
const JSON_OUT = process.argv.includes('--json');

const problems = [];
const infos = [];

const relOf = (p) => path.relative(HOST_DIR, p).replace(/\\/g, '/');

// 1) Directorios de test (a cualquier profundidad) y su categoría (carpeta contenedora).
const dirNums = new Map(); // numero -> [rutas relativas]
const byCategory = new Map(); // categoría (ruta relativa) -> Map(numero -> dirName)
function walk(dir) {
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		if (!e.isDirectory()) continue;
		const p = path.join(dir, e.name);
		const m = /^(\d{3})_(.+)$/.exec(e.name);
		if (m) {
			const num = Number(m[1]);
			if (!dirNums.has(num)) dirNums.set(num, []);
			dirNums.get(num).push(relOf(p));
			const cat = relOf(dir);
			if (!byCategory.has(cat)) byCategory.set(cat, new Map());
			byCategory.get(cat).set(num, e.name);
		} else {
			walk(p);
		}
	}
}
walk(HOST_DIR);

for (const [num, names] of dirNums) {
	if (names.length > 1) problems.push(`numero duplicado HOST-${String(num).padStart(3, '0')}: ${names.join(', ')}`);
}

// 2) Catálogo por categoría.
for (const [cat, dirs] of byCategory) {
	const readme = path.join(HOST_DIR, cat, 'README.md');
	if (!fs.existsSync(readme)) {
		problems.push(`categoría sin README.md de catálogo: ${cat}`);
		continue;
	}
	const rows = [];
	for (const line of fs.readFileSync(readme, 'utf8').split(/\r?\n/)) {
		const m = /^\|\s*HOST-(\d{3})\s*\|\s*\[[^\]]*\]\((\d{3})_([^/)]+)\//.exec(line);
		if (!m) continue;
		rows.push({ num: Number(m[1]), dirNum: Number(m[2]), dirName: `${m[2]}_${m[3]}` });
	}
	const rowNums = new Set(rows.map((r) => r.num));
	if (rowNums.size !== rows.length) {
		const seen = new Set();
		for (const r of rows) {
			if (seen.has(r.num)) problems.push(`${cat}: HOST-${String(r.num).padStart(3, '0')} aparece dos veces`);
			seen.add(r.num);
		}
	}
	for (const r of rows) {
		if (r.num !== r.dirNum) problems.push(`${cat}: HOST-${String(r.num).padStart(3, '0')} enlaza a ${r.dirName} (numero distinto)`);
		if (!dirs.has(r.num)) problems.push(`${cat}: fila HOST-${String(r.num).padStart(3, '0')} sin directorio en la categoría`);
	}
	for (const [num, dirName] of dirs) {
		if (!rowNums.has(num)) problems.push(`${cat}: directorio sin fila en el catálogo: ${dirName}`);
	}
}

// 3) Huecos (aviso, no fallo).
const present = [...dirNums.keys()].sort((a, b) => a - b);
const max = present.length ? present[present.length - 1] : 0;
const missing = [];
for (let i = 0; i <= max; i++) if (!dirNums.has(i)) missing.push(i);
if (missing.length) infos.push(`huecos (no usados): ${missing.map((n) => String(n).padStart(3, '0')).join(', ')}`);

if (JSON_OUT) {
	console.log(JSON.stringify({ tests: dirNums.size, categories: byCategory.size, problems, infos }, null, 2));
}
if (!QUIET && !JSON_OUT) {
	for (const i of infos) console.log(`[test-numbering] aviso: ${i}`);
}
if (problems.length) {
	for (const p of problems) console.error(`[test-numbering] FAIL: ${p}`);
	console.error(`[test-numbering] ${problems.length} problema(s) de numeracion/catalogo.`);
	process.exit(1);
}
console.log(`[test-numbering] OK: ${dirNums.size} tests en ${byCategory.size} categoría(s), sin duplicados y catálogos sincronizados.`);
