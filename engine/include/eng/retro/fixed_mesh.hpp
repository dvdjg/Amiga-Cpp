#pragma once

/// \file fixed_mesh.hpp
/// **Especialización retro** de `eng::math3d::mesh_traits` para la coordenada 16 bits
/// (`Fixed<s16,0>`, la LONGITUD del `obj2c`): culling y claves de orden con aritmética cruda
/// `s16`/`s32` —productos `16×16` (`muls.w`) y `32×16` sin `__mulsi3`—, como el original.
/// El header genérico `core/mesh3d.hpp` no conoce esta representación; sólo aquí se nombra
/// `Fixed<s16,0>`. Quien use la malla 16 bits (plataforma/demos) incluye este header.

#include <eng/core/arith.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/core/types.hpp>

namespace eng::math3d {

namespace detail {

/// Producto `s32 × s16 -> s32` (se conservan los 32 bits bajos, como el `*` directo) sin
/// `__mulsi3`: se parte el operando de 32 bits y se usan dos multiplicaciones de 16 bits
/// nativas. El segundo operando se sign-extiende (de ahí la corrección de `b < 0`); el
/// resultado queda bit a bit igual que `(s32)a * (s32)b` (probado por HOST-013).
[[nodiscard]] constexpr s32 mul32x16(s32 a, s16 b) {
	const u16 a_lo = static_cast<u16>(static_cast<u32>(a) & 0xFFFFu);
	const s16 a_hi = static_cast<s16>(static_cast<u32>(a) >> 16);
	u32 lo_prod = eng::math::arith<s16>::mulu(a_lo, static_cast<u16>(b));
	if (b < 0) lo_prod -= static_cast<u32>(a_lo) << 16;
	const s32 hi_prod = eng::math::arith<s16>::mul(a_hi, b);
	return static_cast<s32>(lo_prod + (static_cast<u32>(hi_prod) << 16));
}

} // namespace detail

/// `mesh_traits` para la coordenada `Fixed<s16,0>` (LONGITUD de 16 bits).
template <typename P>
struct mesh_traits<eng::math::Fixed<s16, 0, P>> {
	using scalar = eng::math::Fixed<s16, 0, P>;
	using key = s16;

	/// Culling de una cara desde `cam` con aritmética **cruda `s16`/`s32`**: diferencias `s16`,
	/// productos con `mul_wide` (`muls.w`) y el mixto `32×16` con `mul32x16` (sin `__mulsi3`).
	/// Mismo signo que el genérico; lo consume `MeshFaceOrder`/`face_visible`.
	[[nodiscard]] static constexpr s32 face_signed_area(const Vec3t<scalar>& a,
							    const Vec3t<scalar>& b,
							    const Vec3t<scalar>& c,
							    const Vec3t<scalar>& cam) {
		const s16 ux = static_cast<s16>(b.v[0].v - a.v[0].v);
		const s16 uy = static_cast<s16>(b.v[1].v - a.v[1].v);
		const s16 uz = static_cast<s16>(b.v[2].v - a.v[2].v);
		const s16 vx = static_cast<s16>(c.v[0].v - a.v[0].v);
		const s16 vy = static_cast<s16>(c.v[1].v - a.v[1].v);
		const s16 vz = static_cast<s16>(c.v[2].v - a.v[2].v);
		const s32 nx = eng::math::mul_wide(uy, vz) - eng::math::mul_wide(uz, vy);
		const s32 ny = eng::math::mul_wide(uz, vx) - eng::math::mul_wide(ux, vz);
		const s32 nz = eng::math::mul_wide(ux, vy) - eng::math::mul_wide(uy, vx);
		return detail::mul32x16(nx, static_cast<s16>(cam.v[0].v - a.v[0].v)) +
		       detail::mul32x16(ny, static_cast<s16>(cam.v[1].v - a.v[1].v)) +
		       detail::mul32x16(nz, static_cast<s16>(cam.v[2].v - a.v[2].v));
	}

