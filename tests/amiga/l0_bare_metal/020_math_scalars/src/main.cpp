#include <eng/retro/fixed_mesh.hpp>
// ============================================================================
// Test L0-020: batería de matemáticas del engine sobre los TRES escalares.
// ============================================================================
//
// Objetivo: comprobar las funciones matemáticas de `eng/core` + `eng/retro` con los tres
// escalares del engine y con operaciones **entre** ellos (sobre todo matriciales), en
// HARDWARE Amiga y **sin usar `float`/`double` en ningún momento**.
//
// Escalares:
//   - `MiniFloat16`  (MF)      coma flotante de 16 bits (1|5|10), 10 bits de mantisa.
//   - `q12`          (fixed)   `Fixed<s16,12>` = 4.12 (1.0 == 4096).
//   - `q8`           (fixed88) `Fixed<s16,8>`  = 8.8  (1.0 == 256), alias `Coord88`.
//   - `q0`           (entero)  `Fixed<s16,0>`  = LONGITUD (1.0 == 1).
//
// Operaciones entre tipos: `Mat<3,MF> * Vec<3,q12>`, `Mat<3,MF> * Vec<3,q8>`,
// `transform_fix`/`transform_fix88`/`transform_point`/`project`, `mul_fixed`/`mul_fix`/
// `mul_fix88`, `fixed_to_mf`/`mf_to_fixed`.
//
// Cómo comprueba sin float (regla del proyecto): el error se mide SIEMPRE en unidades de
// 1/4096 (el ULP de 4.12) y se compara con una tolerancia por caso. Los valores de
// referencia son **invariantes** (identidades y rangos) más unos pocos **dorados**
// enteros. Nada de `to_double`, `printf("%f")` ni `libm`: el código corre igual en el
// 68000 con `-mcpu=68000`.
//
// Por qué una tolerancia por caso y no una global: cada escalar tiene una precisión
// distinta (MF ~2^-10, 4.12 = 2^-12, 8.8 = 2^-8) y cada operación acumula errores
// distintos (una división cuesta más que un producto). La tolerancia se documenta en el
// propio caso y el host solo compara `max_err <= tol`.
//
// Resultado: el test rellena `g_math_report` (símbolo C, layout POD) con un registro por
// caso y lo publica por el canal lateral; `verify-math.ts` lo lee y exige
// `failed_count == 0`. Además resume el recuento en `g_eng_run_status.detail` y espera
// unos frames antes de volver a Workbench (el runner necesita ese margen).
//
// Verificación por host: `tests/l0_bare_metal/020_math_scalars/verify-math.sh`.

#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <eng/core/geometry.hpp>
#include <eng/core/interp.hpp>
#include <eng/core/noise.hpp>
#include <eng/core/scalar_ops.hpp>
#include <eng/core/spline.hpp>
#include <eng/core/fast_div.hpp>
#include <eng/core/isqrt.hpp>
#include <eng/core/light.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/core/minifloat_math.hpp>
#include <eng/retro/fixed_trig.hpp>
#include <eng/retro/fixed_q.hpp>
#include <eng/retro/minifloat_fixed.hpp>
#include <eng/retro/sintab.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

using eng::s16;
using eng::s32;
using eng::u32;
using eng::math::MiniFloat16;
namespace em = eng::math;
namespace er = eng::retro;
namespace m3 = eng::math3d;

using MF = MiniFloat16;
using q12 = er::q12;    // 4.12
using q8 = er::Coord88; // 8.8  (fixed88)
using q0 = er::q0;      // entero

// ============================================================================
//  Marco de pruebas (registro POD que el host lee por el canal lateral)
// ============================================================================
//
// Un "caso" agrupa las comprobaciones de una función (o familia) sobre un escalar.
// `max_err` es el PEOR error observado, en unidades de 1/4096; `tol` la tolerancia del
// caso. El caso pasa si `max_err <= tol`.

constexpr u32 k_math_magic = 0x4d415448u; // "MATH"
constexpr u32 k_math_version = 1u;
constexpr u32 k_max_cases = 100u;

struct MathCase {
	char name[20]; // nombre legible (NUL-terminado)
	s16 pass;      // 1 = OK, 0 = fallo
	s16 kind;      // 0=MF 1=q12 2=q8 3=q0 4=mixto
	s32 max_err;   // peor error en unidades de 1/4096
	s32 tol;       // tolerancia del caso (misma unidad)
};

struct MathReport {
	u32 magic;
	u32 version;
	u32 case_count;
	u32 failed_count;
	u32 checks;
	MathCase cases[k_max_cases];
};

} // namespace

// El reporte vive a nivel global con linkage C para que el símbolo `g_math_report` sea
// localizable en el `.map` (el script host lo lee por el canal lateral).
extern "C" {
__attribute__((used)) MathReport g_math_report {};
}

