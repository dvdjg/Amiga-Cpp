// ---------------------------------------------------------------------------
// Gate `demo-ergonomics`: las demos/juegos nuevos no arrastran plomeria de
// plataforma ni numeros magicos en la fachada de juego. Reglas en
// `docs/engine/architecture/API_ERGONOMICS.md`:
//
//   - no `#include "support/..."` (p. ej. `gcc8_c_support.h`);
//   - no declarar `SysBase` ni `g_eng_run_status` (los pone el engine:
//     `amiga_boot.cpp` debil + `run_demo`);
//   - no `app.run(0x...)` (por defecto es indefinido) ni `mark_failed`/`mark_ready`
//     con literales hexadecimales (usar `BootFail`/constantes con nombre).
//
// El bootstrap canonico es `eng::amiga::run_demo(demo, display)` con el display
// declarado imperativamente en `main` (`GameDisplay`), y el HUD con
// `eng::debug::draw_hud`.
//
// Baseline: demos historicas (solo puede encoger). Uso:
//   node tools/check/demo-ergonomics.mjs [--list|--update-baseline]
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const BASELINE_PATH = path.join(ROOT, 'tools/check/demo-ergonomics-baseline.txt');
const SCAN_DIRS = ['demos', 'games'];

const RULES = [
	{
		id: 'support-header',
		re: /#\s*include\s*[<"]support\//,
		why: 'incluye cabecera de soporte de plataforma; usa `<eng/api/api.hpp>` + `<eng/platform/amiga/entry.hpp>`',
	},
	{
		id: 'sysbase',
		re: /\bSysBase\b/,
		why: 'nombra `SysBase`; lo inicializa `engine/src/platform/amiga/amiga_boot.cpp`',
	},
	{
		id: 'run-status',
		re: /RunStatus\s+g_eng_run_status/,
		why: 'define `g_eng_run_status`; lo define el engine (debil) y lo lee el bootstrap',
	},
	{
		id: 'hex-run',
		re: /\brun\s*\(\s*0x/,
		why: '`run(0x...)`: por defecto `run()` ya es indefinido',
	},
	{
		id: 'hex-mark',
		re: /mark_(?:failed|ready)\s*\([^)]*0x/,
		why: 'literal hexadecimal en `mark_*`; usa `eng::debug::BootFail` o una constante con nombre',
	},
	{
		id: 'hud-manual-clear',
		re: /hud_surface\(\)[\s\S]{0,200}?fill_(?:rect|span)\([^;]*?,\s*0[uU]?\s*\)/,
		why: 'el HUD ya nace limpio en `CanvasPlayfield::begin`/`XlimitedScene::begin`; no limpies Chip RAM a mano con `fill_rect(0, 0, …, 0)`',
	},
];

function listSources(dir) {
	const out = [];
	const walk = (d) => {
		let entries;
		try {
			entries = fs.readdirSync(d, { withFileTypes: true });
		} catch {
			return;
		}
		for (const e of entries) {
			const p = path.join(d, e.name);
			if (e.isDirectory()) {
				if (e.name === 'out' || e.name === 'vision' || e.name === 'data') continue;
				walk(p);
			} else if (/\.(cpp|hpp|h)$/.test(e.name)) {
				out.push(p);
			}
		}
	};
	walk(dir);
	return out;
}

/// Directorio de la demo/juego que contiene `src/` (una entrada por app).
function findApps() {
	const out = [];
	const walk = (dir) => {
		let entries;
		try {
			entries = fs.readdirSync(dir, { withFileTypes: true });
		} catch {
			return;
		}
		for (const e of entries) {
			if (!e.isDirectory()) continue;
			const p = path.join(dir, e.name);
			if (/^\d{3}_/.test(e.name) && fs.existsSync(path.join(p, 'src'))) {
				out.push(p);
			}
			walk(p);
		}
	};
	for (const top of SCAN_DIRS) walk(path.join(ROOT, top));
	return out;
}

/// Quita comentarios C/C++ (las menciones en un comentario didáctico no son una violación).
function stripComments(s) {
	return s.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ');
}

const rel = (p) => path.relative(ROOT, p).replace(/\\/g, '/');
const apps = findApps().sort();
const offenders = [];
for (const app of apps) {
	const hits = [];
	for (const file of listSources(path.join(app, 'src'))) {
		let text;
		try {
			text = stripComments(fs.readFileSync(file, 'utf8'));
		} catch {
			continue;
		}
		for (const rule of RULES) {
			if (rule.re.test(text)) {
				hits.push({ rule: rule.id, file: rel(file) });
			}
		}
	}
	if (hits.length) offenders.push({ app: rel(app), hits });
}

const args = process.argv.slice(2);
if (args.includes('--list')) {
	for (const o of offenders) {
		const rules = [...new Set(o.hits.map((h) => h.rule))].join(', ');
		console.log(`${o.app} :: ${rules}`);
	}
	console.log(`[demo-ergonomics] ${offenders.length}/${apps.length} apps con reglas historicas.`);
	process.exit(0);
}
if (args.includes('--update-baseline')) {
	const lines = [
		'# Baseline del gate demo-ergonomics (ver tools/check/demo-ergonomics.mjs):',
		'# apps historicas con plomeria de plataforma o numeros magicos. NO-REINCIDENCIA:',
		'# migrar a `run_demo` + `draw_hud` y borrar la linea. Solo puede encoger.',
	];
	for (const o of offenders) lines.push(o.app);
	fs.writeFileSync(BASELINE_PATH, lines.join('\n') + '\n');
	console.log(`[demo-ergonomics] baseline actualizado (${offenders.length} apps)`);
	process.exit(0);
}

const baseline = fs.existsSync(BASELINE_PATH)
	? new Set(fs.readFileSync(BASELINE_PATH, 'utf8').split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#')))
	: new Set();
const violations = offenders.filter((o) => !baseline.has(o.app));
if (violations.length) {
	for (const v of violations) {
		for (const h of v.hits) {
			console.error(`[demo-ergonomics] FALLO: ${h.file} usa "${h.rule}" (${RULES.find((r) => r.id === h.rule).why})`);
		}
	}
	console.error(`[demo-ergonomics] ${violations.length} app(s) nueva(s) con plomeria/numeros magicos. Ver API_ERGONOMICS.md.`);
	process.exit(1);
}
console.log(`[demo-ergonomics] OK: ${apps.length} apps (${offenders.length} historicas en baseline).`);
