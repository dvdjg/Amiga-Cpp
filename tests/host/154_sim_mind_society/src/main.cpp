// ============================================================================
// Test HOST-154: mente ampliada, conocimiento, jerarquia, genetica y colonia
// ============================================================================
//
// Valida la segunda capa de `eng::sim`:
//
//   1) `knowledge.hpp`: aprender/refinar, confianza, olvido, desalojo y transmision.
//   2) `mind.hpp`: doce emociones; los recuerdos negativos alimentan ira/tristeza/odio y
//      los positivos amor/compasion/cordialidad; `attitude_toward` por actor.
//   3) `hierarchy.hpp`: poder, sumision y rebeldia con reglas parametricas (autonomia,
//      deferencia, miedo).
//   4) `genetics.hpp`: herencia con sesgo de dominancia, mutacion, expresion a
//      personalidad y clasificacion de castas.
//   5) `colony.hpp`: censo de castas, reparto de roles y estigmergia sobre
//      `eng::ai::InfluenceMap` (depositar/seguir/decair feromonas).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/154_sim_mind_society

#include <cstdio>

#include <eng/core/random.hpp>
#include <eng/sim/colony.hpp>
#include <eng/sim/genetics.hpp>
#include <eng/sim/hierarchy.hpp>
#include <eng/sim/knowledge.hpp>
#include <eng/sim/lifecycle.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/relationship.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// 1) Conocimiento -------------------------------------------------------------
void test_knowledge() {
	KnowledgeSet ks;
	check(ks.empty() && top_confidence(ks) == 0u, "knowledge: arranca vacio");

	// Refuerzo saturante.
	learn(ks, KnowledgeKind::FoodSource, 3u, 100u);
	learn(ks, KnowledgeKind::FoodSource, 3u, 100u);
	learn(ks, KnowledgeKind::FoodSource, 3u, 100u);
	check(confidence_for(ks, KnowledgeKind::FoodSource, 3u) == 255u,
	      "knowledge: el refuerzo satura en 255");
	check(knows(ks, KnowledgeKind::FoodSource, 3u), "knowledge: se sabe con confianza");
	auto best = best_knowledge(ks, KnowledgeKind::FoodSource);
	check(best.valid() && best->subject == 3u, "knowledge: mejor creencia del tipo");

	// Olvido a largo plazo.
	decay_knowledge(ks, 155u);
	check(confidence_for(ks, KnowledgeKind::FoodSource, 3u) == 100u, "knowledge: decay resta");
	decay_knowledge(ks, 200u);
	check(ks.empty(), "knowledge: se olvida del todo");

	// Desalojo de la creencia mas debil al llenarse (capacidad 8).
	KnowledgeSet full;
	for (eng::u16 i = 1u; i <= 8u; ++i) {
		learn(full, KnowledgeKind::Danger, i, static_cast<eng::u8>(i * 25u));
	}
	check(full.size() == kMaxKnowledge, "knowledge: capacidad llena");
	learn(full, KnowledgeKind::Danger, 9u, 225u); // supera a la mas debil (25)
	check(!find_knowledge(full, KnowledgeKind::Danger, 1u).valid() &&
		      find_knowledge(full, KnowledgeKind::Danger, 9u).valid(),
	      "knowledge: desaloja la mas debil");

	// Transmision (el emisor ensena; el receptor recibe mermado).
	KnowledgeSet teacher;
	KnowledgeSet student;
	learn(teacher, KnowledgeKind::Prey, 7u, 200u);
	const eng::u8 shared = share(teacher, student);
	check(shared == 1u && confidence_for(student, KnowledgeKind::Prey, 7u) == 176u,
	      "knowledge: share transmite con perdida");
}

// 2) Mente ampliada -----------------------------------------------------------
void test_mind() {
	static_assert(emotion_count == 12u, "deben ser 12 emociones");
	check(emotion_mod(128u) == 0, "mind: 128 es neutro");
	check(emotion_mod(255u) == 100, "mind: 255 da +100");
	check(emotion_mod(0u) == -100, "mind: 0 da -100");

	// Recuerdos negativos -> ira, tristeza, odio; poco amor.
	Mind resentful;
	Needs n {};
	Personality p {};
	resentful.remember(MemoryKind::WasBetrayed, 5u, 200u);
	resentful.remember(MemoryKind::WasSubjugated, 5u, 200u);
	check(resentful.attitude_toward(5u) < 0, "mind: actitud negativa hacia el agresor");
	for (int i = 0; i < 24; ++i) {
		resentful.update(n, p);
	}
	check(resentful.emotions.anger > 30u && resentful.emotions.sadness > 30u &&
		      resentful.emotions.hatred > 30u,
	      "mind: lo negativo alimenta ira/tristeza/odio");
	check(resentful.emotions.love < 60u, "mind: lo negativo enfría el amor");
	check(resentful.stress > 0u, "mind: el malestar genera estres");

	// Recuerdos positivos -> amor, compasion, cordialidad.
	Mind grateful;
	grateful.remember(MemoryKind::WasHelped, 6u, 200u);
	grateful.remember(MemoryKind::WasTaught, 6u, 200u);
	check(grateful.attitude_toward(6u) > 0, "mind: actitud positiva hacia el aliado");
	for (int i = 0; i < 24; ++i) {
		grateful.update(n, p);
	}
	check(grateful.emotions.love > 60u && grateful.emotions.compassion > 100u &&
		      grateful.emotions.cordiality > 100u,
	      "mind: lo positivo alimenta amor/compasion/cordialidad");
}

