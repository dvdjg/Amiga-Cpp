#pragma once

/// \file minifloat_math.hpp
/// Funciones matemáticas clásicas sobre `MiniFloat16`: `sqrt`, `exp`, `exp2`/`pow2`,
/// `log`, `log2`/`log10`, `pow`, `hypot`, trigonometría (`sin`/`cos`/`tan` y `sincos`)
/// e inversas (`atan`/`atan2`/`asin`/`acos`), implementadas **solo con aritmética de 16
/// bits** (nada de `float`, nada de `libgcc`). El objetivo es que el escalar
/// (`minifloat.hpp`) sea "casi como un float" dentro de sus ~10 bits de mantisa.
///
/// Estrategia (común a todas): reducción de rango + serie de Taylor/minimax evaluada
/// en Horner. La reducción se hace con operaciones exactas o de error acotado para no
/// perder los 10 bits del tipo antes de la serie:
///
///   - `exp(x)`: se calcula `z = x·log2e` en **Q4.11** (con `muls.w`), se parte
///     `z = n + f` y `2^f` se evalúa con una serie en **Q1.14**; `2^n` se aplica
///     ajustando el exponente. No hay "partir por la mitad y elevar al cuadrado", así
///     que no se amplifica el error: ~10 bits en todo el rango.
///   - `log(x)`: se separa `x = m·2^k` (con `m` en [1,2)) y se usa la serie de `atanh`,
///     `log(m) = 2·(t + t³/3 + t⁵/5 + …)` con `t = (m-1)/(m+1) <= 1/3` (converge
///     rápido). `k·ln2` se calcula aparte, sin cancelación.
///   - `sqrt(x)`: se parte el exponente en par/impar (exacto) y se itera Newton
///     `y = (y + m/y)/2` sobre la mantisa normalizada.
///   - `pow(base, e)`: si `e` es un entero pequeño (|e| <= 64) se calcula por
///     **cuadrado y multiplicación** (exacto para potencias exactas y admite base
///     negativa); si no, `exp(e·log(base))`.
///   - `sin/cos`: reducción a `r` en `[-π/4, π/4]` con **Cody-Waite** (`π/2 = 1.5 +
///     c2 + c3`; el primer trozo `1.5` tiene pocos bits, así que `x − n·1.5` es EXACTO
///     por Sterbenz) y series de Taylor. Los argumentos grandes pierden precisión al
///     reducir: el dominio fiable es `|x| <= 2π` (ver la doc).
///   - `atan/atan2/asin/acos`: polinomio minimax de `atan` en [0,1] (error < 2e-5) y
///     reducción `atan(x) = π/2 − atan(1/x)` para `|x| > 1`; `asin/acos` se apoyan en
///     `atan2` y `sqrt`.
///
/// Las rutinas más pequeñas y calientes llevan `[[gnu::always_inline]]`: en 68000 una
/// llamada (`jsr`/`rts`, guardar/restaurar registros) cuesta más que el propio cálculo,
/// así que conviene que el compilador las funda con el llamador.
///
/// Uso acotado y honesto: `MiniFloat16` da ~3 dígitos decimales; estas funciones
/// heredan ese límite, no lo mejoran. Fuera del rango `[2^-14, 65504]` saturan (0/∞) y
/// `log(x <= 0)`, `sqrt(x < 0)`, `asin/acos` fuera de [-1,1] y base negativa de `pow`
/// con exponente no entero son **indefinidos** (devuelven ∞, por contrato sencillo y sin
/// NaN).
///
/// Restricciones del engine: `gnu++23`, sin STL, sin excepciones, sin RTTI, sin
/// asignación dinámica. Depende de `eng/core/minifloat.hpp` y de `eng/core/arith.hpp`
/// (para forzar `muls.w` en el núcleo Q1.14).
///
/// **Estado de verificación: verificada por demo** — `demos/amiga/084_mf_rotation`
/// construye una rotación 3D con `sin`/`cos` de `MiniFloat16` y un self-test en hardware
/// de `sin(π/2)`, `exp(0)` y `sqrt(4)` (build/run/analyze OK). Ampliada por el test host
/// `tests/host/057_minifloat16_math`.

#include <eng/core/arith.hpp>
#include <eng/core/minifloat.hpp>
#include <eng/core/numeric_traits.hpp>
#include <eng/core/scalar_fwd.hpp>

/// Fuerza el inline donde una llamada cuesta más que el cálculo (68000). Se anula al
/// final del fichero para no contaminar al que incluye.
#if defined(__GNUC__) || defined(__clang__)
#define ENG_MF_AI [[gnu::always_inline]]
#else
#define ENG_MF_AI
#endif

