// Comprueba la "regla de oro" de diseño: las cabeceras GENÉRICAS del engine no deben fijar
// un escalar concreto. Sólo las cabeceras de una implementación concreta (`retro/`,
// `platform/`, `cpu/`) y las del propio escalar (`fixed*.hpp`, `minifloat*.hpp`) pueden
// nombrar representaciones concretas (`Fixed<s16,s32>`, `q0/q8/q12/q24`, `MiniFloat16`) ni
// incluir el soporte matemático (`fixed_math.hpp`/`minifloat_math.hpp`) o un backend
// (`retro/…`). Los alias locales (`using X = Fixed<…>`) los cubren los patrones de tipo.
//
// Los infractores históricos (mientras se completa la reubicación de las especializaciones)
// se aceptan de forma explícita en `generic-headers-baseline.txt`. Cualquier fichero NUEVO
// con un tipo concreto falla.
//
// Uso: node tools/check/generic-headers.mjs [--quiet]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const ENG = path.join(ROOT, 'engine/include/eng');

const QUIET = process.argv.includes('--quiet');

// Directorios/cabeceras exentas (implementación concreta o del propio escalar). `field/` es
// la capa de dispositivo de display (playfield/surface = hardware Amiga), como `platform/`.
const EXEMPT_DIR = [/(^|\/)(retro|platform|cpu|field)\//];
// Exentas por fichero: el propio escalar (`fixed`/`minifloat` + su `_math`) y las cabeceras cuya
// dependencia del escalar es EXPLÍCITA en el nombre (`fixed_affine`, etc.) o que son de conveniencia
// (`scalar`, `scalar_fwd`). Ver AGENTS §1.10.
const EXEMPT_FILE = [
	/fixed(_math)?\.hpp$/,
	/minifloat(_math)?\.hpp$/,
	/fixed_.*\.hpp$/, // implementación específica de Fixed (nombre lo declara): fixed_affine, …
	/scalar\.hpp$/,
	/scalar_fwd\.hpp$/,
];

// Patrones de tipo concreto (en código, no en comentarios).
const PATTERNS = [
	/Fixed<\s*(eng::)?s(8|16|32|64)/,
	/\bq(0|8|12|24)\b/,
	/\bMiniFloat16\b/,
	// Alias INTERNO a un escalar concreto (`using MF = MiniFloat16;`, `using W = Fixed<WR,…>`):
	// fijar el escalar aunque sea "genérico" en la representación. Se excluye el alias que
	// envuelve `typename …` (p. ej. `using T = Fixed<typename common_repr<A,B>::type,…>`, que es
	// promoción genérica legítima).
	/using\s+\w+\s*=\s*(eng::math::)?(Fixed\s*<(?!\s*typename)|MiniFloat16\b)/,
	// Una cabecera genérica tampoco debe INCLUIR un escalar concreto ni su soporte matemático, ni
	// un backend retro: eso la ata a esa representación (se incluye el escalar, no su formato).
	// (§1.10: el escalar lo elige el consumidor; el algoritmo usa solo el vocabulario genérico.)
	/#\s*include\s*[<"]eng\/(core\/(fixed|minifloat)(_math)?\.hpp|retro\/)/,
];

const baseline = new Set(
	(fs.existsSync(path.join(__dirname, 'generic-headers-baseline.txt'))
		? fs.readFileSync(path.join(__dirname, 'generic-headers-baseline.txt'), 'utf8')
		: ''
	)
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
	if (EXEMPT_DIR.some((r) => r.test(rel))) continue;
	if (EXEMPT_FILE.some((r) => r.test(rel))) continue;
	const lines = fs.readFileSync(abs, 'utf8').split(/\r?\n/);
	let hit = false;
	lines.forEach((line, i) => {
		const t = line.trim();
		if (t.startsWith('//') || t.startsWith('*') || t.startsWith('/*')) return;
		const code = line.replace(/"[^"]*"/g, '""'); // ignora literales de cadena (mensajes)
		if (PATTERNS.some((re) => re.test(code))) {
			hit = true;
			if (!baseline.has(rel)) problems.push(`eng/${rel}:${i + 1}: tipo concreto en cabecera genérica`);
		}
	});
	if (hit && baseline.has(rel)) exempted.push(rel);
}

if (!QUIET) for (const f of exempted) console.log(`[generic-headers] aviso: ${f} (baseline; pendiente de reubicar)`);
if (problems.length) {
	for (const p of problems) console.error(`[generic-headers] FAIL: ${p}`);
	console.error(`[generic-headers] ${problems.length} cabecera(s) genérica(s) con tipo concreto.`);
	process.exit(1);
}
if (!QUIET) console.log('[generic-headers] OK: cabeceras genéricas sin tipos concretos (baseline aparte).');