namespace {

MathCase* g_cur = nullptr;
s32 g_cur_max = 0;

void case_begin(const char* name, s16 kind, s32 tol) {
	if (g_math_report.case_count >= k_max_cases) return;
	MathCase& c = g_math_report.cases[g_math_report.case_count];
	for (int i = 0; i < 19; ++i) {
		c.name[i] = name[i];
		if (name[i] == '\0') break;
	}
	c.name[19] = '\0';
	c.pass = 1;
	c.kind = kind;
	c.max_err = 0;
	c.tol = tol;
	g_cur = &c;
	g_cur_max = 0;
}

void case_end() {
	if (g_cur == nullptr) return;
	g_cur->max_err = g_cur_max;
	g_cur->pass = (g_cur_max <= g_cur->tol) ? 1 : 0;
	if (g_cur->pass == 0) ++g_math_report.failed_count;
	++g_math_report.case_count;
	g_cur = nullptr;
}

void rec(s32 units) {
	++g_math_report.checks;
	if (units > g_cur_max) g_cur_max = units;
}

/// Comprobación booleana: si falla, se anota un error "infinito" (fuerza el fallo).
void see(bool ok) { rec(ok ? 0 : 1000000); }

// --- Error de un escalar en unidades de 1/4096 -----------------------------

s32 mag(s32 v) { return v < 0 ? -v : v; }

s32 units(q12 d) { return mag(d.v); }      // ya está en 1/4096
s32 units(q8 d) { return mag(d.v) << 4; }  // 1/256 -> 1/4096
s32 units(q0 d) { return mag(d.v) << 12; } // 1 -> 1/4096
s32 units(MF d) { return mag(er::mf_to_fixed<12>(em::abs(d)).v); }
s32 units(s32 d) { return mag(d); } // crudo 4.12 (los puentes devuelven `fix` = s16)

// Envuelven un `fix`/`fix88` crudo en su tipo Fixed para comparar con la tolerancia correcta.
q12 wrap_fix(er::fix v) { return q12 {v}; }
q8 wrap_fix88(er::fix88 v) { return q8 {v}; }

template <typename S>
void eq(S got, S want) {
	rec(units(got - want));
}

/// Comparación de enteros (isqrt/fast_div/mesh3d): error en unidades de 1/4096.
void eqi(s32 got, s32 want) { rec(mag(got - want)); }

// --- Constructores sin float ----------------------------------------------
//
// `q12`/`q8` se construyen por su valor crudo; `MF` a partir de la misma fracción con
// `fix_to_mf` (conversión entera exacta, sin `float`).

q12 r12(int raw) { return q12 {static_cast<s16>(raw)}; }
q8 r8(int raw) { return q8 {static_cast<s16>(raw)}; }
q0 r0(int raw) { return q0 {static_cast<s16>(raw)}; }
MF mf12(int raw12) { return er::fix_to_mf(static_cast<er::fix>(raw12)); }

constexpr MF kHalf = em::mfdetail::k_half;
constexpr MF kOneMf = em::mfdetail::k_one;
constexpr MF kTwoMf = em::mfdetail::k_two;
constexpr MF kPiMf = em::mfdetail::k_pi;
constexpr MF kHalfPiMf = em::mfdetail::k_half_pi;

MF mf_int(int i) { return em::scalar_traits<MF>::from_int(i); }

// --- Utilidades para escalares fixed --------------------------------------

template <typename S>
constexpr S one_fx() {
	return S {static_cast<typename S::repr>(typename S::repr(1) << S::exp)};
}
template <typename S>
constexpr S three_quarters() {
	const S one = one_fx<S>();
	return S {static_cast<typename S::repr>((one.v >> 1) + (one.v >> 2))};
}

// ============================================================================
//  linalg: vectores, producto escalar, matrices y normalización
// ============================================================================

template <typename S>
void t_vec_basics(const char* name, s16 kind) {
	case_begin(name, kind, 0);
	const S one = one_fx<S>();
	const S two = one + one;
	const S three = two + one;
	const em::Vec<3, S> a {{one, one, one}};
	const em::Vec<3, S> b {{two, two, two}};
	eq((a + b).v[0], three);
	eq((b - a).v[2], one);
	eq((-a).v[1], S {static_cast<typename S::repr>(-one.v)});
	case_end();
}

void t_vec_basics_all() {
	t_vec_basics<q12>("vec add/sub q12", 1);
	t_vec_basics<q8>("vec add/sub q8", 2);
	case_begin("vec add/sub mf", 0, 0);
	{
		const em::Vec<3, MF> a {{kOneMf, kOneMf, kHalf}};
		const em::Vec<3, MF> b {{kHalf, kOneMf, kHalf}};
		eq((a + b).v[0], kOneMf + kHalf);
		eq((a + b).v[1], kTwoMf);
		eq((b - a).v[2], MF::zero());
		eq((-a).v[2], MF::zero() - kHalf);
	}
	case_end();
}

template <typename S>
void t_dot(const char* name, s16 kind, em::Vec<3, S> a, em::Vec<3, S> b, S want, S want4) {
	case_begin(name, kind, 1);
	eq(em::dot(a, b), want);
	const em::Vec<4, S> a4 {{a.v[0], a.v[1], a.v[2], a.v[0]}};
	const em::Vec<4, S> b4 {{b.v[0], b.v[1], b.v[2], b.v[1]}};
	eq(em::dot(a4, b4), want4);
	case_end();
}

void t_dot_all() {
	// 0.5*1 + 1*2 + 1.5*0.5 = 3.25 -> 13312 (cuarta componente: +1.0 -> 17408).
	t_dot<q12>("dot fusionado q12", 1, {{r12(2048), r12(4096), r12(6144)}},
		   {{r12(4096), r12(8192), r12(2048)}}, r12(13312), r12(17408));
	// 1.0*0.5 + 2.0*1.0 + 0.5*0.5 = 2.75 -> 704 (cuarta: +1.0 -> 960).
	t_dot<q8>("dot fusionado q8", 2, {{r8(256), r8(512), r8(128)}},
		  {{r8(128), r8(256), r8(128)}}, r8(704), r8(960));
	t_dot<MF>("dot fusionado mf", 0, {{kHalf, kOneMf, mf12(6144)}},
		  {{kOneMf, kTwoMf, kHalf}}, mf12(13312), mf12(17408));
	// `dot(fila, vector)` fusionado: lee la fila de una matriz como `M*v`.
	case_begin("dot(fila,vec) q12", 1, 1);
	{
		const q12 row[3] {r12(2048), r12(4096), r12(6144)};
		eq(em::dot(row, em::Vec<3, q12> {{r12(4096), r12(8192), r12(2048)}}), r12(13312));
	}
	case_end();
}

void t_mat_mul_q12() {
	case_begin("mat*mat q12", 1, 1);
	using M = em::Mat<3, q12>;
	const M id = M::identity();
	const M a {{{r12(4096), r12(2048), r12(0)},
		    {r12(0), r12(4096), r12(2048)},
		    {r12(2048), r12(0), r12(4096)}}};
	const M ai = a * id;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) eq(ai.m[i][j], a.m[i][j]);
	const M aa = a * a;
	eq(aa.m[0][0], r12(4096)); // 1*1 + .5*0 + 0*.5 = 1.0
	eq(aa.m[0][1], r12(4096)); // 1*.5 + .5*1 = 1.0
	eq(aa.m[0][2], r12(1024)); // 1*0 + .5*.5 = 0.25
	eq(aa.m[1][2], r12(4096)); // 1*.5 + .5*1 = 1.0
	eq(aa.m[2][0], r12(4096)); // .5*1 + 1*.5 = 1.0
	case_end();
}

