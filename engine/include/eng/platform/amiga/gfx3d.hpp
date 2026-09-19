#pragma once

/// \file gfx3d.hpp
/// **Gráficos 3D** sobre el álgebra genérica (`eng/core/linalg.hpp`): construcción de
/// rotaciones, escala y transformación afín, **plantillados sobre el escalar**.
///
/// No hay un tipo único de matriz/vector: `Mat3<S>`, `Affine3<SR, SL>` y `P3<S>` son
/// alias de plantilla y cada uso instancia el escalar que le corresponde (por defecto el
/// escalar del target: `eng::real`/`eng::coord`). El mismo `load_rotate` sirve para
/// `float` y para `q12`: el ángulo va en **radianes** en el propio escalar y las
/// operaciones concretas (seno, coseno, producto normalizado) las aporta `S`.
///
/// Convención: `out = M·v + t` con la parte lineal en RATIO (`SR`) y la traslación en
/// LONGITUD (`SL`).

#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/core/scalar.hpp>
#include <eng/core/scalar_math.hpp>
#include <eng/core/types.hpp>
#include <eng/retro/fixed_trig.hpp>

namespace eng::math3d {

/// Matriz lineal 3×3 de ratios (por defecto, el escalar `eng::real` del target).
template <class S = eng::real>
using Mat3 = eng::math::Mat<3, S>;
/// Transformación afín: lineal (RATIO `SR`) + traslación (LONGITUD `SL`).
template <class SR = eng::real, class SL = eng::coord>
using Affine3 = eng::math::Affine<3, SR, SL>;
/// Punto de LONGITUD del álgebra genérica (`eng::coord` por defecto).
template <class S = eng::coord>
using P3 = eng::math::Vec<3, S>;

namespace detail {

/// `a·b + c·d` con la normalización **fusionada** (una sola, como el `dot` de 4 términos):
/// exacto para fixed y óptimo para float. Es el ladrillo de las filas de `load_rotate`.
template <class S>
[[nodiscard]] constexpr S ratio2(S a, S b, S c, S d) {
	return eng::math::dot(eng::math::Vec<2, S> {{a, c}}, eng::math::Vec<2, S> {{b, d}});
}

} // namespace detail

/// Senos/cosenos de los tres ejes de una rotación, calculados una vez (una pasada por eje).
template <class S>
struct SinCos3 {
	S sinX {};
	S cosX {};
	S sinY {};
	S cosY {};
	S sinZ {};
	S cosZ {};
};

/// Calcula los `(sin, cos)` de los tres ejes (una lectura de tabla por eje).
template <class S, class Unit>
[[nodiscard]] constexpr SinCos3<S> sincos3(eng::math::Angle<S, Unit> ax,
					   eng::math::Angle<S, Unit> ay,
					   eng::math::Angle<S, Unit> az) {
	SinCos3<S> sc {};
	eng::math::angle_sincos<S, Unit>::op(ax, sc.sinX, sc.cosX);
	eng::math::angle_sincos<S, Unit>::op(ay, sc.sinY, sc.cosY);
	eng::math::angle_sincos<S, Unit>::op(az, sc.sinZ, sc.cosZ);
	return sc;
}

/// Carga `M = Rx·Ry·Rz` (igual que `LoadRotate3D`) desde los `(sin, cos)` ya calculados.
template <class S>
inline void load_rotate_from_sincos(Mat3<S>& m, const SinCos3<S>& sc) {
	const S tmp0 = eng::math::mul_norm(sc.sinY, sc.cosZ);
	const S tmp1 = eng::math::mul_norm(sc.sinY, sc.sinZ);

	m.m[0][0] = eng::math::mul_norm(sc.cosY, sc.cosZ);
	m.m[0][1] = -eng::math::mul_norm(sc.cosY, sc.sinZ);
	m.m[0][2] = sc.sinY;
	m.m[1][0] = detail::ratio2(sc.cosX, sc.sinZ, sc.sinX, tmp0);
	m.m[1][1] = detail::ratio2(sc.cosX, sc.cosZ, -sc.sinX, tmp1);
	m.m[1][2] = -eng::math::mul_norm(sc.sinX, sc.cosY);
	m.m[2][0] = detail::ratio2(sc.sinX, sc.sinZ, -sc.cosX, tmp0);
	m.m[2][1] = detail::ratio2(sc.sinX, sc.cosZ, sc.cosX, tmp1);
	m.m[2][2] = eng::math::mul_norm(sc.cosX, sc.cosY);
}

/// Carga `M = Rz·Ry·Rx` (igual que `LoadReverseRotate3D`) desde los `(sin, cos)` ya calculados.
template <class S>
inline void load_reverse_rotate_from_sincos(Mat3<S>& m, const SinCos3<S>& sc) {
	const S tmp0 = eng::math::mul_norm(sc.sinX, sc.sinY);
	const S tmp1 = eng::math::mul_norm(sc.cosX, sc.sinY);

	m.m[0][0] = eng::math::mul_norm(sc.cosY, sc.cosZ);
	m.m[0][1] = detail::ratio2(tmp0, sc.cosZ, -sc.cosX, sc.sinZ);
	m.m[0][2] = detail::ratio2(tmp1, sc.cosZ, sc.sinX, sc.sinZ);
	m.m[1][0] = eng::math::mul_norm(sc.cosY, sc.sinZ);
	m.m[1][1] = detail::ratio2(tmp0, sc.sinZ, sc.cosX, sc.cosZ);
	m.m[1][2] = detail::ratio2(tmp1, sc.sinZ, -sc.sinX, sc.cosZ);
	m.m[2][0] = -sc.sinY;
	m.m[2][1] = eng::math::mul_norm(sc.sinX, sc.cosY);
	m.m[2][2] = eng::math::mul_norm(sc.cosX, sc.cosY);
}

/// Carga `M = Rx(ax)·Ry(ay)·Rz(az)` (igual que `LoadRotate3D`). Ángulos `Angle<S,Unit>`.
template <class S, class Unit>
inline void load_rotate(Mat3<S>& m, eng::math::Angle<S, Unit> ax, eng::math::Angle<S, Unit> ay,
			eng::math::Angle<S, Unit> az) {
	load_rotate_from_sincos(m, sincos3(ax, ay, az));
}

/// Carga `M = Rz(az)·Ry(ay)·Rx(ax)` (igual que `LoadReverseRotate3D`). Ángulos `Angle<S,Unit>`.
template <class S, class Unit>
inline void load_reverse_rotate(Mat3<S>& m, eng::math::Angle<S, Unit> ax,
				eng::math::Angle<S, Unit> ay, eng::math::Angle<S, Unit> az) {
	load_reverse_rotate_from_sincos(m, sincos3(ax, ay, az));
}

/// Variantes que reciben los `(sin, cos)` ya calculados (`sincos3`): reutilízalos cuando
/// varias matrices comparten los mismos ángulos (p. ej. directa + inversa).
template <class S>
inline void load_rotate(Mat3<S>& m, const SinCos3<S>& sc) {
	load_rotate_from_sincos(m, sc);
}
template <class S>
inline void load_reverse_rotate(Mat3<S>& m, const SinCos3<S>& sc) {
	load_reverse_rotate_from_sincos(m, sc);
}

/// Escala la parte lineal in situ (factores RATIO del mismo escalar que la matriz).
template <class S>
inline void scale(Mat3<S>& m, S sx, S sy, S sz) {
	for (int i = 0; i < 3; ++i) {
		m.m[i][0] = eng::math::mul_norm(m.m[i][0], sx);
		m.m[i][1] = eng::math::mul_norm(m.m[i][1], sy);
		m.m[i][2] = eng::math::mul_norm(m.m[i][2], sz);
	}
}

/// `out = M·in` (sin traslación). Genérico sobre el escalar de la matriz y del vector.
template <class SR, class SL>
inline void transform(const eng::math::Mat<3, SR>& m, eng::math::Vec<3, SL>* out,
		      const eng::math::Vec<3, SL>* in, u32 n) {
	for (u32 i = 0; i < n; ++i) {
		const eng::math::Vec<3, SL>& p = in[i];
		out[i] = eng::math::Vec<3, SL> {eng::math::dot(m.row(0), p),
						eng::math::dot(m.row(1), p),
						eng::math::dot(m.row(2), p)};
	}
}

/// `out = M·in + t` (afín). Es lo que usa el mesh: su "model" lleva traslación.
template <class SR, class SL>
inline void transform(const eng::math::Affine<3, SR, SL>& a, eng::math::Vec<3, SL>* out,
		      const eng::math::Vec<3, SL>* in, u32 n) {
	for (u32 i = 0; i < n; ++i) {
		const eng::math::Vec<3, SL>& p = in[i];
		out[i] = eng::math::Vec<3, SL> {eng::math::dot(a.m.row(0), p) + a.t.v[0],
						eng::math::dot(a.m.row(1), p) + a.t.v[1],
						eng::math::dot(a.m.row(2), p) + a.t.v[2]};
	}
}

// `mesh_transform` (el vector de malla -> mundo) es genérico y vive en `mesh3d`: usa
// `eng::math::transform(Affine, Vec)`, así que no hace falta una versión por formato.

/// Inversa de una transformación **rígida** (rotación + traslación, sin escala ni
/// cizalla): la parte lineal es ortonormal, así que `m⁻¹ = mᵀ` y `t⁻¹ = −mᵀ·t`, sin
/// divisiones. `compose(a, inverse_rigid(a))` vuelve a la identidad (redondeo aparte).
///
/// Para una transformación con escala por ejes, la inversa necesita los recíprocos `1/s`
/// (una división por eje): no la cubre esta función.
template <class SR, class SL>
[[nodiscard]] inline eng::math::Affine<3, SR, SL> inverse_rigid(
	const eng::math::Affine<3, SR, SL>& a) {
	const eng::math::Mat<3, SR> mt = eng::math::transpose(a.m);
	const eng::math::Vec<3, SL> t = -eng::math::transform(eng::math::Affine<3, SR, SL> {mt, {}}, a.t);
	return eng::math::Affine<3, SR, SL> {mt, t};
}

} // namespace eng::math3d
