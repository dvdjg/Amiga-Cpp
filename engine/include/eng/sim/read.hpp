#pragma once

/// \file read.hpp
/// **Lectura de tells** (`eng::sim`, capa de persona): el modelo que un observador
/// construye sobre un rival a partir de los gestos que le ve y de lo que revela en el
/// showdown.
///
/// El problema del aprendizaje es que la **fuerza de la mano solo se conoce al mostrar**:
/// sin showdown no hay etiqueta y no se aprende. Por eso "hay que conocerle bien" para
/// fiarse de su tell. El modelo acumula, por rival y gesto, cuántas veces ese gesto
/// coincidió con mano **fuerte** y cuántas con **débil** (`TellStat`), y estima con
/// aritmética entera (Bayes-lite) la probabilidad de mano fuerte dado el gesto.
///
/// - Un **pardillo** (fugas genuinas) genera una asociación fuerte y estable: fiarse paga.
/// - Un **listillo** (fugas ruidosas o invertidas) deja la asociación cerca del 50 %: el
///   observador no puede fiarse y su incertidumbre se mantiene.
/// - La **suspicacia** del observador descuenta los indicios "demasiado claros" (posible
///   farol invertido) y el **arquetipo supuesto** actúa de prior.
///
/// Todo entero, sin heap y determinista.
///
/// Verificación: HOST-201.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/sim/archetypes.hpp>
#include <eng/sim/expression.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Estadística de un gesto observado en un rival: con cuántas manos fuertes y débiles
/// coincidió.
struct TellStat {
	eng::u16 strong = 0u;
	eng::u16 weak = 0u;

	[[nodiscard]] constexpr eng::u16 total() const noexcept {
		return static_cast<eng::u16>(strong + weak);
	}
};

/// Parámetros de la lectura.
struct ReadParams {
	eng::u16 prior_strong = 500u;   ///< probabilidad a priori de mano fuerte (por mil)
	eng::u8 suspicion = 50u;        ///< cuánto descuenta los tells "demasiado claros"
	eng::u8 archetype_bias = 30u;   ///< cuánto pesa el arquetipo supuesto en el prior
	eng::u8 min_samples = 3u;       ///< showdowns mínimos para fiarse de un tell
	eng::u8 strong_threshold = 40u; ///< indicio (|·|) a partir del cual se usa
};

/// Estilo de tells deducido de un rival.
enum class TellStyle : eng::u8 {
	Unknown = 0u, ///< sin muestras suficientes
	Readable,     ///< asociación fuerte y estable (pardillo)
	Noisy,        ///< asociación débil (listillo/embustero)
};

/// Modelo de lectura de un observador sobre varios rivales y gestos.
template <eng::usize MaxTargets, eng::usize NumGestures>
struct ReadModel {
	TellStat tells[MaxTargets][NumGestures] {};
	eng::u16 hands_seen[MaxTargets] {};
	Archetype archetype_guess[MaxTargets] {};
	eng::u8 uncertainty[MaxTargets] {}; ///< 255 = nada conocido

	constexpr void reset() noexcept {
		for (eng::usize i = 0u; i < MaxTargets; ++i) {
			hands_seen[i] = 0u;
			archetype_guess[i] = Archetype::Count;
			uncertainty[i] = 255u;
			for (eng::usize j = 0u; j < NumGestures; ++j) {
				tells[i][j] = TellStat {};
			}
		}
	}

	/// Índice de un gesto en la lista rastreada, o `NumGestures` si no está.
	[[nodiscard]] static constexpr eng::u8 slot_of(const GestureKind* tracked,
	                                               eng::usize count,
	                                               GestureKind g) noexcept {
		for (eng::usize i = 0u; i < count; ++i) {
			if (tracked[i] == g) {
				return static_cast<eng::u8>(i);
			}
		}
		return static_cast<eng::u8>(NumGestures);
	}
};

/// Registra el resultado de un showdown: los gestos que se fugaron durante la mano se
/// etiquetan como mano fuerte o débil. Sin showdown no se llama (no hay etiqueta).
template <eng::usize MaxTargets, eng::usize NumGestures>
constexpr void label_showdown(ReadModel<MaxTargets, NumGestures>& m, eng::u8 target, bool strong,
                              eng::Span<const GestureKind> tracked,
                              eng::Span<const LeakedGesture> leaked) noexcept {
	if (target >= MaxTargets) {
		return;
	}
	if (m.hands_seen[target] < 0xffffu) {
		++m.hands_seen[target];
	}
	for (eng::usize i = 0u; i < leaked.size(); ++i) {
		for (eng::usize s = 0u; s < tracked.size() && s < NumGestures; ++s) {
			if (tracked[s] != leaked[i].kind) {
				continue;
			}
			if (strong) {
				if (m.tells[target][s].strong < 0xffffu) {
					++m.tells[target][s].strong;
				}
			} else {
				if (m.tells[target][s].weak < 0xffffu) {
					++m.tells[target][s].weak;
				}
			}
		}
	}
	// Incertidumbre baja con la exposición (pero no se anula: siempre queda duda).
	const eng::u16 seen = m.hands_seen[target];
	m.uncertainty[target] = seen >= 32u ? 0u
	                       : seen >= 8u ? static_cast<eng::u8>(64u - seen * 8u)
	                                    : static_cast<eng::u8>(255u - seen * 24u);
}

