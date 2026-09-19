#pragma once

/// \file needs.hpp
/// **Necesidades** de una criatura (`eng::sim`): presiones en `[0, 255]` donde 0 es
/// "satisfecho" y 255 "crítico". Cuanto más alta, más urgencia aporta a la decisión.
///
/// Seis ejes cubren la supervivencia, el refugio y la vida social:
/// - `hunger`: hambre (sube con el tiempo, baja al comer).
/// - `fatigue`: cansancio (sube despierto, baja al dormir).
/// - `fear`: miedo (sube con amenazas, se apacigua solo).
/// - `social`: necesidad de compañía / estatus (sube en soledad).
/// - `injury`: daño acumulado (sube al ser herido, baja al curar).
/// - `exposure`: **estrés ambiental** genérico, con su `HazardKind` asociado.
///
/// `exposure` no se llama "lluvia" a propósito: el mismo eje modela lluvia, frío
/// extremo, ola de calor, tormenta de arena o radiación. El mundo fija el **tipo**
/// (`HazardKind`) y la **severidad**; la criatura solo ve una presión que la empuja a
/// buscar refugio. Reglas:
///
/// - `set_exposure(n, kind, severity)`: sube la presión y recuerda el peligro.
/// - `clear_exposure(n, calm_rate)`: al cesar el peligro, la presión baja por tick.
/// - `hazard_for(kind)` orienta al juego sobre qué protección buscar (abrigo, sombra,
///   cueva, refugio), sin que el modelo de criatura conozca el clima concreto.
///
/// El modelo es puramente entero y lineal: apto para el 68000 y fácil de razonar. Las
/// curvas se implementan como tablas/`if` en el juego, no aquí.
///
/// Verificación: HOST-152.

#include <eng/core/types.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Eje de necesidad, para recorrer/consultar de forma genérica.
enum class Need : eng::u8 {
	Hunger = 0,
	Fatigue = 1,
	Fear = 2,
	Social = 3,
	Injury = 4,
	Exposure = 5,
	Count = 6,
};

/// Peligro ambiental que genera estrés/exposición. La criatura no interpreta el clima:
/// solo sabe que hay una presión y de qué tipo, para elegir la protección adecuada.
enum class HazardKind : eng::u8 {
	None = 0,
	Rain = 1,      ///< lluvia torrencial
	Cold = 2,      ///< frío extremo
	Heat = 3,      ///< calor extremo
	Storm = 4,     ///< tormenta / vendaval
	Dust = 5,      ///< tormenta de polvo o ceniza
	Radiation = 6, ///< radiación / contaminación
	Flood = 7,     ///< crecida/inundación
	Count = 8,
};

/// Protección que mitiga un peligro (el juego la traduce a tiles/objetos). Es una
/// pista de diseño: `Rain` se cubre con techo, `Cold` con abrigo/cueva, `Heat` con
/// sombra/agua, etc.
enum class ShelterKind : eng::u8 {
	None = 0,
	Roof = 1,     ///< refugio cubierto
	Warm = 2,     ///< fuente de calor
	Shade = 3,    ///< sombra / agua fresca
	Sealed = 4,   ///< recinto sellado
	High = 5,     ///< terreno elevado
	Count = 6,
};

/// Protección recomendada para un peligro.
[[nodiscard]] constexpr ShelterKind shelter_for(HazardKind kind) noexcept {
	switch (kind) {
		case HazardKind::Rain:
		case HazardKind::Storm:
		case HazardKind::Dust:
			return ShelterKind::Roof;
		case HazardKind::Cold:
			return ShelterKind::Warm;
		case HazardKind::Heat:
			return ShelterKind::Shade;
		case HazardKind::Radiation:
			return ShelterKind::Sealed;
		case HazardKind::Flood:
			return ShelterKind::High;
		default:
			return ShelterKind::None;
	}
}

/// Conjunto de necesidades de una criatura.
struct Needs {
	eng::u8 hunger = 0;
	eng::u8 fatigue = 0;
	eng::u8 fear = 0;
	eng::u8 social = 0;
	eng::u8 injury = 0;
	eng::u8 exposure = 0;
	eng::u8 hazard = static_cast<eng::u8>(HazardKind::None); ///< tipo de peligro activo

	/// ¿Alguna necesidad ha llegado al máximo? (muerte/colapso inminente).
	[[nodiscard]] constexpr bool any_critical() const noexcept {
		return hunger == 255u || fatigue == 255u || injury == 255u || exposure == 255u;
	}

	[[nodiscard]] constexpr HazardKind hazard_kind() const noexcept {
		return static_cast<HazardKind>(hazard);
	}
};