namespace eng::math {

namespace mfdetail {

using MF = MiniFloat16;

// ----------------------------------------------------------------------------
//  Diagnóstico de dominio en COMPILACIÓN: si una CONSTANTE fuera del dominio fiable
//  llega a estas funciones (declaradas, sin definir) durante la evaluación constante,
//  el compilador falla y nombra el límite. En runtime la rama `if consteval` no se
//  ejecuta, así que nunca se llaman ni se enlazan.
// ----------------------------------------------------------------------------
void mf16_domain_sin_cos_must_be_within_2pi();
void mf16_domain_exp_must_be_within_pm11();
void mf16_domain_exp2_must_be_within_neg15_16();
void mf16_domain_log_must_be_positive();
void mf16_domain_sqrt_must_be_non_negative();
void mf16_domain_asin_acos_must_be_within_pm1();

// ============================================================================
//  Constantes (construidas en compile-time; el `float` solo vive aquí)
// ============================================================================

inline constexpr MF k_pi = MF(3.14159265358979f);
inline constexpr MF k_half_pi = MF(1.57079632679490f);
inline constexpr MF k_two_pi = MF(6.28318530717959f);
inline constexpr MF k_inv_two_pi = MF(0.159154943091895f); // 1/(2π)
inline constexpr MF k_inv_half_pi = MF(0.636619772367581f); // 2/π
inline constexpr MF k_ln2 = MF(0.693147180559945f);
inline constexpr MF k_inv_ln2 = MF(1.44269504088896f);
inline constexpr MF k_e = MF(2.71828182845905f);
inline constexpr MF k_sqrt2 = MF(1.41421356237310f);
inline constexpr MF k_log10_2 = MF(0.30102999566398f);  // log10(2)
inline constexpr MF k_log10_e = MF(0.43429448190325f);  // log10(e)
inline constexpr MF k_half = MF(0.5f);
inline constexpr MF k_one = MF(1.0f);
inline constexpr MF k_two = MF(2.0f);

// Trozos de Cody-Waite para π/2. `c1 = 1.5` tiene mantisa corta: `n·c1` es exacto
// para |n| pequeño y `x − n·c1` lo es por Sterbenz, así que la cancelación no pierde
// bits. c2 y c3 recogen el resto de π/2.
inline constexpr float k_pio2_c1f = 1.5f;
inline constexpr MF k_pio2_c1 = MF(k_pio2_c1f);
inline constexpr float k_pio2_c2f = 1.57079632679490f - k_pio2_c1f;
inline constexpr MF k_pio2_c2 = MF(k_pio2_c2f);
inline constexpr float k_pio2_c3f = 1.57079632679490f - k_pio2_c1f - static_cast<float>(k_pio2_c2);
inline constexpr MF k_pio2_c3 = MF(k_pio2_c3f);

// ============================================================================
//  Helpers internos
// ============================================================================

/// `x · 2^k` ajustando el campo exponente (exacto salvo saturación 0/∞). Evita la
/// multiplicación por potencias de dos. Con `e == 0` redondea al mínimo normal, igual
/// que la aritmética del tipo.
[[nodiscard]] ENG_MF_AI constexpr MF mf_ldexp(MF x, int k) {
	const eng::u16 mag = static_cast<eng::u16>(x.raw & 0x7FFFu);
	if (mag == 0u || mag >= MF::exp_mask) return x;
	const int e = static_cast<int>((x.raw >> 10) & 31) + k;
	const eng::u16 s = static_cast<eng::u16>(x.raw & MF::sign_mask);
	if (e >= MF::exp_inf) return MF::from_raw(static_cast<eng::u16>(s | MF::exp_mask));
	if (e <= 0) return MF::from_raw(e == 0 ? static_cast<eng::u16>(s | (1u << 10)) : s);
	return MF::from_raw(
		static_cast<eng::u16>(s | (static_cast<eng::u16>(e) << 10) | (x.raw & MF::man_mask)));
}

/// Entero → `MiniFloat16` sin `float` (para los multiplicadores de Cody-Waite). Exacto
/// en el rango pequeño que se usa; trunca por encima de 10 bits de mantisa.
[[nodiscard]] ENG_MF_AI constexpr MF mf_from_int(int n) {
	if (n == 0) return MF::zero();
	const bool neg = n < 0;
	eng::u32 a = static_cast<eng::u32>(neg ? -n : n);
	int msb = 0;
	while ((a >> (msb + 1)) != 0u) ++msb;
	eng::u16 mant;
	if (msb > 10)
		mant = static_cast<eng::u16>((a >> (msb - 10)) & 0x3FFu);
	else
		mant = static_cast<eng::u16>((a << (10 - msb)) & 0x3FFu);
	const int e = msb + MF::bias;
	return MF::from_raw(
		static_cast<eng::u16>((neg ? MF::sign_mask : 0u) | (static_cast<eng::u16>(e) << 10) | mant));
}

/// `MiniFloat16` → entero truncando hacia cero. Seguro para |x| < 32768; satura fuera.
[[nodiscard]] ENG_MF_AI constexpr int mf_to_int_trunc(MF x) {
	const int e = static_cast<int>((x.raw >> 10) & 31) - MF::bias;
	if (e < 0) return 0;
	if (e > 14) return (x.raw & MF::sign_mask) != 0u ? -32767 : 32767;
	const int mant = 0x400 | (x.raw & MF::man_mask);
	const int v = (e <= 10) ? (mant >> (10 - e)) : (mant << (e - 10));
	return (x.raw & MF::sign_mask) != 0u ? -v : v;
}

/// Entero más cercano (empate hacia fuera), sin `float`.
[[nodiscard]] ENG_MF_AI constexpr int mf_round_int(MF x) {
	const MF half = MF(0.5f);
	return mf_to_int_trunc((x.raw & MF::sign_mask) != 0u ? x - half : x + half);
}

/// `base^n` (n >= 1) por cuadrado y multiplicación. No usa `exp`/`log`: es el camino
/// exacto para potencias enteras.
[[nodiscard]] ENG_MF_AI constexpr MF mf_pow_int(MF base, int n) {
	MF result = MF::one();
	MF b = base;
	while (n != 0) {
		if ((n & 1) != 0) result = result * b;
		n >>= 1;
		if (n != 0) b = b * b;
	}
	return result;
}

// ---------------------------------------------------------------------------
//  Núcleo Q1.14 (s16) para `exp`: evita la amplificación del "elevar al cuadrado"
// ---------------------------------------------------------------------------

using eng::s16;
using eng::s32;

/// Producto Q1.14 × Q1.14 -> Q1.14 con redondeo. `arith<s16>::mul` fuerza `muls.w`
/// (16×16→32) en 68000; el `>>14` es un `asr`. Los operandos del polinomio no llegan a
/// desbordar el intermedio de 32 bits.
[[nodiscard]] ENG_MF_AI constexpr s16 q14_mul(s16 a, s16 b) {
	s32 p = arith<s16>::mul(a, b); // escala 2^28
	p += 1 << 13;                  // redondeo al bit 14
	return static_cast<s16>(p >> 14);
}

/// Suma Q1.14 con saturación (los términos del polinomio son pequeños; la saturación es
/// una red de seguridad).
[[nodiscard]] ENG_MF_AI constexpr s16 q14_add(s16 a, s16 b) {
	const s32 s = static_cast<s32>(a) + static_cast<s32>(b);
	if (s > 32767) return 32767;
	if (s < -32768) return static_cast<s16>(-32768);
	return static_cast<s16>(s);
}

/// `MiniFloat16` -> Q4.11 (s16), redondeando. Exacto para `|x| < 16` con mantisa de 10
/// bits; por debajo de `2^-11` redondea a 0 (error absoluto < 5e-4, aceptable para la
/// precisión del tipo).
[[nodiscard]] ENG_MF_AI constexpr s16 mf16_to_q11(MF x) {
	if (x.is_zero()) return 0;
	const bool neg = (x.raw & MF::sign_mask) != 0u;
	const int ef = static_cast<int>((x.raw >> 10) & 31) - MF::bias;
	const s32 mant = 0x400 | (x.raw & MF::man_mask); // [1024, 2047] = value·1024·2^-ef
	const int sh = ef + 1;                           // value·2^11 = mant·2^sh
	s32 q;
	if (sh >= 0)
		q = mant << sh;
	else if (sh > -12)
		q = (mant + (1 << (-sh - 1))) >> (-sh);
	else
		q = 0;
	if (q > 32767) q = 32767;
	return static_cast<s16>(neg ? -q : q);
}

/// Q1.14 (s16) -> `MiniFloat16` con redondeo al más cercano. Conversión manual: nada de
/// `float` (que en 68000 arrastraría `libgcc`).
[[nodiscard]] ENG_MF_AI constexpr MF q14_to_mf16(s16 v) {
	if (v == 0) return MF::zero();
	const bool neg = v < 0;
	eng::u32 a = static_cast<eng::u32>(neg ? -static_cast<s32>(v) : static_cast<s32>(v));
	int msb = 0;
	while ((a >> (msb + 1)) != 0u) ++msb;
	int ef = msb - 14; // value = a/2^14, con el 1 implícito en el bit `msb`
	eng::u32 m;
	const int shift = 10 - msb;
	if (shift >= 0) {
		m = a << shift;
	} else {
		const eng::u32 rem = a & ((1u << (-shift)) - 1u);
		m = a >> (-shift);
		if (rem >= (1u << (-shift - 1))) ++m; // medio ulp hacia arriba
	}
	if ((m & 0x800u) != 0u) { // el redondeo desbordó el bit 10
		m >>= 1;
		++ef;
	}
	const int e = ef + MF::bias;
	const eng::u16 s = static_cast<eng::u16>(neg ? MF::sign_mask : 0u);
	if (e <= 0) return MF::from_raw(e == 0 ? static_cast<eng::u16>(s | (1u << 10)) : s);
	if (e >= MF::exp_inf) return MF::from_raw(static_cast<eng::u16>(s | MF::exp_mask));
	return MF::from_raw(static_cast<eng::u16>(s | (static_cast<eng::u16>(e) << 10) | (m & 0x3FFu)));
}

/// `2^f = exp(f·ln2)` para `f` en [-0.5, 0.5], en Q1.14, por serie de Taylor de 8
/// términos (`g = f·ln2`, `|g| <= 0.347`). Se evalúa SOLO en compilación, para
/// materializar la tabla `k_exp2_q14`; en runtime la usa la interpolación de abajo
/// (menos instrucciones: 1 `muls.w` + adds en vez de 16 `muls.w`).
[[nodiscard]] constexpr s16 q14_exp2_poly(s16 f14) {
	const s16 g = q14_mul(f14, 11356); // ln2 en Q1.14
	s16 p = 3;                         // 1/5040
	p = q14_add(23, q14_mul(p, g));    // 1/720
	p = q14_add(137, q14_mul(p, g));   // 1/120
	p = q14_add(683, q14_mul(p, g));   // 1/24
	p = q14_add(2731, q14_mul(p, g));  // 1/6
	p = q14_add(8192, q14_mul(p, g));  // 1/2
	p = q14_add(16384, q14_mul(p, g)); // 1
	p = q14_add(16384, q14_mul(p, g)); // 1 + ...
	return p;
}

/// Tabla `2^(-0.5 + i/64)` en Q1.14, `i in [0,64]` (65 nodos, 130 bytes). Materializada
/// en compile-time desde `q14_exp2_poly`; el error de interpolación lineal (~1.5e-5)
/// queda por debajo del redondeo del tipo.
inline constexpr eng::ct_array<s16, 65> k_exp2_q14 {[](eng::usize i) -> s16 {
	return q14_exp2_poly(static_cast<s16>(-8192 + static_cast<int>(i) * 256));
}};

/// `2^f` para `f` en [-0.5, 0.5] (Q1.14) por **tabla + interpolación lineal** (1 `muls.w`).
/// Sustituye a la serie en el camino caliente de `exp`/`exp2`.
[[nodiscard]] ENG_MF_AI constexpr s16 q14_exp2_frac(s16 f14) {
	const eng::u32 g = static_cast<eng::u32>(static_cast<s32>(f14) + 8192); // [0, 16384]
	const eng::u32 idx = g >> 8;                                           // 0..64
	if (idx >= 64u) return k_exp2_q14[64];
	const s16 a = k_exp2_q14[idx];
	const s16 delta = static_cast<s16>(k_exp2_q14[idx + 1u] - a);
	// `frac/256` en Q1.14 es `frac << 6`; `q14_mul` usa `muls.w` (nada de `__mulsi3`).
	const eng::u32 frac = g & 0xFFu;
	return static_cast<s16>(a + q14_mul(delta, static_cast<s16>(frac << 6)));
}

/// Núcleo compartido de `2^z`: parte `z` (Q4.11) en `n + f`, evalúa `2^f` en Q1.14 y
/// aplica `2^n` con `mf_ldexp`. Lo usan `exp` (con `z = x·log2e`) y `exp2` (con `z = x`).
[[nodiscard]] ENG_MF_AI constexpr MF mf_exp2_from_q11(s32 zq) {
	s32 n;
	if (zq >= 0)
		n = (zq + 1024) >> 11;
	else
		n = -(((-zq) + 1024) >> 11);
	const s32 fq = zq - (n << 11);             // f en [-0.5, 0.5]
	const s16 f14 = static_cast<s16>(fq << 3); // Q4.11 -> Q1.14
	return mf_ldexp(q14_to_mf16(q14_exp2_frac(f14)), static_cast<int>(n));
}

/// `log(m)` para `m` en [1,2) por la serie de `atanh` (t <= 1/3).
[[nodiscard]] ENG_MF_AI constexpr MF mf_log_m(MF m) {
	const MF t = (m - k_one) / (m + k_one);
	const MF t2 = t * t;
	MF p = MF(1.0f / 9.0f);
	p = mul_add(p, t2, MF(1.0f / 7.0f));
	p = mul_add(p, t2, MF(1.0f / 5.0f));
	p = mul_add(p, t2, MF(1.0f / 3.0f));
	p = mul_add(p, t2, k_one);
	return (t * p) * k_two;
}

/// `sin(r)` para `|r| <= π/4` (Taylor, error < 2^-19 antes de redondear).
///
/// Se probó una tabla de cuarto de onda (65 nodos Q1.14 + interpolación) y **no
/// compensa**: ahorra ~11 `muls.w` pero el índice (MF→entero) y la vuelta Q1.14→MF
/// añaden ~50 ramas, así que el tamaño crece (c_mf_loop 4849→5131 instr) para un
/// balance de ciclos casi nulo. La serie se queda.
[[nodiscard]] ENG_MF_AI constexpr MF mf_sin_small(MF r) {
	const MF r2 = r * r;
	MF p = MF(-1.0f / 5040.0f);
	p = mul_add(p, r2, MF(1.0f / 120.0f));
	p = mul_add(p, r2, MF(-1.0f / 6.0f));
	p = mul_add(p, r2, k_one);
	return r * p;
}

/// `cos(r)` para `|r| <= π/4` (Taylor, 5 términos).
[[nodiscard]] ENG_MF_AI constexpr MF mf_cos_small(MF r) {
	const MF r2 = r * r;
	MF p = MF(1.0f / 40320.0f);
	p = mul_add(p, r2, MF(-1.0f / 720.0f));
	p = mul_add(p, r2, MF(1.0f / 24.0f));
	p = mul_add(p, r2, MF(-0.5f));
	p = mul_add(p, r2, k_one);
	return p;
}

/// Reduce `x = n·(π/2) + r` con `r` en `[-π/4, π/4]`; deja `n mod 4` en `q`.
ENG_MF_AI constexpr void mf_reduce_pio2(MF x, MF& r, int& q) {
	const int n = mf_round_int(x * k_inv_half_pi);
	const MF nf = mf_from_int(n);
	r = x - nf * k_pio2_c1;
	r = r - nf * k_pio2_c2;
	r = r - nf * k_pio2_c3;
	q = n & 3;
}

/// `atan(x)` para `|x| <= 1`: minimax impar de grado 9 en `x²` (Rajan et al.), error
/// máximo ~2e-5 en [0,1]. En MiniFloat16 el error del polinomio queda por debajo del
/// redondeo del tipo.
[[nodiscard]] ENG_MF_AI constexpr MF mf_atan_unit(MF x) {
	const MF x2 = x * x;
	MF p = MF(0.0208351f);
	p = mul_add(p, x2, MF(-0.0851330f));
	p = mul_add(p, x2, MF(0.1801410f));
	p = mul_add(p, x2, MF(-0.3302995f));
	p = mul_add(p, x2, MF(0.9998660f));
	return x * p;
}

} // namespace mfdetail

// ============================================================================
//  API
// ============================================================================

/// Raíz cuadrada por Newton. `x < 0` es indefinido (devuelve ∞); `x = 0` o ∞ se
/// propagan.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 sqrt(MiniFloat16 x) {
	using namespace mfdetail;
	if consteval {
		if (x < MF::zero()) mf16_domain_sqrt_must_be_non_negative();
	}
	if (x.is_zero()) return x;
	if ((x.raw & MiniFloat16::sign_mask) != 0u || x.is_inf())
		return MiniFloat16::from_raw(MiniFloat16::exp_mask);
	int e = static_cast<int>((x.raw >> 10) & 31) - MiniFloat16::bias;
	MF m = MF::from_raw(static_cast<eng::u16>(0x3C00u | (x.raw & MiniFloat16::man_mask)));
	if ((e & 1) != 0) { // exponente impar -> mantisa en [2,4) y exponente par
		m = m * k_two;
		--e;
	}
	MF y = m; // arranque: y0 = m (convergencia cuadrática, error inicial < 2x)
	for (int i = 0; i < 5; ++i) y = (y + m / y) * k_half;
	return mf_ldexp(y, e / 2);
}

