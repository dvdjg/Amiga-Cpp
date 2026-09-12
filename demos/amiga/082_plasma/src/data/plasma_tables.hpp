#pragma once

/// \file plasma_tables.hpp
/// Tablas del plasma, **verbatim** de `effects/plasma/plasma.c::GeneratePlasmaTables`:
///
///   tab1[i] = fx4i(3*47) * SIN(rad*2) >> 16   (rad = i*16)
///   tab2[i] = fx4i(3*31) * COS(rad*2) >> 16
///   tab3[i] = fx4i(3*37) * SIN(rad*2) >> 16
///
/// `fx4i(i) = i << 4` y `SIN`/`COS` son la tabla 4.12 exacta del original
/// (`eng/core/sintab.hpp` via `math2d`). El `>> 16` es aritmetico (como el C original,
/// que asigna a `char`).

#include <eng/core/math2d.hpp>
#include <eng/core/types.hpp>

namespace plasma_data {

struct PlasmaTables {
	eng::s8 tab1[256] {};
	eng::s8 tab2[256] {};
	eng::s8 tab3[256] {};

	constexpr PlasmaTables() {
		const eng::s32 k1 = static_cast<eng::s32>(static_cast<eng::u16>(3 * 47) << 4); // 2256
		const eng::s32 k2 = static_cast<eng::s32>(static_cast<eng::u16>(3 * 31) << 4); // 1488
		const eng::s32 k3 = static_cast<eng::s32>(static_cast<eng::u16>(3 * 37) << 4); // 1776
		for (int i = 0; i < 256; ++i) {
			const eng::u16 rad2 = static_cast<eng::u16>(i * 16 * 2);
			tab1[i] = static_cast<eng::s8>((k1 * eng::math2d::sin_q12(rad2)) >> 16);
			tab2[i] = static_cast<eng::s8>((k2 * eng::math2d::cos_q12(rad2)) >> 16);
			tab3[i] = static_cast<eng::s8>((k3 * eng::math2d::sin_q12(rad2)) >> 16);
		}
	}
};

inline constexpr PlasmaTables kTables {};

} // namespace plasma_data
