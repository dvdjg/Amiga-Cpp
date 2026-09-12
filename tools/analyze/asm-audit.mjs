#!/usr/bin/env node
// ---------------------------------------------------------------------------
// Auditor de codegen 68000: desensambla un ELF y reporta, POR FUNCION, cuantas
// llamadas a rutinas de soporte caras emite el compilador (libgcc/soft-float).
// Sirve para decidir DONDE merece la pena optimizar a mano, en vez de leer el
// asm entero o micro-optimizar todo "por si acaso".
//
// Que cuenta (por funcion, excluyendo los propios stubs):
//   mul32   __mulsi3 / __umulsi3        (multiplicacion 32 bits: ~70 ciclos)
//   div32   __divsi3 / __udivsi3        (division 32 bits: ~150+ ciclos)
//   mod32   __modsi3 / __umodsi3        (modulo 32 bits)
//   float   __addsf3 __mulsf3 __divsf3 __fixsfsi __floatsisf ... (soft-float)
//   shift   __ashlsi3 / __ashrsi3 / __lshrsi3 (desplazamiento por variable)
//   calls   nº de jsr/jbsr (contexto, no es un error por si mismo)
//   zext    and[.i].l / and[.i].w #255 / #65535  (extension redundante; --ext)
//
// Criterio: si el hot path de una demo tiene mul32/div32/float, mirar el asm y,
// si no se puede re-expresar (u16, potencias de dos, NTTP), bajar esa rutina a
// .s. Un `__mulsi3`/`__divsi3` por pixel/frame es ~pagado 2304 veces.
//
// Uso:
//   node tools/analyze/asm-audit.mjs <elf> [--strict] [--ext] [--json] [--top N]
//   node tools/analyze/asm-audit.mjs --demo demos/amiga/082_plasma
//   node tools/analyze/asm-audit.mjs --all [--root out/demos] [--engine] [--top N]
// Env: AMIGA_OBJDUMP (ruta al objdump m68k); si no, usa la del toolchain.
// ---------------------------------------------------------------------------
import { execFileSync } from 'node:child_process';
import * as fs from 'node:fs';
import * as path from 'node:path';

const objdump = process.env.AMIGA_OBJDUMP
	|| 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32/opt/bin/m68k-amiga-elf-objdump.exe';

const args = process.argv.slice(2);
const flags = new Set(args.filter((a) => a.startsWith('--')));
const positional = args.filter((a) => !a.startsWith('--'));
const argAfter = (name, def) => {
	const i = args.indexOf(name);
	return i >= 0 && args[i + 1] && !args[i + 1].startsWith('--') ? args[i + 1] : def;
};
const topN = parseInt(argAfter('--top', '25'), 10) || 25;

function findAllElfs(root) {
	const out = [];
	const walk = (d) => {
		for (const e of fs.readdirSync(d, { withFileTypes: true })) {
			const p = path.join(d, e.name);
			if (e.isDirectory()) walk(p);
			else if (e.name.endsWith('.elf')) out.push(p);
		}
	};
	walk(root);
	// Canonico: <demo>/<cfg>/<demo>.<cfg>.elf (evita duplicados sueltos).
	return out.filter((p) => path.basename(path.dirname(p)) !== path.basename(p, '.elf') && /\/[^/]+\/[^/]+\.elf$/.test(p.replace(/\\/g, '/')));
}

function elfForDemo(demoPath) {
	const leaf = path.basename(demoPath.replace(/[\\/]+$/, ''));
	const root = path.resolve('out/demos', leaf);
	if (!fs.existsSync(root)) throw new Error(`no existe ${root} (¿demo sin compilar?)`);
	const found = [];
	const walk = (d) => {
		for (const e of fs.readdirSync(d, { withFileTypes: true })) {
			const p = path.join(d, e.name);
			if (e.isDirectory()) walk(p);
			else if (e.name.endsWith('.elf')) found.push(p);
		}
	};
	walk(root);
	if (found.length === 0) throw new Error(`sin .elf en ${root}`);
	found.sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
	return found[0];
}

const STUBS = /^_*(__mulsi3|__umulsi3|__divsi3|__udivsi3|__modsi3|__umodsi3|__ashlsi3|__ashrsi3|__lshrsi3|__addsf3|__subsf3|__mulsf3|__divsf3|__negsf2|__fixsfsi|__floatsisf|__cmp[gs]f2)$/;

const PATTERNS = [
	['mul32', /\b_*(__mulsi3|__umulsi3)\b/, 3],
	['div32', /\b_*(__divsi3|__udivsi3)\b/, 5],
	['mod32', /\b_*(__modsi3|__umodsi3)\b/, 5],
	['float', /\b_*(__addsf3|__subsf3|__mulsf3|__divsf3|__negsf2|__fixsfsi|__floatsisf|__cmpsf2|__cmpgf2)\b/, 5],
	['shift', /\b_*(__ashlsi3|__ashrsi3|__lshrsi3)\b/, 2],
];
const ZEXT = /\band[il]?\.l\s+#255\b|\band[il]?\.w\s+#255\b|\band[il]?\.l\s+#65535\b/i;
const KEYS = ['mul32', 'div32', 'mod32', 'float', 'shift', 'calls', 'zext'];
const zero = () => ({ mul32: 0, div32: 0, mod32: 0, float: 0, shift: 0, calls: 0, zext: 0 });
const scoreOf = (f) => f.mul32 * 3 + (f.div32 + f.mod32 + f.float) * 5 + f.shift * 2;

