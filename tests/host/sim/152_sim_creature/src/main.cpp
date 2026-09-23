// ============================================================================
// Test HOST-152: modelo de criatura del ecosistema (`eng::sim`)
// ============================================================================
//
// Valida los componentes de una criatura y su decision por utilidad:
//
//   1) Tamano controlado por plantilla: `Needs`/`Personality`/`Relationship` ocupan
//      bytes fijos y `AbstractCreature<MaxTrackers,MaxRelations>` crece con la
//      capacidad (el presupuesto de RAM se decide en compilacion).
//   2) `Needs`: tick lineal (hambre/cansancio suben, miedo se apacigua, social sube),
//      saturacion, y acciones (feed/rest/calm/socialize/heal/hurt/set_rain).
//   3) `Personality`/`apply_mod`: modificador porcentual entero y sus recortes.
//   4) `Relationship`: alta/actualizacion, ajuste de afinidad con recorte, desalojo
//      de la relacion mas debil y busqueda de la mas intensa.
//   5) `Tracker`: observacion/refresco, olvido por `decay`, desalojo por confianza y
//      rechazo cuando lo nuevo no mejora lo que ya hay.
//   6) `Mind`: memoria episodica acotada, balance de valencia y actualizacion afectiva.
//   7) `Behavior`: puntuacion por utilidad (huir/cazar/comer/dormir/social/refugio),
//      modificadores de personalidad y afecto, y eleccion con histeresis.
//   8) `AbstractCreature`: dano/curacion, flags y refugio.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/152_sim_creature

#include <cstdio>

#include <eng/core/math/random.hpp>
#include <eng/sim/behavior.hpp>
#include <eng/sim/creature.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/needs.hpp>
#include <eng/sim/personality.hpp>
#include <eng/sim/relationship.hpp>
#include <eng/sim/tracker.hpp>
#include <eng/sim/types.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// 1) Tamano por plantilla -----------------------------------------------------
static_assert(sizeof(Needs) == 7u, "Needs debe medir 7 bytes");
static_assert(sizeof(Personality) == 10u, "Personality debe medir 10 bytes");
static_assert(sizeof(Relationship) == 6u, "Relationship debe medir 6 bytes");

void test_layout() {
	// Mas trackers/relaciones => criatura mas grande solo si la plantilla lo pide.
	constexpr eng::usize small = sizeof(AbstractCreature<2, 2>);
	constexpr eng::usize large = sizeof(AbstractCreature<10, 10>);
	check(small < large, "layout: la capacidad de plantilla cambia el tamano");
	std::printf("tamanos: Needs=%zu Personality=%zu Emotions=%zu Mind=%zu Tracker=%zu "
		    "Rel=%zu Creature<6,6>=%zu Creature<2,2>=%zu Creature<10,10>=%zu\n",
		    sizeof(Needs), sizeof(Personality), sizeof(Emotions), sizeof(Mind),
		    sizeof(Tracker), sizeof(Relationship), sizeof(AbstractCreature<>), small,
		    large);
}

// 2) Necesidades --------------------------------------------------------------
void test_needs() {
	Needs n {};
	const NeedRates rates {}; // hambre+1, cansancio+1, miedo-4, social+1, curacion 0

	tick(n, rates, false);
	check(n.hunger == 1 && n.fatigue == 1 && n.social == 1 && n.fear == 0,
	      "needs: tick despierto sube hambre/cansancio/social");

	n.fear = 10;
	tick(n, rates, false);
	check(n.fear == 6, "needs: el miedo se apacigua con el tick");

	// Saturacion: nunca desborda por arriba ni por abajo.
	n.hunger = 254;
	rise(n, Need::Hunger, 5);
	check(n.hunger == 255, "needs: rise satura en 255");
	ease(n, Need::Hunger, 10);
	check(n.hunger == 245, "needs: ease resta");
	ease(n, Need::Hunger, 255);
	check(n.hunger == 0, "needs: ease no baja de 0");

	check(n.any_critical() == false, "needs: sin criticos al inicio");
	n.injury = 255;
	check(n.any_critical(), "needs: injury 255 es critico");

	// Acciones: cada una mueve su eje en la direccion correcta.
	Needs a {};
	a.hunger = 100;
	feed(a, 30);
	check(a.hunger == 70, "needs: feed sacia");
	a.fatigue = 80;
	rest(a, 50);
	check(a.fatigue == 30, "needs: rest recupera");
	a.fear = 40;
	calm(a, 100);
	check(a.fear == 0, "needs: calm apacigua");
	a.social = 60;
	socialize(a, 20);
	check(a.social == 40, "needs: socialize sacia");
	a.injury = 50;
	heal(a, 10);
	check(a.injury == 40, "needs: heal cura");
	a.injury = 50;
	hurt(a, 250);
	check(a.injury == 255, "needs: hurt satura");
	set_rain(a, 123);
	check(a.exposure == 123 && a.hazard_kind() == HazardKind::Rain,
	      "needs: set_rain fija exposicion y tipo");
	set_exposure(a, HazardKind::Heat, 200);
	check(a.exposure == 200 && a.hazard_kind() == HazardKind::Heat,
	      "needs: set_exposure generico (calor)");
	clear_hazard(a);
	check(a.hazard_kind() == HazardKind::None, "needs: clear_hazard");
	check(shelter_for(HazardKind::Rain) == ShelterKind::Roof &&
		      shelter_for(HazardKind::Cold) == ShelterKind::Warm &&
		      shelter_for(HazardKind::Heat) == ShelterKind::Shade,
	      "needs: cada peligro sugiere su proteccion");
}

