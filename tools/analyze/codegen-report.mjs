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
#include <eng/core/light.hpp>
#include <eng/core/interp.hpp>
#include <eng/core/geometry.hpp>
#include <eng/core/minifloat.hpp>
#include <eng/core/minifloat_math.hpp>
#include <eng/retro/fixed_q.hpp>
#include <eng/retro/minifloat_fixed.hpp>
#include <eng/platform/amiga/lib3d.hpp>
using namespace eng::math;
using namespace eng::retro;
using eng::s16;
using eng::s32;
using eng::u16;
struct HalfEvenPolicy { using Round = rounding::HalfEven; using Overflow = overflow::Wrap; };
using q14 = Fixed<s16, 14>;
eng::u16 g_tab[512] {}; // mutable: impide que el optimizador pliegue la tabla a constante
extern "C" s16 c_shade(s32 v, s32 e) { return light_ops<>::shade(v, e, g_tab); }

extern "C" q24 c_mul_q12(s16 a, s16 b) { return q12{a} * q12{b}; }
extern "C" Fixed<s32, 26> c_mul_mixed(s16 a, s16 b) { return q12{a} * q14{b}; } // 4.12*2.14 -> exp 26
extern "C" s16 c_mul_mixed_narrow(s16 a, s16 b) { return (q12{a} * q14{b}).rescale<12>().cast<s16>().v; }
extern "C" s16 c_norm_trunc(q24 a) { return a.rescale<12>().cast<s16>().v; }
extern "C" s16 c_norm_halfup(q24 a) { return a.retag<RoundPolicy>().rescale<12>().cast<s16>().v; }
extern "C" s16 c_norm_even(q24 a) { return a.retag<HalfEvenPolicy>().rescale<12>().cast<s16>().v; }
extern "C" q12 c_dot2(s16 a0,s16 b0,s16 c0,s16 d0){ return dot(q12{a0},q12{b0},q12{c0},q12{d0}); }
extern "C" q12 c_dot3(s16 a0,s16 b0,s16 c0,s16 d0,s16 e0,s16 f0){ return dot(q12{a0},q12{b0},q12{c0},q12{d0},q12{e0},q12{f0}); }
extern "C" void c_transform3(s16* out, const Mat<3,q12>* m, const Vec<3,q0>* t, const Vec<3,q0>* p) {
	const Affine<3,q12,q0> a{*m,*t}; const Vec<3,q0> r = transform(a,*p);
	for (int i=0;i<3;++i) out[i]=r.v[i].v;
}
extern "C" void c_matmul3(Mat<3,q12>* out, const Mat<3,q12>* a, const Mat<3,q12>* b){ *out = (*a)*(*b); }
extern "C" s16 c_dotrow(const q12* row, s16 x, s16 y, s16 z) {
	const Vec<3,q0> v {{q0{x},q0{y},q0{z}}};
	return dot(row, v).v;
}
extern "C" void c_proj(eng::object3d::Object3D* o, s16* bbox) { eng::lib3d::transform_vertices(*o, 128, 128, bbox); }

// --- Vocabulario generico sobre fixed (mul_norm/div_norm): debe ser nativo ---
extern "C" s16 c_fx_lerp(s16 a, s16 b, s16 t) { return lerp(q12{a}, q12{b}, q12{t}).v; }
extern "C" s16 c_fx_inv_lerp(s16 a, s16 b, s16 v) { return inv_lerp(q12{a}, q12{b}, q12{v}).v; }
extern "C" s16 c_fx_remap(s16 v, s16 lo, s16 hi, s16 olo, s16 ohi) { return remap(q12{v}, q12{lo}, q12{hi}, q12{olo}, q12{ohi}).v; }
extern "C" s16 c_fx_cross2(s16 ax, s16 ay, s16 bx, s16 by) { return cross2(Vec<2,q12>{{q12{ax},q12{ay}}}, Vec<2,q12>{{q12{bx},q12{by}}}).v; }
extern "C" void c_fx_rotate2(s16* o, s16 x, s16 y, s16 c, s16 s) { const Vec<2,q12> r = rotate2(Vec<2,q12>{{q12{x},q12{y}}}, q12{c}, q12{s}); o[0]=r.v[0].v; o[1]=r.v[1].v; }

