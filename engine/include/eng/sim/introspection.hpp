#pragma once

/// \file introspection.hpp
/// **Introspección simulada** (`eng::sim`, capa de persona): lo que un personaje "sabe de
/// sí mismo" al decidir, expresado como señales que el juego puede aportar desde su motor.
///
/// Cualquier juego que quiera usar la psicología de NPC necesita responder, desde el punto
/// de vista **subjetivo** del personaje, a preguntas como: ¿estoy seguro de esta jugada o
/// dudo entre varias? ¿la he consultado en un libro? ¿veo que el rival se ha equivocado?
/// ¿me veo apurado por tiempo o por desventaja? Este módulo convierte esos **hechos
/// objetivos** (`DecisionFacts`, agnósticos del juego) en un `Introspection` que alimenta
/// el afecto (`Mind`) y el estado (`PsycheState`), y de ahí las **fugas** (`expression.hpp`).
///
/// No conoce ajedrez, Go ni póker: el juego traduce su búsqueda/libro/reloj a `DecisionFacts`.
///
/// ```text
///   motor del juego                introspección            persona
///   ┌───────────────┐  hechos   ┌──────────────┐  aplica  ┌───────────────┐
///   │ search/libro/ │ ────────► │Introspection │ ───────► │ Mind/Psyche   │ ─► fugas
///   │ reloj/eval    │           │(confianza…)  │          │(afecto/estado)│
///   └───────────────┘           └──────────────┘          └───────────────┘
/// ```
///
/// Verificación: HOST-206.

#include <eng/core/types/types.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/psyche.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Señales de introspección, en `[0,255]` (128 = neutro en las bipolares).
struct Introspection {
	eng::u8 confidence = 128u; ///< seguridad en la jugada elegida
	eng::u8 doubt = 0u;        ///< duda entre alternativas
	eng::u8 pressure = 0u;     ///< prisa (tiempo) o desventaja
	eng::u8 surprise = 0u;     ///< sorpresa por lo que hizo el rival
	eng::u8 satisfaction = 0u; ///< satisfacción por la propia jugada
	eng::u8 alert = 0u;        ///< alerta: ha visto un error del rival
	eng::u8 knowledge = 0u;    ///< consultó libro/conocimiento (no improvisa)
};

/// Hechos objetivos que el juego aporta (todos agnósticos del juego concreto).
struct DecisionFacts {
	eng::s32 best_score = 0;    ///< evaluación de la mejor jugada (desde su punto de vista)
	eng::s32 second_score = 0;  ///< evaluación de la segunda mejor (para medir el margen)
	eng::s32 prev_eval = 0;     ///< evaluación esperada antes de la jugada del rival
	eng::u8 time_left = 255u;   ///< tiempo restante normalizado (255 = sin prisa)
	eng::u8 moves_available = 1u; ///< nº de jugadas legales (1 = forzada)
	bool book_hit = false;      ///< consultó libro/conocimiento
};

/// Pesos del mapeo. Todos porcentuales; ajustarlos cambia la "personalidad expresiva".
struct IntrospectionParams {
	eng::u8 margin_scale = 100u;   ///< cuánto pesa el margen entre 1.ª y 2.ª jugada
	eng::u8 doubt_scale = 120u;    ///< cuánto pesa el margen pequeño en la duda
	eng::u8 time_pressure = 80u;   ///< cuánto pesa el poco tiempo en la presión
	eng::u8 losing_pressure = 60u; ///< cuánto pesa ir perdiendo en la presión
	eng::u8 book_confidence = 90u; ///< cuánto sube la confianza consultar el libro
	eng::u8 blunder_alert = 90u;   ///< cuánto pesa ver un error del rival
	eng::u8 surprise_scale = 80u;  ///< cuánto pesa que la posición empeore
	eng::u8 good_move = 70u;       ///< cuánto pesa una jugada claramente buena
};

namespace detail {

/// Satura a `[0,255]`.
[[nodiscard]] constexpr eng::u8 clamp255(eng::s32 v) noexcept {
	if (v < 0) {
		return 0u;
	}
	return v > 255 ? static_cast<eng::u8>(255u) : static_cast<eng::u8>(v);
}

} // namespace detail