// 3) Personalidad y modificadores --------------------------------------------
void test_personality() {
	check(trait_mod(50) == 0, "trait_mod: 50 es neutro");
	check(trait_mod(100) == 100, "trait_mod: 100 da +100");
	check(trait_mod(0) == -100, "trait_mod: 0 da -100");
	check(trait_mod(75) == 50, "trait_mod: 75 da +50");

	check(apply_mod(500, 100) == 1000, "apply_mod: +100% dobla");
	check(apply_mod(500, -100) == 0, "apply_mod: -100% anula");
	check(apply_mod(200, 50) == 300, "apply_mod: +50% de 200");
	check(apply_mod(1000, 50) == 1000, "apply_mod: satura en 1000");
	check(apply_mod(0, 80) == 0, "apply_mod: base 0 no cambia");
}

// 4) Relaciones ---------------------------------------------------------------
void test_relationships() {
	RelationshipList<3> rels;
	set_relation(rels, 10u, RelationKind::Eats, 20);
	auto r = find_rel(rels, 10u);
	check(r.valid() && r->kind == RelationKind::Eats && r->affinity == 20,
	      "rel: alta inicial");

	set_relation(rels, 10u, RelationKind::Rival, -10);
	r = find_rel(rels, 10u);
	check(r.valid() && r->kind == RelationKind::Rival && r->affinity == -10,
	      "rel: set actualiza tipo y afinidad");

	adjust_affinity(rels, 10u, RelationKind::Rival, 5);
	check(affinity_toward(rels, 10u) == -5, "rel: ajuste suma");
	adjust_affinity(rels, 20u, RelationKind::Pack, 200);
	check(affinity_toward(rels, 20u) == 100, "rel: ajuste recorta a +100");

	auto strongest = strongest_rel(rels, RelationKind::Pack);
	check(strongest.valid() && strongest->target == 20u, "rel: la mas intensa del tipo");

	// Desalojo: con la lista llena se descarta la relacion de menor magnitud.
	RelationshipList<2> r2;
	set_relation(r2, 1u, RelationKind::Afraid, -5);
	set_relation(r2, 2u, RelationKind::Family, 50);
	set_relation(r2, 3u, RelationKind::Pack, 80);
	check(!find_rel(r2, 1u).valid(), "rel: desaloja la mas debil");
	check(find_rel(r2, 2u).valid() && find_rel(r2, 3u).valid(),
	      "rel: conserva las intensas");
}