void t_mat_mul_mf() {
	case_begin("mat*mat mf", 0, 16);
	using M = em::Mat<3, MF>;
	const M id = M::identity();
	const M a {{{kOneMf, kHalf, MF::zero()},
		    {MF::zero(), kOneMf, kHalf},
		    {kHalf, MF::zero(), kOneMf}}};
	const M ai = a * id;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j) eq(ai.m[i][j], a.m[i][j]);
	const M aa = a * a;
	eq(aa.m[0][0], kOneMf);            // 1*1 + .5*0 = 1
	eq(aa.m[0][1], mf12(4096));        // 1*.5 + .5*1 = 1.0
	eq(aa.m[1][2], mf12(4096));        // 1*.5 + .5*1 = 1.0
	case_end();
}

void t_mulnorm_divnorm() {
	case_begin("mul_norm/div_norm q12", 1, 1);
	eq(em::mul_norm(r12(4096), r12(2048)), r12(2048)); // 1.0 * 0.5
	eq(em::mul_norm(r12(2048), r12(2048)), r12(1024)); // 0.5 * 0.5
	eq(em::div_norm(r12(4096), r12(2048)), r12(8192)); // 1.0 / 0.5 = 2.0
	case_end();
	case_begin("mul_norm/div_norm q8", 2, 1);
	eq(em::mul_norm(r8(256), r8(128)), r8(128));
	eq(em::div_norm(r8(256), r8(128)), r8(512));
	case_end();
	case_begin("mul_norm mf", 0, 4);
	eq(em::mul_norm(kOneMf, kHalf), kHalf);
	eq(em::mul_norm(kHalf, kHalf), mf12(1024));
	case_end();
}

// ============================================================================
//  interp: recorte, interpolación y easing
// ============================================================================

template <typename S>
void t_clamp_lerp(const char* name, s16 kind, S lo, S mid, S hi, S t, S want, s32 tol) {
	case_begin(name, kind, tol);
	eq(em::clamp(mid, lo, hi), mid);
	eq(em::clamp(lo - mid, lo, hi), lo); // por debajo -> recorta
	eq(em::saturate(mid), mid);
	eq(em::saturate(hi), one_fx<S>());
	eq(em::lerp(lo, hi, t), want);
	eq(em::lerp(lo, hi, S {}), lo);
	eq(em::lerp(lo, hi, one_fx<S>()), hi);
	case_end();
}

void t_interp_all() {
	// lerp(0, 2, 0.5) = 1.0
	t_clamp_lerp<q12>("clamp/lerp q12", 1, r12(0), r12(2048), r12(8192), r12(2048), r12(4096), 1);
	t_clamp_lerp<q8>("clamp/lerp q8", 2, r8(0), r8(128), r8(512), r8(128), r8(256), 1);
	case_begin("clamp/lerp mf", 0, 4);
	eq(em::clamp(kOneMf, MF::zero(), kTwoMf), kOneMf);
	eq(em::saturate(kHalf), kHalf);
	eq(em::saturate(kTwoMf), kOneMf);
	eq(em::lerp(MF::zero(), kTwoMf, kHalf), kOneMf);
	eq(em::lerp(MF::zero(), kTwoMf, MF::zero()), MF::zero());
	case_end();

	case_begin("inv_lerp/remap q12", 1, 4);
	eq(em::inv_lerp(r12(0), r12(8192), r12(4096)), r12(2048));
	eq(em::remap(r12(4096), r12(0), r12(8192), r12(0), r12(4096)), r12(2048));
	case_end();
	case_begin("inv_lerp/remap q8", 2, 4);
	eq(em::inv_lerp(r8(0), r8(512), r8(256)), r8(128));
	case_end();
	case_begin("inv_lerp mf", 0, 16);
	eq(em::inv_lerp(MF::zero(), kTwoMf, kOneMf), kHalf);
	case_end();

	case_begin("smoothstep q12", 1, 2);
	eq(em::smoothstep(r12(0)), r12(0));
	eq(em::smoothstep(r12(4096)), r12(4096));
	eq(em::smoothstep(r12(2048)), r12(2048)); // 3*.25 - 2*.125 = 0.5
	case_end();
	case_begin("smoothstep/smootherstep mf", 0, 8);
	eq(em::smoothstep(MF::zero()), MF::zero());
	eq(em::smoothstep(kOneMf), kOneMf);
	eq(em::smootherstep(MF::zero()), MF::zero());
	eq(em::smootherstep(kOneMf), kOneMf);
	eq(em::smootherstep(kHalf), kHalf);
	case_end();
	case_begin("smoothstep/smootherstep q8", 2, 4);
	eq(em::smoothstep(r8(128)), r8(128));
	eq(em::smootherstep(r8(128)), r8(128));
	case_end();
}

