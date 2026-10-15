#!/usr/bin/env node
// Auditoría de calidad por demo (recurrente): al ejecutar una demo responde
// «¿se ha empeorado?» y «¿merece la pena?» con señales verificables:
//
//   1. fps medido vs baseline de `docs/guides/roadmap/BITACORA_SCROLL_TILES.md`
//      (la mejor muestra registrada; −10 % = EMPEORADO, mismo criterio que check-fps);
//   2. suelo de fps: < 25 = MEJORAR (la regla del procedimiento); < 10 = ELIMINAR
//      (candidata clara: ninguna técnica por debajo de 10 fps es usable);
//   3. uso de hardware (escaneo estático del fuente): `draw_line(`/`draw_text(`/
//      `set_pixel(` en el camino por frame sin `FramePlan` → raster CPU en el bucle
//      (antipatrón real: 077/084 a ~1,4 fps por líneas CPU punto a punto);
//   4. intención/visión: `--analyze` ejecuta el criterio propio de la demo
//      (analyze-screenshot.sh) sobre su última captura; `--vision` además pregunta a
//      Ollama con el `vision-points.json` de la demo (descripción esperada).
//
// Uso:
//   node tools/debug/audit-demos-quality.mjs --all [--reuse] [--analyze] [--vision] [--json]
//   node tools/debug/audit-demos-quality.mjs <demoId...> [--reuse]
//
//   --reuse   reutiliza `out/fleet-fps/fleet-fps.json` (medido antes) en vez de medir.
//   --analyze ejecuta analyze-demo.sh (captura + criterio de la demo).
//   --vision  pregunta a Ollama por la descripción esperada (vision-points.json).
//
// Salida: out/fleet-quality/quality.json + quality.md (tabla + veredictos).
// La lista canónica de demos a eliminar/mejorar vive en
// `docs/guides/roadmap/DEMOS_A_ELIMINAR_O_MEJORAR.md`.
import * as fs from 'node:fs';
import * as path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const OUT = path.join(ROOT, 'out/fleet-quality');
const FLEET = path.join(ROOT, 'out/fleet-fps/fleet-fps.json');
const BITACORA = path.join(ROOT, 'docs/guides/roadmap/BITACORA_SCROLL_TILES.md');
fs.mkdirSync(OUT, { recursive: true });

const args = process.argv.slice(2);
const REUSE = args.includes('--reuse');
const ANALYZE = args.includes('--analyze');
const VISION = args.includes('--vision');
const JSON_OUT = args.includes('--json');
const ALL = args.includes('--all');
let demos = args.filter((a) => !a.startsWith('--'));