// 5) Trackers -----------------------------------------------------------------
void test_trackers() {
	TrackerList<3> tr;
	observe(tr, TrackerKind::Threat, 5u, 0u, 10, 10, 100, 0u);
	observe(tr, TrackerKind::Threat, 5u, 0u, 20, 20, 50, 5u); // refresco: pos y tick
	auto t = find_tracker(tr, 5u, TrackerKind::Threat);
	check(t.valid() && t->x == 20 && t->confidence == 100 && t->last_seen == 5u,
	      "tracker: refresca posicion y conserva la confianza mayor");
	check(confidence_of(tr, 5u, TrackerKind::Threat) == 100, "tracker: consulta de confianza");

	decay(tr, 60);
	check(confidence_of(tr, 5u, TrackerKind::Threat) == 40, "tracker: decay baja confianza");
	decay(tr, 50);
	check(tr.empty() && !best_tracker(tr, TrackerKind::Threat).valid(),
	      "tracker: se olvida al llegar a 0");

	// Desalojo y rechazo: capacidad 2.
	TrackerList<2> tr2;
	observe(tr2, TrackerKind::Prey, 1u, 0u, 0, 0, 30, 0u);
	observe(tr2, TrackerKind::Prey, 2u, 0u, 0, 0, 50, 0u);
	observe(tr2, TrackerKind::Prey, 3u, 0u, 0, 0, 10, 0u); // peor que la mas debil
	check(!find_tracker(tr2, 3u, TrackerKind::Prey).valid(),
	      "tracker: rechaza lo que no mejora");
	observe(tr2, TrackerKind::Prey, 3u, 0u, 0, 0, 200, 0u); // desaloja la de 30
	check(!find_tracker(tr2, 1u, TrackerKind::Prey).valid() &&
		      find_tracker(tr2, 3u, TrackerKind::Prey).valid(),
	      "tracker: desaloja el de menor confianza");

	forget(tr2, 2u, TrackerKind::Prey);
	check(!find_tracker(tr2, 2u, TrackerKind::Prey).valid(), "tracker: forget puntual");
}

// 6) Mente --------------------------------------------------------------------
void test_mind() {
	Mind m;
	check(m.recent.empty(), "mind: sin recuerdos al inicio");

	m.remember(MemoryKind::Ate, 1u, 100);
	m.remember(MemoryKind::Played, 2u, 100);
	check(m.recent.size() == 2u, "mind: guarda recuerdos");
	check(m.recent[0].age == 1u, "mind: envejece los recuerdos previos");
	check(m.memory_balance() > 0, "mind: recuerdos positivos dan balance positivo");

	m.remember(MemoryKind::WasHurt, 3u, 200);
	m.remember(MemoryKind::Robbed, 3u, 200);
	m.remember(MemoryKind::SawEnemy, 3u, 100);
	// Capacidad 4: el recuerdo mas antiguo se descarta.
	check(m.recent.size() == kMaxMemoryEvents, "mind: memoria acotada");

	check(emotion_mod(128u) == 0, "mind: 128 es neutro");
	check(emotion_mod(255u) == 100, "mind: 255 da +100");
	check(emotion_mod(0u) == -100, "mind: 0 da -100");

	// El miedo alto empuja el eje afectivo hacia arriba.
	Mind m2;
	Needs n {};
	Personality p {};
	n.fear = 200;
	m2.update(n, p);
	check(m2.emotions.fear == 12u, "mind: el miedo sube por pasos");
	for (int i = 0; i < 30; ++i) {
		m2.update(n, p);
	}
	check(m2.emotions.fear > 190u, "mind: converge al objetivo de miedo");
	check(m2.fear_mod() > 40, "mind: el miedo da modificador positivo");
}

// 7) Decision por utilidad ----------------------------------------------------
// Escenario "amenaza cerca": con nerviosismo maximo y miedo alto, huir domina.
void test_behavior_scores() {
	AbstractCreature<> c {};
	c.personality.nervousness = 100;
	c.mind.emotions.fear = 255;
	observe(c.trackers, TrackerKind::Threat, 1u, 0u, 0, 0, 255, 0u); // encima
	c.needs.hunger = 0;

	BehaviorScores s {};
	const BehaviorContext ctx {};
	score_behaviors<SimTraits>(c, ctx, s);
	check(s[Behavior::Flee] >= 600, "behavior: huir puntua alto con amenaza+miedo");
	check(s[Behavior::Flee] > s[Behavior::Wander], "behavior: huir supera deambular");

	// Hambre extrema sin amenaza -> buscar comida domina.
	AbstractCreature<> hungry {};
	hungry.needs.hunger = 255;
	BehaviorScores sh {};
	score_behaviors<SimTraits>(hungry, ctx, sh);
	check(sh[Behavior::SeekFood] == 1000, "behavior: hambre extrema -> comer al maximo");
	check(sh[Behavior::SeekFood] > sh[Behavior::Sleep], "behavior: comer supera dormir");

	// Cansancio extremo -> dormir.
	AbstractCreature<> tired {};
	tired.needs.fatigue = 255;
	BehaviorScores st {};
	score_behaviors<SimTraits>(tired, ctx, st);
	check(st[Behavior::Sleep] >= 900, "behavior: cansancio extremo -> dormir");

	// Peligro ambiental (lluvia/frio/calor...) con refugio -> buscar abrigo.
	AbstractCreature<> wet {};
	wet.needs.exposure = 255;
	wet.needs.hazard = static_cast<eng::u8>(HazardKind::Cold);
	BehaviorContext rain_ctx {};
	rain_ctx.environment_severe = true;
	rain_ctx.has_den = true;
	BehaviorScores sr {};
	score_behaviors<SimTraits>(wet, rain_ctx, sr);
	check(sr[Behavior::SeekShelter] > sr[Behavior::Wander],
	      "behavior: con peligro ambiental manda buscar refugio");
	check(wet.needs.hazard_kind() == HazardKind::Cold && exposed(wet.needs),
	      "needs: el peligro se guarda con su tipo");

	// Social con aliado y sociabilidad alta (solo con `society`).
	AbstractCreature<> social {};
	social.needs.social = 255;
	social.personality.sociability = 100;
	observe(social.trackers, TrackerKind::Friend, 2u, 0u, 0, 0, 255, 0u);
	BehaviorScores ss {};
	score_behaviors<SimTraits>(social, ctx, ss);
	check(ss[Behavior::Socialize] >= 900, "behavior: socializar puntua alto");

	// El perfil sin sociedad desactiva socializar/atender en compilacion.
	BehaviorScores lean {};
	score_behaviors<SimTraitsLean>(social, ctx, lean);
	check(lean[Behavior::Socialize] == 0 && lean[Behavior::Tend] == 0,
	      "behavior: SimTraitsLean elimina los modulos sociales");
}