/// Extremos de un easing: f(0)=0 y f(1)=1 (invariante de toda la familia).
template <typename S>
void t_easing_ends(const char* name, s16 kind, s32 tol) {
	case_begin(name, kind, tol);
	const S zero = S {};
	const S one = one_fx<S>();
	const S half = S {static_cast<typename S::repr>(one.v >> 1)};
	eq(em::ease_in_quad(zero), zero);
	eq(em::ease_in_cubic(zero), zero);
	eq(em::ease_out_quad(one), one);
	eq(em::ease_in_out_cubic(one), one);
	eq(em::ease_in_back(one), one);
	eq(em::ease_out_back(one), one);
	eq(em::ease_in_out_quad(half), half);
	eq(em::ease_in_out_cubic(half), half);
	case_end();
}

void t_easing_all() {
	t_easing_ends<q12>("ease quad/cubic/back ends q12", 1, 2);
	t_easing_ends<q8>("ease quad/cubic/back ends q8", 2, 4);
	case_begin("ease quad/cubic mf", 0, 8);
	eq(em::ease_in_quad(MF::zero()), MF::zero());
	eq(em::ease_in_quad(kHalf), mf12(1024)); // 0.25
	eq(em::ease_in_cubic(kHalf), mf12(512)); // 0.125
	eq(em::ease_out_quad(kOneMf), kOneMf);
	eq(em::ease_in_out_cubic(kOneMf), kOneMf);
	eq(em::ease_in_out_cubic(kHalf), kHalf);
	case_end();
	case_begin("ease back mf", 0, 8);
	eq(em::ease_in_back(MF::zero()), MF::zero());
	eq(em::ease_in_back(kOneMf), kOneMf);
	eq(em::ease_out_back(kOneMf), kOneMf);
	eq(em::ease_in_out_back(kHalf), kHalf);
	case_end();
	case_begin("ease sine/expo mf", 0, 8);
	eq(em::ease_in_sine(MF::zero()), MF::zero());
	eq(em::ease_out_sine(MF::zero()), MF::zero());
	eq(em::ease_in_sine(kOneMf), kOneMf);
	eq(em::ease_in_out_sine(kHalf), kHalf);
	eq(em::ease_in_expo(MF::zero()), MF::zero());
	eq(em::ease_out_expo(kOneMf), kOneMf);
	eq(em::ease_in_out_expo(kOneMf), kOneMf);
	case_end();
	case_begin("smooth_damp mf", 0, 8);
	eq(em::smooth_damp(MF::zero(), kOneMf, kOneMf, kOneMf), kHalf); // 1 - 2^-1
	eq(em::smooth_damp(kOneMf, kOneMf, kOneMf, kOneMf), kOneMf);    // ya en el objetivo
	case_end();
}

/// repeat/pingpong: rango y periodicidad (invariantes, sin dorados frágiles).
template <typename S>
void t_repeat_pingpong(const char* name, s16 kind, s32 tol) {
	case_begin(name, kind, tol);
	const S one = one_fx<S>();
	const S two = one + one;
	const S quarter = S {static_cast<typename S::repr>(one.v >> 2)};
	const S three_q = three_quarters<S>();
	eq(em::repeat(one + quarter, one), quarter);
	eq(em::pingpong(quarter, one), quarter);
	eq(em::pingpong(one + quarter, one), three_q);
	eq(em::pingpong(one, one), one); // pico del rebote
	eq(em::pingpong(two, one), S {}); // dos periodos -> 0
	see(!(em::repeat(three_q, one) < S {}));
	see(em::repeat(three_q, one) < one);
	see(!(em::pingpong(three_q, one) < S {}));
	see(!(one < em::pingpong(three_q, one)));
	case_end();
}

void t_repeat_all() {
	t_repeat_pingpong<q12>("repeat/pingpong q12", 1, 2);
	t_repeat_pingpong<q8>("repeat/pingpong q8", 2, 2);
	case_begin("repeat/pingpong mf", 0, 8);
	eq(em::repeat(kOneMf + kHalf, kOneMf), kHalf); // 1.5 -> 0.5
	eq(em::pingpong(kHalf, kOneMf), kHalf);
	eq(em::pingpong(kOneMf + kHalf, kOneMf), kHalf);
	case_end();
}

// ============================================================================
//  scalar_ops: min/max/abs/sign, move_towards y deadzone
// ============================================================================

template <typename S>
void t_scalar_ops(const char* name, s16 kind) {
	case_begin(name, kind, 0);
	const S one = one_fx<S>();
	const S two = one + one;
	const S three = two + one;
	const S neg = S {static_cast<typename S::repr>(-two.v)};
	eq(em::min(one, two), one);
	eq(em::max(one, neg), one);
	eq(em::abs(neg), two);
	eq(em::sign(neg), S {static_cast<typename S::repr>(-one.v)});
	eq(em::sign(two), one);
	eq(em::sign(S {}), S {});
	eq(em::move_towards(S {}, two, one), one);
	eq(em::move_towards(one, two, two), two);
	eq(em::move_towards(two, S {}, one), one);
	eq(em::deadzone(one, two), S {});                                 // dentro
	eq(em::deadzone(three_quarters<S>() + two, two), three_quarters<S>());
	eq(em::deadzone(S {static_cast<typename S::repr>(-three.v)}, two),
	   S {static_cast<typename S::repr>(-one.v)});                    // conserva signo
	case_end();
}