/// Ajusta el prior según el arquetipo supuesto del rival: un arquetipo embustero
/// (`deceit` alto) baja el prior de mano fuerte (sus gestos mienten).
[[nodiscard]] constexpr eng::u16 prior_for_archetype(eng::u16 prior,
                                                     Archetype guess,
                                                     eng::u8 bias) noexcept {
	if (guess == Archetype::Count) {
		return prior;
	}
	const eng::u8 deceit = archetype_def(guess).psyche.deceit;
	const eng::s32 adjust = eng::math::div_wide(
		static_cast<eng::s32>(static_cast<eng::s16>(deceit) - 50) * static_cast<eng::s32>(bias),
		static_cast<eng::s16>(100));
	eng::s32 p = static_cast<eng::s32>(prior) - adjust;
	if (p < 0) {
		p = 0;
	}
	if (p > 1000) {
		p = 1000;
	}
	return static_cast<eng::u16>(p);
}

/// Probabilidad `[0,1000]` de mano fuerte dado que el rival hizo el gesto del `slot`.
/// Sin muestras devuelve el prior (ajustado por el arquetipo supuesto).
template <eng::usize MaxTargets, eng::usize NumGestures>
[[nodiscard]] constexpr eng::u16 p_strong(const ReadModel<MaxTargets, NumGestures>& m,
                                          eng::u8 target, eng::u8 slot,
                                          const ReadParams& p = ReadParams {}) noexcept {
	const eng::u16 prior = prior_for_archetype(p.prior_strong,
	                                           target < MaxTargets ? m.archetype_guess[target]
	                                                               : Archetype::Count,
	                                           p.archetype_bias);
	if (target >= MaxTargets || slot >= NumGestures) {
		return prior;
	}
	const TellStat& t = m.tells[target][slot];
	// (strong*1000 + prior) / (total+1), en por mil, sin libcalls.
	const u32 num = static_cast<u32>(eng::math::mulu16(t.strong, 1000u)) + prior;
	const u32 den = static_cast<u32>(t.total()) + 1u;
	return static_cast<eng::u16>(eng::util::div32(num, den));
}

/// Indicio del gesto en `[-100, +100]`: positivo = el rival suele tener mano fuerte
/// cuando lo hace; negativo = suele tener mano débil. La suspicacia descuenta los
/// indicios muy extremos (posible inversión) y `min_samples` exige exposición.
template <eng::usize MaxTargets, eng::usize NumGestures>
[[nodiscard]] constexpr eng::s16 tell_indicio(const ReadModel<MaxTargets, NumGestures>& m,
                                              eng::u8 target, eng::u8 slot,
                                              const ReadParams& p = ReadParams {}) noexcept {
	if (target >= MaxTargets || slot >= NumGestures || m.hands_seen[target] < p.min_samples) {
		return 0;
	}
	const eng::u16 ps = p_strong(m, target, slot, p);
	eng::s16 ind = static_cast<eng::s16>(ps) - 500; // [-500, 500]
	ind = eng::math::div_wide(static_cast<eng::s32>(ind), static_cast<eng::s16>(5)); // [-100,100]
	// Descuento por suspicacia: cuanto más extremo, más se desconfía.
	const eng::s16 mag = ind < 0 ? static_cast<eng::s16>(-ind) : ind;
	const eng::s16 discount = eng::math::div_wide(
		static_cast<eng::s32>(mag) * static_cast<eng::s32>(p.suspicion), static_cast<eng::s16>(100));
	return static_cast<eng::s16>(ind - (ind > 0 ? discount : -discount));
}

/// Clasifica la fiabilidad de los tells de un rival a partir de la asociación más fuerte.
template <eng::usize MaxTargets, eng::usize NumGestures>
[[nodiscard]] constexpr TellStyle classify_tells(const ReadModel<MaxTargets, NumGestures>& m,
                                                 eng::u8 target,
                                                 const ReadParams& p = ReadParams {}) noexcept {
	if (target >= MaxTargets || m.hands_seen[target] < p.min_samples) {
		return TellStyle::Unknown;
	}
	eng::s16 best = 0;
	for (eng::usize s = 0u; s < NumGestures; ++s) {
		const eng::s16 ind = tell_indicio(m, target, static_cast<eng::u8>(s), p);
		const eng::s16 mag = ind < 0 ? static_cast<eng::s16>(-ind) : ind;
		if (mag > best) {
			best = mag;
		}
	}
	return best >= static_cast<eng::s16>(p.strong_threshold) ? TellStyle::Readable
	                                                         : TellStyle::Noisy;
}

} // namespace eng::sim
