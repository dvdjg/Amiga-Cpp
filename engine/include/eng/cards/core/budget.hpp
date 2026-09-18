#pragma once

/// \file budget.hpp
/// Presupuesto de memoria del motor de naipes: perfiles `N20`…`N512` y reparto
/// determinista de la RAM libre entre equity Monte Carlo, modelo de rival,
/// histórico de acciones y explicación.
///
/// Igual que `eng::board/core/budget.hpp`, **ningún tamaño se fija con macros**:
/// se calcula en `init` a partir de la RAM libre real y se expone en un
/// `CardPlan`. El plan elige el perfil mayor cuyo `planned_bytes()` cabe; si no
/// cabe ni el mínimo, devuelve `N20` como fallback.
///
/// El perfil decide **cuánto piensa** el bot (muestras de Monte Carlo, resolución
/// del modelo de rival) y **cuánto recuerda** (histórico, plantillas), no la
/// legalidad: un `N20` juega el mismo póker con menos información.
///
/// | Perfil | Footprint | Muestras MC/jugada | Modelo de rival | Histórico | Explicación |
/// |---|---|---|---|---|---|
/// | `N20`  | ~20 kB  | 0 (heurística)  | no            | 16 acciones | mínima |
/// | `N64`  | ~64 kB  | 32              | 1 rival       | 64 acciones | básica |
/// | `N128` | ~128 kB | 96              | 3 rivales     | 128 acciones| media |
/// | `N256` | ~256 kB | 224             | 6 rivales     | 256 acciones| rica |
/// | `N512` | ~512 kB | 448             | 8 rivales     | 512 acciones| completa |
///
/// Verificación: HOST-161. Estado: verificado por test host; **NO VERIFICADO** en
/// demo/hardware (sin consumidor en `games/` todavía).

#include <eng/core/types.hpp>

#include <eng/cards/core/types.hpp>

namespace eng::cards {

/// Perfiles de footprint objetivo del motor de naipes.
enum class CardProfile : u8 {
	N20 = 0u,
	N64,
	N128,
	N256,
	N512,
	Count,
};

/// Reparto de memoria decidido. Todos los campos son bytes salvo los contadores
/// de muestras/reles/acciones.
struct CardPlan {
	CardProfile profile = CardProfile::N20;
	u32 free_bytes = 0u;
	u32 engine_overhead_bytes = 0u;
	u32 equity_bytes = 0u;
	u32 opponent_bytes = 0u;
	u32 history_bytes = 0u;
	u32 explain_bytes = 0u;
	u32 preflop_bytes = 0u;
	u16 mc_samples = 0u;      ///< rollouts Monte Carlo por decisión (0 = heurística)
	u8 tracked_opponents = 0u;
	u8 max_seats = 10u;

	[[nodiscard]] constexpr u32 planned_bytes() const noexcept {
		return engine_overhead_bytes + equity_bytes + opponent_bytes + history_bytes +
		       explain_bytes + preflop_bytes;
	}
};

/// Capacidades fijas de cada perfil.
[[nodiscard]] constexpr CardPlan card_profile_plan(CardProfile profile) noexcept {
	CardPlan plan {};
	plan.profile = profile;
	switch (profile) {
	case CardProfile::N20:
		plan.engine_overhead_bytes = 4096u;
		plan.equity_bytes = 2048u;
		plan.opponent_bytes = 512u;
		plan.history_bytes = 2048u;
		plan.explain_bytes = 4096u;
		plan.preflop_bytes = 4096u;
		plan.mc_samples = 0u;
		plan.tracked_opponents = 0u;
		break;
	case CardProfile::N64:
		plan.engine_overhead_bytes = 8192u;
		plan.equity_bytes = 12288u;
		plan.opponent_bytes = 4096u;
		plan.history_bytes = 8192u;
		plan.explain_bytes = 12288u;
		plan.preflop_bytes = 8192u;
		plan.mc_samples = 32u;
		plan.tracked_opponents = 1u;
		break;
	case CardProfile::N128:
		plan.engine_overhead_bytes = 8192u;
		plan.equity_bytes = 32768u;
		plan.opponent_bytes = 12288u;
		plan.history_bytes = 16384u;
		plan.explain_bytes = 24576u;
		plan.preflop_bytes = 16384u;
		plan.mc_samples = 96u;
		plan.tracked_opponents = 3u;
		break;
	case CardProfile::N256:
		plan.engine_overhead_bytes = 8192u;
		plan.equity_bytes = 65536u;
		plan.opponent_bytes = 32768u;
		plan.history_bytes = 32768u;
		plan.explain_bytes = 49152u;
		plan.preflop_bytes = 32768u;
		plan.mc_samples = 224u;
		plan.tracked_opponents = 6u;
		break;
	case CardProfile::N512:
		plan.engine_overhead_bytes = 8192u;
		plan.equity_bytes = 131072u;
		plan.opponent_bytes = 65536u;
		plan.history_bytes = 65536u;
		plan.explain_bytes = 131072u;
		plan.preflop_bytes = 65536u;
		plan.mc_samples = 448u;
		plan.tracked_opponents = 8u;
		break;
	case CardProfile::Count:
		break;
	}
	return plan;
}

/// Elige el perfil mayor cuyo `planned_bytes()` cabe en `free_bytes` (fallback
/// `N20`). Determinista y sin dependencias de plataforma.
[[nodiscard]] constexpr CardPlan plan_cards_memory(u32 free_bytes) noexcept {
	for (cards_int index = static_cast<cards_int>(CardProfile::N512); index >= 0; --index) {
		CardPlan candidate = card_profile_plan(static_cast<CardProfile>(index));
		if (candidate.planned_bytes() <= free_bytes) {
			candidate.free_bytes = free_bytes;
			return candidate;
		}
	}
	CardPlan fallback = card_profile_plan(CardProfile::N20);
	fallback.free_bytes = free_bytes;
	return fallback;
}

} // namespace eng::cards