/// Exponencial `e^x`. Satura a ∞ por encima del máximo finito y a 0 por debajo del
/// mínimo normal.
///
/// `exp(x) = 2^(x·log2e)`. Se calcula `z = x·log2e` en Q4.11 (con `muls.w`), se parte
/// `z = n + f` y `2^f` se evalúa con la serie en Q1.14 y se aplica `2^n` ajustando el
/// exponente. A diferencia del "partir por la mitad y elevar al cuadrado", aquí no hay
/// amplificación del error: la precisión se mantiene ~10 bits en todo el rango.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 exp(MiniFloat16 x) {
	using namespace mfdetail;
	if consteval {
		if (!in_range(x, -11.5, 11.5)) mf16_domain_exp_must_be_within_pm11();
	}
	if (x.is_zero()) return MF::one();
	if (x.is_inf()) return (x.raw & MiniFloat16::sign_mask) != 0u ? MF::zero() : x;
	if (x > MF(12.0f)) return MF::from_raw(MiniFloat16::exp_mask);
	if (x < MF(-11.0f)) return MF::zero();

	const s32 xq = mf16_to_q11(x);                 // x en Q4.11
	const s32 zq = (xq * 23637 + (1 << 13)) >> 14; // ·log2e (Q1.14) -> Q4.11
	return mf_exp2_from_q11(zq);
}

