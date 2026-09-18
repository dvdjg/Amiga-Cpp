#pragma once

/// \file minifloat_fixed.hpp
/// Puente entre `MiniFloat16` y el vocabulario fixed-point **retro** (`fix` = 4.12,
/// `fix88` = 8.8): conversiones explícitas, producto mixto y, sobre todo, **transformar
/// coordenadas fijas con una matriz de ratios `MiniFloat16`**.
///
/// El caso típico de un efecto/juego: la parte lineal de una transformación (senos,
/// cosenos, escalas) vive en `MiniFloat16` (rango amplio, ~10 bits), y las coordenadas
/// de objeto/mundo viven en fixed de 16 bits con 4.12 o 8.8 bits de fracción (barato de
/// sumar e interpolar). Aquí se combinan sin degradar las coordenadas:
///
/// ```
///   Mat<N, MiniFloat16> (RATIO)  x  Vec<N, Coord12> (LONGITUD)  ->  Vec<N, Coord12>
/// ```
///
/// Cómo se hace (producto escalar **fusionado**): la matriz MF se convierte **una vez**
/// a 4.12 y cada fila acumula los productos `ratio·coordenada` en 32 bits con `muls.w`;
/// se normaliza con **un único** desplazamiento `>> 12`. La coordenada puede estar en
/// 4.12, 8.8 o entero (`q0`, el `Vec2` de `lib2d`) sin cambiar la fórmula, y NO pasa por
/// la mantisa de 10 bits del MF (conserva su fracción); tampoco se redondea producto a
/// producto. Como la razón se guarda en 4.12, las entradas de la matriz deben caber en
/// `[-8, 8]`.
///
/// Sobre tipos y tags: **no** se crea un tipo nuevo porque el engine ya tiene el tag que
/// hace falta —`eng::math::Fixed<s16,Frac>` distingue 4.12 de 8.8 por su parámetro
/// `Exp`, que es justo el dato que evita mezclarlos (ambos son `s16` en crudo). La parte
/// de razón la aporta `MiniFloat16`, que es otro tipo. Las funciones tipadas son la vía
/// recomendada; las variantes `*_fix`/`*_fix88` existen para interoperar con los ports
/// 1:1 que todavía trabajan con `s16` crudo.
///
/// Todas las conversiones son **explícitas y con nombre** (nada de conversiones
/// implícitas entre formatos, regla del engine). Cuando el valor no cabe en el destino
/// se **satura** (no envuelve). Restricciones: `gnu++23`, sin STL, sin excepciones, sin
/// asignación dinámica.

#include <eng/core/linalg.hpp>
#include <eng/core/minifloat.hpp>
#include <eng/core/numeric_traits.hpp>
#include <eng/retro/fixed_q.hpp>

/// Fuerza el inline donde la llamada cuesta más que el cálculo (68000). Macro local,
/// anulada al final para no contaminar a quien incluye.
#if defined(__GNUC__) || defined(__clang__)
#define ENG_MF_FIX_AI [[gnu::always_inline]]
#else
#define ENG_MF_FIX_AI
#endif

