// Comprueba que los enlaces relativos de la documentacion (Markdown) apunten a
// ficheros o directorios existentes. Falla (exit 1) si encuentra un enlace roto.
// Pensado para el CI (regresion / tests host) y para no dejar referencias rotas
// al mover o renombrar documentos.
//
// Uso: node tools/check/links.mjs [raices...] [--quiet] [--json]
//   raices   ficheros/dirs Markdown a revisar (por defecto, la documentacion canonica)
//   --quiet  solo resumen y fallos
//   --json   imprime el resultado como JSON
//
// Por defecto solo se revisa la documentacion **mantenida** (router, arquitectura,
// guias, testing, build, emulacion e indices). Los arboles historicos/importados
// (docs/engine/c-engine, docs/legacy, docs/debugging, docs/emulation/*.md importados)
// conservan enlaces al repo de origen y se excluyen para no fallar por deuda ajena;
// para revisarlos, pasar la raiz explicitamente.
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
  'docs/guides/methodology/README.md',
  'docs/guides/methodology/DEMO_VISUAL_DEBUG.md',
  'docs/testing',
  'docs/build/BUILD_AND_RUN.md',
  'docs/emulation/README.md',
  'docs/reference/README.md',
  'docs/tools/README.md',
  'docs/demos/README.md',
];
const SKIP = new Set(['.git', 'node_modules', 'dist', 'out', 'obj', '__pycache__', '.vscode', 'build', 'assets', 'legacy']);
const GENERATED = new Set(['out', 'obj', 'dist']);
const EXT = new Set(['.md', '.markdown']);

const args = process.argv.slice(2);
const quiet = args.includes('--quiet');
const json = args.includes('--json');
const roots = args.filter((a) => !a.startsWith('--'));
const ROOTS = roots.length ? roots : DEFAULT_ROOTS;

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

if (json) {
  console.log(JSON.stringify({ checked: seen.size, broken }, null, 2));
} else if (broken.length === 0) {
  console.log(`[links] OK: ${seen.size} documento(s), sin enlaces relativos rotos.`);
} else {
  console.error(`[links] ${broken.length} enlace(s) roto(s) en ${seen.size} documento(s):`);
  for (const b of broken) console.error(`  ${b.file}:${b.line}: ${b.target}`);
}
process.exit(broken.length === 0 ? 0 : 1);