// 3) Jerarquia y libertad -----------------------------------------------------
void test_hierarchy() {
	const HierarchyParams hp {};
	const eng::u8 weak = contest_power(20u, 20u, 50u, hp);
	const eng::u8 strong = contest_power(90u, 90u, 90u, hp);
	check(strong > weak, "jerarquia: mas dominancia/fuerza/estado -> mas poder");
	check(rank_band(strong) == 4u && rank_band(10u) == 0u, "jerarquia: bandas de poder");

	Personality neutral {};
	Mind mind {};
	check(should_submit(weak, strong, neutral, mind),
	      "jerarquia: el debil se somete al fuerte");
	check(!should_submit(strong, weak, neutral, mind),
	      "jerarquia: el fuerte no se somete al debil");

	// Alta autonomia + poca deferencia -> se resiste aunque sea mas debil.
	Personality rebel {};
	rebel.autonomy = 100;
	Mind defiant {};
	defiant.deference = 0;
	check(!should_submit(weak, strong, rebel, defiant),
	      "jerarquia: la autonomia encarece someterse");
	check(defiance_score(strong, weak, rebel, defiant) >
		      submission_score(weak, strong, neutral, mind),
	      "jerarquia: el fuerte rebelde resiste mas de lo que el debil se somete");
}

// 4) Genetica -----------------------------------------------------------------
void test_genetics() {
	Genome a {};
	Genome b {};
	a.set(Gene::Aggression, 90u);
	b.set(Gene::Aggression, 10u);
	eng::Xoroshiro64pp rng {11u, 13u};

	// Sesgo de dominancia 100% hacia A, sin mutacion.
	GeneticsParams all_a {};
	all_a.dominance_bias = 100u;
	all_a.mutation_rate = 0u;
	check(inherit(a, b, rng, all_a).gene(Gene::Aggression) == 90u,
	      "genetica: dominancia del padre A");

	GeneticsParams all_b {};
	all_b.dominance_bias = 0u;
	all_b.mutation_rate = 0u;
	check(inherit(a, b, rng, all_b).gene(Gene::Aggression) == 10u,
	      "genetica: dominancia del padre B");

	check(mutate_gene(50u, rng, 0u) == 50u, "genetica: span 0 no muta");
	const eng::u8 mutated = mutate_gene(50u, rng, 40u);
	check(mutated <= 90u, "genetica: la mutacion se recorta");

	// Expresion a personalidad.
	Genome g {};
	g.set(Gene::Aggression, 80u);
	g.set(Gene::Sociability, 90u);
	g.set(Gene::Dominance, 70u);
	const Personality p = genome_to_personality(g);
	check(p.aggression == 80u && p.sociability == 90u && p.dominance == 70u,
	      "genetica: el genoma se expresa en rasgos");

	// Castas por umbrales.
	Genome queen {};
	queen.set(Gene::Size, 90u);
	check(caste_of(queen) == Caste::Queen, "genetica: tamano grande -> reina");
	Genome soldier {};
	soldier.set(Gene::Aggression, 80u);
	check(caste_of(soldier) == Caste::Soldier, "genetica: agresiva -> soldado");
	Genome scout {};
	scout.set(Gene::Speed, 80u);
	check(caste_of(scout) == Caste::Scout, "genetica: veloz -> exploradora");
	Genome nurse {};
	nurse.set(Gene::Empathy, 80u);
	check(caste_of(nurse) == Caste::Nurse, "genetica: empatica -> nodriza");
	Genome drone {};
	drone.set(Gene::Size, 30u);
	check(caste_of(drone) == Caste::Drone, "genetica: pequena -> zangano");
	check(caste_of(Genome {}) == Caste::Worker, "genetica: por defecto -> obrera");
	check(caste_works(Caste::Worker) && !caste_reproduces(Caste::Worker),
	      "genetica: la obrera trabaja y no se reproduce");
}

