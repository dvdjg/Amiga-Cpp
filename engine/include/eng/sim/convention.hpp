#pragma once

/// \file convention.hpp
/// **Convenciones secretas** (`eng::sim`, capa de persona): un código de gestos pactado
/// entre dos o más jugadores para comunicarse sin que los demás lo entiendan. Es el
/// sustrato del **Mus**.
///
/// - `Convention`: mapa `gesto → significado` (una señal `SignalKind`), con el **disimulo**
///   con que se ejecuta y la **exposición** acumulada (riesgo de ser descubierta).
/// - `emit_convention`: traduce un gesto pactado a la señal que ve el compañero.
/// - `decode_convention`: el compañero que comparte el código lo decodifica; quien no lo
///   comparte solo ve un gesto sin sentido.
/// - `infer_convention`: un observador externo que ve **repetición correlacionada** de
///   gestos entre los mismos jugadores puede deducir que hay un pacto y subir su sospecha;
///   el `exposure` sube con cada uso y baja con el disimulo.
///
/// Todo entero, sin heap y determinista.
///
/// Verificación: HOST-203.

#include <eng/core/types/types.hpp>
#include <eng/sim/communication.hpp>
#include <eng/sim/expression.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

inline constexpr eng::usize kMaxConventionGestures = 8u;

/// Una convención: qué significa cada gesto pactado, cuánto se disimula y cuánto se ha
/// expuesto.
struct Convention {
	eng::u16 id = 0u; ///< identidad del pacto (0 = ninguna)
	SignalKind meaning[kMaxConventionGestures] {};
	GestureKind gesture[kMaxConventionGestures] {};
	eng::u8 count = 0u;
	eng::u8 concealment = 50u; ///< 0..100, cuánto disimulan al ejecutarla
	eng::u8 exposure = 0u;     ///< 0..255, riesgo acumulado de ser descubierta

	/// ¿Existe el pacto?
	[[nodiscard]] constexpr bool active() const noexcept { return id != 0u && count > 0u; }

	/// Añade un par gesto→señal. Devuelve `false` si está llena.
	constexpr bool add(GestureKind g, SignalKind s) noexcept {
		if (count >= kMaxConventionGestures) {
			return false;
		}
		gesture[count] = g;
		meaning[count] = s;
		++count;
		return true;
	}

	/// Señal asociada a un gesto, o `SignalKind::Count` si no está pactado.
	[[nodiscard]] constexpr SignalKind lookup(GestureKind g) const noexcept {
		for (eng::u8 i = 0u; i < count; ++i) {
			if (gesture[i] == g) {
				return meaning[i];
			}
		}
		return SignalKind::Count;
	}
};

/// Resultado de intentar decodificar un gesto.
enum class ConventionDecode : eng::u8 {
	NotShared = 0u, ///< quien mira no comparte la convención (ve un gesto sin sentido)
	UnknownGesture, ///< comparte la convención pero el gesto no está pactado
	Decoded,        ///< gesto pactado: hay señal
};

/// Emite una señal de convención desde un gesto pactado. Sube la exposición según el
/// disimulo. `sender`/`room`/`x`/`y` describen al emisor; `intensity` es la del gesto.
[[nodiscard]] constexpr Signal emit_convention(Convention& c, GestureKind g, EntityId sender,
                                               FactionId faction, RoomId room, eng::s16 x,
                                               eng::s16 y, eng::u8 intensity,
                                               const SignalParams& p = SignalParams {}) noexcept {
	Signal s {};
	s.sender = sender;
	s.faction = faction;
	s.room = room;
	s.x = x;
	s.y = y;
	s.kind = c.lookup(g);
	s.intensity = intensity;
	s.range = p.base_range; // una seña pactada no depende del oído: se ve
	// La exposición sube con cada uso y con la intensidad; el disimulo la frena.
	const eng::u8 used = u8_sat_add(intensity / 4u, 8u);
	const eng::u8 damped = u8_scale(used, static_cast<eng::u8>(100u - c.concealment / 2u));
	c.exposure = u8_sat_add(c.exposure, damped);
	return s;
}

/// Decodifica un gesto visto. `shares` indica si quien mira comparte el pacto `c`.
[[nodiscard]] constexpr ConventionDecode decode_convention(const Convention& c, GestureKind g,
                                                           bool shares,
                                                           SignalKind& out) noexcept {
	if (!shares || !c.active()) {
		return ConventionDecode::NotShared;
	}
	const SignalKind s = c.lookup(g);
	if (s == SignalKind::Count) {
		return ConventionDecode::UnknownGesture;
	}
	out = s;
	return ConventionDecode::Decoded;
}

/// Observación de un gesto por un externo, para inferir convenciones: quién lo hizo y en
/// qué contexto (una decisión). Un observador acumula repeticiones correlacionadas.
struct ConventionObserver {
	eng::u16 pair_repeats = 0u; ///< veces que el mismo par repitió un gesto en decisión
	eng::u8 suspicion = 0u;     ///< 0..255, sospecha de que hay un pacto
	eng::u8 last_gesture = 0xffu;
	EntityId last_pair_a = no_entity;
	EntityId last_pair_b = no_entity;
};

/// Registra que `a` hizo el gesto `g` mirando a `b` (o justo antes de que `b` actuara).
/// Si el mismo par repite gesto en momentos de decisión, la sospecha sube.
constexpr void observe_for_convention(ConventionObserver& obs, EntityId a, EntityId b,
                                      GestureKind g, bool at_decision,
                                      eng::u8 rate = 40u) noexcept {
	if (!at_decision) {
		return;
	}
	const bool same_pair = (obs.last_pair_a == a && obs.last_pair_b == b) ||
	                       (obs.last_pair_a == b && obs.last_pair_b == a);
	const bool same_gesture = obs.last_gesture == static_cast<eng::u8>(g);
	if (same_pair && same_gesture) {
		obs.pair_repeats = static_cast<eng::u16>(obs.pair_repeats + 1u);
		obs.suspicion = u8_sat_add(obs.suspicion, rate);
	} else if (same_pair) {
		obs.suspicion = u8_sat_add(obs.suspicion, static_cast<eng::u8>(rate / 2u));
	}
	obs.last_pair_a = a;
	obs.last_pair_b = b;
	obs.last_gesture = static_cast<eng::u8>(g);
}

/// ¿El observador ha deducido que hay una convención?
[[nodiscard]] constexpr bool convention_inferred(const ConventionObserver& obs,
                                                 eng::u8 threshold = 180u) noexcept {
	return obs.suspicion >= threshold;
}

/// Ajusta la exposición de la convención al disimulo con el tiempo: el disimulo alto la
/// baja (si no se usa), el bajo la mantiene.
constexpr void convention_decay(Convention& c, eng::u8 amount = 3u) noexcept {
	const eng::u8 relief = u8_scale(amount, c.concealment);
	c.exposure = u8_sat_sub(c.exposure, relief);
}

} // namespace eng::sim
