// ============================================================================
// Test HOST-204: integracion persona <-> cartas (eng::cards/ai/persona_bot.hpp)
// ============================================================================
//
// Valida que la persona sesga los parametros del bot, que el estado (tilt) los ajusta y
// que la emision/lectura de tells conecta con `eng::sim` (expression/read).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/204_cards_persona

#include <cstdio>

#include <eng/core/random.hpp>

#include <eng/cards/ai/persona_bot.hpp>
#include <eng/cards/core/types.hpp>

namespace {

using namespace eng::cards;
using namespace eng::sim;
using eng::u8;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_params_from_persona() {
	eng::Xoroshiro64pp rng {1u, 2u};
	const Persona pardillo = make_persona(Archetype::Pardillo, rng, 0u);
	const Persona embustero = make_persona(Archetype::Embustero, rng, 0u);
	const Persona engreido = make_persona(Archetype::Engreido, rng, 0u);
	const Persona timorato = make_persona(Archetype::Timorato, rng, 0u);

	PsycheState neutral {};
	const BotParams p_pardillo = params_from_persona(pardillo, neutral);
	const BotParams p_embustero = params_from_persona(embustero, neutral);
	const BotParams p_engreido = params_from_persona(engreido, neutral);
	const BotParams p_timorato = params_from_persona(timorato, neutral);

	check(p_embustero.bluff_permille > p_pardillo.bluff_permille,
	      "persona: el embustero farolea mas que el pardillo");
	check(p_engreido.aggression_permille > p_timorato.aggression_permille,
	      "persona: el engreido es mas agresivo que el timorato");
	check(p_timorato.call_margin_permille > p_engreido.call_margin_permille,
	      "persona: el timorato exige mas colchon (mas cauto)");
}

void test_state_modulates() {
	eng::Xoroshiro64pp rng {3u, 4u};
	const Persona p = make_persona(Archetype::Metodico, rng, 0u);
	PsycheState calm {};
	calm.tilt = 0u;
	calm.confidence = 50u;
	PsycheState tilted = calm;
	tilted.tilt = 200u;
	const BotParams calm_p = params_from_persona(p, calm);
	const BotParams tilt_p = params_from_persona(p, tilted);
	check(tilt_p.aggression_permille > calm_p.aggression_permille,
	      "estado: el tilt sube la agresion");
	check(tilt_p.bluff_permille >= calm_p.bluff_permille, "estado: el tilt sube el farol");
}

void test_emit_tells() {
	eng::Xoroshiro64pp rng {5u, 6u};
	const Persona pardillo = make_persona(Archetype::Pardillo, rng, 0u);
	const Persona flematico = make_persona(Archetype::Flematico, rng, 0u);

	Mind excited;
	excited.emotions.fear = 220u;
	excited.emotions.joy = 220u;
	PsycheState s {};
	s.composure = 50u;
	s.tension = 120u;

	const LeakList pardillo_leaks = emit_tells(excited, s, pardillo);
	const LeakList flematico_leaks = emit_tells(excited, s, flematico);
	check(pardillo_leaks.size() > 0u, "tells: el pardillo se delata");
	check(flematico_leaks.size() <= pardillo_leaks.size(),
	      "tells: el flematico se delata menos");
}

void test_read_showdown() {
	eng::Xoroshiro64pp rng {7u, 8u};
	const Persona pardillo = make_persona(Archetype::Pardillo, rng, 0u);
	Mind excited;
	excited.emotions.fear = 230u;
	PsycheState s {};
	s.composure = 40u;
	s.tension = 150u;
	const LeakList leaked = emit_tells(excited, s, pardillo);

	ReadModel<4, kPokerGestureCount> model;
	model.reset();
	// El pardillo tiene mano fuerte cada vez que se delata: el observador lo aprende.
	for (u8 i = 0u; i < 12u; ++i) {
		read_showdown(model, 0u, true, leaked);
	}
	check(model.hands_seen[0] == 12u, "lectura: cuenta los showdowns");

	// Encuentra el gesto mas informativo y comprueba el indicio.
	eng::s16 best = 0;
	for (u8 g = 0u; g < kPokerGestureCount; ++g) {
		const eng::s16 ind = tell_indicio(model, 0u, g);
		const eng::s16 mag = ind < 0 ? static_cast<eng::s16>(-ind) : ind;
		if (mag > best) {
			best = mag;
		}
	}
	check(best > 40, "lectura: el observador aprende el tell del pardillo");
	check(classify_tells(model, 0u) == TellStyle::Readable,
	      "lectura: clasifica al pardillo como legible");
}

void test_decide_with_persona() {
	// Dos personas distintas juegan distinto ante la misma mesa.
	eng::Xoroshiro64pp rng {9u, 10u};
	Table t;
	eng::Xoroshiro64pp hand_rng {11u, 12u};
	start_hand(t, hand_rng, 3u, 1000, 5, 10, 0u);
	const Persona engreido = make_persona(Archetype::Engreido, rng, 0u);
	const Persona timorato = make_persona(Archetype::Timorato, rng, 0u);
	PsycheState neutral {};
	const CardPlan plan = card_profile_plan(CardProfile::N20);

	const u8 actor = t.to_act;
	eng::Xoroshiro64pp rng_a {13u, 14u};
	eng::Xoroshiro64pp rng_b {13u, 14u};
	const Action a = decide_with_persona(t, actor, engreido, neutral, plan, BotStyle::Balanced,
	                                     nullptr, rng_a);
	const Action b = decide_with_persona(t, actor, timorato, neutral, plan, BotStyle::Balanced,
	                                     nullptr, rng_b);
	// Las acciones son legales y el flujo no se rompe.
	Action legal[12] {};
	const u8 n = legal_actions(t, legal, 12u);
	bool a_legal = false;
	bool b_legal = false;
	for (u8 i = 0u; i < n; ++i) {
		if (legal[i].type == a.type) {
			a_legal = true;
		}
		if (legal[i].type == b.type) {
			b_legal = true;
		}
	}
	check(a_legal && b_legal, "decision: ambas acciones son legales");
}

void test_range_with_tells() {
	eng::Xoroshiro64pp rng {21u, 22u};
	eng::Xoroshiro64pp table_rng {31u, 32u};
	PreflopTable table;
	build_preflop_table(table, table_rng, 24u);

	Table t;
	eng::Xoroshiro64pp hand_rng {41u, 42u};
	start_hand(t, hand_rng, 3u, 1000, 5, 10, 0u);
	const u8 hero = 0u;

	OpponentModel model;
	for (u8 i = 0u; i < 10u; ++i) {
		model.observe(1u, ActionType::Call, Street::Preflop);
		model.observe(2u, ActionType::Call, Street::Preflop);
	}
	model.reset_street(Street::Flop);

	// Sin tells: rango base.
	ReadModel<4, kPokerGestureCount> empty_reads;
	empty_reads.reset();
	HandRange base;
	opponent_range_with_tells(model, empty_reads, t, hero, &table, base);

	// Con un tell que apunta a mano fuerte (el rival se delata con mano fuerte), el
	// rango debe estrecharse (menos clases).
	const Persona pardillo = make_persona(Archetype::Pardillo, rng, 0u);
	Mind excited;
	excited.emotions.fear = 230u;
	excited.emotions.anger = 120u;
	PsycheState s {};
	s.composure = 40u;
	s.tension = 150u;
	const LeakList leaked = emit_tells(excited, s, pardillo);

	ReadModel<4, kPokerGestureCount> reads;
	reads.reset();
	for (u8 i = 0u; i < 12u; ++i) {
		read_showdown(reads, 1u, true, leaked);
		read_showdown(reads, 2u, true, leaked);
	}
	HandRange with_tell;
	opponent_range_with_tells(model, reads, t, hero, &table, with_tell);

	check(with_tell.class_count() > 0u, "rango+tells: rango no vacio");
	check(with_tell.class_count() <= base.class_count(),
	      "rango+tells: un tell de fuerza estrecha el rango");
}

} // namespace

int main() {
	std::printf("eng::cards persona:\n");
	test_params_from_persona();
	test_state_modulates();
	test_emit_tells();
	test_read_showdown();
	test_decide_with_persona();
	test_range_with_tells();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards persona (parametros por arquetipo, tells y lectura)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
