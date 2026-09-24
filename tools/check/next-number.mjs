// Siguiente numero libre dentro de un AMBITO (demos o tests host).
//
// El numero de una demo/test es unico **dentro de su ambito** (el directorio que contiene
// las demos: `demos/techniques/<familia>/<categoria>`, `demos/features/<feature>/<plataforma>`,
// o `tests/host/<categoria>`); ver `docs/ai-dev-environment/NUMBERING.md`.
//
// Uso:
//   node tools/check/next-number.mjs demos/techniques/amiga/copper
//   node tools/check/next-number.mjs tests/host/graphics
//
// Imprime el siguiente numero libre = (maximo usado en el ambito) + 1.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const scope = process.argv[2];

if (!scope) {
	console.error('Uso: node tools/check/next-number.mjs <ambito>');
	console.error('  p. ej. demos/techniques/amiga/copper | demos/features/ui/amiga | tests/host/core');
	process.exit(2);
}

const dir = path.join(ROOT, scope);
if (!fs.existsSync(dir) || !fs.statSync(dir).isDirectory()) {
	console.error(`No existe el ambito: ${scope}`);
	process.exit(2);
}

let max = -1;
for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
	if (!e.isDirectory()) continue;
	const m = /^(\d{3})_/.exec(e.name);
	if (m) max = Math.max(max, Number(m[1]));
}
const next = max + 1;
if (next > 999) {
	console.error(`[next-number] ${scope}: ambito agotado (999)`);
	process.exit(1);
}
console.log(`[next-number] ${scope}: siguiente libre = ${String(next).padStart(3, '0')}`);
