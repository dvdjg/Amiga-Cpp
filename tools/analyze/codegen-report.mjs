#!/usr/bin/env node
// Informe de codegen 68000 de la librería de matemáticas: compila una sonda con una
// función por construcción y reporta instrucciones, muls.w, shifts, libcalls y si el
// bucle queda plegado o desenrollado. Es la medición reproducible de "qué genera g++".
//
// Uso: node tools/analyze/codegen-report.mjs
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '../..');
const BIN = process.env.AMIGA_BIN_PATH || 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32';
const CXX = `${BIN}/opt/bin/m68k-amiga-elf-g++.exe`;
const SRC = `${ROOT}/out/tmp/codegen-probe.cpp`;
const ASM = `${ROOT}/out/tmp/codegen-probe.s`;

const probe = `#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
using namespace eng::math;
using eng::s16;
using eng::s32;
struct HalfEvenPolicy { using Round = rounding::HalfEven; using Overflow = overflow::Wrap; };
using q14 = Fixed<s16, 14>;

extern "C" q24 c_mul_q12(s16 a, s16 b) { return q12{a} * q12{b}; }
extern "C" Fixed<s32, 26> c_mul_mixed(s16 a, s16 b) { return q12{a} * q14{b}; } // 4.12*2.14 -> exp 26
extern "C" s16 c_mul_mixed_narrow(s16 a, s16 b) { return (q12{a} * q14{b}).norm<12>().narrow<s16>().v; }
extern "C" s16 c_norm_trunc(q24 a) { return a.norm<12>().narrow<s16>().v; }
extern "C" s16 c_norm_halfup(q24 a) { return a.retag<RoundPolicy>().norm<12>().narrow<s16>().v; }
extern "C" s16 c_norm_even(q24 a) { return a.retag<HalfEvenPolicy>().norm<12>().narrow<s16>().v; }
extern "C" q12 c_dot2(s16 a0,s16 b0,s16 c0,s16 d0){ return dot(q12{a0},q12{b0},q12{c0},q12{d0}); }
extern "C" q12 c_dot3(s16 a0,s16 b0,s16 c0,s16 d0,s16 e0,s16 f0){ return dot(q12{a0},q12{b0},q12{c0},q12{d0},q12{e0},q12{f0}); }
extern "C" void c_transform3(s16* out, const Mat<3,q12>* m, const Vec<3,q0>* t, const Vec<3,q0>* p) {
	const Affine<3,q12,q0> a{*m,*t}; const Vec<3,q0> r = transform(a,*p);
	for (int i=0;i<3;++i) out[i]=r.v[i].v;
}
extern "C" void c_matmul3(Mat<3,q12>* out, const Mat<3,q12>* a, const Mat<3,q12>* b){ *out = (*a)*(*b); }
`;

fs.mkdirSync(`${ROOT}/out/tmp`, { recursive: true });
fs.writeFileSync(SRC, probe);
try {
  execFileSync(CXX, ['-std=gnu++23', '-mcpu=68000', '-O2', '-fomit-frame-pointer',
    `-I${ROOT}/engine/include`, '-S', '-o', ASM, SRC], { stdio: 'pipe' });
} catch (e) {
  console.error('fallo el compilado cruzado:\n' + e.stderr?.toString());
  process.exit(1);
}

const lines = fs.readFileSync(ASM, 'latin1').split(/\r?\n/);
const fns = [];
let cur = null;
for (const ln of lines) {
  const m = ln.match(/^([A-Za-z_][A-Za-z0-9_]*):$/);
  if (m && m[1].startsWith('c_')) { cur = { name: m[1], ins: 0, mul: 0, asr: 0, lib: 0, branch: 0 }; fns.push(cur); continue; }
  if (!cur) continue;
  if (ln.includes('.size')) { cur = null; continue; }
  const s = ln.trim();
  if (!s || s.startsWith('.')) { if (/^(dbra|bra|bne|beq|jne|jmp|jhi|jeq|blt|bgt|blo|bhi)/.test(s)) cur.branch++; continue; }
  cur.ins++;
  if (/muls\.w|mulu\.w/.test(s)) cur.mul++;
  if (/asr\.l|lsr\.l/.test(s)) cur.asr++;
  if (/jsr|bsr/.test(s)) cur.lib++;
  if (/^(dbra|bra|bne|beq|jne|jmp|jhi|jeq|blt|bgt|blo|bhi)/.test(s)) cur.branch++;
}

console.log('construccion                     instr  muls.w  shifts  saltos  libcalls  bucle');
for (const f of fns) {
  const loop = f.branch > 0 ? 'plegado' : 'lineal';
  console.log(`${f.name.padEnd(32)} ${String(f.ins).padStart(5)} ${String(f.mul).padStart(7)} ${String(f.asr).padStart(7)} ${String(f.branch).padStart(7)} ${String(f.lib).padStart(9)}  ${loop}`);
}
