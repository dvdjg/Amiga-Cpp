#pragma once

/// \file budget.hpp
/// **Presupuesto y degradación del HUD** (H4): coste declarado del *backing*, conflicto de
/// canales de sprite y **escalera de degradación determinista** con histéresis
/// (`HUD_SYSTEM.md` §6.6/§6.7). La escalera decide el **nivel** (0 = completo; 1..6 en el
/// orden canónico); aplicarlo es del presentador (tasa de refresco, animaciones) y nunca
/// afecta a los valores del modelo ni al *hit-test*.
///
/// Verificación: **HOST-471** (coste byte a byte contra referencia, encaje en el
/// presupuesto, conflicto de canales y escalera determinista bajo presupuesto simulado).

#include <eng/core/types/types.hpp>
#include <eng/hud/region.hpp>

namespace eng::hud {

/// Nivel máximo de la escalera (`HUD_SYSTEM.md` §6.7): 6 pasos ordenados.
inline constexpr eng::u8 kHudDegradeMaxLevel = 6u;

/// Bytes de *backing* de un rectángulo de overlay: filas redondeadas a palabra (bitmap
/// planar) × alto × planos. Es la referencia del coste declarado en
/// `HudBudget::max_backing_bytes` (`HUD_SYSTEM.md` §6.4).
[[nodiscard]] constexpr eng::u32 hud_backing_bytes(eng::u16 width, eng::u16 height,
						   eng::u8 planes) noexcept {
	const eng::u32 row_bytes = ((static_cast<eng::u32>(width) + 15u) / 16u) * 2u;
	return row_bytes * static_cast<eng::u32>(height) * static_cast<eng::u32>(planes);
}

/// ¿Cabe el *backing* de esa caja (ancho × alto, `planes` planos) en el presupuesto de la
/// región? Un overlay `Backing` sin encaje es un *setup* inválido.
[[nodiscard]] constexpr bool hud_backing_fits(const HudBudget& budget, eng::u16 width,
					      eng::u16 height, eng::u8 planes) noexcept {
	return hud_backing_bytes(width, height, planes) <= budget.max_backing_bytes;
}

/// ¿Dos reservas de canales de sprite (máscara SPR0..SPR7) se solapan? El solape es un
/// *setup* inválido: dos dueños del mismo canal (`winuae/sprite-dma.md` §Armado de un canal).
[[nodiscard]] constexpr bool hud_channels_conflict(eng::u8 a, eng::u8 b) noexcept {
	return (a & b) != 0u;
}

/// **Gasto de una región en el frame** (telemetría H4, `HUD_SYSTEM.md` §6.6/§8): lo que el
/// presentador consumió (operaciones, blits, movimientos de Copper, canales de sprite,
/// bytes de *backing*) y el nivel de degradación vigente. El presentador lo rellena
/// (`reset` + acumulación); el juego lo lee para su telemetría y `VALIDATION.md`.
struct HudRegionStats {
	eng::u16 ops = 0u;            ///< operaciones de dibujo aplicadas
	eng::u16 blits = 0u;          ///< jobs de Blitter lanzados
	eng::u16 copper_moves = 0u;   ///< movimientos de Copper emitidos
	eng::u8 sprite_channels = 0u; ///< canales SPRx usados (máscara)
	eng::u8 bobs = 0u;            ///< BOBs de overlay dibujados
	eng::u32 backing_bytes = 0u;  ///< bytes de *backing* usados
	eng::u8 degrade_level = 0u;   ///< nivel de la escalera (0 = completo)

	/// Acumula el gasto de otro recuento (p. ej. por capas dentro de la región).
	constexpr void add(const HudRegionStats& o) noexcept {
		ops = static_cast<eng::u16>(ops + o.ops);
		blits = static_cast<eng::u16>(blits + o.blits);
		copper_moves = static_cast<eng::u16>(copper_moves + o.copper_moves);
		sprite_channels = static_cast<eng::u8>(sprite_channels | o.sprite_channels);
		bobs = static_cast<eng::u8>(bobs + o.bobs);
		backing_bytes += o.backing_bytes;
		if (o.degrade_level > degrade_level) {
			degrade_level = o.degrade_level;
		}
	}
};

/// Campo del presupuesto excedido (el primero según el orden de la escalera de §6.7).
enum class HudBudgetOver : eng::u8 {
	None = 0,       ///< dentro del presupuesto
	DrawOps,        ///< se excedieron las operaciones de dibujo
	Blits,          ///< se excedieron los jobs de Blitter
	CopperMoves,    ///< se excedieron los movimientos de Copper
	BackingBytes,   ///< se excedieron los bytes de *backing*
	SpriteChannels, ///< se usaron canales fuera de la máscara reservada
	Bobs,           ///< se excedieron los BOBs de overlay
};

/// **Comprobador del presupuesto** (`HUD_SYSTEM.md` §6.6): compara el gasto de la región
/// contra su `HudBudget` y devuelve el primer campo excedido (o `None`). Un campo con
/// presupuesto 0 = sin límite declarado (la máscara de sprites 0 = sin canales reservados,
/// así que cualquier uso es exceso). Determinista y sin efectos: la reacción (degradar) es
/// del presentador.
[[nodiscard]] constexpr HudBudgetOver hud_budget_check(const HudBudget& budget,
						       const HudRegionStats& used) noexcept {
	if (budget.max_draw_ops != 0u && used.ops > budget.max_draw_ops) {
		return HudBudgetOver::DrawOps;
	}
	if (budget.max_blits_per_frame != 0u && used.blits > budget.max_blits_per_frame) {
		return HudBudgetOver::Blits;
	}
	if (budget.max_copper_moves_per_line != 0u &&
	    used.copper_moves > budget.max_copper_moves_per_line) {
		return HudBudgetOver::CopperMoves;
	}
	if (budget.max_backing_bytes != 0u && used.backing_bytes > budget.max_backing_bytes) {
		return HudBudgetOver::BackingBytes;
	}
	// Canales de sprite: el gasto debe caber en la máscara reservada de la región
	// (usar un canal fuera de ella es el solape que `hud_channels_conflict` rechaza).
	if ((used.sprite_channels & static_cast<eng::u8>(~budget.sprite_channel_mask)) != 0u) {
		return HudBudgetOver::SpriteChannels;
	}
	if (budget.max_bobs != 0u && used.bobs > budget.max_bobs) {
		return HudBudgetOver::Bobs;
	}
	return HudBudgetOver::None;
}

/// **Telemetría agregada del HUD** (H4): un `HudRegionStats` por región del modelo más el
/// recuento de cambios de degradación. El presentador lo rellena por frame; el juego lo
/// publica (p. ej. por contadores del periférico) para el gate de la demo.
inline constexpr eng::u8 kHudStatsMaxRegions = 8u;

struct HudStats {
	HudRegionStats regions[kHudStatsMaxRegions] {};
	eng::u8 count = 0u;    ///< regiones con telemetría (índice = región del modelo)
	eng::u32 changes = 0u; ///< cambios de nivel de degradación (acumulado)