void t_scalar_ops_all() {
	t_scalar_ops<q12>("min/max/abs/sign/... q12", 1);
	t_scalar_ops<q8>("min/max/abs/sign/... q8", 2);
	t_scalar_ops<q0>("min/max/abs/sign/... q0", 3);
	case_begin("min/max/abs/sign/... mf", 0, 0);
	eq(em::min(kOneMf, kTwoMf), kOneMf);
	eq(em::max(kOneMf, MF::zero() - kTwoMf), kOneMf);
	eq(em::abs(MF::zero() - kTwoMf), kTwoMf);
	eq(em::sign(MF::zero() - kOneMf), MF::zero() - kOneMf);
	eq(em::sign(kTwoMf), kOneMf);
	eq(em::move_towards(MF::zero(), kTwoMf, kOneMf), kOneMf);
	eq(em::move_towards(kOneMf, kTwoMf, kTwoMf), kTwoMf);
	eq(em::deadzone(kOneMf, kTwoMf), MF::zero());
	eq(em::deadzone(kTwoMf + kOneMf, kTwoMf), kOneMf);
	case_end();
}

// ============================================================================
//  geometry
// ============================================================================

void t_geometry_q12() {
	case_begin("cross2/perp/rotate2 q12", 1, 2);
	using V2 = em::Vec<2, q12>;
	eq(em::cross2(V2 {{r12(4096), r12(0)}}, V2 {{r12(0), r12(4096)}}), r12(4096));
	eq(em::cross2(V2 {{r12(0), r12(4096)}}, V2 {{r12(4096), r12(0)}}), r12(-4096));
	const V2 p = em::perp(V2 {{r12(4096), r12(0)}});
	eq(p.v[0], r12(0));
	eq(p.v[1], r12(4096));
	const V2 rr = em::rotate2(V2 {{r12(4096), r12(0)}}, r12(0), r12(4096));
	eq(rr.v[0], r12(0));
	eq(rr.v[1], r12(4096));
	const V2 sc = em::vscale(V2 {{r12(2048), r12(1024)}}, r12(8192));
	eq(sc.v[0], r12(4096));
	eq(sc.v[1], r12(2048));
	const V2 lp = em::vlerp(V2 {{r12(0), r12(0)}}, V2 {{r12(4096), r12(8192)}}, r12(2048));
	eq(lp.v[0], r12(2048));
	eq(lp.v[1], r12(4096));
	case_end();
}

void t_geometry_mf() {
	case_begin("cross2/perp/rotate2 mf", 0, 8);
	using V2 = em::Vec<2, MF>;
	eq(em::cross2(V2 {{kOneMf, MF::zero()}}, V2 {{MF::zero(), kOneMf}}), kOneMf);
	const V2 p = em::perp(V2 {{kOneMf, MF::zero()}});
	eq(p.v[0], MF::zero());
	eq(p.v[1], kOneMf);
	const V2 rr = em::rotate2(V2 {{kOneMf, MF::zero()}}, MF::zero(), kOneMf);
	eq(rr.v[0], MF::zero());
	eq(rr.v[1], kOneMf);
	eq(em::vscale(V2 {{kHalf, MF::zero()}}, kTwoMf).v[0], kOneMf);
	case_end();
	case_begin("length/distance/normalize mf", 0, 8);
	using V3 = em::Vec<3, MF>;
	eq(em::length(V3 {{MF::zero(), MF::zero(), kTwoMf}}), kTwoMf);
	eq(em::length(V3 {{kOneMf, MF::zero(), MF::zero()}}), kOneMf);
	eq(em::length_sq(V3 {{kTwoMf, MF::zero(), MF::zero()}}), mf12(16384)); // 4.0
	eq(em::distance(V3 {{MF::zero(), MF::zero(), MF::zero()}},
			V3 {{MF::zero(), MF::zero(), kTwoMf}}),
	   kTwoMf);
	const V3 n = em::normalize(V3 {{kTwoMf, MF::zero(), MF::zero()}});
	eq(n.v[0], kOneMf);
	eq(n.v[1], MF::zero());
	case_end();
}

// ============================================================================
//  spline
// ============================================================================

void t_spline() {
	case_begin("hermite/bezier q12", 1, 2);
	// hermite(p0,m0,p1,m1): t=0 -> p0; t=1 -> p1.
	eq(em::hermite(r12(0), r12(4096), r12(8192), r12(0), r12(0)), r12(0));
	eq(em::hermite(r12(0), r12(4096), r12(8192), r12(0), r12(4096)), r12(8192));
	// bezier2(p0,p1,p2): t=0 -> p0; t=1 -> p2; t=.5 -> .25p0+.5p1+.25p2 = 1.0
	eq(em::bezier2(r12(0), r12(4096), r12(8192), r12(0)), r12(0));
	eq(em::bezier2(r12(0), r12(4096), r12(8192), r12(4096)), r12(8192));
	eq(em::bezier2(r12(0), r12(4096), r12(8192), r12(2048)), r12(4096));
	case_end();
	case_begin("hermite/bezier mf", 0, 8);
	eq(em::hermite(MF::zero(), kOneMf, kTwoMf, MF::zero(), MF::zero()), MF::zero());
	eq(em::hermite(MF::zero(), kOneMf, kTwoMf, MF::zero(), kOneMf), kTwoMf);
	eq(em::bezier2(MF::zero(), kOneMf, kTwoMf, MF::zero()), MF::zero());
	eq(em::bezier2(MF::zero(), kOneMf, kTwoMf, kOneMf), kTwoMf);
	case_end();
	case_begin("catmull_rom q8", 2, 4);
	// Pasa por los puntos de control: catmull(p0,p1,p2,p3,0)=p1, (..,1)=p2.
	eq(em::catmull_rom(r8(0), r8(128), r8(384), r8(512), r8(0)), r8(128));
	eq(em::catmull_rom(r8(0), r8(128), r8(384), r8(512), r8(256)), r8(384));
	case_end();
}