// --- Baseline de fps (misma tabla que check-fps) ----------------------------
function readBaseline() {
	const lines = fs.readFileSync(BITACORA, 'utf8').split(/\r?\n/);
	const headerIdx = lines.findIndex((l) => /^\|\s*Demo\s*\|/.test(l));
	const out = new Map();
	if (headerIdx < 0) return out;
	let sepIdx = headerIdx + 1;
	while (sepIdx < lines.length && !/^\|[\s|:-]+\|$/.test(lines[sepIdx])) sepIdx++;
	for (let i = sepIdx + 1; i < lines.length && lines[i].startsWith('|'); i++) {
		const cells = lines[i].split('|').slice(1, -1).map((c) => c.trim());
		const demo = (cells[0] || '').replace(/`/g, '');
		const fps = Number((cells[2] || '').replace(',', '.'));
		if (demo && Number.isFinite(fps)) out.set(demo, fps);
	}
	return out;
}

// --- Localiza el fuente de una demo por su id (sufijo del directorio) --------
function findDemoSrc(demoId) {
	const base = demoId.replace(/^(engine_amiga_|ui_amiga_|board_chess_amiga_|cards_amiga_|sim_amiga_)/, '');
	try {
		const hit = execFileSync(
			'bash',
			['-c', `find demos -path "*${base}/src/main.cpp" | head -1`],
			{ cwd: ROOT, encoding: 'utf8' },
		).trim();
		return hit ? path.join(ROOT, hit) : null;
	} catch {
		return null;
	}
}

// --- Señales de hardware (escaneo estático, heurísticas documentadas) -------
const HW_RULES = [
	{
		id: 'linea_cpu',
		// Línea dibujada sin FramePlan en el mismo punto → raster CPU (077/084: ~1,4 fps).
		re: /draw_line\(/,
		anti: /(dt\.line\(|&plan|plan,|plan\))/,
		msg: 'draw_line sin FramePlan (raster CPU; las líneas deben ir por Blitter)',
	},
	{
		id: 'texto_cpu',
		re: /draw_text(_literal)?\(/,
		anti: /(dt\.text|plan)/,
		msg: 'draw_text sin FramePlan (texto por CPU; usar el plan/Blitter o sprites)',
	},
];

function scanHardware(srcPath) {
	if (!srcPath || !fs.existsSync(srcPath)) return [];
	const text = fs.readFileSync(srcPath, 'utf8');
	const lines = text.split(/\r?\n/);
	const flags = [];
	for (const rule of HW_RULES) {
		const hits = [];
		lines.forEach((l, i) => {
			if (rule.re.test(l) && !rule.anti.test(l)) hits.push(i + 1);
		});
		if (hits.length > 0) flags.push({ id: rule.id, msg: rule.msg, lines: hits.slice(0, 6) });
	}
	return flags;
}

// --- Medidas (reutiliza el inventario de fps o mide) -------------------------
function loadMeasures() {
	if (REUSE && fs.existsSync(FLEET)) {
		const rows = JSON.parse(fs.readFileSync(FLEET, 'utf8'));
		const m = new Map();
		for (const r of rows) m.set(r.demo, r);
		return m;
	}
	return null;
}

function measureOne(demo) {
	try {
		const out = execFileSync(
			'node',
			[path.join(ROOT, 'tools/debug/measure-fps.mjs'), demo, 'A500_debug', '--json'],
			{ cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] },
		);
		const j = JSON.parse(out.trim().split('\n').pop());
		return { demo, fps: j.emulatedFps, fields: j.fieldsPerFrame, cycles: j.cyclesPerFrame, detail: j.detail };
	} catch {
		return { demo, fps: null, fields: null, cycles: 0, detail: 'FALLO' };
	}
}

// --- Analyze / vision por demo (opt-in) --------------------------------------
function analyzeOne(demoId) {
	const base = demoId.replace(/^(engine_amiga_|ui_amiga_|board_chess_amiga_|cards_amiga_|sim_amiga_)/, '');
	try {
		const dir = execFileSync('bash', ['-c', `find demos -path "*${base}/src/main.cpp" | head -1`],
			{ cwd: ROOT, encoding: 'utf8' }).trim().replace(/\/src\/main\.cpp$/, '');
		if (!dir) return 'no-dir';
		execFileSync('bash', ['tools/analyze/analyze-demo.sh', dir],
			{ cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
		return 'ok';
	} catch {
		return 'fail';
	}
}

function visionOne(demoId) {
	const base = demoId.replace(/^(engine_amiga_|ui_amiga_|board_chess_amiga_|cards_amiga_|sim_amiga_)/, '');
	try {
		const dir = execFileSync('bash', ['-c', `find demos -path "*${base}" -maxdepth 6 -type d | head -1`],
			{ cwd: ROOT, encoding: 'utf8' }).trim();
		const vp = path.join(ROOT, dir, 'vision-points.json');
		if (!dir || !fs.existsSync(vp)) return 'sin-points';
		const shot = path.join(ROOT, 'out/run', demoId, 'A500_debug/screenshot.png');
		if (!fs.existsSync(shot)) return 'sin-captura';
		const prompt = fs.readFileSync(vp, 'utf8');
		const res = execFileSync('node', ['out/run/vision.mjs', '/dev/stdin', shot],
			{ cwd: ROOT, encoding: 'utf8', input: prompt, stdio: ['pipe', 'pipe', 'pipe'] });
		return res.trim().slice(0, 400);
	} catch {
		return 'vision-fallo';
	}
}

// --- Veredicto ---------------------------------------------------------------
function verdictOf(row) {
	const reasons = [];
	let level = 'OK';
	const bump = (l) => {
		if (l === 'ELIMINAR' || level === 'ELIMINAR') level = 'ELIMINAR';
		else if (l === 'MEJORAR' || level === 'MEJORAR') level = 'MEJORAR';
		else if (level === 'SIN MEDIDA') level = l;
	};
	if (row.fps === null || row.fps === 0 || row.detail === 'FALLO') {
		level = 'SIN MEDIDA';
		// `fields` ≈ 1001,6 = el contador de frame quedó congelado durante la ventana:
		// la demo volvió al SO (crash) tras READY (verificado en 221: pc en la ROM con el
		// engine anterior; la captura del arranque sale bien y el crash llega después).
		if (row.fields !== null && row.fields > 1000) {
			reasons.push('contador congelado: vuelve al SO (crash) tras READY');
		} else {
			reasons.push('no se pudo medir (¿no arranca / init lento / falta runner?)');
		}
	} else {
		if (row.fps < 10) {
			bump('ELIMINAR');
			reasons.push(`fps ${row.fps} < 10 (técnica no usable)`);
		} else if (row.fps < 25) {
			bump('MEJORAR');
			reasons.push(`fps ${row.fps} < 25 (suelo del procedimiento)`);
		}
		if (row.baseline && row.fps < row.baseline * 0.9) {
			bump('MEJORAR');
			reasons.push(`empeorado: ${row.fps} vs baseline ${row.baseline} (−10 %)`);
		}
	}
	for (const f of row.hw) {
		bump(row.fps !== null && row.fps < 25 ? 'MEJORAR' : 'MEJORAR');
		reasons.push(`hardware: ${f.msg} (líneas ${f.lines.join(',')})`);
	}
	if (ANALYZE && row.analyze && row.analyze !== 'ok') {
		bump('MEJORAR');
		reasons.push(`analyze: ${row.analyze} (no supera su criterio de captura)`);
	}
	return { level, reasons };
}

// --- Main --------------------------------------------------------------------
const measures = loadMeasures();
if (!measures && demos.length === 0) {
	console.error('[quality] sin --all ni demos: nada que auditar.');
	process.exit(2);
}
if (demos.length === 0 && measures) demos = [...measures.keys()].sort();

const baseline = readBaseline();
const rows = [];
for (const demo of demos) {
	process.stdout.write(`[quality] ${demo} ... `);
	const m = measures?.get(demo) ?? measureOne(demo);
	const src = findDemoSrc(demo);
	const row = {
		demo,
		fps: m.fps,
		fields: m.fields,
		cycles: m.cycles,
		detail: m.detail,
		baseline: baseline.get(demo) ?? null,
		hw: scanHardware(src),
		// Sin fuente en `demos/` = build antigua de una demo retirada (out/demos): no entra
		// en el listado (se limpia con el borrado de su carpeta en out/).
		retired: src === null,
		analyze: ANALYZE ? analyzeOne(demo) : null,
		vision: VISION ? visionOne(demo) : null,
	};
	const v = verdictOf(row);
	row.level = row.retired ? 'RETIRADA' : v.level;
	row.reasons = v.reasons;
	rows.push(row);
	console.log(`${row.level}${row.fps !== null ? ` (${row.fps} fps)` : ''}`);
}

fs.writeFileSync(path.join(OUT, 'quality.json'), JSON.stringify(rows, null, 2), 'utf8');
const md = [
	'| demo | fps | campos | baseline | veredicto | motivos |',
	'|---|---|---|---|---|---|',
	...rows.map(
		(r) =>
			`| ${r.demo} | ${r.fps ?? '-'} | ${r.fields ?? '-'} | ${r.baseline ?? '-'} | ${r.level} | ${r.reasons.join('; ') || '-'} |`,
	),
].join('\n');
fs.writeFileSync(path.join(OUT, 'quality.md'), md + '\n', 'utf8');
const byLevel = {};
for (const r of rows) byLevel[r.level] = (byLevel[r.level] ?? 0) + 1;
if (JSON_OUT) console.log(JSON.stringify(rows));
else {
	console.log(`\n[quality] ${rows.length} demos -> ${JSON.stringify(byLevel)}`);
	console.log(`[quality] tabla en out/fleet-quality/quality.md`);
}
