// Comprueba que los enlaces relativos de la documentacion (Markdown) apunten a
// ficheros o directorios existentes. Falla (exit 1) si encuentra un enlace roto.
// Pensado para el CI (regresion / tests host) y para no dejar referencias rotas
// al mover o renombrar documentos.
//
// Uso: node tools/check/links.mjs [raices...] [--quiet] [--json] [--update-baseline]
//   raices            ficheros/dirs Markdown a revisar (por defecto, la doc canonica)
//   --quiet           solo resumen y fallos
//   --json            imprime el resultado como JSON
//   --update-baseline regenera el baseline de enlaces rotos conocidos y sale 0
//
// Deuda historica: los arboles importados que aun conservan enlaces al repo de origen
// (docs/engine/c-engine, docs/legacy) se revisan, pero sus roturas actuales estan
// listadas en `tools/check/links-baseline.txt` y se aceptan como conocidas. El check
// falla solo ante rotura NUEVA (no en el baseline). Regenerar tras arreglar enlaces:
// `node tools/check/links.mjs --update-baseline`.
//
// No se comprueban: enlaces absolutos (http/https/mailto), anclas puras (#...),
// rutas absolutas de Windows, el manual OCR (`*.cat.md`) y destinos bajo directorios
// generados (out/, obj/, dist/).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

const DEFAULT_ROOTS = [
  'AGENTS.md',
  'docs/README.md',
  'docs/STRUCTURE.md',
  'docs/CONTINUATION_CONTEXT.md',
  'docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md',
  'docs/engine/architecture',
  'docs/guides/roadmap',
  'docs/guides/optimization',
  'docs/guides/methodology',
  'docs/testing',
  'docs/build/BUILD_AND_RUN.md',
  'docs/debugging',
  'docs/emulation',
  'docs/reference/README.md',
  'docs/tools/README.md',
  'docs/demos/README.md',
  'docs/engine/c-engine',
  'docs/legacy',
];
const SKIP = new Set(['.git', 'node_modules', 'dist', 'out', 'obj', '__pycache__', '.vscode', 'build', 'assets', 'legacy']);
const GENERATED = new Set(['out', 'obj', 'dist']);
const EXT = new Set(['.md', '.markdown']);

const args = process.argv.slice(2);

if (args.includes('--help') || args.includes('-h')) {
  console.log(`Comprueba que los enlaces relativos de la documentación (Markdown) apunten a
ficheros o directorios existentes.

Uso: node tools/check/links.mjs [raíces...] [--quiet] [--json] [--help]

  raíces   ficheros o directorios Markdown a revisar. Por defecto, la
           documentación canónica (AGENTS.md, router, arquitectura, guías,
           testing, build, debugging, emulación e índices).
  --quiet            imprime solo el resumen y los fallos.
  --json             imprime el resultado como JSON.
  --update-baseline  regenera tools/check/links-baseline.txt con los enlaces rotos
                     actuales y sale 0 (acepta la deuda historica existente).
  --help             muestra esta ayuda.

Los enlaces rotos listados en tools/check/links-baseline.txt (p. ej. de
docs/engine/c-engine o docs/legacy, que apuntan al repo de origen) se aceptan
como deuda conocida; el check solo falla ante rotura NUEVA.

No se comprueban: enlaces absolutos (http/https/mailto), anclas puras (#...),
rutas absolutas de Windows, el manual OCR (*.cat.md) y destinos bajo
directorios generados (out/, obj/, dist/).

Salida: 0 si no hay enlaces rotos; 1 si hay alguno.`);
  process.exit(0);
}

const quiet = args.includes('--quiet');
const json = args.includes('--json');
const updateBaseline = args.includes('--update-baseline');
const roots = args.filter((a) => !a.startsWith('-'));
const ROOTS = roots.length ? roots : DEFAULT_ROOTS;

const BASELINE_PATH = path.join(__dirname, 'links-baseline.txt');
const baselineKey = (b) => `${b.file}|${b.target}`;
function readBaseline() {
  const set = new Set();
  if (!fs.existsSync(BASELINE_PATH)) return set;
  for (const line of fs.readFileSync(BASELINE_PATH, 'utf8').split(/\r?\n/)) {
    const s = line.trim();
    if (s && !s.startsWith('#')) set.add(s);
  }
  return set;
}

