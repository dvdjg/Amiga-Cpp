// Comprueba que los archivos de texto del repo sean UTF-8 valido, sin BOM y sin
// mojibake (UTF-8 doblemente codificado) ni caracter de reemplazo. Falla (exit 1)
// si encuentra alguno. Pensado para el CI (regresion / tests host).
//
// Uso: node tools/check/encoding.mjs [raices...]
import fs from 'node:fs';
import path from 'node:path';

const ROOTS = process.argv.slice(2).length
  ? process.argv.slice(2)
  : ['engine', 'demos', 'tests', 'tools', 'docs', 'host-tools', 'games', 'artifacts'];
const SKIP = new Set(['.git', 'node_modules', 'dist', 'out', 'obj', '__pycache__', '.vscode', 'build', 'assets', 'legacy']);
const EXT = new Set(['.cpp', '.hpp', '.h', '.c', '.md', '.sh', '.mjs', '.ts', '.js', '.json', '.txt', '.tsx', '.py', '.asm', '.s', '.inc']);
const MOJI = /\u00C3[\u0080-\u00BF]|\u00C2[\u0080-\u00BF]|\u00E2\u0080|\uFFFD/;
const utf8 = new TextDecoder('utf-8', { fatal: true });

function walk(dir, out = []) {
  if (!fs.existsSync(dir)) return out;
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (SKIP.has(e.name)) continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else if (EXT.has(path.extname(e.name).toLowerCase())) out.push(p);
  }
  return out;
}

const bad = [];
for (const root of ROOTS) {
  for (const f of walk(root)) {
    const buf = fs.readFileSync(f);
    if (buf.length >= 3 && buf[0] === 0xEF && buf[1] === 0xBB && buf[2] === 0xBF) {
      bad.push(`${f}: BOM UTF-8 (no usar)`);
      continue;
    }
    try { utf8.decode(buf); } catch { bad.push(`${f}: no es UTF-8 valido`); continue; }
    if (MOJI.test(buf.toString('utf8'))) bad.push(`${f}: mojibake (UTF-8 doblemente codificado)`);
  }
}

if (bad.length === 0) {
  console.log('[encoding] OK: todo UTF-8 valido y sin mojibake.');
  process.exit(0);
}
console.error(`[encoding] ${bad.length} archivo(s) con problema:`);
for (const b of bad) console.error('  ' + b);
process.exit(1);