/// `2^x` (exponencial en base 2). Es el núcleo de `exp` sin la multiplicación por
/// `log2e`: más rápido y exacto en los enteros (`2^10 = 1024`). Satura a ∞/0 fuera de
/// `[-15, 16]`.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 exp2(MiniFloat16 x) {
	using namespace mfdetail;
	if consteval {
		if (!in_range(x, -15.5, 16.5)) mf16_domain_exp2_must_be_within_neg15_16();
	}
	if (x.is_zero()) return MF::one();
	if (x.is_inf()) return (x.raw & MiniFloat16::sign_mask) != 0u ? MF::zero() : x;
	if (x > MF(16.0f)) return MF::from_raw(MiniFloat16::exp_mask);
	if (x < MF(-15.0f)) return MF::zero();
	return mf_exp2_from_q11(mf16_to_q11(x));
}

/// Alias de `exp2` (2^x). **No** confundir con `pow(x, 2)`, que es x² y va por el
/// camino entero exacto.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 pow2(MiniFloat16 x) { return exp2(x); }

/// Logaritmo natural. `x <= 0` es indefinido (devuelve ∞); 0 devuelve −∞.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 log(MiniFloat16 x) {
	using namespace mfdetail;
	if consteval {
		if (x <= MF::zero()) mf16_domain_log_must_be_positive();
	}
	if (x.is_zero())
		return MF::from_raw(static_cast<eng::u16>(MiniFloat16::sign_mask | MiniFloat16::exp_mask));
	if ((x.raw & MiniFloat16::sign_mask) != 0u) return MF::from_raw(MiniFloat16::exp_mask);
	if (x.is_inf()) return x;
	const int ef = static_cast<int>((x.raw >> 10) & 31);
	const MF m = MF::from_raw(static_cast<eng::u16>(0x3C00u | (x.raw & MiniFloat16::man_mask)));
	return mf_from_int(ef - MiniFloat16::bias) * k_ln2 + mf_log_m(m);
}