namespace eng::retro {

namespace detail {

using eng::s16;
using eng::s32;
using MF = eng::math::MiniFloat16;

// Diagnóstico de dominio en compilación: como la razón se guarda en 4.12 (±8), una
// entrada de matriz CONSTANTE fuera de `[-8, 8]` saturaría en silencio; aquí se avisa
// al compilar (mismo patrón que minifloat_math: llamada no-constexpr en `if consteval`).
void mf16_fix_domain_ratio_must_be_within_4_12();

/// `mant · 2^-frac` -> `MiniFloat16` (redondeo al más cercano). Genérico en `frac`, así
/// sirve para 4.12, 8.8 y cualquier otra escala.
[[nodiscard]] constexpr MF mf_from_fixed(s32 mant, int frac) {
	if (mant == 0) return MF::zero();
	const bool neg = mant < 0;
	eng::u32 a = neg ? (0u - static_cast<eng::u32>(mant)) : static_cast<eng::u32>(mant);
	int msb = 0;
	while ((a >> (msb + 1)) != 0u) ++msb; // posición del 1 implícito
	int ef = msb - frac;                   // exponente real del valor
	eng::u32 m;
	const int shift = 10 - msb; // lleva el 1 implícito al bit 10
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
	if (e <= 0) return MF::from_raw(static_cast<eng::u16>(e == 0 ? (s | (1u << 10)) : s));
	if (e >= MF::exp_inf) return MF::from_raw(static_cast<eng::u16>(s | MF::exp_mask));
	return MF::from_raw(
		static_cast<eng::u16>(s | (static_cast<eng::u16>(e) << 10) | (m & 0x3FFu)));
}

/// `MiniFloat16` -> `mant` de `frac` bits de fracción, **saturado** a `s16`. La
/// conversión a coordenada no puede representar `|x| >= 8` en 4.12 (o `>= 128` en 8.8).
[[nodiscard]] constexpr s16 mf_to_fixed(MF x, int frac) {
	const eng::u16 mag = static_cast<eng::u16>(x.raw & 0x7FFFu);
	if (mag == 0u) return 0;
	const bool neg = (x.raw & MF::sign_mask) != 0u;
	if (mag >= MF::exp_mask) return neg ? static_cast<s16>(-32768) : static_cast<s16>(32767);
	const int ef = static_cast<int>((x.raw >> 10) & 31) - MF::bias;
	const eng::u32 m = 0x400u | (x.raw & MF::man_mask); // [1024, 2047]
	const int shift = ef + frac - 10;                    // value·2^frac = m·2^shift
	eng::u32 q;
	if (shift >= 0) {
		// m >= 1024 = 2^10, así que a partir de shift 5 ya desborda el s16 positivo.
		if (shift >= 15 || (m << shift) > 32767u)
			return neg ? static_cast<s16>(-32768) : static_cast<s16>(32767);
		q = m << shift;
	} else {
		const int sh = -shift;
		q = (sh >= 32) ? 0u : (m + (1u << (sh - 1))) >> sh; // redondeo
	}
	if (neg) return (q >= 32768u) ? static_cast<s16>(-32768) : static_cast<s16>(-static_cast<s32>(q));
	return static_cast<s16>(q);
}

/// Multiplica-acumula con saturación para el producto escalar fusionado. El acumulador
/// es 32 bits; el tope `2^28` está MUY por encima de lo que puede sobrevivir al
/// desplazamiento de normalización (el resultado satura a `s16` antes), así que saturar
/// ahí no cambia el resultado y evita desbordar `s32` con 3-4 términos.
[[nodiscard]] constexpr s32 sat_mac(s32 acc, s16 a, s16 b) {
	const s32 s = acc + static_cast<s32>(a) * static_cast<s32>(b);
	if (s > (1 << 28)) return 1 << 28;
	if (s < -(1 << 28)) return -(1 << 28);
	return s;
}

/// Satura un acumulador a `s16`.
[[nodiscard]] constexpr s16 sat16(s32 v) {
	if (v > 32767) return 32767;
	if (v < -32768) return static_cast<s16>(-32768);
	return static_cast<s16>(v);
}

} // namespace detail

// ============================================================================
//  Coordenadas tipadas (el tag es el exponente de `Fixed`)
// ============================================================================

/// Coordenada de subpíxel 4.12 (LONGITUD): misma representación que un RATIO 4.12,
/// pero el alias documenta la intención. Distinta de `Coord88` en el tipo.
using Coord12 = eng::math::Fixed<s16, 12>;
/// Coordenada de subpíxel 8.8.
using Coord88 = eng::math::Fixed<s16, 8>;

// ============================================================================
//  Conversiones (explícitas)
// ============================================================================

/// `Fixed<s16, Frac>` -> `MiniFloat16` (puede saturar a 0/∞ si se sale del rango MF).
template <int Frac>
[[nodiscard]] ENG_MF_FIX_AI constexpr eng::math::MiniFloat16 fixed_to_mf(
	eng::math::Fixed<s16, Frac> v) {
	return detail::mf_from_fixed(v.v, Frac);
}

/// `MiniFloat16` -> `Fixed<s16, Frac>` (satura al rango del fixed destino).
template <int Frac>
[[nodiscard]] ENG_MF_FIX_AI constexpr eng::math::Fixed<s16, Frac> mf_to_fixed(
	eng::math::MiniFloat16 x) {
	return {detail::mf_to_fixed(x, Frac)};
}

/// `fix` crudo (4.12) -> `MiniFloat16`.
[[nodiscard]] ENG_MF_FIX_AI constexpr eng::math::MiniFloat16 fix_to_mf(fix v) {
	return detail::mf_from_fixed(v, 12);
}
/// `MiniFloat16` -> `fix` crudo (4.12), saturado.
[[nodiscard]] ENG_MF_FIX_AI constexpr fix mf_to_fix(eng::math::MiniFloat16 x) {
	return detail::mf_to_fixed(x, 12);
}
/// `fix88` crudo (8.8) -> `MiniFloat16`.
[[nodiscard]] ENG_MF_FIX_AI constexpr eng::math::MiniFloat16 fix88_to_mf(fix88 v) {
	return detail::mf_from_fixed(v, 8);
}
/// `MiniFloat16` -> `fix88` crudo (8.8), saturado.
[[nodiscard]] ENG_MF_FIX_AI constexpr fix88 mf_to_fix88(eng::math::MiniFloat16 x) {
	return detail::mf_to_fixed(x, 8);
}

// ============================================================================
//  Producto mixto (ratio MF × valor fijo -> valor fijo)
// ============================================================================

/// `r · v` con `r` en MF (RATIO) y `v` en `Fixed<s16, Frac>`, resultado en el MISMO
/// fixed que `v` (un solo redondeo). La razón se lleva a 4.12, así que el resultado es
/// `(r·2^12 · v) >> 12`: válido para 4.12, 8.8 y entero (`q0`) sin cambiar de fórmula.
template <int Frac>
[[nodiscard]] ENG_MF_FIX_AI constexpr eng::math::Fixed<s16, Frac> mul_fixed(
	eng::math::MiniFloat16 r, eng::math::Fixed<s16, Frac> v) {
	if consteval { // la razón se lleva a 4.12 (±8): fuera de ahí saturaría en silencio
		if (!eng::math::in_range(r, -8.0, 8.0)) detail::mf16_fix_domain_ratio_must_be_within_4_12();
	}
	const s16 rq = detail::mf_to_fixed(r, 12);
	// Producto INTENCIONADAMENTE en punto fijo entero (no `mul_norm`, que aplica a
	// escalares con exponente en el tipo): la fusión `(rq·v)>>12` conserva la precisión
	// de la coordenada y evita normalizar dos veces.
	const s32 p = static_cast<s32>(rq) * static_cast<s32>(v.v);
	return {detail::sat16((p + 2048) >> 12)};
}

/// `r · v` con `v` en `fix` (4.12) crudo.
[[nodiscard]] ENG_MF_FIX_AI constexpr fix mul_fix(eng::math::MiniFloat16 r, fix v) {
	return mul_fixed(r, eng::math::Fixed<s16, 12> {v}).v;
}
/// `r · v` con `v` en `fix88` (8.8) crudo.
[[nodiscard]] ENG_MF_FIX_AI constexpr fix88 mul_fix88(eng::math::MiniFloat16 r, fix88 v) {
	return mul_fixed(r, eng::math::Fixed<s16, 8> {v}).v;
}

// ============================================================================
//  Transformación de coordenadas fijas con matriz MF
// ============================================================================

/// Matriz de ratios MF ya **convertida a 4.12**: conviene prepararla UNA vez y aplicarla
/// a muchos puntos (evita reconvertir la matriz por cada vértice).
template <int N>
struct RatioMat {
	s16 m[N][N];
};

/// Convierte una `Mat<N, MiniFloat16>` a 4.12 (con saturación). Comprueba en compilación
/// que las razones constantes caben en `[-8, 8]`.
template <int N>
[[nodiscard]] constexpr RatioMat<N> prepare_ratio(const eng::math::Mat<N, eng::math::MiniFloat16>& m) {
	if consteval {
		for (int i = 0; i < N; ++i)
			for (int k = 0; k < N; ++k)
				if (!eng::math::in_range(m.m[i][k], -8.0, 8.0))
					detail::mf16_fix_domain_ratio_must_be_within_4_12();
	}
	RatioMat<N> r {};
	for (int i = 0; i < N; ++i)
		for (int k = 0; k < N; ++k) r.m[i][k] = detail::mf_to_fixed(m.m[i][k], 12);
	return r;
}

/// `M · v` (ROTACIÓN/ESCALA) sobre la matriz ya preparada (4.12) y coordenadas fijas.
template <int N, int Frac>
[[nodiscard]] constexpr eng::math::Vec<N, eng::math::Fixed<s16, Frac>> transform(
	const RatioMat<N>& mq, const eng::math::Vec<N, eng::math::Fixed<s16, Frac>>& p) {
	eng::math::Vec<N, eng::math::Fixed<s16, Frac>> out {};
	for (int i = 0; i < N; ++i) {
		s32 acc = 0;
		for (int k = 0; k < N; ++k) acc = detail::sat_mac(acc, mq.m[i][k], p.v[k].v);
		out.v[i].v = detail::sat16((acc + 2048) >> 12);
	}
	return out;
}

/// `M · p` (ROTACIÓN/ESCALA) desde la matriz MF (la prepara al vuelo; usa la versión de
/// `RatioMat` si vas a transformar varios puntos).
template <int N, int Frac>
[[nodiscard]] constexpr eng::math::Vec<N, eng::math::Fixed<s16, Frac>> transform(
	const eng::math::Mat<N, eng::math::MiniFloat16>& m,
	const eng::math::Vec<N, eng::math::Fixed<s16, Frac>>& p) {
	return transform(prepare_ratio(m), p);
}

/// `M · p + t` (afín): traslación `t` en el mismo fixed que `p`.
template <int N, int Frac>
[[nodiscard]] constexpr eng::math::Vec<N, eng::math::Fixed<s16, Frac>> transform(
	const eng::math::Mat<N, eng::math::MiniFloat16>& m,
	const eng::math::Vec<N, eng::math::Fixed<s16, Frac>>& p,
	const eng::math::Vec<N, eng::math::Fixed<s16, Frac>>& t) {
	auto out = transform(m, p);
	for (int i = 0; i < N; ++i) out.v[i].v = detail::sat16(static_cast<s32>(out.v[i].v) + t.v[i].v);
	return out;
}

/// Punto 3D -> homogéneo 4D: `M · (p, 1)`. La `w` resultante queda en el mismo fixed
/// que `p` (vale 1 para una matriz afín, y otra cosa para una de proyección).
template <int Frac>
[[nodiscard]] constexpr eng::math::Vec<4, eng::math::Fixed<s16, Frac>> transform_point(
	const eng::math::Mat<4, eng::math::MiniFloat16>& m,
	const eng::math::Vec<3, eng::math::Fixed<s16, Frac>>& p) {
	const eng::math::Vec<4, eng::math::Fixed<s16, Frac>> h = {
		p.v[0], p.v[1], p.v[2], eng::math::Fixed<s16, Frac> {static_cast<s16>(1 << Frac)}};
	return transform(m, h);
}

/// Proyección: `M · (p,1)` y división por `w`, devolviendo **MF** (las coordenadas de
/// pantalla pueden superar el rango del fixed). La cadena va en MF porque los
/// intermedios homogéneos pueden salirse de `[-8, 8]`; sirve para un `Mat<4>` de cámara.
template <int Frac>
[[nodiscard]] constexpr eng::math::Vec<3, eng::math::MiniFloat16> project(
	const eng::math::Mat<4, eng::math::MiniFloat16>& m,
	const eng::math::Vec<3, eng::math::Fixed<s16, Frac>>& p) {
	const eng::math::Vec<4, eng::math::MiniFloat16> h = {
		fixed_to_mf(p.v[0]), fixed_to_mf(p.v[1]), fixed_to_mf(p.v[2]),
		eng::math::MiniFloat16::one()};
	const eng::math::Vec<4, eng::math::MiniFloat16> r = m * h;
	return {r.v[0] / r.v[3], r.v[1] / r.v[3], r.v[2] / r.v[3]};
}

/// Igual que `transform`, con coordenadas `fix` (4.12) crudas. Puente para los ports.
template <int N>
[[nodiscard]] constexpr eng::math::Vec<N, fix> transform_fix(
	const eng::math::Mat<N, eng::math::MiniFloat16>& m, const eng::math::Vec<N, fix>& p) {
	eng::math::Vec<N, Coord12> q {};
	for (int i = 0; i < N; ++i) q.v[i].v = p.v[i];
	const auto r = transform(m, q);
	eng::math::Vec<N, fix> out {};
	for (int i = 0; i < N; ++i) out.v[i] = r.v[i].v;
	return out;
}

/// `M · p + t` con `p` y `t` en `fix` crudo.
template <int N>
[[nodiscard]] constexpr eng::math::Vec<N, fix> transform_fix(
	const eng::math::Mat<N, eng::math::MiniFloat16>& m, const eng::math::Vec<N, fix>& p,
	const eng::math::Vec<N, fix>& t) {
	auto out = transform_fix(m, p);
	for (int i = 0; i < N; ++i) out.v[i] = detail::sat16(static_cast<s32>(out.v[i]) + t.v[i]);
	return out;
}

/// Igual que `transform`, con coordenadas `fix88` (8.8) crudas.
template <int N>
[[nodiscard]] constexpr eng::math::Vec<N, fix88> transform_fix88(
	const eng::math::Mat<N, eng::math::MiniFloat16>& m, const eng::math::Vec<N, fix88>& p) {
	eng::math::Vec<N, Coord88> q {};
	for (int i = 0; i < N; ++i) q.v[i].v = p.v[i];
	const auto r = transform(m, q);
	eng::math::Vec<N, fix88> out {};
	for (int i = 0; i < N; ++i) out.v[i] = r.v[i].v;
	return out;
}

} // namespace eng::retro

#undef ENG_MF_FIX_AI