// --- MiniFloat16: aritmetica, matematicas y puente con fixed (sin libgcc) ---
extern "C" u16 c_mf_mul(u16 a, u16 b) { return (MiniFloat16::from_raw(a) * MiniFloat16::from_raw(b)).raw; }
extern "C" u16 c_mf_div(u16 a, u16 b) { return (MiniFloat16::from_raw(a) / MiniFloat16::from_raw(b)).raw; }
extern "C" u16 c_mf_sqrt(u16 a) { return sqrt(MiniFloat16::from_raw(a)).raw; }
extern "C" u16 c_mf_math(u16 a) {
	const MiniFloat16 x = MiniFloat16::from_raw(a);
	return (sin(x) + exp(x) + log(x) + sqrt(x)).raw;
}
extern "C" s16 c_mf_fixed_mul(u16 r, s16 v) { return mul_fix(MiniFloat16::from_raw(r), v); }
extern "C" void c_mf_transform(const u16* mm, const s16* pp, s16* out) {
	Mat<3, MiniFloat16> m {};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) m.m[i][j] = MiniFloat16::from_raw(mm[i * 3 + j]);
	const Vec<3, fix> p = {pp[0], pp[1], pp[2]};
	const Vec<3, fix> r = transform_fix(m, p);
	for (int i = 0; i < 3; ++i) out[i] = r.v[i];
}
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
  if (/\b(muls|mulu)(\.[wl])?\b/.test(s)) cur.mul++;
  if (/asr\.l|lsr\.l/.test(s)) cur.asr++;
  if (/jsr|bsr/.test(s)) cur.lib++;
  if (/^(dbra|bra|bne|beq|jne|jmp|jhi|jeq|blt|bgt|blo|bhi)/.test(s)) cur.branch++;
}

console.log('construccion                     instr  muls.w  shifts  saltos  libcalls  bucle');
for (const f of fns) {
  const loop = f.branch > 0 ? 'plegado' : 'lineal';
  console.log(`${f.name.padEnd(32)} ${String(f.ins).padStart(5)} ${String(f.mul).padStart(7)} ${String(f.asr).padStart(7)} ${String(f.branch).padStart(7)} ${String(f.lib).padStart(9)}  ${loop}`);
}

// --- Gate: el camino caliente no puede llamar a las rutinas de 32 bits de libgcc ---
// (__mulsi3/__divsi3/…). Si aparece, una operacion que deberia ser `muls.w`/`mulu.w`
// nativos se ha convertido en una llamada (~50+ ciclos). Los `jsr` a funciones propias
// (cuerpos no inlined) son normales y solo se informan. `--report` no falla.
const asmText = fs.readFileSync(ASM, 'latin1');
const FORBIDDEN = ['__mulsi3', '__umulsi3', '__divsi3', '__udivsi3'];
const hit = FORBIDDEN.filter((s) => asmText.includes(s));
const called = fns.filter((f) => f.lib > 0).map((f) => `${f.name} (${f.lib} jsr)`);
if (hit.length) {
  console.error(`\n[codegen] FAIL: el camino caliente llama a libgcc -> ${hit.join(', ')}`);
  process.exit(1);
}
if (called.length && !process.argv.includes('--report')) {
  console.log(`\n[codegen] nota: jsr a funciones propias (no libgcc): ${called.join(', ')}`);
}

// --- Gate 68000: nada de instrucciones de 68020+ (p. ej. divsl.l/divul.l) ---
// Compilamos con -mcpu=68000, pero si alguien cambia el target la division 32/32 se
// convierte en `divsl.l` (68020) y crashea en un A500. Se detecta aqui.
const BAD_68020 = [
  ['divsl.l', /\bdivsl\.l\b/],
  ['divul.l', /\bdivul\.l\b/],
  ['divs.l', /\bdivs\.l\b/],
  ['divu.l', /\bdivu\.l\b/],
  ['muls.l', /\bmuls\.l\b/],
  ['mulu.l', /\bmulu\.l\b/],
];
const bad = BAD_68020.filter(([, re]) => re.test(asmText)).map(([name]) => name);
if (bad.length) {
  console.error(`\n[codegen] FAIL: instrucciones de 68020 en el target 68000 -> ${bad.join(', ')}`);
  process.exit(1);
}
// Evidencia positiva: la division de fixed (div_norm) debe ser `divs.w` nativa.
if (!/\bdivs(\.w)?\b/.test(asmText)) {
  console.error('\n[codegen] FAIL: no aparece divs.w (la division de fixed deberia ser nativa).');
  process.exit(1);
}

console.log('[codegen] OK: sin libcalls (__mulsi3/__divsi3) y sin instrucciones 68020.');
