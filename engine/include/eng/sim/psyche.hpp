#pragma once

/// \file psyche.hpp
/// **Estado psicológico temporal** de un personaje (`eng::sim`, capa de persona):
/// compostura efectiva, tensión, confianza, tilt, fatiga, ánimo, racha e imagen de mesa.
/// Cambia durante la partida por los **eventos de la mesa** y modula la expresión
/// (`expression.hpp`) y la decisión.
///
/// La dinámica es un modelo lineal **paramétrico** (`PsycheParams`): cada eje tiende por
/// pasos hacia un objetivo derivado del carácter (`Persona`), los afectos (`Mind`) y los
/// eventos recientes. Todo entero, determinista y sin heap.
///
/// - **Carácter** (estable) = `Persona`; **estado** (temporal) = `PsycheState`.
/// - Un **flemático** aguanta la racha; un **irascible** entra en tilt.
/// - El estado alimenta `LeakContext` (para las fugas) y un modificador de decisión.
///
/// Verificación: HOST-202.

#include <eng/core/types.hpp>
#include <eng/sim/expression.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/persona.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Eventos de la mesa que mueven el estado psicológico.
enum class TableEventKind : eng::u8 {
	WonShowdown = 0,
	LostShowdown,
	WonByFold,
	Bluffed,        ///< farol exitoso
	CaughtBluffing, ///< farol cazado
	BadBeat,        ///< perder con mano fuerte
	SuckedOut,      ///< ganar con suerte
	Slowrolled,     ///< le hicieron slowroll
	WasRead,        ///< le leyeron un tell
	ReadOpponent,   ///< leyó a otro
	LostBigPot,
	WonBigPot,
	Count,
};

/// Estado temporal de la psique, `u8` por eje.
struct PsycheState {
	eng::u8 composure = 50u;  ///< compostura base (la efectiva la calcula la expresión)
	eng::u8 tension = 0u;     ///< activación/nervios (0 = calmado)
	eng::u8 confidence = 50u; ///< seguridad actual
	eng::u8 tilt = 0u;        ///< descontrol acumulado
	eng::u8 fatigue = 0u;     ///< cansancio
	eng::u8 mood = 50u;       ///< ánimo del día
	eng::u8 streak = 50u;     ///< racha (0 = muy mala, 100 = muy buena)
	eng::u8 table_image = 50u; ///< cómo cree que lo ven (alto = respetado)
};

/// Pesos de la dinámica. Todos porcentuales; cambiarlos reajusta la "psicología".
struct PsycheParams {
	eng::u8 tilt_from_badbeat = 60u;   ///< cuánto tilt da un bad beat
	eng::u8 tilt_from_loss = 35u;      ///< cuánto tilt da perder
	eng::u8 tilt_from_caught = 50u;    ///< cuánto tilt da que le cacen un farol
	eng::u8 tilt_decay = 6u;           ///< cuánto se disipa el tilt por turno
	eng::u8 conf_from_win = 30u;       ///< confianza que da ganar
	eng::u8 conf_from_loss = 30u;      ///< confianza que quita perder
	eng::u8 streak_step = 20u;         ///< cuánto mueve la racha cada resultado
	eng::u8 fatigue_step = 3u;         ///< fatiga por mano
	eng::u8 mood_from_joy = 40u;       ///< el ánimo sigue a la alegría
	eng::u8 step = 8u;                 ///< paso de aproximación por actualización
};

/// Convierte el carácter en el estado inicial (compostura, confianza, ánimo).
[[nodiscard]] constexpr PsycheState initial_psyche(const Persona& p) noexcept {
	PsycheState s {};
	s.composure = p.psyche.composure;
	s.confidence = p.psyche.self_esteem;
	s.mood = p.psyche.optimism;
	s.tension = static_cast<eng::u8>(p.base.nervousness / 2u);
	s.tilt = 0u;
	s.fatigue = 0u;
	s.streak = 50u;
	s.table_image = 50u;
	return s;
}

