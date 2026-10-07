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
// Además (aviso no bloqueante) lista las cabeceras con TIPOS CRUDOS en contexto genérico
// (`ct_array<u16>`, `class T = s32`, `using X = u8`): deuda visible sin romper CI. Plan de
// ampliación a gate con baseline: docs/guides/roadmap/ROADMAP_GENERICIDAD_PLANTILLAS.md (F4).
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
// (`scalar`, `scalar_fwd`). Ver AGENTS §1.11.
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
	// (§1.11: el escalar lo elige el consumidor; el algoritmo usa solo el vocabulario genérico.)
	/#\s*include\s*[<"]eng\/(core\/(fixed|minifloat)(_math)?\.hpp|retro\/)/,
];

// Tipos crudos que el gate AÚN no bloquea (solo avisa; ampliación a gate con baseline pendiente,
// ver docs/guides/roadmap/ROADMAP_GENERICIDAD_PLANTILLAS.md F4): patrones de alta señal donde el
// tipo crudo ata una plantilla genérica. Un tipo de dominio (`u8` de un píxel, `u16` de un
// registro) en una cabecera de dominio es correcto y no se persigue con esto.
const RAW_PATTERNS = [
	{ name: 'ct_array<T> con tipo crudo', re: /\bct_array<\s*(eng::)?(s8|s16|s32|s64|u8|u16|u32|u64|float|double)\b/ },
	{ name: 'parámetro de tipo con defecto crudo', re: /\b(class|typename)\s+\w+\s*=\s*(eng::)?(s8|s16|s32|s64|u8|u16|u32|u64|float|double)\b/ },
	{ name: 'alias interno a tipo crudo', re: /\busing\s+\w+\s*=\s*(eng::)?(s8|s16|s32|s64|u8|u16|u32|u64|float|double)\b/ },
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
/// Deuda visible (no bloqueante): líneas con tipo crudo en contexto genérico, por fichero.
const rawCounts = new Map();
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
		if (RAW_PATTERNS.some((p) => p.re.test(code))) {
			rawCounts.set(rel, (rawCounts.get(rel) ?? 0) + 1);
		}
	});
	if (hit && baseline.has(rel)) exempted.push(rel);
}

if (!QUIET) for (const f of exempted) console.log(`[generic-headers] aviso: ${f} (baseline; pendiente de reubicar)`);
if (!QUIET && rawCounts.size > 0) {
	const total = [...rawCounts.values()].reduce((a, b) => a + b, 0);
	console.log(
		`[generic-headers] aviso: ${total} uso(s) de tipo crudo en contexto genérico en ${rawCounts.size} cabecera(s) (deuda visible; plan en docs/guides/roadmap/ROADMAP_GENERICIDAD_PLANTILLAS.md):`,
	);
	for (const [f, n] of [...rawCounts.entries()].sort((a, b) => b[1] - a[1]).slice(0, 20)) {
		console.log(`[generic-headers]   ${f}: ${n}`);
	}
}
if (problems.length) {
	for (const p of problems) console.error(`[generic-headers] FAIL: ${p}`);
	console.error(`[generic-headers] ${problems.length} cabecera(s) genérica(s) con tipo concreto.`);
	process.exit(1);
}
if (!QUIET) console.log('[generic-headers] OK: cabeceras genéricas sin tipos concretos (baseline aparte).');
