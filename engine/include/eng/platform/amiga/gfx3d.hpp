#pragma once

/// \file gfx3d.hpp
/// Especialización **para gráficos de Amiga** de la librería genérica
/// (`eng/core/linalg.hpp`): rotaciones construidas con la tabla de seno 4.12 y los
/// tipos concretos que usan los efectos (`Mat3` = `Mat<3,q12>`, `Affine3` =
/// `Affine<3,q12,q0>`, `Vec3` en crudo). La aritmética de matrices vive en `eng::math`;
/// aquí sólo queda lo que depende del **formato 4.12** (los senos) o del chipset.
///
/// Convención: `out = M·v + t` con la parte lineal en 4.12 (RATIO) y la traslación en
/// LONGITUD. Eso es exactamente `Affine<3, q12, q0>`.

#include <eng/retro/angles.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/core/types.hpp>
#include <eng/retro/fixed_q.hpp>

namespace eng::math3d {

using eng::retro::cos_q12;
using eng::retro::fix;
using eng::retro::sin_q12;

/// Matriz lineal 3×3 de ratios en 4.12.
using Mat3 = eng::math::Mat<3, eng::retro::q12>;
/// Transformación afín completa: lineal (RATIO 4.12) + traslación (LONGITUD entera).
using Affine3 = eng::math::Affine<3, eng::retro::q12, eng::retro::q0>;
/// Punto en LONGITUD del álgebra genérica. Es EXACTAMENTE el vértice de malla
/// (`Vec3 = Vec<3, Coord>`), porque `retro::q0` y `math3d::Coord` son el mismo
/// `math::Fixed<s16,0>`: ya no hace falta convertir `{s16 x,y,z}` a `Vec<3,q0>`.
using P3 = eng::math::Vec<3, eng::retro::q0>;
static_assert(sizeof(P3) == sizeof(Vec3), "math3d::P3 y math3d::Vec3 deben coincidir");

// `Vec3`, `Face` y el back-face culling son del MODELO de malla, no de este formato:
// viven en `eng/core/mesh3d.hpp`. Aquí sólo queda lo que usa el 4.12.

/// Carga `M = Rx(ax)·Ry(ay)·Rz(az)` (igual que `LoadRotate3D`).
inline void load_rotate(Mat3& m, u16 ax, u16 ay, u16 az) {
	using eng::retro::q12;
	const fix sinX = sin_q12(ax), cosX = cos_q12(ax);
	const fix sinY = sin_q12(ay), cosY = cos_q12(ay);
	const fix sinZ = sin_q12(az), cosZ = cos_q12(az);

	const fix tmp0 = eng::math::dot(q12 {sinY}, q12 {cosZ}).v;
	const fix tmp1 = eng::math::dot(q12 {sinY}, q12 {sinZ}).v;

	m.m[0][0] = eng::math::dot(q12 {cosY}, q12 {cosZ});
	m.m[0][1] = -eng::math::dot(q12 {cosY}, q12 {sinZ});
	m.m[0][2] = q12 {sinY};
	m.m[1][0] = eng::math::dot(q12 {cosX}, q12 {sinZ}, q12 {sinX}, q12 {tmp0});
	m.m[1][1] = eng::math::dot(q12 {cosX}, q12 {cosZ}, -q12 {sinX}, q12 {tmp1});
	m.m[1][2] = -eng::math::dot(q12 {sinX}, q12 {cosY});
	m.m[2][0] = eng::math::dot(q12 {sinX}, q12 {sinZ}, -q12 {cosX}, q12 {tmp0});
	m.m[2][1] = eng::math::dot(q12 {sinX}, q12 {cosZ}, q12 {cosX}, q12 {tmp1});
	m.m[2][2] = eng::math::dot(q12 {cosX}, q12 {cosY});
}

/// Carga `M = Rz(az)·Ry(ay)·Rx(ax)` (igual que `LoadReverseRotate3D`).
inline void load_reverse_rotate(Mat3& m, u16 ax, u16 ay, u16 az) {
	using eng::retro::q12;
	const fix sinX = sin_q12(ax), cosX = cos_q12(ax);
	const fix sinY = sin_q12(ay), cosY = cos_q12(ay);
	const fix sinZ = sin_q12(az), cosZ = cos_q12(az);

	const fix tmp0 = eng::math::dot(q12 {sinX}, q12 {sinY}).v;
	const fix tmp1 = eng::math::dot(q12 {cosX}, q12 {sinY}).v;

	m.m[0][0] = eng::math::dot(q12 {cosY}, q12 {cosZ});
	m.m[0][1] = eng::math::dot(q12 {tmp0}, q12 {cosZ}, -q12 {cosX}, q12 {sinZ});
	m.m[0][2] = eng::math::dot(q12 {tmp1}, q12 {cosZ}, q12 {sinX}, q12 {sinZ});
	m.m[1][0] = eng::math::dot(q12 {cosY}, q12 {sinZ});
	m.m[1][1] = eng::math::dot(q12 {tmp0}, q12 {sinZ}, q12 {cosX}, q12 {cosZ});
	m.m[1][2] = eng::math::dot(q12 {tmp1}, q12 {sinZ}, -q12 {sinX}, q12 {cosZ});
	m.m[2][0] = -q12 {sinY};
	m.m[2][1] = eng::math::dot(q12 {sinX}, q12 {cosY});
	m.m[2][2] = eng::math::dot(q12 {cosX}, q12 {cosY});
}

/// Escala la parte lineal in situ (factores RATIO en 4.12, el mismo escalar que la matriz).
inline void scale(Mat3& m, eng::retro::q12 sx, eng::retro::q12 sy, eng::retro::q12 sz) {
	for (int i = 0; i < 3; ++i) {
		m.m[i][0] = eng::math::mul_norm(m.m[i][0], sx);
		m.m[i][1] = eng::math::mul_norm(m.m[i][1], sy);
		m.m[i][2] = eng::math::mul_norm(m.m[i][2], sz);
	}
}

/// `out = M·in` (sin traslación). Los vértices son LONGITUDES (`Coord`), el MISMO escalar
/// del álgebra: no hay conversión, solo `dot(fila, vértice)` (producto fusionado exacto).
inline void transform(const Mat3& m, Vec3* out, const Vec3* in, u32 n) {
	for (u32 i = 0; i < n; ++i) {
		const Vec3& p = in[i];
		out[i] = Vec3 {{eng::math::dot(m.row(0), p), eng::math::dot(m.row(1), p),
				eng::math::dot(m.row(2), p)}};
	}
}

/// `out = M·in + t` (afín). Es lo que usa el mesh: su "model" lleva traslación.
inline void transform(const Affine3& a, Vec3* out, const Vec3* in, u32 n) {
	for (u32 i = 0; i < n; ++i) {
		const Vec3& p = in[i];
		out[i] = Vec3 {{eng::math::dot(a.m.row(0), p) + a.t.v[0],
				eng::math::dot(a.m.row(1), p) + a.t.v[1],
				eng::math::dot(a.m.row(2), p) + a.t.v[2]}};
	}
}

// `mesh_transform` (el vector de malla -> mundo) es genérico y vive en `mesh3d`: usa
// `eng::math::transform(Affine, Vec)`, así que no hace falta una versión por formato.

/// Inversa de una transformación **rígida** (rotación + traslación, sin escala ni
/// cizalla): la parte lineal es ortonormal, así que `m⁻¹ = mᵀ` y `t⁻¹ = −mᵀ·t`, sin
/// divisiones. `compose(a, inverse_rigid(a))` vuelve a la identidad (redondeo 4.12 aparte).
///
/// Para una transformación con escala por ejes, la inversa necesita los recíprocos `1/s`
/// (una división por eje): no la cubre esta función.
[[nodiscard]] inline Affine3 inverse_rigid(const Affine3& a) {
	const Mat3 mt = eng::math::transpose(a.m);
	const P3 t = -eng::math::transform(Affine3 {mt, {}}, a.t);
	return Affine3 {mt, t};
}

} // namespace eng::math3d
