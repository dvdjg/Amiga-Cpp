// ============================================================================
// Test HOST-159: rumores, memoria de grupo y reputacion colectiva
// ============================================================================
//
// Valida `eng/sim/rumor.hpp` y su integracion con `Society`/`Economy` y con el mundo:
//
//   1) `contribute`: el conocimiento individual se hace colectivo con distorsion.
//   2) `GroupMemory`: saber/recordar por faccion.
//   3) `apply_group_knowledge`: la memoria colectiva mueve reputacion (enemigos/aliados) y
//      la demanda economica (fuentes de comida).
//   4) `SimWorld::diffuse_knowledge`: difunde entre correligionarios de la misma region,
//      alimenta la memoria de grupo y aplica sus efectos sociales.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/159_sim_rumors

#include <cstdio>

#include <eng/sim/economy.hpp>
#include <eng/sim/knowledge.hpp>
#include <eng/sim/rumor.hpp>
#include <eng/sim/society.hpp>
#include <eng/sim/world.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_group_memory() {
	RumorParams rp {};
	KnowledgeSet individual;
	learn(individual, KnowledgeKind::Enemy, 3u, 220u);
	learn(individual, KnowledgeKind::FoodSource, 7u, 140u);
	learn(individual, KnowledgeKind::Danger, 9u, 100u); // por debajo del umbral

	GroupMemory<kMaxFactions> memory;
	check(memory.of(2u).empty(), "grupo: arranca sin memoria");
	const eng::u8 n = contribute(memory, 2u, individual, rp);
	check(n == 2u, "grupo: solo se aporta lo que supera el umbral");
	check(confidence_for(memory.of(2u), KnowledgeKind::Enemy, 3u) == 196u,
	      "grupo: la creencia se guarda con distorsion");
	check(confidence_for(memory.of(2u), KnowledgeKind::Danger, 9u) == 0u,
	      "grupo: lo poco fiable no entra");
	check(memory.knows(2u, KnowledgeKind::Enemy, 3u),
	      "grupo: la faccion recuerda lo aprendido");

	// Dos fuentes del mismo rumor refuerzan la creencia.
	contribute(memory, 2u, individual, rp);
	check(confidence_for(memory.of(2u), KnowledgeKind::Enemy, 3u) == 255u,
	      "grupo: los rumores repetidos refuerzan");
}

void test_group_effects() {
	RumorParams rp {};
	GroupMemory<kMaxFactions> memory;
	KnowledgeSet team;
	learn(team, KnowledgeKind::Enemy, 3u, 200u);
	learn(team, KnowledgeKind::Ally, 4u, 200u);
	learn(team, KnowledgeKind::FoodSource, 1u, 200u);
	contribute(memory, 1u, team, rp);

	Society soc;
	Economy eco;
	eco.reset();
	const eng::s16 delta = apply_group_knowledge(soc, eco, 1u, memory, rp);
	check(soc.rep(3u) == -static_cast<eng::s8>(rp.enemy_penalty),
	      "grupo: saber de un enemigo baja su reputacion");
	check(soc.rep(4u) == static_cast<eng::s8>(rp.ally_bonus),
	      "grupo: saber de un aliado la sube");
	check(eco.demand_of(ItemKind::Food) == rp.food_demand,
	      "grupo: saber de comida sube la demanda");
	check(delta == static_cast<eng::s16>(rp.ally_bonus) -
			       static_cast<eng::s16>(rp.enemy_penalty),
	      "grupo: cambio neto de reputacion");
}

void test_world_diffusion() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	const EntityId a = w.spawn(1u, 2u, 0u, 0, 0);
	const EntityId b = w.spawn(1u, 2u, 0u, 1, 0);
	w.find(a)->set_realized(true);
	w.find(b)->set_realized(true);
	learn(w.find(a)->knowledge, KnowledgeKind::Enemy, 3u, 220u);
	learn(w.find(a)->knowledge, KnowledgeKind::FoodSource, 7u, 200u);

	const eng::u8 events = w.diffuse_knowledge();
	check(events > 0u, "difusion: hay eventos de rumor");
	check(confidence_for(w.find(b)->knowledge, KnowledgeKind::Enemy, 3u) >= 128u,
	      "difusion: el correligionario de la region aprende");
	check(confidence_for(w.group_memory().of(2u), KnowledgeKind::Enemy, 3u) >= 196u,
	      "difusion: la memoria de la faccion se alimenta");
	check(w.society().rep(3u) <= static_cast<eng::s8>(-30),
	      "difusion: la reputacion colectiva cambia");

	// Una faccion con memoria de comida sube la demanda del mercado.
	check(w.economy().demand_of(ItemKind::Food) > 0u,
	      "difusion: la memoria colectiva mueve la economia");
}

} // namespace

int main() {
	std::printf("Sim rumors:\n");
	test_group_memory();
	test_group_effects();
	test_world_diffusion();

	if (g_fail == 0u) {
		std::printf("OK: Sim rumors (memoria de grupo, reputacion colectiva, difusion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
