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
const topN = (() => {
	const i = args.indexOf('--top');
	return i >= 0 ? parseInt(args[i + 1], 10) || 25 : 25;
})();

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

let elf = positional[0];
const demoIdx = args.indexOf('--demo');
if (demoIdx >= 0 && args[demoIdx + 1]) elf = elfForDemo(args[demoIdx + 1]);
if (!elf) {
	console.error('uso: node tools/analyze/asm-audit.mjs <elf> [--strict] [--ext] [--json] [--top N]');
	console.error('     node tools/analyze/asm-audit.mjs --demo demos/amiga/<demo>');
	process.exit(2);
}
if (!fs.existsSync(elf)) { console.error('no existe ' + elf); process.exit(2); }

let raw;
try {
	// `-r` incluye las relocaciones: en un .o sin enlazar el `jsr` sale como
	// `jsr 0 <...>` y el simbolo real solo aparece en la linea de relocacion
	// (`R_68K_32 __mulsi3`). En un ELF enlazado el nombre ya sale en el `jsr`.
	raw = execFileSync(objdump, ['-d', '-r', '-C', '--no-show-raw-ins', elf], { encoding: 'utf8' });
} catch (e) {
	console.error('objdump falló: ' + e.message);
	process.exit(2);
}

const lines = raw.split(/\r?\n/);
const funcs = new Map();
const ignored = new Set();
let cur = null;
for (const line of lines) {
	const m = line.match(/^[0-9a-f]+ <(.+)>:$/);
	if (m) {
		cur = m[1];
		if (STUBS.test(cur)) { ignored.add(cur); cur = null; }
		else if (!funcs.has(cur)) funcs.set(cur, { mul32: 0, div32: 0, mod32: 0, float: 0, shift: 0, calls: 0, zext: 0 });
		continue;
	}
	if (!cur) continue;
	const f = funcs.get(cur);
	for (const [key, re] of PATTERNS) if (re.test(line)) f[key]++;
	if (/\b(jsr|jbsr|bsr)\b/.test(line)) f.calls++;
	if (flags.has('--ext') && ZEXT.test(line)) f.zext++;
}

const rows = [...funcs.entries()]
	.map(([name, f]) => ({
		name, ...f,
		score: f.mul32 * 3 + (f.div32 + f.mod32 + f.float) * 5 + f.shift * 2,
	}))
	.filter((r) => r.score > 0)
	.sort((a, b) => b.score - a.score || b.calls - a.calls);

const totals = rows.reduce((acc, r) => {
	for (const k of ['mul32', 'div32', 'mod32', 'float', 'shift', 'zext']) if (r[k]) acc[k] = (acc[k] || 0) + r[k];
	return acc;
}, {});

if (flags.has('--json')) {
	console.log(JSON.stringify({ elf, functions: rows, totals, ignored: [...ignored] }, null, 2));
} else {
	console.log(`asm-audit: ${elf}`);
	console.log(`  funciones con helpers caros: ${rows.length}${ignored.size ? ` (stubs ignorados: ${ignored.size})` : ''}`);
	if (rows.length === 0) {
		console.log('  (ninguna) — el hot path no llama a libgcc/soft-float: no hace falta asm por este motivo.');
	} else {
		console.log('  peso  mul div mod flt shf  calls  funcion');
		for (const r of rows.slice(0, topN)) {
			console.log(
				`  ${String(r.score).padStart(4)}  ${String(r.mul32).padStart(3)} ${String(r.div32).padStart(3)} ${String(r.mod32).padStart(3)} ${String(r.float).padStart(3)} ${String(r.shift).padStart(3)}  ${String(r.calls).padStart(5)}  ${r.name}`);
		}
		const t = Object.entries(totals).map(([k, v]) => `${k}=${v}`).join(' ');
		console.log(`  TOTAL ${t || '(nada)'}`);
	}
}

if (flags.has('--strict') && rows.length > 0) {
	console.error('FALLO --strict: hay llamadas a helpers caros (ver arriba).');
	process.exit(1);
}
process.exit(0);