// ============================================================================
//  noise (MF: necesita división; no aplica a fixed)
// ============================================================================

void t_noise_mf() {
	case_begin("value_noise/fbm mf", 0, 8);
	const MF n = em::value_noise1(kHalf, 1234u, 0);
	see(!(n < MF::zero()));
	see(n < kOneMf);
	const MF n2 = em::value_noise2(kHalf, kOneMf, 7u, 0);
	see(!(n2 < MF::zero()));
	see(n2 < kOneMf);
	see(em::value_noise1(kHalf, 1234u, 0).raw == n.raw); // determinista
	// tileable: x y x+period dan el mismo valor (period=8).
	const MF a = em::value_noise1(mf12(8192), 99u, 8);
	const MF b = em::value_noise1(mf12(8192) + mf_int(8), 99u, 8);
	eq(a, b);
	const MF f = em::fbm1(kHalf, 55u, 3, kTwoMf, kHalf, 0);
	see(!(f < MF::zero()));
	see(f < kOneMf);
	case_end();
}

// ============================================================================
//  minifloat_math (solo MF: el fixed no tiene trascendentes)
// ============================================================================

void t_mf_math() {
	case_begin("sqrt/exp/exp2/log/pow mf", 0, 32);
	eq(em::sqrt(mf12(16384)), kTwoMf);             // sqrt(4)=2
	eq(em::sqrt(mf12(4096)), kOneMf);              // sqrt(1)=1
	eq(em::exp2(MF::zero()), kOneMf);              // 2^0=1
	eq(em::exp2(kOneMf), kTwoMf);                  // 2^1=2
	eq(em::exp2(MF::zero() - kOneMf), kHalf);      // 2^-1=0.5
	eq(em::exp(MF::zero()), kOneMf);               // e^0=1
	eq(em::log(kOneMf), MF::zero());               // ln(1)=0
	eq(em::log2(mf12(8192)), kOneMf);              // log2(2)=1
	eq(em::pow(kTwoMf, mf12(12288)), mf_int(8));   // 2^3=8
	eq(em::hypot(kOneMf, MF::zero()), kOneMf);
	case_end();
	case_begin("sin/cos/tan mf", 0, 32);
	eq(em::sin(MF::zero()), MF::zero());
	eq(em::cos(MF::zero()), kOneMf);
	eq(em::sin(kHalfPiMf), kOneMf);          // sin(π/2)=1
	eq(em::cos(kPiMf), MF::zero() - kOneMf); // cos(π)=-1
	eq(em::tan(MF::zero()), MF::zero());
	// identidad pitagórica en un ángulo no trivial: sin²+cos² = 1
	const MF s = em::sin(mf12(2048));
	const MF c = em::cos(mf12(2048));
	eq(em::sqrt(em::mul_add(s, s, c * c)), kOneMf);
	case_end();
	case_begin("atan2/asin/acos/wrap mf", 0, 32);
	eq(em::atan2(MF::zero(), kOneMf), MF::zero()); // atan2(0,1)=0
	eq(em::atan2(kOneMf, MF::zero()), kHalfPiMf);  // atan2(1,0)=π/2
	eq(em::asin(MF::zero()), MF::zero());
	eq(em::acos(kOneMf), MF::zero());
	eq(em::wrap_angle(kHalfPiMf), kHalfPiMf);              // ya plegado
	eq(em::wrap_angle(kHalfPiMf + kTwoMf * kPiMf), kHalfPiMf); // +2π no cambia
	eq(em::angle_diff(kHalfPiMf, kHalfPiMf), MF::zero());
	case_end();
}

// ============================================================================
//  minifloat_fixed: operaciones ENTRE tipos (matriciales sobre todo)
// ============================================================================