void test_behavior_choice() {
	// Mismo estado, ruido 0: el resultado es determinista.
	AbstractCreature<> c {};
	c.personality.nervousness = 100;
	c.mind.emotions.fear = 255;
	observe(c.trackers, TrackerKind::Threat, 1u, 0u, 0, 0, 255, 0u);
	BehaviorContext ctx {};
	eng::Xoroshiro64pp rng {7u, 11u};
	const Behavior b = choose_behavior<SimTraits>(c, ctx, rng, 120, 0u);
	check(b == Behavior::Flee, "choose: con amenaza+miedo elige huir");

	// Histeresis: huir (382) supera a deambular (300) por menos del margen (120), asi
	// que desde `Wander` NO cambia; desde `Idle` (190) si, porque 382 >= 190 + 120.
	AbstractCreature<> m {};
	m.personality.nervousness = 75;
	observe(m.trackers, TrackerKind::Threat, 1u, 0u, 0, 0, 255, 0u); // encima
	m.behavior = Behavior::Wander;
	m.behavior_score = 300;
	eng::Xoroshiro64pp rng2 {7u, 11u};
	const Behavior b2 = choose_behavior<SimTraits>(m, ctx, rng2, 120, 0u);
	check(b2 == Behavior::Wander, "choose: histeresis evita el cambio marginal");

	// El mismo estado con el comportamiento actual bajo si cambia.
	m.behavior = Behavior::Idle;
	m.behavior_score = 0;
	eng::Xoroshiro64pp rng3 {7u, 11u};
	const Behavior b3 = choose_behavior<SimTraits>(m, ctx, rng3, 120, 0u);
	check(b3 == Behavior::Flee, "choose: desde idle si cambia a huir");
}

// 8) Criatura -----------------------------------------------------------------
void test_creature() {
	AbstractCreature<> c {};
	check(c.alive() && !c.realized(), "creature: nace viva y abstracta");

	c.take_damage(30);
	check(c.health == 70 && c.needs.injury == 30, "creature: el dano resta salud y suma herida");
	check((c.flags & flags::injured) != 0u, "creature: marca herida");

	c.restore(50);
	check(c.health == 120 && (c.flags & flags::injured) == 0u,
	      "creature: curar limpia el flag de herida");

	c.set_den(2u, 5, 6);
	c.room = 2u;
	check(c.at_home() && c.den_room == 2u, "creature: en el refugio");

	c.set_realized(true);
	check(c.realized(), "creature: se puede realizar");

	c.take_damage(255);
	check(!c.alive() && c.health == 0 && !c.realized(),
	      "creature: el dano letal mata y desrealiza");
}

} // namespace

int main() {
	std::printf("Sim creature:\n");
	test_layout();
	test_needs();
	test_personality();
	test_relationships();
	test_trackers();
	test_mind();
	test_behavior_scores();
	test_behavior_choice();
	test_creature();

	if (g_fail == 0u) {
		std::printf("OK: Sim creature (needs, personalidad, relaciones, trackers, mente, "
			    "utilidad)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
