#pragma once

/// \file persona_bot.hpp
/// **Integración de la capa de persona con `eng::cards`**: convierte la `Persona` y su
/// `PsycheState` (`eng::sim`) en los parámetros de decisión del bot y en la **expresión**
/// (tells) que el resto de la mesa puede leer.
///
/// - `params_from_persona`: deriva `BotParams` (vpip, agresividad, farol, colchón,
///   umbral de apuesta) del arquetipo, ajustados por el estado (tilt sube agresión, miedo
///   la baja) y por las aptitudes (pericia).
/// - `emit_tells`: al resolver una acción, calcula las fugas con `expression.hpp` y
///   devuelve los gestos visibles.
/// - `read_opponent_tell`: el observador acumula en su `ReadModel` lo que ve y, en el
///   showdown, lo etiqueta como mano fuerte/débil.
///
/// Así el bot **juega como su personaje** (no todos igual) y **se le nota**, que es lo que
/// da ambiente a la mesa. La lectura realimenta la decisión vía `tell_indicio`.
///
/// Verificación: HOST-204. Estado: verificado por test host; consumido por `games/200_holdem`.

#include <eng/core/types.hpp>

#include <eng/cards/ai/bot.hpp>
#include <eng/cards/core/types.hpp>

#include <eng/sim/expression.hpp>
#include <eng/sim/persona.hpp>
#include <eng/sim/psyche.hpp>
#include <eng/sim/read.hpp>