/// Logaritmo en base 2. Se apoya en el exponente (exacto en potencias de dos:
/// `log2(8) = 3`) y en `log(m)·log2(e)`. Mismo dominio que `log`.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 log2(MiniFloat16 x) {
	using namespace mfdetail;
	if consteval {
		if (x <= MF::zero()) mf16_domain_log_must_be_positive();
	}
	if (x.is_zero())
		return MF::from_raw(static_cast<eng::u16>(MiniFloat16::sign_mask | MiniFloat16::exp_mask));
	if ((x.raw & MiniFloat16::sign_mask) != 0u) return MF::from_raw(MiniFloat16::exp_mask);
	if (x.is_inf()) return x;
	const int ef = static_cast<int>((x.raw >> 10) & 31);
	const MF m = MF::from_raw(static_cast<eng::u16>(0x3C00u | (x.raw & MiniFloat16::man_mask)));
	return mf_from_int(ef - MiniFloat16::bias) + mf_log_m(m) * k_inv_ln2;
}

/// Logaritmo en base 10. Mismo dominio que `log`.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 log10(MiniFloat16 x) {
	using namespace mfdetail;
	if consteval {
		if (x <= MF::zero()) mf16_domain_log_must_be_positive();
	}
	if (x.is_zero())
		return MF::from_raw(static_cast<eng::u16>(MiniFloat16::sign_mask | MiniFloat16::exp_mask));
	if ((x.raw & MiniFloat16::sign_mask) != 0u) return MF::from_raw(MiniFloat16::exp_mask);
	if (x.is_inf()) return x;
	const int ef = static_cast<int>((x.raw >> 10) & 31);
	const MF m = MF::from_raw(static_cast<eng::u16>(0x3C00u | (x.raw & MiniFloat16::man_mask)));
	return mf_from_int(ef - MiniFloat16::bias) * k_log10_2 + mf_log_m(m) * k_log10_e;
}

