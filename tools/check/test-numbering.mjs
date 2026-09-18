// Comprueba la numeracion de los tests host (`tests/host/NNN_<nombre>/`):
//   1. sin prefijos `NNN` duplicados entre directorios;
//   2. el catalogo `tests/host/README.md` y los directorios coinciden 1:1;
//   3. en cada fila del catalogo, el `HOST-NNN` y el directorio enlazado usan el
//      MISMO numero (evita catalogos desincronizados tras renombrar).
//
// Regla (ver `tests/host/README.md`): el numero es unico y no se reutiliza; ante una
// colision se renumera el test mas nuevo. Los huecos (numeros nunca usados) NO son
// fallo: se informan como aviso.
//
// Uso: node tools/check/test-numbering.mjs [--quiet] [--json]
// Falla (exit 1) si hay duplicados o el catalogo no cuadra con los directorios.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const HOST_DIR = path.join(ROOT, 'tests/host');
const README = path.join(HOST_DIR, 'README.md');

const QUIET = process.argv.includes('--quiet');
const JSON_OUT = process.argv.includes('--json');

const problems = [];
const infos = [];

// 1) Directorios con prefijo NNN_.
const dirNums = new Map(); // numero -> [nombres]
for (const entry of fs.readdirSync(HOST_DIR, { withFileTypes: true })) {
  if (!entry.isDirectory()) continue;
  const m = /^(\d{3})_(.+)$/.exec(entry.name);
  if (!m) {
    problems.push(`directorio sin prefijo NNN_: ${entry.name}`);
    continue;
  }
  const num = Number(m[1]);
  if (!dirNums.has(num)) dirNums.set(num, []);
  dirNums.get(num).push(entry.name);
}
for (const [num, names] of dirNums) {
  if (names.length > 1) problems.push(`numero duplicado HOST-${String(num).padStart(3, '0')}: ${names.join(', ')}`);
}

// 2) Filas del catalogo (HOST-NNN -> directorio enlazado).
const rows = []; // { num, dirNum, dirName }
for (const line of fs.readFileSync(README, 'utf8').split(/\r?\n/)) {
  const m = /^\|\s*HOST-(\d{3})\s*\|\s*\[[^\]]*\]\((\d{3})_([^/)]+)\//.exec(line);
  if (!m) continue;
  rows.push({ num: Number(m[1]), dirNum: Number(m[2]), dirName: `${m[2]}_${m[3]}` });
}
const catalogNums = new Set(rows.map((r) => r.num));
if (catalogNums.size !== rows.length) {
  const seen = new Set();
  for (const r of rows) {
    if (seen.has(r.num)) problems.push(`HOST-${String(r.num).padStart(3, '0')} aparece dos veces en el catalogo`);
    seen.add(r.num);
  }
}
for (const r of rows) {
  if (r.num !== r.dirNum) {
    problems.push(`HOST-${String(r.num).padStart(3, '0')} enlaza a ${r.dirName} (numero distinto)`);
  }
  if (!fs.existsSync(path.join(HOST_DIR, r.dirName))) {
    problems.push(`HOST-${String(r.num).padStart(3, '0')} enlaza a ${r.dirName}, que no existe`);
  }
}

// 3) Directorios sin fila y filas sin directorio.
for (const [num, names] of dirNums) {
  if (!catalogNums.has(num)) problems.push(`directorio sin fila en el catalogo: ${names.join(', ')}`);
}
for (const r of rows) {
  if (!dirNums.has(r.num)) problems.push(`fila ${String(r.num).padStart(3, '0')} sin directorio en tests/host`);
}

// 4) Huecos (aviso, no fallo).
const present = [...dirNums.keys()].sort((a, b) => a - b);
const max = present.length ? present[present.length - 1] : 0;
const missing = [];
for (let i = 0; i <= max; i++) if (!dirNums.has(i)) missing.push(i);
if (missing.length) infos.push(`huecos (no usados): ${missing.map((n) => String(n).padStart(3, '0')).join(', ')}`);

if (JSON_OUT) {
  console.log(JSON.stringify({ tests: dirNums.size, problems, infos }, null, 2));
}
if (!QUIET && !JSON_OUT) {
  for (const i of infos) console.log(`[test-numbering] aviso: ${i}`);
}
if (problems.length) {
  for (const p of problems) console.error(`[test-numbering] FAIL: ${p}`);
  console.error(`[test-numbering] ${problems.length} problema(s) de numeracion/catalogo.`);
  process.exit(1);
}
console.log(`[test-numbering] OK: ${dirNums.size} tests, sin duplicados y catalogo sincronizado.`);