void t_intertype() {
	case_begin("mul_fixed/mul_fix88", 4, 4);
	eq(er::mul_fixed(kOneMf, r12(4096)), r12(4096)); // r=1.0 -> igual
	eq(er::mul_fixed(kHalf, r12(4096)), r12(2048));  // r=0.5 -> mitad
	eq(er::mul_fixed(kHalf, r8(256)), r8(128));
	eq(wrap_fix(er::mul_fix(kHalf, static_cast<er::fix>(4096))), r12(2048));
	eq(wrap_fix88(er::mul_fix88(kHalf, static_cast<er::fix88>(256))), r8(128));
	case_end();
	case_begin("fixed_to_mf/mf_to_fixed", 4, 2);
	eq(er::mf_to_fixed<12>(kOneMf), r12(4096));
	eq(er::mf_to_fixed<8>(kOneMf), r8(256));
	eq(er::fixed_to_mf(r12(4096)), kOneMf);
	eq(er::fixed_to_mf(r8(256)), kOneMf);
	eq(er::fix_to_mf(static_cast<er::fix>(2048)), kHalf);
	eq(wrap_fix(er::mf_to_fix(kHalf)), r12(2048));
	eq(wrap_fix88(er::mf_to_fix88(kHalf)), r8(128));
	case_end();
	// Matriz MF (rotación/escala) x coordenada fixed: la vía inter-tipo es `transform`
	// (no `Mat<MF>*Vec<q12>`, que el engine prohíbe a propósito: exige el MISMO escalar).
	case_begin("transform(Mat<MF>,Vec<q12>)", 1, 2);
	{
		const em::Mat<3, MF> id = em::Mat<3, MF>::identity();
		const em::Vec<3, q12> p {{r12(2048), r12(4096), r12(6144)}};
		const em::Vec<3, q12> r = er::transform(id, p);
		eq(r.v[0], r12(2048));
		eq(r.v[1], r12(4096));
		eq(r.v[2], r12(6144));
		// Escala por 2 -> (1,2,3)
		em::Mat<3, MF> m = em::Mat<3, MF>::identity();
		m.m[0][0] = kTwoMf;
		m.m[1][1] = kTwoMf;
		m.m[2][2] = kTwoMf;
		const em::Vec<3, q12> sc = er::transform(m, p);
		eq(sc.v[0], r12(4096));
		eq(sc.v[1], r12(8192));
		eq(sc.v[2], r12(12288));
		// Rotación de 90° alrededor de Z: (1,0,0) -> (0,1,0)
		em::Mat<3, MF> rot = em::Mat<3, MF>::identity();
		rot.m[0][0] = MF::zero();
		rot.m[0][1] = MF::zero() - kOneMf;
		rot.m[1][0] = kOneMf;
		rot.m[1][1] = MF::zero();
		const em::Vec<3, q12> rr = er::transform(rot, em::Vec<3, q12> {{r12(4096), r12(0), r12(0)}});
		eq(rr.v[0], r12(0));
		eq(rr.v[1], r12(4096));
	}
	case_end();
	case_begin("transform(Mat<MF>,Vec<q8>)", 2, 2);
	{
		em::Mat<3, MF> m = em::Mat<3, MF>::identity();
		m.m[0][0] = kHalf;
		const em::Vec<3, q8> p {{r8(256), r8(512), r8(768)}};
		const em::Vec<3, q8> r = er::transform(m, p);
		eq(r.v[0], r8(128));
		eq(r.v[1], r8(512));
	}
	case_end();
	case_begin("transform_fix/fix88", 2, 2);
	{
		const em::Mat<3, MF> id = em::Mat<3, MF>::identity();
		const em::Vec<3, er::fix> pf {{static_cast<er::fix>(1024), static_cast<er::fix>(2048),
					      static_cast<er::fix>(3072)}};
		const em::Vec<3, er::fix> rf = er::transform_fix(id, pf);
		eq(er::fix_to_mf(rf.v[0]), er::fix_to_mf(static_cast<er::fix>(1024)));
		const em::Vec<3, er::fix88> p8 {{static_cast<er::fix88>(64), static_cast<er::fix88>(128),
						 static_cast<er::fix88>(192)}};
		const em::Vec<3, er::fix88> rv = er::transform_fix88(id, p8);
		eq(er::fix88_to_mf(rv.v[1]), er::fix88_to_mf(static_cast<er::fix88>(128)));
	}
	case_end();
	case_begin("transform_point/project mf", 0, 16);
	{
		const em::Mat<4, MF> id = em::Mat<4, MF>::identity();
		const em::Vec<3, q12> p {{r12(1024), r12(2048), r12(3072)}};
		const em::Vec<4, q12> h = er::transform_point(id, p);
		eq(h.v[0], r12(1024));
		eq(h.v[3], r12(4096)); // w = 1.0
		const em::Vec<3, MF> proj = er::project(id, p);
		eq(proj.v[0], mf12(1024));
		eq(proj.v[1], mf12(2048));
	}
	case_end();
}

// ============================================================================
//  utilidades enteras: isqrt y fast_div
// ============================================================================

void t_integer_utils() {
	case_begin("isqrt", 3, 0);
	// Salidas del port. `isqrt` es RÁPIDO y puede SUBESTIMAR (como el original): es exacto
	// en muchas entradas pero devuelve `floor(sqrt(n)) - 1` en otras (9->2, 36->5, 49->6,
	// 1000000->999). Los dorados fijan ese comportamiento; las invariantes lo acotan.
	eqi(static_cast<s32>(eng::isqrt(0u)), 0);
	eqi(static_cast<s32>(eng::isqrt(1u)), 1);
	eqi(static_cast<s32>(eng::isqrt(2u)), 1);
	eqi(static_cast<s32>(eng::isqrt(4u)), 2);
	eqi(static_cast<s32>(eng::isqrt(9u)), 2);        // subestima 1
	eqi(static_cast<s32>(eng::isqrt(16u)), 4);
	eqi(static_cast<s32>(eng::isqrt(36u)), 5);       // subestima 1
	eqi(static_cast<s32>(eng::isqrt(100u)), 10);
	eqi(static_cast<s32>(eng::isqrt(255u)), 15);
	eqi(static_cast<s32>(eng::isqrt(256u)), 16);
	eqi(static_cast<s32>(eng::isqrt(4096u)), 64);
	eqi(static_cast<s32>(eng::isqrt(1000000u)), 999); // subestima 1
	// Invariantes sin float: nunca sobreestima y en [0,255] subestima como mucho 1.
	for (eng::u32 n = 0; n <= 255u; ++n) {
		const s32 s = static_cast<s32>(eng::isqrt(n));
		see(s * s <= static_cast<s32>(n));
		see((s + 2) * (s + 2) > static_cast<s32>(n));
	}
	case_end();

	case_begin("fast_div/wrap_period", 3, 0);
	eqi(static_cast<s32>(eng::fast_div<8>::q(100u)), 12);  // potencia de dos -> shift
	eqi(static_cast<s32>(eng::fast_div<8>::r(100u)), 4);
	eqi(static_cast<s32>(eng::fast_div<10>::q(100u)), 10); // no potencia de dos
	eqi(static_cast<s32>(eng::fast_div<10>::r(100u)), 0);
	eqi(static_cast<s32>(eng::fast_div<3>::q(100u)), 33);
	eqi(static_cast<s32>(eng::fast_div<3>::r(100u)), 1);
	eng::u32 qq = 0, rr = 0;
	eng::fast_div<7>::qr(100u, qq, rr);
	eqi(static_cast<s32>(qq), 14);
	eqi(static_cast<s32>(rr), 2);
	eng::runtime_div::qr(100u, 7u, qq, rr);
	eqi(static_cast<s32>(qq), 14);
	eqi(static_cast<s32>(rr), 2);
	see(eng::is_pow2(8u));
	see(!eng::is_pow2(10u));
	eqi(static_cast<s32>(eng::ilog2(8u)), 3);
	eqi(eng::wrap_period(-1, 8u), 7);   // potencia de dos: máscara, correcto con negativos
	eqi(eng::wrap_period(-3, 10u), 7);  // no potencia de dos: módulo con signo
	eqi(eng::wrap_period(15, 8u), 7);
	eqi(eng::asr_floor(-5, 1u), -3);    // floor(-2.5) = -3
	case_end();
}