/// `sqrt(x² + y²)` con escalado previo (`r = menor/mayor`) para no desbordar al
/// elevar al cuadrado. Infinito si alguno lo es.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 hypot(MiniFloat16 x, MiniFloat16 y) {
	using namespace mfdetail;
	if (x.is_inf() || y.is_inf()) return MF::from_raw(MiniFloat16::exp_mask);
	const bool nx = (x.raw & MiniFloat16::sign_mask) != 0u;
	const bool ny = (y.raw & MiniFloat16::sign_mask) != 0u;
	MF a = nx ? -x : x;
	MF b = ny ? -y : y;
	if (a < b) { // deja `a` como el mayor
		const MF t = a;
		a = b;
		b = t;
	}
	if (a.is_zero()) return MF::zero();
	const MF r = b / a;
	return a * sqrt(k_one + r * r);
}

/// Potencia `base^e`. Si `e` es entero (|e| <= 64) se resuelve por cuadrado y
/// multiplicación (exacto para potencias exactas y admite base negativa); si no, se
/// usa `exp(e·log(base))` y la base debe ser positiva.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 pow(MiniFloat16 base, MiniFloat16 e) {
	using namespace mfdetail;
	const int ni = mf_to_int_trunc(e);
	if (ni > -65 && ni < 65 && mf_from_int(ni) == e) { // exponente entero exacto
		if (ni == 0) return MF::one();
		MF p = mf_pow_int(base, ni < 0 ? -ni : ni);
		if (ni < 0) p = MF::one() / p;
		return p;
	}
	if (base.is_zero()) {
		if (e.is_zero()) return MF::one();
		return (e.raw & MiniFloat16::sign_mask) != 0u ? MF::from_raw(MiniFloat16::exp_mask)
							      : MF::zero();
	}
	if ((base.raw & MiniFloat16::sign_mask) != 0u) return MF::from_raw(MiniFloat16::exp_mask);
	return exp(e * log(base));
}

