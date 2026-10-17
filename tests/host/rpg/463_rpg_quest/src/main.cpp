// ============================================================================
// Test HOST-463: narrativa -- quests declarativas sobre hechos (`eng::rpg/quest.hpp`)
// ============================================================================
//
// Valida el hito M5: condiciones/acciones como DATOS, avance por stages con cascada en el
// mismo tick, quests que comparten un hecho (dos "NPCs" reaccionan sin conocerse), fast-out
// por epoch (con `quest_invalidate` para cambios que no tocan hechos), consumo defensivo de
// inventario (política `Items`) y las consultas de journal.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/463_rpg_quest

#include <cstdio>

#include <eng/rpg/quest.hpp>

namespace {

using namespace eng::rpg;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Hechos del contenido de prueba.
enum : eng::u16 {
	kWolfKills = 0u,
	kHuntDone = 1u,
	kTradeTrace = 2u,
	kTradeDone = 3u,
	kRelic = 4u,
	kVictory = 5u,
};

/// Inventario de prueba para la política `Items` (pelts = tipo 2; consume_ok puede fallar).
struct TestItems {
	eng::u16 pelts = 0u;
	bool consume_ok = true;
	[[nodiscard]] eng::u16 count(eng::u16 type, eng::u16 /*tags*/) const noexcept {
		return type == 2u ? pelts : 0u;
	}
	bool consume(eng::u16 type, eng::u16 /*tags*/, eng::u8 qty) noexcept {
		if (!consume_ok || type != 2u || pelts < qty) {
			return false;
		}
		pelts = static_cast<eng::u16>(pelts - qty);
		return true;
	}
};

/// Libro de contenido del test: tres quests cruzadas en hechos.
struct Book {
	eng::util::Array<QuestCond, 4> conds {};
	eng::util::Array<QuestAct, 5> acts {};
	eng::util::Array<QuestStage, 4> stages {};
	eng::util::Array<QuestDef, 3> quests {};
	QuestBook book {};

