#!/usr/bin/env node
// Comparativa de `.s` para expression templates: compila la MISMA expresión escrita dos
// veces (operadores sueltos vs `eng::math::et`) y reporta instrucciones, escrituras a pila,
// llamadas y si el bucle se desenrolla. Es la medición reproducible de "qué ahorra la
// fusión" en 68000. La conclusión medida: la ganancia está en el ESCALAR (menos temporales
// de `MiniFloat16`); en un `Vec`/`Mat` diminuto el compilador ya elimina los temporales y
// el árbol no aporta.
//
// Uso: node tools/analyze/expr-asm-compare.mjs
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '../..');
const BIN = process.env.AMIGA_BIN_PATH || 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32';
const CXX = `${BIN}/opt/bin/m68k-amiga-elf-g++.exe`;
const SRC = `${ROOT}/out/tmp/expr-asm-compare.cpp`;
const ASM = `${ROOT}/out/tmp/expr-asm-compare.s`;

const probe = `#include <eng/core/math/minifloat.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/expr.hpp>
using namespace eng::math;
using namespace eng::math::et;

MiniFloat16 mf16_ops(MiniFloat16 a, MiniFloat16 b, MiniFloat16 c, MiniFloat16 d) {
	return a + b * c - d;
}
MiniFloat16 mf16_et(MiniFloat16 a, MiniFloat16 b, MiniFloat16 c, MiniFloat16 d) {
	return evaluate<MiniFloat16>(val(a) + val(b) * val(c) - val(d));
}

Vec<3, MiniFloat16> vec3_mf16_ops(const Vec<3, MiniFloat16>& a, const Vec<3, MiniFloat16>& b,
                                  const Vec<3, MiniFloat16>& d, MiniFloat16 k) {
	return a + b * k - d;
}
Vec<3, MiniFloat16> vec3_mf16_et(const Vec<3, MiniFloat16>& a, const Vec<3, MiniFloat16>& b,
                                 const Vec<3, MiniFloat16>& d, MiniFloat16 k) {
	Vec<3, MiniFloat16> out;
	eval_into(out, val(a) + val(b) * k - val(d));
	return out;
}

Vec<3, float> vec3_float_ops(const Vec<3, float>& a, const Vec<3, float>& b, const Vec<3, float>& d, float k) {
	return a + b * k - d;
}
Vec<3, float> vec3_float_et(const Vec<3, float>& a, const Vec<3, float>& b, const Vec<3, float>& d, float k) {
	Vec<3, float> out;
	eval_into(out, val(a) + val(b) * k - val(d));
	return out;
}
`;

fs.mkdirSync(`${ROOT}/out/tmp`, { recursive: true });
fs.writeFileSync(SRC, probe);
execFileSync(CXX, ['-std=gnu++23', '-mcpu=68000', '-O2', '-fomit-frame-pointer',
	`-I${ROOT}/engine/include`, '-S', '-o', ASM, SRC], { stdio: 'pipe' });

const lines = fs.readFileSync(ASM, 'utf8').split(/\r?\n/);

const want = ['mf16_ops', 'mf16_et', 'vec3_mf16_ops', 'vec3_mf16_et', 'vec3_float_ops', 'vec3_float_et'];
const rows = [];
for (let i = 0; i < lines.length; i++) {
	// El nombre manglado de una función libre empieza por `_Z<longitud><nombre>`.
	const mm = /^_Z(\d+)(.*):$/.exec(lines[i]);
	if (!mm) {
		continue;
	}
	const fname = mm[2].slice(0, parseInt(mm[1], 10));
	if (!want.includes(fname)) {
		continue;
	}
	let end = i + 1;
	while (end < lines.length && !lines[end].startsWith('\t.size')) {
		end++;
	}
	let ins = 0;
	let stackWrites = 0;
	let calls = 0;
	let branches = 0;
	let loops = 0;
	for (let j = i + 1; j < end; j++) {
		const raw = lines[j];
		if (!/^\t/.test(raw)) {
			continue;
		}
		const t = raw.trim();
		if (t.startsWith('.') || t.endsWith(':')) {
			continue;
		}
		ins++;
		if (/\(\s*%sp\s*\)/.test(t) && /^move/.test(t)) {
			stackWrites++;
		}
		if (/^(jsr|bsr)/.test(t)) {
			calls++;
		}
		if (/^(dbf|dbra)/.test(t)) {
			loops++;
		}
		if (/^(b|j)/.test(t) && !/^(jsr|bsr)/.test(t)) {
			branches++;
		}
	}
	rows.push({ name: fname, ins, stackWrites, calls, branches, loops });
}

const pad = (s, n) => String(s).padEnd(n);
console.log('expr-asm (68000, -O2, -fomit-frame-pointer)\n');
console.log(pad('funcion', 18) + pad('ins', 6) + pad('a-pila', 8) + pad('calls', 7) + pad('saltos', 8) + 'bucle');
for (const r of rows) {
	console.log(pad(r.name, 18) + pad(r.ins, 6) + pad(r.stackWrites, 8) + pad(r.calls, 7) + pad(r.branches, 8) + r.loops);
}
console.log('\n(desenrollado = sin bucle; "a-pila" son temporales que no caben en registro; en');
console.log(' soft-float, una `calls` baja con `saltos` alto indica bucle no desenrollado.)');
