#pragma once

/// \file sprite_collision.hpp
/// **Colisión de hardware de sprites** (`CLXCON`/`CLXDAT`, AHRM 3.ª cap. 7): configura qué
/// pares de sprite y qué bitplanes participan, y decodifica el resultado. Es *pixel-perfect*
/// y **sin posición** (solo dice *que* hubo choque, no dónde); complementa la colisión por
/// software (`core/util/collision.hpp`, `field::collide_cpu`/`Backend::blitter_collide`).
///
/// El registro `CLXDAT` **se autolimpia al leerlo**: leerlo una vez por frame.
///
/// ```text
///   CLXCON ($DFF098, escritura)        CLXDAT ($DFF00E, lectura, autolimpia)
///   ┌ 15..12 ENSP7..ENSP1 (pares) ┐     ┌ 14..9  sprite vs sprite (pares)  ┐
///   │ 11..6  ENBP6..ENBP1 (planos)│     │ 8..5   planos PARES vs sprite    │
///   │ 5..0   MVBP6..MVBP1 (match) ┘     │ 4..1   planos IMPARES vs sprite  │
///   └                                    │ 0      planos pares vs impares   ┘
/// ```
///
/// Fuente: AHRM 3.ª, Table 7-3 (CLXDAT) y Table 7-4 (CLXCON).

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Configuración de la colisión de hardware (lo que se escribe en `CLXCON`).
struct SpriteCollisionConfig {
	/// Pares de sprite habilitados (bit `i` = par `i`): 0→0/1, 1→2/3, 2→4/5, 3→6/7.
	/// El sprite **par** del grupo siempre participa; este bit añade el **impar**.
	u8 sprite_pairs = 0;
	/// Bitplanes habilitados (bit `j` = `BPj+1`). Si es 0, **toda** colisión de bitplanes
	/// se detecta (los planos deshabilitados no filtran).
	u8 bitplanes = 0;
	/// Polaridad exigida (`MVBP`) a los bitplanes habilitados para registrar el choque.
	u8 match = 0;
};

/// Codifica `CLXCON` (`$DFF098`): `ENSP(15-12) | ENBP(11-6) | MVBP(5-0)`.
[[nodiscard]] constexpr u16 encode_clxcon(SpriteCollisionConfig c) noexcept {
	return static_cast<u16>((static_cast<u16>(c.sprite_pairs & 0x0Fu) << 12) |
				(static_cast<u16>(c.bitplanes & 0x3Fu) << 6) |
				static_cast<u16>(c.match & 0x3Fu));
}

/// Resultado de una lectura de `CLXDAT` (`$DFF00E`). Bits según AHRM Table 7-3.
struct SpriteCollisionResult {
	u16 raw = 0;

	/// Planos pares vs planos impares (bit 0).
	[[nodiscard]] constexpr bool even_vs_odd_bitplanes() const noexcept {
		return (raw & 0x0001u) != 0u;
	}
	/// Planos **impares** vs el par `pair` (0..3) → bits 1..4.
	[[nodiscard]] constexpr bool odd_bpl_vs_sprite(u8 pair) const noexcept {
		return (raw & (static_cast<u16>(1u) << (1u + pair))) != 0u;
	}
	/// Planos **pares** vs el par `pair` (0..3) → bits 5..8.
	[[nodiscard]] constexpr bool even_bpl_vs_sprite(u8 pair) const noexcept {
		return (raw & (static_cast<u16>(1u) << (5u + pair))) != 0u;
	}
	/// Choque entre dos pares de sprite `a` < `b` (0..3) → bits 9..14.
	[[nodiscard]] constexpr bool sprite_vs_sprite(u8 a, u8 b) const noexcept {
		// Pares ordenados: (0,1)->9 (0,2)->10 (0,3)->11 (1,2)->12 (1,3)->13 (2,3)->14.
		if (a > b) {
			const u8 t = a;
			a = b;
			b = t;
		}
		const u8 idx = static_cast<u8>(a * 3u - (a * (a - 1u)) / 2u + (b - a - 1u));
		return (raw & (static_cast<u16>(1u) << (9u + idx))) != 0u;
	}
};

/// Decodifica una lectura de `CLXDAT` (ya leído del hardware; aquí solo se interpreta).
[[nodiscard]] constexpr SpriteCollisionResult decode_clxdat(u16 raw) noexcept {
	return SpriteCollisionResult {raw};
}

} // namespace eng::graphics