function parseElf(elf) {
	let raw;
	try {
		// `-r` incluye relocaciones: en un .o sin enlazar el `jsr` sale como
		// `jsr 0 <...>` y el simbolo real solo aparece en `R_68K_32 __mulsi3`.
		raw = execFileSync(objdump, ['-d', '-r', '-C', '--no-show-raw-ins', elf], { encoding: 'utf8' });
	} catch { return null; }
	const funcs = new Map();
	const ignored = new Set();
	let cur = null;
	for (const line of raw.split(/\r?\n/)) {
		const m = line.match(/^[0-9a-f]+ <(.+)>:$/);
		if (m) {
			cur = m[1];
			if (STUBS.test(cur)) { ignored.add(cur); cur = null; }
			else if (!funcs.has(cur)) funcs.set(cur, zero());
			continue;
		}
		if (!cur) continue;
		const f = funcs.get(cur);
		for (const [key, re] of PATTERNS) if (re.test(line)) f[key]++;
		if (/\b(jsr|jbsr|bsr)\b/.test(line)) f.calls++;
		if (flags.has('--ext') && ZEXT.test(line)) f.zext++;
	}
	return { funcs, ignored };
}

function printRows(rows, srcLabel) {
	console.log(`asm-audit: ${srcLabel}`);
	console.log(`  funciones con helpers caros: ${rows.length}`);
	if (rows.length === 0) {
		console.log('  (ninguna) — el hot path no llama a libgcc/soft-float: no hace falta asm por este motivo.');
		return;
	}
	console.log('  peso  mul div mod flt shf  calls  funcion');
	for (const r of rows.slice(0, topN)) {
		console.log(
			`  ${String(r.score).padStart(4)}  ${String(r.mul32).padStart(3)} ${String(r.div32).padStart(3)} ${String(r.mod32).padStart(3)} ${String(r.float).padStart(3)} ${String(r.shift).padStart(3)}  ${String(r.calls).padStart(5)}  ${r.name}`);
	}
	const totals = {};
	for (const r of rows) for (const k of ['mul32', 'div32', 'mod32', 'float', 'shift', 'zext']) if (r[k]) totals[k] = (totals[k] || 0) + r[k];
	console.log('  TOTAL ' + (Object.entries(totals).map(([k, v]) => `${k}=${v}`).join(' ') || '(nada)'));
}

if (flags.has('--all')) {
	const root = argAfter('--root', 'out/demos');
	if (!fs.existsSync(root)) { console.error('no existe ' + root); process.exit(2); }
	const elfs = findAllElfs(root);
	const agg = new Map(); // name -> counts(max) + demos
	let scanned = 0;
	for (const e of elfs) {
		const parsed = parseElf(e);
		if (!parsed) continue;
		scanned++;
		const demo = path.basename(path.dirname(path.dirname(e)));
		for (const [name, f] of parsed.funcs) {
			if (scoreOf(f) <= 0) continue;
			const cur = agg.get(name) || { ...zero(), demos: new Set() };
			for (const k of KEYS) cur[k] = Math.max(cur[k], f[k]);
			cur.demos.add(demo);
			agg.set(name, cur);
		}
	}
	let rows = [...agg.entries()].map(([name, f]) => ({ name, ...f, score: scoreOf(f) }));
	if (flags.has('--engine')) rows = rows.filter((r) => r.name.includes('eng::'));
	rows.sort((a, b) => b.score - a.score || b.calls - a.calls);
	if (flags.has('--json')) {
		console.log(JSON.stringify({ root, scanned, functions: rows.map((r) => ({ ...r, demos: [...r.demos] })) }, null, 2));
	} else {
		printRows(rows, `${root} (${scanned} ELFs agregados)`);
	}
	process.exit(0);
}

let elf = positional[0];
const demoIdx = args.indexOf('--demo');
if (demoIdx >= 0 && args[demoIdx + 1]) elf = elfForDemo(args[demoIdx + 1]);
if (!elf) {
	console.error('uso: node tools/analyze/asm-audit.mjs <elf> [--strict] [--ext] [--json] [--top N]');
	console.error('     node tools/analyze/asm-audit.mjs --demo demos/amiga/<demo>');
	console.error('     node tools/analyze/asm-audit.mjs --all [--root out/demos] [--engine]');
	process.exit(2);
}
if (!fs.existsSync(elf)) { console.error('no existe ' + elf); process.exit(2); }
const parsed = parseElf(elf);
if (!parsed) { console.error('objdump falló para ' + elf); process.exit(2); }
const rows = [...parsed.funcs.entries()]
	.map(([name, f]) => ({ name, ...f, score: scoreOf(f) }))
	.filter((r) => r.score > 0)
	.sort((a, b) => b.score - a.score || b.calls - a.calls);
if (flags.has('--json')) {
	console.log(JSON.stringify({ elf, functions: rows, ignored: [...parsed.ignored] }, null, 2));
} else {
	printRows(rows, elf);
}
if (flags.has('--strict') && rows.length > 0) {
	console.error('FALLO --strict: hay llamadas a helpers caros (ver arriba).');
	process.exit(1);
}
process.exit(0);