// Nota: `log`, `exp`, `sqrt`, `pow`, `sin`... usan los nombres del `<cmath>` del host;
// como toman `MiniFloat16`, el ADL los resuelve frente a los de `std::`. No hay
// ambigüedad.

/// Seno y coseno en una pasada (una sola reducción de rango, no dos). Dominio fiable
/// `|x| <= 2π`; con argumentos mayores la reducción pierde bits.
ENG_MF_AI constexpr void sincos(MiniFloat16 x, MiniFloat16& out_sin, MiniFloat16& out_cos) {
	using namespace mfdetail;
	if consteval { // dominio fiable |x| <= 2π (con margen para el redondeo de 2π a MF)
		if (!in_range(x, -6.3, 6.3)) mf16_domain_sin_cos_must_be_within_2pi();
	}
	if (x.is_zero()) {
		out_sin = x;
		out_cos = MF::one();
		return;
	}
	if (x.is_inf()) {
		out_sin = MF::zero();
		out_cos = MF::zero();
		return;
	}
	MF r;
	int q;
	mf_reduce_pio2(x, r, q);
	const MF sr = mf_sin_small(r);
	const MF cr = mf_cos_small(r);
	switch (q) {
	case 0: out_sin = sr; out_cos = cr; break;
	case 1: out_sin = cr; out_cos = -sr; break;
	case 2: out_sin = -sr; out_cos = -cr; break;
	default: out_sin = -cr; out_cos = sr; break;
	}
}

/// Seno. Dominio fiable `|x| <= 2π`; con argumentos mayores la reducción pierde bits.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 sin(MiniFloat16 x) {
	MiniFloat16 s;
	MiniFloat16 c;
	sincos(x, s, c);
	return s;
}