namespace eng::cards {

/// Convierte un modificador porcentual `[-100,+100]` en ajuste de un parámetro por mil.
/// Producto 16×16 (`mul_wide` = `muls.w`) y cociente `div_wide` (`divs.w`), sin libgcc.
[[nodiscard]] constexpr u16 adjust_permille(u16 value, eng::s16 mod) noexcept {
	const eng::s32 v = static_cast<eng::s32>(value);
	const eng::s32 delta = eng::math::div_wide(
		eng::math::mul_wide(static_cast<eng::s16>(value), mod), static_cast<eng::s16>(100));
	eng::s32 r = v + delta;
	if (r < 0) {
		return 0u;
	}
	if (r > 1000) {
		return 1000u;
	}
	return static_cast<u16>(r);
}

/// Media entera de tres valores con signo sin `__divsi3` (`div_wide` = `divs.w`).
[[nodiscard]] constexpr eng::s16 avg3(eng::s16 a, eng::s16 b, eng::s16 c) noexcept {
	return eng::math::div_wide(static_cast<eng::s32>(a) + b + c, static_cast<eng::s16>(3));
}

/// Media entera de dos valores con signo (desplazamiento aritmético).
[[nodiscard]] constexpr eng::s16 avg2(eng::s16 a, eng::s16 b) noexcept {
	return static_cast<eng::s16>((static_cast<eng::s32>(a) + b) >> 1);
}

/// Deriva los parámetros del bot del arquetipo y del estado psicológico. El estilo
/// (`BotStyle`) queda como base; la persona lo sesga.
[[nodiscard]] constexpr BotParams params_from_persona(const eng::sim::Persona& persona,
                                                      const eng::sim::PsycheState& psyche,
                                                      BotStyle style = BotStyle::Balanced) noexcept {
	BotParams p = bot_params(style);
	const eng::sim::PsycheTraits& t = persona.psyche;

	// Agresividad: temper/vanity/risk la suben; patience la modera.
	eng::s16 aggr_mod = avg3(eng::sim::trait_mod(t.temper), eng::sim::trait_mod(t.vanity),
	                         eng::sim::trait_mod(t.risk));
	p.aggression_permille = adjust_permille(p.aggression_permille, aggr_mod);

	// Farol: deceit lo sube, honesty lo baja.
	const eng::s16 bluff_mod = avg2(eng::sim::trait_mod(t.deceit),
	                                static_cast<eng::s16>(-eng::sim::trait_mod(t.honesty)));
	p.bluff_permille = adjust_permille(p.bluff_permille, bluff_mod);

	// Entrada al bote: gullibility/impulsivity la suben, suspicion la baja.
	const eng::s16 vpip_mod = avg3(eng::sim::trait_mod(t.gullibility),
	                               eng::sim::trait_mod(t.impulsivity),
	                               static_cast<eng::s16>(-eng::sim::trait_mod(t.suspicion)));
	p.vpip_permille = adjust_permille(p.vpip_permille, vpip_mod);

	// Colchón: la aversión al riesgo y la poca confianza lo suben (juega más cauto).
	const eng::s16 caution_mod = avg2(
		static_cast<eng::s16>(-eng::sim::trait_mod(t.risk) / 2),
		static_cast<eng::s16>((50 - static_cast<eng::s16>(psyche.confidence)) / 2));
	p.call_margin_permille = adjust_permille(p.call_margin_permille, caution_mod);

	// Umbral de apuesta: optimism lo baja (apuesta más).
	p.bet_permille = adjust_permille(
		p.bet_permille,
		static_cast<eng::s16>(-eng::sim::trait_mod(t.optimism) / 2));

	// Estado: el tilt sube agresividad y farol; la fatiga la baja.
	const eng::s16 state_mod = eng::sim::psyche_aggression_mod(psyche);
	p.aggression_permille = adjust_permille(p.aggression_permille, avg2(state_mod, 0));
	p.bluff_permille = adjust_permille(p.bluff_permille, avg3(state_mod, 0, 0));
	return p;
}

/// Bot con persona: parámetros derivados del arquetipo/estado y del plan de memoria.
[[nodiscard]] inline Action decide_with_persona(const Table& t, u8 seat,
                                                const eng::sim::Persona& persona,
                                                const eng::sim::PsycheState& psyche,
                                                const CardPlan& plan, BotStyle style,
                                                const OpponentModel* model,
                                                eng::Xoroshiro64pp& rng,
                                                const PreflopTable* table = nullptr,
                                                const HandRange* opponent_range = nullptr) noexcept {
	BotParams params = params_from_persona(persona, psyche, style);
	params.mc_samples = plan.mc_samples;
	params.use_mc = plan.mc_samples > 0u;
	return decide(t, seat, params, model, rng, table, opponent_range);
}

/// Gestos candidatos por defecto de una mesa de póker (los relevantes del juego).
inline constexpr eng::sim::GestureKind kPokerGestures[] = {
	eng::sim::GestureKind::BlinkFast,   eng::sim::GestureKind::PupilDilate,
	eng::sim::GestureKind::WideEyes,    eng::sim::GestureKind::HandTremor,
	eng::sim::GestureKind::NoseFlare,   eng::sim::GestureKind::Swallow,
	eng::sim::GestureKind::Smile,       eng::sim::GestureKind::Grimace,
	eng::sim::GestureKind::LipPress,    eng::sim::GestureKind::FistClench,
	eng::sim::GestureKind::LeanBack,    eng::sim::GestureKind::StareDown,
	eng::sim::GestureKind::GazeAversion, eng::sim::GestureKind::Sigh,
	eng::sim::GestureKind::Smirk,
};
inline constexpr eng::usize kPokerGestureCount =
	sizeof(kPokerGestures) / sizeof(kPokerGestures[0]);

/// Emite los tells del asiento al resolver su acción: calcula las fugas del estado y las
/// devuelve para que los rivales las lean.
[[nodiscard]] inline eng::sim::LeakList emit_tells(const eng::sim::Mind& mind,
                                                   const eng::sim::PsycheState& psyche,
                                                   const eng::sim::Persona& persona,
                                                   const eng::sim::ExpressionParams& p =
                                                       eng::sim::ExpressionParams {}) noexcept {
	const eng::sim::LeakContext ctx = eng::sim::leak_context(psyche, persona);
	eng::sim::LeakList out;
	eng::sim::compute_leaks(mind, ctx, kPokerGestures, out, p);
	return out;
}

/// Lectura: al ver los tells de `target`, el observador los etiqueta. `strong` es la
/// fuerza real de la mano del rival en el showdown (solo entonces hay etiqueta).
template <eng::usize MaxTargets, eng::usize NumGestures>
inline void read_showdown(eng::sim::ReadModel<MaxTargets, NumGestures>& model, u8 target,
                          bool strong, const eng::sim::LeakList& leaked) noexcept {
	eng::sim::label_showdown(model, target, strong,
	                         eng::Span<const eng::sim::GestureKind> {kPokerGestures,
	                                                                 kPokerGestureCount},
	                         leaked.span());
}

/// Indicio agregado de los tells de `target`: el `tell_indicio` de mayor magnitud entre
/// los gestos vistos (positivo = suele tener mano fuerte; negativo = suele tener débil).
template <eng::usize MaxTargets, eng::usize NumGestures>
[[nodiscard]] inline eng::s16 tell_signal(const eng::sim::ReadModel<MaxTargets, NumGestures>& model,
                                          u8 target,
                                          const eng::sim::ReadParams& p =
                                              eng::sim::ReadParams {}) noexcept {
	eng::s16 best = 0;
	for (u8 g = 0u; g < kPokerGestureCount; ++g) {
		const eng::s16 ind = eng::sim::tell_indicio(model, target, g, p);
		const eng::s16 mag = ind < 0 ? static_cast<eng::s16>(-ind) : ind;
		const eng::s16 best_mag = best < 0 ? static_cast<eng::s16>(-best) : best;
		if (mag > best_mag) {
			best = ind;
		}
	}
	return best;
}

/// Rango de un rival combinando su **línea de apuesta** (`opponent_range_from_model`) con
/// el **tell leído** (`ReadModel`). Un tell que apunta a mano fuerte **estrecha** el rango
/// (juega menos manos y mejores); uno que apunta a debilidad lo **ensancha**. Requiere la
/// tabla preflop; si no está, deja el rango completo.
template <eng::usize MaxTargets, eng::usize NumGestures>
inline void opponent_range_with_tells(const OpponentModel& model,
                                      const eng::sim::ReadModel<MaxTargets, NumGestures>& reads,
                                      const Table& t, u8 hero_seat, const PreflopTable* table,
                                      HandRange& out, const eng::sim::ReadParams& rp =
                                          eng::sim::ReadParams {}) noexcept {
	if (table == nullptr || !table->ready) {
		out.set_all();
		return;
	}
	const u16 base_wide = opponent_range_width_permille(model, t, hero_seat);
	// Indicio medio de los rivales vivos: estrecha si positivo, ensancha si negativo.
	eng::s16 signal_sum = 0;
	u8 n = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		const SeatStatus st = t.seats[i].status;
		if (i != hero_seat && (st == SeatStatus::Active || st == SeatStatus::AllIn)) {
			signal_sum = static_cast<eng::s16>(signal_sum + tell_signal(reads, i, rp));
			++n;
		}
	}
	if (n == 0u) {
		make_range_by_percentile(*table, out, base_wide);
		return;
	}
	const eng::s16 signal = eng::math::div_wide(static_cast<eng::s32>(signal_sum),
	                                            static_cast<eng::s16>(n));
	// El tell ajusta el ancho hasta ±33 ‰ (indicio ±100 / 3). Un tell de **fuerza**
	// (positivo) estrecha el rango; uno de debilidad (negativo) lo ensancha.
	const eng::s16 adjust = eng::math::div_wide(static_cast<eng::s32>(signal),
	                                            static_cast<eng::s16>(3));
	eng::s32 wide = static_cast<eng::s32>(base_wide) - adjust;
	if (wide < 80) {
		wide = 80;
	}
	if (wide > 850) {
		wide = 850;
	}
	make_range_by_percentile(*table, out, static_cast<u16>(wide));
}

} // namespace eng::cards