/// Deriva la introspección de los hechos objetivos. El **margen** entre la mejor y la
/// segunda jugada fija confianza/duda; el libro sube la confianza; el poco tiempo y la
/// desventaja dan presión; un salto de evaluación favorable delata el error del rival y
/// uno desfavorable produce sorpresa.
[[nodiscard]] constexpr Introspection introspect(const DecisionFacts& f,
                                                 const IntrospectionParams& p =
                                                     IntrospectionParams {}) noexcept {
	Introspection in {};
	// Margen: cuánto mejor es la mejor jugada que la segunda (0 si es forzada).
	eng::s32 margin = f.best_score - f.second_score;
	if (f.moves_available <= 1u) {
		margin = 400; // forzada: seguridad total
	}
	if (margin < 0) {
		margin = 0;
	}
	const eng::u8 margin_c = detail::clamp255(margin / 2); // hasta ~100 con margen 200cp

	// Confianza: base + margen + libro, menos duda.
	eng::s32 conf = 128 + static_cast<eng::s32>(u8_scale(margin_c, p.margin_scale)) / 2;
	if (f.book_hit) {
		conf += p.book_confidence / 2;
	}
	// Duda: margen pequeño => mucha duda; forzada => nada.
	eng::s32 doubt = 0;
	if (f.moves_available > 1u) {
		const eng::u8 closeness = static_cast<eng::u8>(margin > 200 ? 0 : (200 - margin) / 2);
		doubt = u8_scale(closeness, p.doubt_scale);
	}
	conf -= doubt / 2;
	in.confidence = detail::clamp255(conf);
	in.doubt = detail::clamp255(doubt);

	// Presión: poco tiempo y/o ir perdiendo (best_score muy negativo).
	eng::s32 pressure = 0;
	if (f.time_left < 255u) {
		pressure += u8_scale(static_cast<eng::u8>(255u - f.time_left), p.time_pressure) / 2;
	}
	if (f.best_score < 0) {
		const eng::s32 losing_cp = -f.best_score;
		const eng::u8 losing = static_cast<eng::u8>(
			losing_cp > 600 ? 255
			                : eng::math::div_wide(losing_cp, static_cast<eng::s16>(3)));
		pressure += u8_scale(losing, p.losing_pressure) / 2;
	}
	in.pressure = detail::clamp255(pressure);

	// Error del rival: la posición mejoró respecto a lo esperado (prev_eval).
	const eng::s32 delta = f.best_score - f.prev_eval;
	if (delta > 0) {
		in.alert = detail::clamp255(u8_scale(static_cast<eng::u8>(delta > 255 ? 255 : delta),
		                                     p.blunder_alert));
	} else if (delta < 0) {
		const eng::u8 worse = static_cast<eng::u8>((-delta) > 255 ? 255 : (-delta));
		in.surprise = detail::clamp255(u8_scale(worse, p.surprise_scale));
	}

	// Satisfacción: jugada propia claramente buena y sin duda.
	if (f.best_score > 100 && margin > 100) {
		const eng::u8 good = static_cast<eng::u8>(f.best_score > 500 ? 255 : f.best_score / 2);
		in.satisfaction = detail::clamp255(u8_scale(good, p.good_move));
	}
	in.knowledge = f.book_hit ? 200u : 0u;
	return in;
}

/// Inyecta la introspección en el afecto (`Mind`) y el estado (`PsycheState`): la
/// confianza y la satisfacción alegran; la duda, la presión y la sorpresa dan miedo; ver
/// un error del rival da ira/impulso; consultar el libro sosiega.
constexpr void introspection_apply(const Introspection& in, Mind& mind,
                                   PsycheState& psyche) noexcept {
	// Confianza alta => alegría/cordialidad; baja => miedo/tensión.
	if (in.confidence >= 128u) {
		const eng::u8 up = static_cast<eng::u8>(in.confidence - 128u);
		mind.emotions.joy = u8_sat_add(mind.emotions.joy, up / 2u);
		mind.emotions.cordiality = u8_sat_add(mind.emotions.cordiality, up / 4u);
		psyche.confidence = u8_sat_add(psyche.confidence, up / 3u);
	} else {
		const eng::u8 down = static_cast<eng::u8>(128u - in.confidence);
		mind.emotions.fear = u8_sat_add(mind.emotions.fear, down / 2u);
		psyche.confidence = u8_sat_sub(psyche.confidence, down / 3u);
	}
	// Duda => miedo leve y curiosidad (mira la posición).
	mind.emotions.fear = u8_sat_add(mind.emotions.fear, in.doubt / 3u);
	mind.emotions.curiosity = u8_sat_add(mind.emotions.curiosity, in.doubt / 4u);
	// Presión => miedo y tensión.
	mind.emotions.fear = u8_sat_add(mind.emotions.fear, in.pressure / 2u);
	psyche.tension = u8_sat_add(psyche.tension, in.pressure / 2u);
	// Sorpresa => miedo y algo de ira.
	mind.emotions.fear = u8_sat_add(mind.emotions.fear, in.surprise / 2u);
	mind.emotions.anger = u8_sat_add(mind.emotions.anger, in.surprise / 4u);
	// Satisfacción => alegría.
	mind.emotions.joy = u8_sat_add(mind.emotions.joy, in.satisfaction / 2u);
	// Alerta (error del rival) => ira/impulso (aprovecha) y alegría.
	mind.emotions.anger = u8_sat_add(mind.emotions.anger, in.alert / 3u);
	mind.emotions.joy = u8_sat_add(mind.emotions.joy, in.alert / 4u);
	// Conocimiento (libro) => sosiega el miedo.
	mind.emotions.fear = u8_sat_sub(mind.emotions.fear, in.knowledge / 6u);
}

/// Nombre legible del estado introspectivo dominante (diagnóstico).
[[nodiscard]] constexpr const char* introspection_dominant(const Introspection& in) noexcept {
	if (in.pressure > 150u) {
		return "presionado";
	}
	if (in.alert > 120u) {
		return "alerta";
	}
	if (in.surprise > 120u) {
		return "sorprendido";
	}
	if (in.doubt > 120u) {
		return "dudando";
	}
	if (in.satisfaction > 120u) {
		return "satisfecho";
	}
	if (in.knowledge > 150u) {
		return "seguro (libro)";
	}
	if (in.confidence > 160u) {
		return "confiado";
	}
	return "neutral";
}

} // namespace eng::sim