	/// Clave de painter por **suma** de las `z` crudas (`s16`, con envoltura como el original);
	/// usada por `face_z_sum` en efectos que ordenan por profundidad acumulada.
	[[nodiscard]] static constexpr s16 z_sum(const Vec3t<scalar>& a, const Vec3t<scalar>& b,
						 const Vec3t<scalar>& c) {
		return static_cast<s16>(a.v[2].v + b.v[2].v + c.v[2].v);
	}

	/// Clave de painter por **mínimo** de las `z` crudas (`s16`); la usa el `MeshFaceOrder`
	/// de la cualidad `ConcaveMesh` (pintor lejos→cerca).
	[[nodiscard]] static constexpr s16 z_min(const Vec3t<scalar>& a, const Vec3t<scalar>& b,
						 const Vec3t<scalar>& c) {
		const s16 ab = a.v[2].v < b.v[2].v ? a.v[2].v : b.v[2].v;
		return ab < c.v[2].v ? ab : c.v[2].v;
	}
};

/// `mesh_traits` para la coordenada ancha `Fixed<s32,E>` (host / 68020): el producto mixto del
/// culling (`32×32 -> 64` y `64×32 -> 96`) excede los `Repr` disponibles, así que sólo se
/// evalúa el **signo** en 128 bits (clave de orden `s32`). En 68000 no hay `__int128` nativo:
/// la malla objetivo allí es `Fixed<s16,0>`.
template <int E, typename P>
struct mesh_traits<eng::math::Fixed<s32, E, P>> {
	using scalar = eng::math::Fixed<s32, E, P>;
	using key = s32;

	[[nodiscard]] static constexpr s32 face_signed_area(const Vec3t<scalar>& a,
							    const Vec3t<scalar>& b,
							    const Vec3t<scalar>& c,
							    const Vec3t<scalar>& cam) {
#if defined(__SIZEOF_INT128__) && !defined(__m68k__)
		const long long ux = static_cast<long long>(b.v[0].v) - a.v[0].v;
		const long long uy = static_cast<long long>(b.v[1].v) - a.v[1].v;
		const long long uz = static_cast<long long>(b.v[2].v) - a.v[2].v;
		const long long vx = static_cast<long long>(c.v[0].v) - a.v[0].v;
		const long long vy = static_cast<long long>(c.v[1].v) - a.v[1].v;
		const long long vz = static_cast<long long>(c.v[2].v) - a.v[2].v;
		const __int128 nx = static_cast<__int128>(uy) * vz - static_cast<__int128>(uz) * vy;
		const __int128 ny = static_cast<__int128>(uz) * vx - static_cast<__int128>(ux) * vz;
		const __int128 nz = static_cast<__int128>(ux) * vy - static_cast<__int128>(uy) * vx;
		const __int128 wx = static_cast<long long>(cam.v[0].v) - a.v[0].v;
		const __int128 wy = static_cast<long long>(cam.v[1].v) - a.v[1].v;
		const __int128 wz = static_cast<long long>(cam.v[2].v) - a.v[2].v;
		const __int128 d = nx * wx + ny * wy + nz * wz;
		return d < 0 ? -1 : (d > 0 ? 1 : 0);
#else
		static_assert(sizeof(P) == 0,
			      "mesh Fixed<s32>: sin __int128 en el objetivo; usar Fixed<s16,0>");
		return 0;
#endif
	}

	[[nodiscard]] static constexpr key z_sum(const Vec3t<scalar>& a, const Vec3t<scalar>& b,
						 const Vec3t<scalar>& c) {
		return a.v[2].v + b.v[2].v + c.v[2].v;
	}

	[[nodiscard]] static constexpr key z_min(const Vec3t<scalar>& a, const Vec3t<scalar>& b,
						 const Vec3t<scalar>& c) {
		const s32 ab = a.v[2].v < b.v[2].v ? a.v[2].v : b.v[2].v;
		return ab < c.v[2].v ? ab : c.v[2].v;
	}
};

} // namespace eng::math3d
