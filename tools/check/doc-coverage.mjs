// Gate: **toda función** de las cabeceras del engine debe llevar un comentario descriptivo
// antes de su firma (qué hace, quién la usa/contexto, params in/out; ver CODING_STYLE.md
// §Documentación de código). Es una **aproximación** heurística (firmas multilínea,
// operadores agrupados): la deuda actual va en `doc-coverage-baseline.txt` (clave
// `eng/ruta:nombre`); una función NUEVA sin comentario falla.
//
// Uso: node tools/check/doc-coverage.mjs [--update-baseline] [subdir]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const ENG = path.join(ROOT, 'engine/include/eng');
const BASELINE = path.join(__dirname, 'doc-coverage-baseline.txt');
const TEMPLATE_BASELINE = path.join(__dirname, 'doc-template-baseline.txt');
const SUB = process.argv.slice(2).find((a) => !a.startsWith('-')) || '';

const isComment = (l) => /^\s*(\/\/|\*|\/\*)/.test(l);
const isBlank = (l) => l.trim() === '';
const isFuncEnd = (l) => {
	const t = l.trim();
	if (!t.endsWith('{') || !t.includes(')')) return false;
	if (/^(if|for|while|switch|else|do|return|struct|class|namespace|union|enum|template|using|\}|\))/.test(t)) return false;
	return !(t.startsWith('//') || t.startsWith('*') || t.startsWith('#'));
};
const count = (l, ch) => [...l].filter((c) => c === ch).length;
const funcName = (line) => {
	const op = line.match(/\boperator\s*([^\s(]*)\s*\(/);
	if (op) return 'operator' + (op[1] || '');
	const m = line.match(/([A-Za-z_~][A-Za-z0-9_]*)\s*\(/);
	return m ? m[1] : '?';
};

function walk(dir) {
	const out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) out.push(...walk(p));
		else if (e.name.endsWith('.hpp')) out.push(p);
	}
	return out;
}

const missing = [];
for (const abs of walk(ENG)) {
	const rel = path.relative(ENG, abs).replace(/\\/g, '/');
	if (SUB && !rel.startsWith(SUB)) continue;
	const lines = fs.readFileSync(abs, 'utf8').split(/\r?\n/);
	lines.forEach((l, i) => {
		if (!isFuncEnd(l)) return;
		let depth = 0;
		let start = i;
		for (let j = i; j >= 0; --j) {
			depth += count(lines[j], ')') - count(lines[j], '(');
			if (depth <= 0) {
				start = j;
				break;
			}
		}
		const st = lines[start].trim();
		if (/^(if|while|for|switch|else|do|return)\b/.test(st) || /\b(if|while|for|switch)\s*\(/.test(st)) return;
		// El comentario puede describir un BLOQUE de declaraciones hermanas (operadores,
		// sobrecargas). Sube mientras no haya linea en blanco: si aparece un comentario antes
		// del blanco, esta documentado; si el bloque arranca en blanco, no.
		let k = start - 1;
		let steps = 0;
		let documented = false;
		for (; k >= 0 && steps < 24; --k, ++steps) {
			const t = lines[k].trim();
			if (isBlank(t)) break;
			if (isComment(t)) {
				documented = true;
				break;
			}
		}
		if (documented) return;
		missing.push({ key: `eng/${rel}:${funcName(st)}`, detail: `eng/${rel}:${start + 1}: ${st.slice(0, 72)}` });
	});
}
missing.sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0));