/// Coseno. Mismo dominio fiable que `sin`.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 cos(MiniFloat16 x) {
	MiniFloat16 s;
	MiniFloat16 c;
	sincos(x, s, c);
	return c;
}

/// Tangente `sin/cos`. Cerca de los polos (`cos ~ 0`) satura a ±∞.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 tan(MiniFloat16 x) {
	MiniFloat16 s;
	MiniFloat16 c;
	sincos(x, s, c);
	if (c.is_zero()) return MiniFloat16::from_raw(MiniFloat16::exp_mask);
	return s / c;
}

/// Pliega un ángulo a `[-π, π]`: `x − 2π·round(x/2π)`. O(1) (sin bucles), útil para
/// mantener los ángulos en el dominio fiable de `sin`/`cos` y para comparar direcciones.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 wrap_angle(MiniFloat16 x) {
	using namespace mfdetail;
	if (x.is_zero() || x.is_inf()) return MF::zero();
	const int n = mf_round_int(x * k_inv_two_pi);
	return x - mf_from_int(n) * k_two_pi;
}

/// Diferencia angular mínima `a − b` plegada a `[-π, π]`.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 angle_diff(MiniFloat16 a, MiniFloat16 b) {
	return wrap_angle(a - b);
}

/// Arco tangente en `(-π/2, π/2)`. `±∞` -> `±π/2`.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 atan(MiniFloat16 x) {
	using namespace mfdetail;
	if (x.is_zero()) return x;
	if (x.is_inf())
		return (x.raw & MiniFloat16::sign_mask) != 0u ? -k_half_pi : k_half_pi;
	const bool neg = (x.raw & MiniFloat16::sign_mask) != 0u;
	const MF a = neg ? -x : x;
	// |x| > 1: atan(x) = π/2 - atan(1/x) (el polinomio solo es válido en [0,1]).
	const MF r = (a > k_one) ? (k_half_pi - mf_atan_unit(k_one / a)) : mf_atan_unit(a);
	return neg ? -r : r;
}

/// Arco tangente de dos argumentos, en `(-π, π]`. Devuelve 0 para `(0,0)`.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 atan2(MiniFloat16 y, MiniFloat16 x) {
	using namespace mfdetail;
	if (x.is_zero()) {
		if (y.is_zero()) return MF::zero();
		return (y.raw & MiniFloat16::sign_mask) != 0u ? -k_half_pi : k_half_pi;
	}
	if (y.is_zero()) return (x.raw & MiniFloat16::sign_mask) != 0u ? k_pi : MF::zero();
	const MF q = atan(y / x); // en (-π/2, π/2)
	if ((x.raw & MiniFloat16::sign_mask) == 0u) return q;
	return (y.raw & MiniFloat16::sign_mask) != 0u ? (q - k_pi) : (q + k_pi);
}

/// Arco seno. Dominio `[-1, 1]`; fuera de él, ∞.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 asin(MiniFloat16 x) {
	using namespace mfdetail;
	if consteval {
		if (!in_range(x, -1.0, 1.0)) mf16_domain_asin_acos_must_be_within_pm1();
	}
	if (x > k_one || x < -k_one) return MF::from_raw(MiniFloat16::exp_mask);
	MF d = k_one - x * x; // sqrt(1 - x²)
	if ((d.raw & MiniFloat16::sign_mask) != 0u) d = MF::zero(); // guarda de redondeo
	return atan2(x, sqrt(d));
}

/// Arco coseno. Dominio `[-1, 1]`; fuera de él, ∞.
[[nodiscard]] ENG_MF_AI constexpr MiniFloat16 acos(MiniFloat16 x) {
	using namespace mfdetail;
	if consteval {
		if (!in_range(x, -1.0, 1.0)) mf16_domain_asin_acos_must_be_within_pm1();
	}
	if (x > k_one || x < -k_one) return MF::from_raw(MiniFloat16::exp_mask);
	MF d = k_one - x * x;
	if ((d.raw & MiniFloat16::sign_mask) != 0u) d = MF::zero();
	return atan2(sqrt(d), x);
}

/// `noise_traits` de `MiniFloat16` (vive en la cabecera del propio escalar): `MiniFloat16`
/// solo representa enteros exactos hasta 2048, así que la coordenada de ruido se acota.
template <>
struct noise_traits<MiniFloat16> {
	static constexpr double max_coord = 2048.0;
};

} // namespace eng::math

#undef ENG_MF_AI
