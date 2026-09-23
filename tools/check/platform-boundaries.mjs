// Comprueba la FRONTERA DOMINIO ↔ PLATAFORMA del engine (modelo de tres anillos,
// docs/engine/architecture/PLATFORM_LAYERS.md):
//
//   1. Ninguna cabecera del engine FUERA de `eng/platform/` puede `#include`
//      `eng/platform/<familia>/…` (el vocabulario de chipset es del anillo 1, no del
//      dominio). El contrato `eng/platform/backend.hpp` SÍ se permite (es la interfaz).
//   2. Las cabeceras del DOMINIO PURO (core, ai, sim, board, cards, ui, os, res,
//      parallel, task, debug, hw, input, audio, memory) no pueden referenciar registros
//      custom (`0xdff…`/`$dff…`) en código.
//
// `cpu/` (m68k) y `retro/` quedan exentos (CPU/vocabulario retro compartido). `field/`,
// `graphics/` y `scene/` son la capa de display (hardware-adyacente): se comprueban para
// el punto 1 pero no para el 2. La deuda histórica se acepta en
// `platform-boundaries-baseline.txt` (una ruta por línea, relativa a `engine/include/eng`).
//
// Uso: node tools/check/platform-boundaries.mjs [--quiet]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const ENG = path.join(ROOT, 'engine/include/eng');

const QUIET = process.argv.includes('--quiet');

// Anillo 1 (vocabulario de chipset): fuera del escaneo del punto 1.
const VOCABULARY_DIR = /^platform\//;
// Cabecera de contrato de backend permitida desde el dominio.
const ALLOWED_INCLUDE = /^eng\/platform\/backend\.hpp$/;
// Dominio puro (punto 2: sin registros custom).
const PURE_DOMAIN = /^(core|ai|sim|board|cards|ui|os|res|parallel|task|debug|hw|input|audio|memory)\//;

const INCLUDE_RE = /#\s*include\s*[<"]([^>"]+)[>"]/;
// Registros custom: `0xdff000`, `$dff0a0`, `DFF000` (en código, no en comentario).
const REGISTER_RE = /0x[dD][fF][fF]|\$[dD][fF][fF]|\bDFF000\b/;

const baselinePath = path.join(__dirname, 'platform-boundaries-baseline.txt');
const baseline = new Set(
	(fs.existsSync(baselinePath) ? fs.readFileSync(baselinePath, 'utf8') : '')
		.split(/\r?\n/)
		.map((l) => l.trim())
		.filter((l) => l && !l.startsWith('#')),
);

function walk(dir) {
	const out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) out.push(...walk(p));
		else if (e.name.endsWith('.hpp')) out.push(p);
	}
	return out;
}

const problems = [];
const exempted = [];
for (const abs of walk(ENG)) {
	const rel = path.relative(ENG, abs).replace(/\\/g, '/');
	const lines = fs.readFileSync(abs, 'utf8').split(/\r?\n/);
	const isVocabulary = VOCABULARY_DIR.test(rel);
	const isPure = PURE_DOMAIN.test(rel);

	lines.forEach((line, i) => {
		const t = line.trim();
		if (t.startsWith('//') || t.startsWith('*') || t.startsWith('/*')) return;
		const code = line.replace(/"[^"]*"/g, '""');

		// Punto 1: include de plataforma desde fuera del vocabulario.
		if (!isVocabulary) {
			const m = INCLUDE_RE.exec(code);
			if (m && /^eng\/platform\//.test(m[1]) && !ALLOWED_INCLUDE.test(m[1])) {
				if (!baseline.has(rel)) {
					problems.push(`eng/${rel}:${i + 1}: dominio incluye vocabulario de plataforma '${m[1]}'`);
				} else exempted.push(rel);
			}
		}

		// Punto 2: registro custom en dominio puro.
		if (isPure && REGISTER_RE.test(code)) {
			if (!baseline.has(rel)) {
				problems.push(`eng/${rel}:${i + 1}: dominio puro referencia registro custom`);
			} else exempted.push(rel);
		}
	});
}

if (!QUIET) for (const f of new Set(exempted)) console.log(`[platform-boundaries] aviso: ${f} (baseline)`);
if (problems.length) {
	for (const p of problems) console.error(`[platform-boundaries] FAIL: ${p}`);
	console.error(`[platform-boundaries] ${problems.length} violación(es) de frontera dominio↔plataforma.`);
	process.exit(1);
}
if (!QUIET) console.log('[platform-boundaries] OK: el dominio no toca el vocabulario de chipset.');
