// ---------------------------------------------------------------------------
// Gate `demo-docs`: por cada demo (demos/**, con fuentes cpp) deben existir, en
// el MISMO directorio que los fuentes:
//
//   src/INTENT.md  — qué hace la demo, qué pretende demostrar, qué elementos hay
//                    en pantalla y cómo evolucionan en el tiempo (secciones
//                    obligatorias; contenido mínimo).
//   src/VISION.md  — la evaluación del modelo de visión, generada con el prompt
//                    CANÓNICO (`tools/analyze/vision-prompt.txt`, idéntico para
//                    todas las demos y SIN pistas). Se verifica: marcador de
//                    generación, prompt verbatim (normalizado) y respuesta.
//
// Las preguntas concretas por entidad son para depurar (vision-run.mjs); la
// evaluación final no lleva pistas — por eso el gate compara el prompt con la
// fuente única.
//
// Baseline: demos históricas (solo puede encoger). Uso:
//   node tools/check/demo-docs.mjs [--list|--update-baseline]
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const CANONICAL = fs.readFileSync(path.join(ROOT, 'tools/analyze/vision-prompt.txt'), 'utf8').trim();
const BASELINE_PATH = path.join(ROOT, 'tools/check/demo-docs-baseline.txt');
const norm = (s) => s.replace(/\s+/g, ' ').trim();

const INTENT_SECTIONS = [
	'## Qué hace',
	'## Qué pretende demostrar',
	'## Elementos en pantalla',
	'## Evolución temporal',
];
const INTENT_MIN_BYTES = 600;

/// Directorios de demo: demos/<...>/NNN_tema con src/*.cpp.
function findDemos() {
	const out = [];
	const walk = (dir) => {
		let entries;
		try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
		for (const e of entries) {
			if (!e.isDirectory()) continue;
			const p = path.join(dir, e.name);
			if (/^\d{3}_/.test(e.name)) {
				const src = path.join(p, 'src');
				if (fs.existsSync(src) && fs.readdirSync(src).some((f) => f.endsWith('.cpp'))) {
					out.push(p);
				}
			}
			walk(p);
		}
	};
	walk(path.join(ROOT, 'demos'));
	return out;
}

const rel = (p) => path.relative(ROOT, p).replace(/\\/g, '/');

/// Extrae el texto entre el primer bloque ```text ... ``` tras `## Prompt`.
function promptBlock(vision) {
	const i = vision.indexOf('## Prompt');
	if (i < 0) return null;
	const a = vision.indexOf('```text', i);
	if (a < 0) return null;
	const b = vision.indexOf('```', a + 7);
	if (b < 0) return null;
	return vision.slice(a + 7, b);
}

function checkIntent(dir) {
	const problems = [];
	const p = path.join(dir, 'src', 'INTENT.md');
	if (!fs.existsSync(p)) {
		problems.push(`${rel(p)}: no existe (describe qué hace/demuestra la demo, elementos en pantalla y evolución)`);
		return problems;
	}
	const s = fs.readFileSync(p, 'utf8');
	for (const sec of INTENT_SECTIONS) {
		if (!s.includes(sec)) problems.push(`${rel(p)}: falta la sección "${sec}"`);
	}
	if (Buffer.byteLength(s, 'utf8') < INTENT_MIN_BYTES) {
		problems.push(`${rel(p)}: contenido insuficiente (< ${INTENT_MIN_BYTES} bytes)`);
	}
	return problems;
}

function checkVision(dir) {
	const problems = [];
	const p = path.join(dir, 'src', 'VISION.md');
	if (!fs.existsSync(p)) {
		problems.push(`${rel(p)}: no existe (evalúa con tools/analyze/vision-final.mjs, prompt canónico)`);
		return problems;
	}
	const s = fs.readFileSync(p, 'utf8');
	if (!s.includes('generado por tools/analyze/vision-final.mjs')) {
		problems.push(`${rel(p)}: falta el marcador del generador (usa vision-final.mjs)`);
	}
	if (!/- Modelo: `/.test(s)) problems.push(`${rel(p)}: falta la línea "- Modelo:"`);
	const block = promptBlock(s);
	if (block === null) {
		problems.push(`${rel(p)}: falta el bloque "## Prompt (canónico...)" con \`\`\`text`);
	} else if (norm(block) !== norm(CANONICAL)) {
		problems.push(`${rel(p)}: el prompt NO coincide con tools/analyze/vision-prompt.txt (sin pistas: idéntico para todas)`);
	}
	const i = s.indexOf('## Respuesta');
	if (i < 0 || s.slice(i + 11).trim().length < 80) {
		problems.push(`${rel(p)}: falta una respuesta sustancial del modelo`);
	}
	return problems;
}

const args = process.argv.slice(2);
const baseline = fs.existsSync(BASELINE_PATH)
	? new Set(fs.readFileSync(BASELINE_PATH, 'utf8').split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#')))
	: new Set();

const demos = findDemos().sort();
const results = [];
for (const dir of demos) {
	const problems = [...checkIntent(dir), ...checkVision(dir)];
	results.push({ demo: rel(dir), problems });
}

if (args.includes('--list')) {
	for (const r of results) {
		console.log(`${r.problems.length === 0 ? 'OK ' : 'MAL'} ${r.demo}` +
			(r.problems.length ? ' :: ' + r.problems[0] : ''));
	}
	process.exit(0);
}
if (args.includes('--update-baseline')) {
	const lines = [
		'# Baseline del gate demo-docs (ver tools/check/demo-docs.mjs): demos sin INTENT.md/VISION.md.',
		'# NO-REINCIDENCIA: solo puede encoger (rellenar los documentos y borrar la línea).',
	];
	for (const r of results) if (r.problems.length) lines.push(r.demo);
	fs.writeFileSync(BASELINE_PATH, lines.join('\n') + '\n');
	console.log(`[demo-docs] baseline actualizado (${results.filter((r) => r.problems.length).length} entradas)`);
	process.exit(0);
}

const violations = results.filter((r) => r.problems.length && !baseline.has(r.demo));
if (violations.length) {
	for (const v of violations) {
		for (const p of v.problems) console.error(`[demo-docs] FALLO: ${p}`);
	}
	console.error(`[demo-docs] ${violations.length} demo(s) sin la documentación de intención/visión.`);
	process.exit(1);
}
console.log(`[demo-docs] OK: ${demos.length} demos, documentación de intención/visión presente (o en baseline).`);