	/// Reinicia el gasto del frame (conserva `changes`).
	constexpr void reset() noexcept {
		for (eng::u8 i = 0u; i < kHudStatsMaxRegions; ++i) {
			regions[i] = HudRegionStats {};
		}
	}

	[[nodiscard]] constexpr HudRegionStats& region(eng::u8 index) noexcept {
		return regions[index < kHudStatsMaxRegions ? index : 0u];
	}
	[[nodiscard]] constexpr const HudRegionStats& region(eng::u8 index) const noexcept {
		return regions[index < kHudStatsMaxRegions ? index : 0u];
	}

	/// ¿Alguna región está degradada?
	[[nodiscard]] constexpr bool degraded() const noexcept {
		for (eng::u8 i = 0u; i < kHudStatsMaxRegions; ++i) {
			if (regions[i].degrade_level != 0u) {
				return true;
			}
		}
		return false;
	}
};

/// **Escalera de degradación** (H4, `HUD_SYSTEM.md` §6.7): niveles 0..6 en el orden canónico
/// (0 completo; 1 *ghost* estático; 2 pulso del medidor estático; 3 contador cada 2 frames;
/// 4 overlay a 25 Hz; 5 sin *ghost*; 6 sin adornos). El presentador mide el coste de la
/// sección HUD por frame (reloj de ciclos) y llama a `update`: por encima del presupuesto
/// sube un nivel y, con **histéresis** (por debajo de 7/8 del presupuesto) baja uno, para no
/// oscilar con el ruido de la medida. Determinista: la misma secuencia de medidas produce la
/// misma secuencia de niveles; `changes()` es la telemetría del gate de la demo.
class HudDegradeLadder {
public:
	/// Presupuesto por frame en ciclos y tope de la escalera (nivel máximo aplicable).
	constexpr void configure(eng::u32 budget_cycles,
				 eng::u8 max_level = kHudDegradeMaxLevel) noexcept {
		m_budget = budget_cycles;
		// 7/8 sin multiplicar (el producto podría desbordar) y en u32 (nada de u64: con
		// `-nostdlib` una división u64 arrastra `__udivdi3`, familia DT-039).
		m_exit = (budget_cycles / 8u) * 7u;
		m_max_level = (max_level > kHudDegradeMaxLevel) ? kHudDegradeMaxLevel : max_level;
	}

	/// Aplica la medida del coste del HUD del frame; devuelve `true` si cambió el nivel.
	constexpr bool update(eng::u32 used_cycles) noexcept {
		if (m_budget == 0u) { // sin presupuesto declarado no se degrada
			return false;
		}
		eng::u8 next = m_level;
		if (used_cycles > m_budget) {
			next = (m_level < m_max_level) ? static_cast<eng::u8>(m_level + 1u) : m_level;
		} else if (used_cycles < m_exit) {
			next = (m_level > 0u) ? static_cast<eng::u8>(m_level - 1u) : 0u;
		}
		if (next == m_level) {
			return false;
		}
		m_level = next;
		++m_changes;
		return true;
	}

	[[nodiscard]] constexpr eng::u8 level() const noexcept { return m_level; }
	[[nodiscard]] constexpr bool degraded() const noexcept { return m_level != 0u; }
	[[nodiscard]] constexpr eng::u32 changes() const noexcept { return m_changes; }
	[[nodiscard]] constexpr eng::u32 budget_cycles() const noexcept { return m_budget; }

private:
	eng::u32 m_budget = 0u;
	eng::u32 m_exit = 0u;
	eng::u8 m_max_level = kHudDegradeMaxLevel;
	eng::u8 m_level = 0u;
	eng::u32 m_changes = 0u;
};

} // namespace eng::hud