/// Velocidades por frame del tick de necesidades (la política la fija el juego).
struct NeedRates {
	eng::u8 hunger = 1u;   ///< sube siempre
	eng::u8 fatigue = 1u;  ///< sube despierto, se usa x2 al dormir
	eng::u8 fear = 4u;     ///< baja (apaciguamiento) por frame
	eng::u8 social = 1u;   ///< sube siempre (necesita compañía)
	eng::u8 injury = 0u;   ///< curación por frame (0 = herida permanente)
	eng::u8 exposure = 3u; ///< ritmo al que cede la exposición sin peligro
};

/// Acceso por eje (para tablas y bucles).
[[nodiscard]] constexpr eng::u8& need(Needs& n, Need k) noexcept {
	switch (k) {
		case Need::Hunger: return n.hunger;
		case Need::Fatigue: return n.fatigue;
		case Need::Fear: return n.fear;
		case Need::Social: return n.social;
		case Need::Injury: return n.injury;
		case Need::Exposure: return n.exposure;
		default: return n.hunger;
	}
}

[[nodiscard]] constexpr eng::u8 need(const Needs& n, Need k) noexcept {
	switch (k) {
		case Need::Hunger: return n.hunger;
		case Need::Fatigue: return n.fatigue;
		case Need::Fear: return n.fear;
		case Need::Social: return n.social;
		case Need::Injury: return n.injury;
		case Need::Exposure: return n.exposure;
		default: return n.hunger;
	}
}

/// Sube una necesidad de forma saturada.
constexpr void rise(Needs& n, Need k, eng::u8 amount) noexcept {
	eng::u8& v = need(n, k);
	v = u8_sat_add(v, amount);
}

/// Baja una necesidad de forma saturada (nunca por debajo de 0).
constexpr void ease(Needs& n, Need k, eng::u8 amount) noexcept {
	eng::u8& v = need(n, k);
	v = u8_sat_sub(v, amount);
}

/// Avanza un frame de necesidades. Si `asleep`, el cansancio baja en vez de subir (al
/// doble de su tasa) y el hambre sube a la mitad (metabolismo en reposo). La exposición
/// solo cede si no la mantiene el clima (el mundo la vuelve a fijar si el peligro sigue).
constexpr void tick(Needs& n, const NeedRates& rates, bool asleep) noexcept {
	rise(n, Need::Hunger, asleep ? static_cast<eng::u8>(rates.hunger / 2u + 1u)
				     : rates.hunger);
	if (asleep) {
		ease(n, Need::Fatigue, static_cast<eng::u8>(rates.fatigue * 2u));
	} else {
		rise(n, Need::Fatigue, rates.fatigue);
	}
	ease(n, Need::Fear, rates.fear);
	rise(n, Need::Social, rates.social);
	ease(n, Need::Injury, rates.injury);
	ease(n, Need::Exposure, rates.exposure);
}

/// Sacia el hambre (comer): `amount` en puntos de necesidad.
constexpr void feed(Needs& n, eng::u8 amount) noexcept { ease(n, Need::Hunger, amount); }

/// Recupera energía (dormir un ciclo completo).
constexpr void rest(Needs& n, eng::u8 amount) noexcept { ease(n, Need::Fatigue, amount); }

/// Calma el miedo (refugio, presencia de aliados).
constexpr void calm(Needs& n, eng::u8 amount) noexcept { ease(n, Need::Fear, amount); }

/// Satisface la necesidad social (interacción con el grupo).
constexpr void socialize(Needs& n, eng::u8 amount) noexcept { ease(n, Need::Social, amount); }

/// Cura el daño (descanso, cuidado de la manada).
constexpr void heal(Needs& n, eng::u8 amount) noexcept { ease(n, Need::Injury, amount); }

/// Registra una herida.
constexpr void hurt(Needs& n, eng::u8 amount) noexcept { rise(n, Need::Injury, amount); }

/// Fija la exposición a un peligro ambiental (el clima lo dicta cada tick). Guarda el
/// tipo de peligro para que la decisión sepa qué protección buscar.
constexpr void set_exposure(Needs& n, HazardKind kind, eng::u8 severity) noexcept {
	n.hazard = static_cast<eng::u8>(kind);
	if (severity > n.exposure) {
		n.exposure = severity;
	}
}

/// Compatibilidad semántica: la lluvia es un peligro de tipo `Rain`.
constexpr void set_rain(Needs& n, eng::u8 severity) noexcept {
	set_exposure(n, HazardKind::Rain, severity);
}

/// Al cesar el peligro, marca el hazard como inactivo (la presión baja por `tick`).
constexpr void clear_hazard(Needs& n) noexcept {
	if (n.hazard_kind() != HazardKind::None) {
		n.hazard = static_cast<eng::u8>(HazardKind::None);
	}
}

/// ¿La criatura está expuesta a un peligro (por encima de `threshold`)?
[[nodiscard]] constexpr bool exposed(const Needs& n, eng::u8 threshold = 100u) noexcept {
	return n.exposure >= threshold;
}

} // namespace eng::sim