function walk(target, out = []) {
  const p = path.isAbsolute(target) ? target : path.join(ROOT, target);
  if (!fs.existsSync(p)) return out;
  const st = fs.statSync(p);
  if (st.isFile()) {
    if (EXT.has(path.extname(p).toLowerCase())) out.push(p);
    return out;
  }
  for (const e of fs.readdirSync(p, { withFileTypes: true })) {
    if (SKIP.has(e.name)) continue;
    const child = path.join(p, e.name);
    if (e.isDirectory()) walk(child, out);
    else if (EXT.has(path.extname(e.name).toLowerCase())) out.push(child);
  }
  return out;
}

const isExternal = (t) =>
  /^[a-z][a-z0-9+.-]*:/i.test(t) || t.startsWith('//') || t.startsWith('#') || t.startsWith('/');

const inGenerated = (p) => {
  const rel = path.relative(ROOT, p).split(/[\\/]/);
  return rel.some((seg) => GENERATED.has(seg));
};

function normalizeTarget(raw) {
  let t = raw.trim();
  if (t.startsWith('<')) {
    const end = t.indexOf('>');
    if (end >= 0) t = t.slice(1, end);
  } else {
    t = t.split(/\s+/)[0];
  }
  const hash = t.indexOf('#');
  if (hash >= 0) t = t.slice(0, hash);
  try { t = decodeURIComponent(t); } catch { /* dejar tal cual */ }
  return t;
}

// Manuales ingeridos (OCR) y grandes: sus enlaces internos no son documentacion
// mantenida del repo.
const SKIP_FILE = /\.cat\.md$/i;

const broken = [];
const files = ROOTS.flatMap((r) => walk(r));
const seen = new Set();

for (const file of files) {
  if (seen.has(file) || SKIP_FILE.test(file)) continue;
  seen.add(file);
  const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/);
  let fence = null;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const fenceMatch = line.match(/^\s*(```+|~~~+)/);
    if (fenceMatch) {
      fence = fence ? null : fenceMatch[1][0].repeat(3);
      continue;
    }
    if (fence) continue;
    const clean = line.replace(/`[^`]*`/g, '');
    const targets = [];
    const inline = /!?\[[^\]]*\]\(\s*(<[^>]+>|[^)\s]+)/g;
    let m;
    while ((m = inline.exec(clean)) !== null) targets.push(m[1]);
    const refDef = clean.match(/^\s{0,3}\[[^\]]+\]:\s*(\S+)/);
    if (refDef) targets.push(refDef[1]);
    for (const raw of targets) {
      if (!raw || isExternal(raw) || /^[a-zA-Z]:[\\/]/.test(raw)) continue;
      const rel = normalizeTarget(raw);
      if (!rel) continue;
      const resolved = path.resolve(path.dirname(file), rel);
      if (inGenerated(resolved)) continue;
      if (fs.existsSync(resolved)) continue;
      if (rel.endsWith('/') && fs.existsSync(resolved)) continue;
      broken.push({
        file: path.relative(ROOT, file).split(path.sep).join('/'),
        line: i + 1,
        target: raw.trim(),
      });
    }
  }
}

if (updateBaseline) {
  const keys = [...new Set(broken.map(baselineKey))].sort();
  const header = '# Enlaces rotos conocidos (deuda historica aceptada).\n'
    + '# Formato: ruta-relativa|destino. Regenerar con: node tools/check/links.mjs --update-baseline\n'
    + '# Si solo tiene esta cabecera, no hay deuda pendiente.\n';
  fs.writeFileSync(BASELINE_PATH, header + keys.join('\n') + (keys.length ? '\n' : ''), 'utf8');
  console.log(`[links] baseline actualizado: ${keys.length} enlace(s) conocido(s) en ${path.relative(ROOT, BASELINE_PATH).split(path.sep).join('/')}.`);
  process.exit(0);
}

const baseline = readBaseline();
const known = broken.filter((b) => baseline.has(baselineKey(b)));
const fresh = broken.filter((b) => !baseline.has(baselineKey(b)));

if (json) {
  console.log(JSON.stringify({ checked: seen.size, baselineKnown: known.length, broken: fresh, known }, null, 2));
} else if (fresh.length === 0) {
  if (!quiet) {
    const extra = known.length ? `; ${known.length} enlace(s) roto(s) conocidos (baseline)` : '';
    console.log(`[links] OK: ${seen.size} documento(s), 0 enlaces rotos nuevos${extra}.`);
  }
} else {
  console.error(`[links] ${fresh.length} enlace(s) roto(s) NUEVOS (no en el baseline) en ${seen.size} documento(s):`);
  for (const b of fresh) console.error(`  ${b.file}:${b.line}: ${b.target}`);
  if (known.length) console.error(`  (${known.length} enlace(s) roto(s) conocidos por baseline omitidos)`);
}
process.exit(fresh.length === 0 ? 0 : 1);