	Book() {
		// c0: lobos >= 3 | c1: caza hecha | c2: 2 pieles | c3: reliquia.
		conds[0] = QuestCond {static_cast<eng::u8>(CondKind::FactGe), kWolfKills, 3u, 0u};
		conds[1] = QuestCond {static_cast<eng::u8>(CondKind::FactGe), kHuntDone, 1u, 0u};
		conds[2] = QuestCond {static_cast<eng::u8>(CondKind::ItemGe), 2u, 2u, 0u};
		conds[3] = QuestCond {static_cast<eng::u8>(CondKind::FactGe), kRelic, 1u, 0u};
		// a0: hunt_done=1 | a1: trade_trace=1 | a2: consumir 2 pieles | a3: trade_done=1
		// a4: victory=1.
		acts[0] = QuestAct {static_cast<eng::u8>(ActKind::SetFact), kHuntDone, 1u, 0u};
		acts[1] = QuestAct {static_cast<eng::u8>(ActKind::SetFact), kTradeTrace, 1u, 0u};
		acts[2] = QuestAct {static_cast<eng::u8>(ActKind::ConsumeItem), 2u, 2u, 0u};
		acts[3] = QuestAct {static_cast<eng::u8>(ActKind::SetFact), kTradeDone, 1u, 0u};
		acts[4] = QuestAct {static_cast<eng::u8>(ActKind::SetFact), kVictory, 1u, 0u};
		// q0 (caza): stage0 (c0 -> a0, cierra). q1 (comercio): stage0 (c1 -> a1) y
		// stage1 (c2 -> a2, a3, cierra). q2 (reliquia): stage0 (c3 -> a4, cierra).
		stages[0] = QuestStage {0u, 1u, 0u, 1u, true};
		stages[1] = QuestStage {1u, 1u, 1u, 1u, false};
		stages[2] = QuestStage {2u, 1u, 2u, 2u, true};
		stages[3] = QuestStage {3u, 1u, 4u, 1u, true};
		quests[0] = QuestDef {0u, 1u};
		quests[1] = QuestDef {1u, 2u};
		quests[2] = QuestDef {3u, 1u};
		book.quests = quests.span();
		book.stages = stages.span();
		book.conds = conds.span();
		book.acts = acts.span();
	}
};

void test_shared_fact_cascade() {
	Book content;
	FactStore<8> facts;
	QuestRuntime<3> rt {};
	TestItems items;

	quest_open(content.book, rt, 0u); // caza
	quest_open(content.book, rt, 1u); // comercio
	check(quest_state(rt, 0u) == QuestState::Active && quest_state(rt, 2u) == QuestState::Inactive,
	      "quests: abiertas 0/1, la 2 sigue inactiva");

	check(quest_tick(facts, content.book, rt, items) == 0u, "quests: sin condiciones cumplidas");
	(void)facts.add(kWolfKills, 1u);
	check(quest_tick(facts, content.book, rt, items) == 0u, "quests: 1 lobo no basta");
	(void)facts.add(kWolfKills, 2u);
	// Un solo cambio (el tercer lobo) completa la caza Y avanza el comercio a su stage 1:
	// las dos quests comparten el hecho `hunt_done` sin llamarse entre sí.
	check(quest_tick(facts, content.book, rt, items) == 2u,
	      "quests: cascada en el mismo tick (caza cerrada + comercio en stage 1)");
	check(quest_state(rt, 0u) == QuestState::Done, "quests: la caza queda Done");
	check(facts.has(kHuntDone), "quests: el hecho compartido escrito por la accion");
	check(quest_state(rt, 1u) == QuestState::Active &&
		  quest_stage(content.book, rt, 1u) == 1u &&
		  quest_stage_count(content.book, 1u) == 2u,
	      "quests: comercio en stage 1/2 (journal)");

	// Fast-out por epoch: cambiar SOLO el inventario no reevalúa hasta invalidar.
	items.pelts = 2u;
	check(quest_tick(facts, content.book, rt, items) == 0u && !facts.has(kTradeDone),
	      "quests: sin hechos nuevos no reevalua (fast-out por epoch)");
	quest_invalidate(rt);
	check(quest_tick(facts, content.book, rt, items) == 1u, "quests: invalidar reevalúa");
	check(items.pelts == 0u, "quests: la accion consume las pieles");
	check(facts.has(kTradeDone) && quest_state(rt, 1u) == QuestState::Done,
	      "quests: comercio cerrado");

	// Reliquia: abrir, escribir el hecho y cerrar.
	quest_open(content.book, rt, 2u);
	(void)facts.set_flag(kRelic);
	check(quest_tick(facts, content.book, rt, items) == 1u, "quests: quest de reliquia avanza");
	check(facts.has(kVictory) && quest_state(rt, 2u) == QuestState::Done,
	      "quests: victoria escrita y quest Done");

	// `force` reevalúa aunque el epoch coincida (no avanza nada aquí).
	check(quest_tick(facts, content.book, rt, items, /*force=*/true) == 0u,
	      "quests: force reevalúa sin cambios (nada que avanzar)");
}

void test_consume_defense() {
	Book content;
	FactStore<8> facts;
	QuestRuntime<3> rt {};
	TestItems items;
	items.pelts = 2u;
	items.consume_ok = false; // el inventario "miente": consume falla

	(void)facts.set_flag(kHuntDone);
	quest_open(content.book, rt, 1u);
	check(quest_tick(facts, content.book, rt, items) == 1u,
	      "defensa: el stage 0 avanza (hecho de caza)");
	check(!facts.has(kTradeDone) && rt.stage[1] == 2u, "defensa: en stage 1");
	check(quest_tick(facts, content.book, rt, items, true) == 0u,
	      "defensa: consume falla y el stage no avanza");
	check(!facts.has(kTradeDone) && quest_state(rt, 1u) == QuestState::Active,
	      "defensa: estado consistente tras el fallo");
}

} // namespace

int main() {
	std::printf("RPG quest (declarativas sobre hechos):\n");
	test_shared_fact_cascade();
	test_consume_defense();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG quest (QuestBook/quest_tick/cascada/fast-out)\n");
	return 0;
}