// 5) Colonia y feromonas ------------------------------------------------------
void test_colony() {
	Colony col;
	check(col.population() == 0u, "colonia: vacia");
	check(col.needed_caste() == Caste::Worker, "colonia: sin miembros pide obrera");

	for (int i = 0; i < 8; ++i) {
		col.note_birth(Caste::Worker);
	}
	check(col.population() == 8u, "colonia: censo");
	check(col.needed_caste() == Caste::Soldier,
	      "colonia: con exceso de obreras pide soldado");
	col.note_death(Caste::Worker);
	check(col.population() == 7u, "colonia: baja de censo");

	check(caste_role_behavior(Caste::Worker) == Behavior::Forage &&
		      caste_role_behavior(Caste::Soldier) == Behavior::Hunt &&
		      caste_role_behavior(Caste::Scout) == Behavior::Wander,
	      "colonia: cada casta prefiere su rol");

	// Estigmergia reutilizando el mapa de influencia.
	eng::ai::InfluenceMap<8, 8> ph {};
	const ColonyParams cp {};
	check(follow_pheromone(ph, cp) == decltype(ph)::no_cell,
	      "feromonas: mapa vacio -> sin rastro");
	lay_pheromone(ph, 10u, cp);
	check(ph.at(10u) == cp.pheromone_deposit, "feromonas: deposito");
	check(follow_pheromone(ph, cp) == 10u, "feromonas: se sigue el rastro fuerte");
	decay_pheromone(ph, cp);
	check(ph.at(10u) == static_cast<eng::u8>(cp.pheromone_deposit - cp.pheromone_decay),
	      "feromonas: decaen");
	reinforce_pheromone(ph, 10u, 50u);
	check(ph.at(10u) ==
		      static_cast<eng::u8>(cp.pheromone_deposit - cp.pheromone_decay + 50u),
	      "feromonas: se refuerzan al reencontrarlas");
}

// 6) Afecto dirigido ----------------------------------------------------------
void test_directed_affect() {
	RelationshipList<4> rels;
	set_relationship(rels, 7u, RelationKind::Mate, 40, 80);
	check(affinity_toward(rels, 7u) == 40 && affect_toward(rels, 7u) == 80,
	      "afecto dirigido: vinculo y carga emocional");
	check(bond_score(rels, 7u) == 120, "afecto dirigido: bond = vinculo + afecto");

	adjust_affect(rels, 7u, RelationKind::Mate, -100);
	check(affect_toward(rels, 7u) == -20, "afecto dirigido: el agravio cambia el afecto");

	set_relationship(rels, 8u, RelationKind::Family, 90, 70);
	auto loved = most_loved(rels);
	check(loved.valid() && loved->target == 8u, "afecto dirigido: aliado mas querido");

	adjust_affect(rels, 9u, RelationKind::Rival, -80);
	auto hated = most_hated(rels);
	check(hated.valid() && hated->target == 9u, "afecto dirigido: rival mas odiado");
	check(bond_score(rels, 9u) == -80, "afecto dirigido: bond negativo hacia el rival");
}

// 7) Ciclo de vida ------------------------------------------------------------
void test_lifecycle() {
	const LifecycleParams lp {};
	check(stage_for(0u, lp) == LifeStage::Infant &&
		      stage_for(60u, lp) == LifeStage::Juvenile &&
		      stage_for(120u, lp) == LifeStage::Adult &&
		      stage_for(230u, lp) == LifeStage::Elder,
	      "lifecycle: etapas por edad");
	check(is_mature(120u, lp) && !is_mature(60u, lp) && !is_mature(230u, lp),
	      "lifecycle: solo el adulto es maduro");
	check(can_reproduce(120u, 100u, 0u, 0u, lp), "lifecycle: adulto sano se reproduce");
	check(!can_reproduce(120u, 30u, 0u, 0u, lp), "lifecycle: herido no se reproduce");
	check(!can_reproduce(120u, 100u, 200u, 0u, lp), "lifecycle: hambriento no se reproduce");

	eng::u8 age = 254u;
	grow(age);
	check(age == 255u, "lifecycle: la edad satura");
	grow(age);
	check(age == 255u, "lifecycle: no desborda");
	check(died_of_old_age(255u, lp), "lifecycle: muerte natural");

	Genome a {};
	Genome b {};
	a.set(Gene::Aggression, 90u);
	b.set(Gene::Aggression, 10u);
	eng::Xoroshiro64pp rng {5u, 6u};
	GeneticsParams gp {};
	gp.dominance_bias = 100u;
	gp.mutation_rate = 0u;
	check(newborn_genome(a, b, rng, gp).gene(Gene::Aggression) == 90u,
	      "lifecycle: la cria hereda con dominancia");

	Genome g {};
	check(caste_of(bias_for_caste(g, Caste::Soldier)) == Caste::Soldier,
	      "lifecycle: la puesta se sesga a soldado");
	check(caste_of(bias_for_caste(g, Caste::Queen)) == Caste::Queen,
	      "lifecycle: la puesta se sesga a reina");
}

} // namespace

int main() {
	std::printf("Sim mind/society:\n");
	test_knowledge();
	test_mind();
	test_hierarchy();
	test_genetics();
	test_colony();
	test_directed_affect();
	test_lifecycle();

	if (g_fail == 0u) {
		std::printf("OK: Sim mind/society (conocimiento, emociones, jerarquia, genetica, "
			    "colonia, afecto dirigido, ciclo de vida)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
