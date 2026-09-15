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

#include <eng/platform/amiga/angles.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/math2d.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/core/types.hpp>
#include <eng/retro/fixed_q.hpp>

namespace eng::math3d {

using math2d::cos_q12;
using math2d::fix;
using math2d::sin_q12;

/// Matriz lineal 3×3 de ratios en 4.12.
using Mat3 = eng::math::Mat<3, eng::retro::q12>;
/// Transformación afín completa: lineal (RATIO 4.12) + traslación (LONGITUD entera).
using Affine3 = eng::math::Affine<3, eng::retro::q12, eng::retro::q0>;

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

/// Escala la parte lineal in situ (factores en 4.12).
inline void scale(Mat3& m, fix sx, fix sy, fix sz) {
	const eng::retro::q12 fx {sx}, fy {sy}, fz {sz};
	for (int i = 0; i < 3; ++i) {
		m.m[i][0] = (m.m[i][0] * fx).rescale<12>().cast<s16>();
		m.m[i][1] = (m.m[i][1] * fy).rescale<12>().cast<s16>();
		m.m[i][2] = (m.m[i][2] * fz).rescale<12>().cast<s16>();
	}
}

/// `out = M·in` (sin traslación). Los vértices son LONGITUDES (`q0`).
inline void transform(const Mat3& m, Vec3* out, const Vec3* in, u32 n) {
	const eng::retro::q0 zero {};
	for (u32 i = 0; i < n; ++i) {
		const eng::retro::q0 x {in[i].x}, y {in[i].y}, z {in[i].z};
		out[i].x = eng::math::dot(m.m[0][0], x, m.m[0][1], y, m.m[0][2], z).v;
		out[i].y = eng::math::dot(m.m[1][0], x, m.m[1][1], y, m.m[1][2], z).v;
		out[i].z = eng::math::dot(m.m[2][0], x, m.m[2][1], y, m.m[2][2], z).v;
	}
	(void)zero;
}

/// `out = M·in + t` (afín). Es lo que usa el mesh: su "model" lleva traslación.
inline void transform(const Affine3& a, Vec3* out, const Vec3* in, u32 n) {
	for (u32 i = 0; i < n; ++i) {
		const eng::retro::q0 x {in[i].x}, y {in[i].y}, z {in[i].z};
		out[i].x = eng::math::dot(a.m.m[0][0], x, a.m.m[0][1], y, a.m.m[0][2], z).v + a.t.v[0].v;
		out[i].y = eng::math::dot(a.m.m[1][0], x, a.m.m[1][1], y, a.m.m[1][2], z).v + a.t.v[1].v;
		out[i].z = eng::math::dot(a.m.m[2][0], x, a.m.m[2][1], y, a.m.m[2][2], z).v + a.t.v[2].v;
	}
}

// `mesh_transform` (el vector de malla -> mundo) es genérico y vive en `mesh3d`: usa
// `eng::math::transform(Affine, Vec)`, así que no hace falta una versión por formato.

} // namespace eng::math3d