// --- Parámetros de plantilla: cada nombre debe aparecer en el bloque de doc que precede
// al `template`. Es una comprobación de **presencia del nombre** (la calidad del contrato
// se revisa); los templates históricos sin esa línea viven en el baseline.
function templateParamsAt(lines, start) {
	let text = '';
	let depth = 0;
	let opened = false;
	for (let j = start; j < lines.length; ++j) {
		const line = lines[j];
		for (const ch of line) {
			if (ch === '<') {
				++depth;
				opened = true;
			} else if (ch === '>') {
				--depth;
			}
			if (opened) text += ch;
		}
		if (opened) text += '\n';
		if (opened && depth <= 0) break;
	}
	const a = text.indexOf('<');
	const b = text.lastIndexOf('>');
	if (a < 0 || b <= a) return [];
	const inside = text.slice(a + 1, b);
	const parts = [];
	let d = 0;
	let cur = '';
	for (const ch of inside) {
		if (ch === '<') ++d;
		else if (ch === '>') --d;
		if (ch === ',' && d === 0) {
			parts.push(cur);
			cur = '';
		} else {
			cur += ch;
		}
	}
	parts.push(cur);
	const names = [];
	for (let p of parts) {
		let dd = 0;
		let cut = -1;
		for (let i = 0; i < p.length; ++i) {
			const ch = p[i];
			if (ch === '<') ++dd;
			else if (ch === '>') --dd;
			else if (ch === '=' && dd === 0) {
				cut = i;
				break;
			}
		}
		if (cut >= 0) p = p.slice(0, cut);
		const ids = [...p.matchAll(/\b[A-Za-z_]\w*\b/g)].map((m) => m[0]);
		if (ids.length) names.push(ids[ids.length - 1]);
	}
	return names;
}

function templateDocAt(lines, start) {
	let j = start - 1;
	const out = [];
	while (j >= 0 && out.length < 30) {
		const t = lines[j].trim();
		if (isBlank(t)) break;
		if (isComment(t)) {
			out.unshift(lines[j]);
			--j;
			continue;
		}
		break;
	}
	return out.join('\n');
}

const templateMissing = [];
for (const abs of walk(ENG)) {
	const rel = path.relative(ENG, abs).replace(/\\/g, '/');
	if (SUB && !rel.startsWith(SUB)) continue;
	const lines = fs.readFileSync(abs, 'utf8').split(/\r?\n/);
	for (let i = 0; i < lines.length; ++i) {
		const t = lines[i].trim();
		if (!t.startsWith('template') || !t.includes('<') || isComment(t)) continue;
		const names = templateParamsAt(lines, i);
		if (names.length === 0) continue;
		const doc = templateDocAt(lines, i);
		for (const name of new Set(names)) {
			const re = new RegExp(`\\b${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`);
			if (!re.test(doc)) {
				templateMissing.push({
					key: `eng/${rel}:template:${name}`,
					detail: `eng/${rel}:${i + 1}: template param ${name} sin mención en la doc`,
				});
			}
		}
	}
}
templateMissing.sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0));

if (process.argv.includes('--update-baseline')) {
	fs.writeFileSync(BASELINE, [...new Set(missing.map((m) => m.key))].join('\n') + '\n', 'utf8');
	fs.writeFileSync(TEMPLATE_BASELINE,
		'# Baseline de parámetros de plantilla sin mención en la doc (ver doc-coverage.mjs).\n' +
		[...new Set(templateMissing.map((m) => m.key))].sort().join('\n') + '\n',
		'utf8');
	console.log(`[doc-coverage] baseline actualizada (${missing.length} funciones, ${templateMissing.length} parámetros de plantilla).`);
	process.exit(0);
}

const baseline = new Set(
	(fs.existsSync(BASELINE) ? fs.readFileSync(BASELINE, 'utf8') : '')
		.split(/\r?\n/).map((l) => l.trim()).filter((l) => l && !l.startsWith('#')),
);
const templateBaseline = new Set(
	(fs.existsSync(TEMPLATE_BASELINE) ? fs.readFileSync(TEMPLATE_BASELINE, 'utf8') : '')
		.split(/\r?\n/).map((l) => l.trim()).filter((l) => l && !l.startsWith('#')),
);
const problems = missing.filter((m) => !baseline.has(m.key));
const templateProblems = templateMissing.filter((m) => !templateBaseline.has(m.key));
if (problems.length || templateProblems.length) {
	for (const p of problems) console.error(`[doc-coverage] FAIL: ${p.detail}`);
	for (const p of templateProblems) console.error(`[doc-coverage] FAIL: ${p.detail}`);
	console.error(`[doc-coverage] ${problems.length} funcion(es) y ${templateProblems.length} parámetro(s) de plantilla nuevos sin documentar.`);
	process.exit(1);
}
console.log('[doc-coverage] OK: funciones y parámetros de plantilla documentados (baselines aparte).');