/// Aplica un evento de la mesa al estado y a los afectos (`Mind`).
constexpr void psyche_observe(PsycheState& s, Mind& m, TableEventKind e,
                              const Persona& p,
                              const PsycheParams& params = PsycheParams {}) noexcept {
	switch (e) {
		case TableEventKind::WonShowdown:
			s.confidence = u8_sat_add(s.confidence, u8_scale(params.conf_from_win, 100u));
			s.streak = u8_sat_add(s.streak, params.streak_step);
			s.tilt = u8_sat_sub(s.tilt, params.tilt_decay);
			m.emotions.joy = u8_sat_add(m.emotions.joy, 60u);
			break;
		case TableEventKind::LostShowdown:
			s.confidence = u8_sat_sub(s.confidence, u8_scale(params.conf_from_loss, 100u));
			s.streak = u8_sat_sub(s.streak, params.streak_step);
			s.tilt = u8_sat_add(s.tilt, u8_scale(params.tilt_from_loss, 100u));
			m.emotions.sadness = u8_sat_add(m.emotions.sadness, 60u);
			break;
		case TableEventKind::WonByFold:
			s.confidence = u8_sat_add(s.confidence, u8_scale(params.conf_from_win, 50u));
			m.emotions.joy = u8_sat_add(m.emotions.joy, 30u);
			break;
		case TableEventKind::Bluffed:
			s.confidence = u8_sat_add(s.confidence, u8_scale(params.conf_from_win, 60u));
			m.emotions.joy = u8_sat_add(m.emotions.joy, 50u);
			break;
		case TableEventKind::CaughtBluffing:
			s.confidence = u8_sat_sub(s.confidence, u8_scale(params.conf_from_loss, 100u));
			s.tilt = u8_sat_add(s.tilt, u8_scale(params.tilt_from_caught, 100u));
			m.emotions.fear = u8_sat_add(m.emotions.fear, 40u);
			m.emotions.anger = u8_sat_add(m.emotions.anger, 40u);
			break;
		case TableEventKind::BadBeat:
			s.tilt = u8_sat_add(s.tilt, u8_scale(params.tilt_from_badbeat, 100u));
			s.streak = u8_sat_sub(s.streak, params.streak_step);
			m.emotions.anger = u8_sat_add(m.emotions.anger, 120u);
			m.emotions.sadness = u8_sat_add(m.emotions.sadness, 80u);
			break;
		case TableEventKind::SuckedOut:
			s.confidence = u8_sat_add(s.confidence, u8_scale(params.conf_from_win, 80u));
			m.emotions.joy = u8_sat_add(m.emotions.joy, 80u);
			break;
		case TableEventKind::Slowrolled:
			m.emotions.anger = u8_sat_add(m.emotions.anger, 100u);
			m.emotions.hatred = u8_sat_add(m.emotions.hatred, 60u);
			s.tilt = u8_sat_add(s.tilt, u8_scale(params.tilt_from_loss, 100u));
			break;
		case TableEventKind::WasRead:
			m.emotions.fear = u8_sat_add(m.emotions.fear, 50u);
			// Aprende a controlarse (sube la compostura efectiva vía estado).
			s.composure = u8_sat_add(s.composure, 5u);
			break;
		case TableEventKind::ReadOpponent:
			s.confidence = u8_sat_add(s.confidence, 10u);
			m.emotions.joy = u8_sat_add(m.emotions.joy, 30u);
			break;
		case TableEventKind::LostBigPot:
			s.tilt = u8_sat_add(s.tilt, u8_scale(params.tilt_from_badbeat, 80u));
			m.emotions.sadness = u8_sat_add(m.emotions.sadness, 100u);
			m.emotions.anger = u8_sat_add(m.emotions.anger, 60u);
			break;
		case TableEventKind::WonBigPot:
			s.confidence = u8_sat_add(s.confidence, u8_scale(params.conf_from_win, 100u));
			s.streak = u8_sat_add(s.streak, params.streak_step);
			m.emotions.joy = u8_sat_add(m.emotions.joy, 100u);
			break;
		default:
			break;
	}
	(void)p;
}

