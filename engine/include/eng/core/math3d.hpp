#pragma once

/// \file math3d.hpp
/// Matemática 3D en **fixed-point 4.12** (port de `lib3d` de demoscene-repo-orig).
///
/// Porta la parte pura de `lib3d`: matrices 3×3 + traslación (`Matrix3D`),
/// rotaciones compuestas `Rx·Ry·Rz` (`LoadRotate3D`) y su inversa
/// (`LoadReverseRotate3D`), composición y transformación de puntos. Reutiliza el
/// formato 4.12 y la tabla de seno de `math2d` (no duplica).
///
/// Convención idéntica al origen: `out = M·v + t` con la parte lineal en 4.12. La
/// composición (`compose`) suma las traslaciones (como el `Compose3D` original);
/// válido para rotaciones o cuando el llamador gestiona la posición aparte.

#include <eng/core/math2d.hpp>
#include <eng/core/types.hpp>

namespace eng::math3d {

using math2d::cos_q12;
using math2d::fix;
using math2d::kOne;
using math2d::normfx;
using math2d::sin_q12;

/// Punto/vector 3D (mismos campos que `Point3D`).
struct Vec3 {
	s16 x = 0;
	s16 y = 0;
	s16 z = 0;
};

/// Matriz 3×3 + traslación, en 4.12 (mismos campos que `Matrix3D`).
struct Mat3x3 {
	fix m00 = kOne; fix m01 = 0; fix m02 = 0; fix x = 0;
	fix m10 = 0; fix m11 = kOne; fix m12 = 0; fix y = 0;
	fix m20 = 0; fix m21 = 0; fix m22 = kOne; fix z = 0;
};

/// Identidad.
constexpr void load_identity(Mat3x3& m) {
	m = Mat3x3 {};
}

/// Suma una traslación.
constexpr void translate(Mat3x3& m, s16 x, s16 y, s16 z) {
	m.x = static_cast<fix>(m.x + x);
	m.y = static_cast<fix>(m.y + y);
	m.z = static_cast<fix>(m.z + z);
}

/// Escala la parte lineal (factores en 4.12).
constexpr void scale(Mat3x3& m, fix sx, fix sy, fix sz) {
	m.m00 = normfx(static_cast<s32>(m.m00) * sx);
	m.m01 = normfx(static_cast<s32>(m.m01) * sy);
	m.m02 = normfx(static_cast<s32>(m.m02) * sz);
	m.m10 = normfx(static_cast<s32>(m.m10) * sx);
	m.m11 = normfx(static_cast<s32>(m.m11) * sy);
	m.m12 = normfx(static_cast<s32>(m.m12) * sz);
	m.m20 = normfx(static_cast<s32>(m.m20) * sx);
	m.m21 = normfx(static_cast<s32>(m.m21) * sy);
	m.m22 = normfx(static_cast<s32>(m.m22) * sz);
}

/// Carga `M = Rx(ax)·Ry(ay)·Rz(az)` (igual que `LoadRotate3D`).
constexpr void load_rotate(Mat3x3& m, u16 ax, u16 ay, u16 az) {
	const fix sinX = sin_q12(ax), cosX = cos_q12(ax);
	const fix sinY = sin_q12(ay), cosY = cos_q12(ay);
	const fix sinZ = sin_q12(az), cosZ = cos_q12(az);

	const fix tmp0 = normfx(static_cast<s32>(sinY) * cosZ);
	const fix tmp1 = normfx(static_cast<s32>(sinY) * sinZ);

	m.m00 = normfx(static_cast<s32>(cosY) * cosZ);
	m.m01 = static_cast<fix>(-normfx(static_cast<s32>(cosY) * sinZ));
	m.m02 = sinY;
	m.x = 0;
	m.m10 = normfx(static_cast<s32>(cosX) * sinZ + static_cast<s32>(sinX) * tmp0);
	m.m11 = normfx(static_cast<s32>(cosX) * cosZ - static_cast<s32>(sinX) * tmp1);
	m.m12 = static_cast<fix>(-normfx(static_cast<s32>(sinX) * cosY));
	m.y = 0;
	m.m20 = normfx(static_cast<s32>(sinX) * sinZ - static_cast<s32>(cosX) * tmp0);
	m.m21 = normfx(static_cast<s32>(sinX) * cosZ + static_cast<s32>(cosX) * tmp1);
	m.m22 = normfx(static_cast<s32>(cosX) * cosY);
	m.z = 0;
}

/// Carga `M = Rz(az)·Ry(ay)·Rx(ax)` (igual que `LoadReverseRotate3D`).
constexpr void load_reverse_rotate(Mat3x3& m, u16 ax, u16 ay, u16 az) {
	const fix sinX = sin_q12(ax), cosX = cos_q12(ax);
	const fix sinY = sin_q12(ay), cosY = cos_q12(ay);
	const fix sinZ = sin_q12(az), cosZ = cos_q12(az);

	const fix tmp0 = normfx(static_cast<s32>(sinX) * sinY);
	const fix tmp1 = normfx(static_cast<s32>(cosX) * sinY);

	m.m00 = normfx(static_cast<s32>(cosY) * cosZ);
	m.m01 = normfx(static_cast<s32>(tmp0) * cosZ - static_cast<s32>(cosX) * sinZ);
	m.m02 = normfx(static_cast<s32>(tmp1) * cosZ + static_cast<s32>(sinX) * sinZ);
	m.x = 0;
	m.m10 = normfx(static_cast<s32>(cosY) * sinZ);
	m.m11 = normfx(static_cast<s32>(tmp0) * sinZ + static_cast<s32>(cosX) * cosZ);
	m.m12 = normfx(static_cast<s32>(tmp1) * sinZ - static_cast<s32>(sinX) * cosZ);
	m.y = 0;
	m.m20 = static_cast<fix>(-sinY);
	m.m21 = normfx(static_cast<s32>(sinX) * cosY);
	m.m22 = normfx(static_cast<s32>(cosX) * cosY);
	m.z = 0;
}

/// Composición `d = a·b` (igual que `Compose3D`: la traslación se suma).
constexpr Mat3x3 compose(const Mat3x3& a, const Mat3x3& b) {
	Mat3x3 d {};
	d.m00 = normfx(static_cast<s32>(a.m00) * b.m00 + static_cast<s32>(a.m01) * b.m10 + static_cast<s32>(a.m02) * b.m20);
	d.m01 = normfx(static_cast<s32>(a.m00) * b.m01 + static_cast<s32>(a.m01) * b.m11 + static_cast<s32>(a.m02) * b.m21);
	d.m02 = normfx(static_cast<s32>(a.m00) * b.m02 + static_cast<s32>(a.m01) * b.m12 + static_cast<s32>(a.m02) * b.m22);
	d.x = static_cast<fix>(a.x + b.x);
	d.m10 = normfx(static_cast<s32>(a.m10) * b.m00 + static_cast<s32>(a.m11) * b.m10 + static_cast<s32>(a.m12) * b.m20);
	d.m11 = normfx(static_cast<s32>(a.m10) * b.m01 + static_cast<s32>(a.m11) * b.m11 + static_cast<s32>(a.m12) * b.m21);
	d.m12 = normfx(static_cast<s32>(a.m10) * b.m02 + static_cast<s32>(a.m11) * b.m12 + static_cast<s32>(a.m12) * b.m22);
	d.y = static_cast<fix>(a.y + b.y);
	d.m20 = normfx(static_cast<s32>(a.m20) * b.m00 + static_cast<s32>(a.m21) * b.m10 + static_cast<s32>(a.m22) * b.m20);
	d.m21 = normfx(static_cast<s32>(a.m20) * b.m01 + static_cast<s32>(a.m21) * b.m11 + static_cast<s32>(a.m22) * b.m21);
	d.m22 = normfx(static_cast<s32>(a.m20) * b.m02 + static_cast<s32>(a.m21) * b.m12 + static_cast<s32>(a.m22) * b.m22);
	d.z = static_cast<fix>(a.z + b.z);
	return d;
}

/// Aplica `m` a `n` puntos (igual que `Transform3D`): `out = M·in + traslación`.
inline void transform(const Mat3x3& m, Vec3* out, const Vec3* in, u32 n) {
	for (u32 i = 0; i < n; ++i) {
		const s16 x = in[i].x, y = in[i].y, z = in[i].z;
		out[i].x = static_cast<s16>(normfx(static_cast<s32>(m.m00) * x + static_cast<s32>(m.m01) * y + static_cast<s32>(m.m02) * z) + m.x);
		out[i].y = static_cast<s16>(normfx(static_cast<s32>(m.m10) * x + static_cast<s32>(m.m11) * y + static_cast<s32>(m.m12) * z) + m.y);
		out[i].z = static_cast<s16>(normfx(static_cast<s32>(m.m20) * x + static_cast<s32>(m.m21) * y + static_cast<s32>(m.m22) * z) + m.z);
	}
}

} // namespace eng::math3d
