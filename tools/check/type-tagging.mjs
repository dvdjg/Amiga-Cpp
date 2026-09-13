import fs from 'node:fs';
import path from 'node:path';

/// Comprueba la regla "el campo nace etiquetado" (INTERNAL_TYPE_SYSTEM.md §1):
/// un campo `MemoryBlock m_x` que luego se convierte (`.buffer<`/`.view<`/`.block<`
/// o `static_cast<T*>(m_x.data)`) deberia ser `eng::Block<Tag>` desde el origen.
///
/// Uso: node tools/check/type-tagging.mjs [raices...]
/// Salida: lista de infracciones; exit 1 si hay alguna no excepcionada.

const ROOTS = process.argv.slice(2).length
  ? process.argv.slice(2)
  : ['engine/include', 'engine/src', 'demos'];

// Excepciones documentadas (fragmento-de-ruta, campo): reserva cruda justificada.
// Vacio: todos los dueños guardan ya `Block<Tag>` desde el origen. Si aparece una
// excepcion nueva, anadirla aqui con su motivo.
const ALLOW = [];

function isAllowed(file, name) {
  const norm = file.replace(/\\/g, '/');
  return ALLOW.some((a) => {
    const idx = a.lastIndexOf(':');
    return a.slice(idx + 1) === name && norm.includes(a.slice(0, idx));
  });
}

function walk(dir, out = []) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else if (/\.(cpp|hpp)$/.test(e.name)) out.push(p);
  }
  return out;
}

const FIELD = /MemoryBlock\s+m_(\w+)/g;
const violations = [];

for (const root of ROOTS) {
  if (!fs.existsSync(root)) continue;
  for (const file of walk(root)) {
    const text = fs.readFileSync(file, 'utf8');
    const names = new Set();
    for (const m of text.matchAll(FIELD)) names.add(m[1]);
    if (!names.size) continue;
    for (const name of names) {
      if (isAllowed(file, name)) continue;
      const conv = new RegExp(
        `m_${name}\\.(buffer|view|block)<|static_cast<[^>]*\\*>\\s*\\(\\s*m_${name}\\.data`
      );
      if (!conv.test(text)) continue;
      violations.push(`${file}: m_${name} se convierte (deberia nacer como eng::Block<Tag>)`);
    }
  }
}

if (violations.length === 0) {
  console.log('[type-tagging] OK: ningun MemoryBlock crudo convertido (fuera de excepciones).');
  process.exit(0);
}
console.error(`[type-tagging] ${violations.length} infraccion(es):`);
for (const v of violations) console.error('  ' + v);
process.exit(1);