/// Avanza el estado un turno: el tilt se disipa, la fatiga sube, el ánimo sigue a la
/// alegría, la tensión sigue al miedo y la compostura deriva hacia su valor de carácter.
constexpr void psyche_update(PsycheState& s, const Mind& m, const Persona& p,
                             const PsycheParams& params = PsycheParams {}) noexcept {
	// Tilt: se disipa hacia 0.
	s.tilt = u8_sat_sub(s.tilt, params.tilt_decay);
	// Fatiga: sube con el tiempo de juego.
	s.fatigue = u8_sat_add(s.fatigue, params.fatigue_step);
	// Tensión: sigue al miedo y al propio tilt.
	const eng::u8 tension_t = clamp_i32(static_cast<eng::s32>(u8_scale(m.emotions.fear, 100u)) +
	                                    u8_scale(s.tilt, 60u));
	approach(s.tension, tension_t, params.step);
	// Ánimo: sigue a la alegría y la racha.
	const eng::u8 mood_t = clamp_i32(
		static_cast<eng::s32>(u8_scale(m.emotions.joy, params.mood_from_joy)) +
		u8_scale(s.streak, 60u));
	approach(s.mood, mood_t, params.step);
	// Compostura: deriva hacia la del carácter (el tilt la baja de forma transitoria).
	const eng::u8 comp_t = p.psyche.composure;
	approach(s.composure, comp_t, static_cast<eng::u8>(params.step / 2u + 1u));
}

/// Contexto de fuga derivado del estado y el carácter (lo consume `expression.hpp`).
[[nodiscard]] constexpr LeakContext leak_context(const PsycheState& s, const Persona& p) noexcept {
	LeakContext ctx {};
	ctx.composure_base = s.composure;
	ctx.tell_control = p.skills.tell_control;
	ctx.fatigue = s.fatigue;
	ctx.tilt = s.tilt;
	ctx.anxiety = s.tension;
	return ctx;
}

/// Modificador de decisión `[-100, +100]` que el estado impone a la agresión/riesgo:
/// tilt y confianza suben, miedo y fatiga bajan.
[[nodiscard]] constexpr eng::s16 psyche_aggression_mod(const PsycheState& s) noexcept {
	const eng::s16 tilt = static_cast<eng::s16>(s.tilt) - 0;
	const eng::s16 conf = static_cast<eng::s16>(s.confidence) - 50;
	const eng::s16 fatigue = static_cast<eng::s16>(s.fatigue);
	eng::s32 mod = static_cast<eng::s32>(tilt) / 2 + conf - fatigue / 3;
	if (mod > 100) {
		mod = 100;
	}
	if (mod < -100) {
		mod = -100;
	}
	return static_cast<eng::s16>(mod);
}

/// Nombre legible de un evento de mesa (diagnóstico).
[[nodiscard]] constexpr const char* table_event_name(TableEventKind e) noexcept {
	switch (e) {
		case TableEventKind::WonShowdown: return "won_showdown";
		case TableEventKind::LostShowdown: return "lost_showdown";
		case TableEventKind::WonByFold: return "won_by_fold";
		case TableEventKind::Bluffed: return "bluffed";
		case TableEventKind::CaughtBluffing: return "caught_bluffing";
		case TableEventKind::BadBeat: return "bad_beat";
		case TableEventKind::SuckedOut: return "sucked_out";
		case TableEventKind::Slowrolled: return "slowrolled";
		case TableEventKind::WasRead: return "was_read";
		case TableEventKind::ReadOpponent: return "read_opponent";
		case TableEventKind::LostBigPot: return "lost_big_pot";
		case TableEventKind::WonBigPot: return "won_big_pot";
		default: return "?";
	}
}

} // namespace eng::sim