// ============================================================================
//  ángulos 4.12 (tabla de seno exacta) y sombreado de luz
// ============================================================================

void t_angles() {
	case_begin("sin/cos (turns)", 1, 8);
	eqi(er::sin(er::turns(0)).v, 0);
	eqi(er::sin(er::turns(er::kHalfPi)).v, 4096); // sin(π/2) = 1.0
	eqi(er::sin(er::turns(2048)).v, 0);           // sin(π) = 0
	eqi(er::sin(er::turns(3072)).v, -4096);       // sin(3π/2) = -1.0
	eqi(er::cos(er::turns(0)).v, 4096);           // cos(0) = 1.0
	eqi(er::cos(er::turns(er::kHalfPi)).v, 0);
	eqi(er::cos(er::turns(2048)).v, -4096);       // cos(π) = -1.0
	// Identidad pitagórica sobre toda la tabla (el redondeo del 4.12 deja ~ULPs).
	for (eng::u16 a = 0; a < er::kAngleSteps; a = static_cast<eng::u16>(a + 137u)) {
		const s32 s = er::sin(er::turns(a)).v;
		const s32 c = er::cos(er::turns(a)).v;
		eqi(((s * s + c * c) + 2048) >> 12, 4096); // 1.0 en 4.12
	}
	case_end();
}

/// Tabla de recíprocos de raíz cuadrada de prueba: 32768 en todas las entradas salvo la
/// última (511), que es 65535, para comprobar el recorte del índice.
struct FlatInvSqrt {
	constexpr eng::u16 operator[](eng::u16 i) const { return i == 511u ? 65535u : 32768u; }
};

void t_light() {
	case_begin("hi16/light_ops::shade", 3, 0);
	eqi(em::hi16(0), 0);
	eqi(em::hi16(0x00010000), 1);
	eqi(em::hi16(0x7fff0000), 0x7fff);
	// shade = (hi16(v) * inv_sqrt[clamp(hi16(e1_sq),511)]) >> 16
	eqi(em::light_ops<>::shade(0x00040000, 0x00000000, FlatInvSqrt {}), 2); // (4*32768)>>16
	eqi(em::light_ops<>::shade(0x00010000, 0x00000000, FlatInvSqrt {}), 0); // (1*32768)>>16
	eqi(em::light_ops<>::shade(0x00040000, 0x02580000, FlatInvSqrt {}), 3); // clamp a 511
	case_end();
}

// ============================================================================
//  mesh3d: visibilidad de caras y claves de orden Z
// ============================================================================

void t_mesh3d() {
	case_begin("mesh3d culling/orden", 3, 0);
	const m3::Vec3 a = m3::vec3(0, 0, 0);
	const m3::Vec3 b = m3::vec3(1, 0, 0);
	const m3::Vec3 c = m3::vec3(0, 1, 0);
	const m3::Vec3 front = m3::vec3(0, 0, 1);
	const m3::Vec3 back = m3::vec3(0, 0, -1);
	eqi(m3::face_signed_area(a, b, c, front), 1);
	eqi(m3::face_signed_area(a, b, c, back), -1);
	see(m3::face_visible(a, b, c, front));
	see(!m3::face_visible(a, b, c, back));
	eqi(m3::face_z_sum(m3::vec3(0, 0, 3), m3::vec3(0, 0, 2), m3::vec3(0, 0, 1)), 6);
	eqi(m3::face_z_min(m3::vec3(0, 0, 3), m3::vec3(0, 0, -1), m3::vec3(0, 0, 2)), -1);
	case_end();
}

// ============================================================================
//  Ejecuta todos los casos
// ============================================================================

void run_all() {
	g_math_report.magic = k_math_magic;
	g_math_report.version = k_math_version;
	g_math_report.case_count = 0;
	g_math_report.failed_count = 0;
	g_math_report.checks = 0;

	t_vec_basics_all();
	t_dot_all();
	t_mat_mul_q12();
	t_mat_mul_mf();
	t_mulnorm_divnorm();
	t_interp_all();
	t_easing_all();
	t_repeat_all();
	t_scalar_ops_all();
	t_geometry_q12();
	t_geometry_mf();
	t_spline();
	t_noise_mf();
	t_mf_math();
	t_intertype();
	t_integer_utils();
	t_angles();
	t_light();
	t_mesh3d();
}

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::debug::mark_init_started(g_eng_run_status);

	run_all();

	// El detalle del run-status resume el resultado: nº de fallos (alto) y de casos (bajo).
	eng::debug::mark_ready(g_eng_run_status,
			       (g_math_report.failed_count << 16) | (g_math_report.case_count & 0xffffu));

	// El runner necesita margen para leer `g_math_report` por el canal lateral; se
	// esperan ~240 frames PAL y se vuelve a Workbench sin haber tocado el display.
	eng::amiga::MinimalBackend backend {};
	backend.boot();
	for (u32 i = 0; i < 240u; ++i) {
		backend.wait_vblank();
		eng::debug::mark_frame(g_eng_run_status, i);
	}
	return 0;
}
