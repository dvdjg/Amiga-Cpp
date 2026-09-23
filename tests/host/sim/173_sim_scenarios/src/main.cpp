// ============================================================================
// Test HOST-173: laboratorio de escenarios del ecosistema
// ============================================================================
//
// No es un test de aserciones duras: es una **simulacion larga** de varios escenarios
// iniciales que imprime un **digesto** de la evolucion (poblacion, nacimientos y muertes
// por causa, medias de necesidades y histograma de conductas). Sirve para que la IA (o un
// humano) observe el sistema a lo largo del tiempo y ajuste parametros hasta que se
// comporte como un mundo vivo.
//
// El test comprueba invariantes suaves (no se corrompe, hay natalidad/mortalidad y la
// poblacion no explota) para poder correr en regresion; el valor esta en el digesto.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/173_sim_scenarios

#include <cstdio>

#include <eng/sim/culture.hpp>
#include <eng/sim/world.hpp>

namespace {

using namespace eng::sim;

static constexpr eng::u16 kW = 40;
static constexpr eng::u8 kRooms = 6;
static constexpr eng::u16 kTicks = 4000;
static constexpr eng::u16 kDigestEvery = 500;
static constexpr eng::u8 kCapacity = 64;

using World = SimWorld<SimTraits, kCapacity, 6, 6, kRooms>;

struct Counters {
	eng::u16 births = 0;
	eng::u16 deaths_hunger = 0;
	eng::u16 deaths_age = 0;
	eng::u16 deaths_predation = 0;
	eng::u16 kills = 0;
	eng::u16 ritual_acts = 0;
	eng::u16 signals = 0;
};

struct Lab {
	Counters ct;
	bool prev_alive[kCapacity] {};
	bool started = false;
	eng::u16 killed[kCapacity] {};
	eng::u8 killed_count = 0;
};

struct Scenario {
	const char* name;
	eng::u8 prey;
	eng::u8 predators;
	eng::u8 social;
	eng::u8 food_bias;    ///< probabilidad/abundancia de alimento (0..255)
	eng::u8 chase_chance; ///< probabilidad de perseguir por tick (0..255)
};

[[nodiscard]] bool is_predator(SpeciesId s) { return s == 2u; }
[[nodiscard]] bool is_prey(SpeciesId s) { return s == 1u; }
[[nodiscard]] bool is_social(SpeciesId s) { return s == 3u; }

void classify_target(SenseTarget& t, SpeciesId observer, SpeciesId target) {	if (observer == target || (is_social(observer) && is_social(target))) {
		t.kind = TrackerKind::Friend;
	} else if (is_predator(observer) && is_prey(target)) {
		t.kind = TrackerKind::Prey;
	} else if (is_prey(observer) && is_predator(target)) {
		t.kind = TrackerKind::Threat;
	} else {
		t.kind = TrackerKind::Friend;
	}
}

/// Cortejo: los adultos sin pareja que se encuentran forman un **vínculo Mate**, de modo
/// que la reproduccion continua aunque muera la generacion inicial (clave para un mundo
/// vivo sostenible). Se hace un numero acotado de emparejamientos por tick.
void form_pairs(World& w, const Scenario& sc) {
	(void)sc;
	eng::u8 formed = 0u;
	for (eng::usize i = 0; i < w.creature_count() && formed < 3u; ++i) {
		World::Creature& a = w.creature(i);
		if (!a.alive() || !is_mature(a.age, w.lifecycle()) ||
		    strongest_rel(a.relationships, RelationKind::Mate).valid()) {
			continue;
		}
		for (eng::usize j = i + 1u; j < w.creature_count(); ++j) {
			World::Creature& b = w.creature(j);
			if (!b.alive() || b.species != a.species || b.room != a.room ||
			    !is_mature(b.age, w.lifecycle()) ||
			    strongest_rel(b.relationships, RelationKind::Mate).valid()) {
				continue;
			}
			set_relationship(a.relationships, b.id, RelationKind::Mate, 70, 75);
			set_relationship(b.relationships, a.id, RelationKind::Mate, 70, 75);
			++formed;
			break;
		}
	}
}

void step(World& w, eng::Xoroshiro64pp& rng, const Scenario& sc, Lab& lab) {
	for (eng::u8 r = 0; r < kRooms; ++r) {
		w.realize_room(r, kCapacity);
	}

	// 1) Percepcion: cada criatura clasifica a las demas y aprende.
	SenseTarget targets[kCapacity];
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		const World::Creature& t = w.creature(i);
		targets[i] = SenseTarget {t.id, t.room, t.x, t.y,
					  static_cast<eng::u8>(30u + t.health / 2u),
					  static_cast<eng::u8>(t.behavior == Behavior::Flee ? 200u : 40u),
					  static_cast<eng::u8>(20u + t.needs.injury),
					  0u, 0u, true, TrackerKind::Noise};
	}
	Observation obs[16];
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		World::Creature& c = w.creature(i);
		if (!c.alive()) {
			continue;
		}
		for (eng::usize j = 0; j < w.creature_count(); ++j) {
			classify_target(targets[j], c.species, w.creature(j).species);
		}
		const eng::u8 n = w.sense(c.id, eng::Span<const SenseTarget> {targets, w.creature_count()},
					  eng::Span<Observation> {obs, 16});
		w.integrate_senses(c.id, eng::Span<const Observation> {obs, n});
	}

	// 2) Decision y necesidades.
	w.tick_realized(rng);
	w.tick_memory();

	// 3) Caza: el depredador con presa cerca la mata y come.
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		World::Creature& p = w.creature(i);
		if (!p.alive() || !is_predator(p.species)) {
			continue;
		}
		auto prey = best_attention_tracker(p.trackers, TrackerKind::Prey);
		if (!prey.valid()) {
			continue;
		}
		auto v = w.find(prey->target);
		if (!v.valid() || !v->alive() || v->room != p.room) {
			continue;
		}
		if (manhattan(p.x, p.y, v->x, v->y) <= 2u) {
			v->flags = static_cast<eng::u8>(v->flags & static_cast<eng::u8>(~flags::alive));
			v->set_realized(false);
			if (lab.killed_count < kCapacity) {
				lab.killed[lab.killed_count++] = v->id;
			}
			feed(p.needs, 140u);
			p.mind.remember(MemoryKind::Ate, v->id, 200u);
			++lab.ct.kills;
		} else if (eng::chance(rng, sc.chase_chance, 255u)) {
			p.x = static_cast<eng::s16>(p.x + (v->x > p.x ? 1 : (v->x < p.x ? -1 : 0)));
			p.y = static_cast<eng::s16>(p.y + (v->y > p.y ? 1 : (v->y < p.y ? -1 : 0)));
		}
	}

	// 4) Comer del bioma (los depredadores NO: dependen de la caza) y deambular.
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		World::Creature& c = w.creature(i);
		if (!c.alive()) {
			continue;
		}
		if (!is_predator(c.species) && c.needs.hunger > 25u &&
		    eng::chance(rng, sc.food_bias, 255u)) {
			feed(c.needs, static_cast<eng::u8>(20u + sc.food_bias / 6u));
			c.mind.remember(MemoryKind::Ate, no_entity, 60u);
		}
		if (eng::chance(rng, 90u, 255u)) {
			c.x = static_cast<eng::s16>(c.x + static_cast<eng::s16>(rng.next_mod(3u)) - 1);
			c.y = static_cast<eng::s16>(c.y + static_cast<eng::s16>(rng.next_mod(3u)) - 1);
			if (c.x < 0) c.x = static_cast<eng::s16>(kW - 1u);
			if (c.y < 0) c.y = static_cast<eng::s16>(kW - 1u);
			c.x = static_cast<eng::s16>(c.x % static_cast<eng::s16>(kW));
			c.y = static_cast<eng::s16>(c.y % static_cast<eng::s16>(kW));
		}
	}

	// 5) Vida social y cultura.
	(void)w.coordinate_packs();
	lab.ct.signals = static_cast<eng::u16>(lab.ct.signals + w.broadcast_signals());
	w.diffuse_knowledge();
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		World::Creature& c = w.creature(i);
		if (!c.alive() || !c.realized()) {
			continue;
		}
		if (c.behavior == Behavior::Socialize && knows_ritual(c.knowledge, RitualKind::Greeting)) {
			(void)w.enact_ritual(c.id, RitualKind::Greeting);
			++lab.ct.ritual_acts;
		}
	}

	// 6) Cortejo, reproduccion con gestacion.
	form_pairs(w, sc);
	if (w.try_reproduce(rng) != no_entity) {
		++lab.ct.births;
	}
	lab.ct.births = static_cast<eng::u16>(lab.ct.births + w.advance_reproduction(rng));

	// 7) Contabilidad de muertes por causa.
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		const World::Creature& c = w.creature(i);
		const bool now = c.alive();
		if (lab.started && i < kCapacity && lab.prev_alive[i] && !now) {
			bool by_pred = false;
			for (eng::u8 k = 0; k < lab.killed_count; ++k) {
				if (lab.killed[k] == c.id) {
					by_pred = true;
				}
			}
			if (by_pred) {
				++lab.ct.deaths_predation;
			} else if (died_of_old_age(c.age, w.lifecycle())) {
				++lab.ct.deaths_age;
			} else {
				++lab.ct.deaths_hunger;
			}
		}
		if (i < kCapacity) {
			lab.prev_alive[i] = now;
		}
	}
	lab.killed_count = 0u;
	lab.started = true;
}

void digest(const char* name, eng::u16 tick, World& w, const Counters& ct) {
	eng::u16 alive = 0, prey = 0, pred = 0, social = 0;
	eng::u32 hunger = 0, fear = 0, injury = 0;
	eng::u16 behavior[static_cast<eng::u16>(Behavior::Count)] {};
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		const World::Creature& c = w.creature(i);
		if (!c.alive()) {
			continue;
		}
		++alive;
		if (is_prey(c.species)) ++prey;
		else if (is_predator(c.species)) ++pred;
		else if (is_social(c.species)) ++social;
		hunger += c.needs.hunger;
		fear += c.needs.fear;
		injury += c.needs.injury;
		++behavior[static_cast<eng::u16>(c.behavior)];
	}
	const eng::u16 a = alive == 0u ? 1u : alive;
	std::printf("[%s] t=%u alive=%u (prey=%u pred=%u soc=%u) births=%u kills=%u deaths "
		    "H=%u A=%u P=%u | hunger=%u fear=%u inj=%u ritual=%u sig=%u\n",
		    name, tick, alive, prey, pred, social, ct.births, ct.kills, ct.deaths_hunger,
		    ct.deaths_age, ct.deaths_predation, static_cast<unsigned>(hunger / a),
		    static_cast<unsigned>(fear / a), static_cast<unsigned>(injury / a),
		    ct.ritual_acts, ct.signals);
	std::printf("        B: idle=%u wander=%u hunt=%u flee=%u food=%u sleep=%u social=%u "
		    "shelter=%u tend=%u help=%u court=%u submit=%u defy=%u teach=%u forage=%u "
		    "avenge=%u\n",
		    behavior[static_cast<eng::u16>(Behavior::Idle)],
		    behavior[static_cast<eng::u16>(Behavior::Wander)],
		    behavior[static_cast<eng::u16>(Behavior::Hunt)],
		    behavior[static_cast<eng::u16>(Behavior::Flee)],
		    behavior[static_cast<eng::u16>(Behavior::SeekFood)],
		    behavior[static_cast<eng::u16>(Behavior::Sleep)],
		    behavior[static_cast<eng::u16>(Behavior::Socialize)],
		    behavior[static_cast<eng::u16>(Behavior::SeekShelter)],
		    behavior[static_cast<eng::u16>(Behavior::Tend)],
		    behavior[static_cast<eng::u16>(Behavior::Help)],
		    behavior[static_cast<eng::u16>(Behavior::Court)],
		    behavior[static_cast<eng::u16>(Behavior::Submit)],
		    behavior[static_cast<eng::u16>(Behavior::Defy)],
		    behavior[static_cast<eng::u16>(Behavior::Teach)],
		    behavior[static_cast<eng::u16>(Behavior::Forage)],
		    behavior[static_cast<eng::u16>(Behavior::Avenge)]);
}

[[nodiscard]] eng::u16 alive_count(const World& w) {
	eng::u16 a = 0;
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		if (w.creature(i).alive()) {
			++a;
		}
	}
	return a;
}

eng::u16 max_population = 0u;
eng::u32 total_births = 0u;
eng::u32 total_deaths = 0u;
eng::u16 min_alive_end = 0xffffu;

void run_scenario(const Scenario& sc, eng::u32 seed) {
	World w;
	eng::Xoroshiro64pp rng {seed, seed + 1u};
	for (eng::u8 r = 0; r < kRooms; ++r) {
		w.set_biome(r, static_cast<BiomeKind>(r % static_cast<eng::u8>(BiomeKind::Count)));
		w.link_rooms(r, static_cast<RoomId>(r + 1u));
	}
	// Ciclo de vida "rapido" para ver nacimientos y muertes dentro de la simulacion.
	LifecycleParams life {};
	life.juvenile_age = 20u;
	life.adult_age = 40u;
	life.elder_age = 200u;
	life.death_age = 255u;
	life.maturation_period = 10u;
	life.repro_cooldown = 120u;
	life.gestation_ticks = 30u;
	life.repro_min_health = 30u;
	life.repro_max_hunger = 150u;
	w.set_lifecycle(life);

	// Poblacion inicial, con parejas (relacion Mate) para que haya reproduccion.
	EntityId prey_ids[kCapacity] {};
	EntityId pred_ids[kCapacity] {};
	eng::u8 np = 0, nd = 0;
	for (eng::u8 i = 0; i < sc.prey; ++i) {
		const EntityId id = w.spawn(1u, 0u, static_cast<RoomId>(i % kRooms),
					    static_cast<eng::s16>(i * 2u), 5);
		if (id != no_entity) {
			prey_ids[np++] = id;
		}
	}
	for (eng::u8 i = 0; i < sc.predators; ++i) {
		const EntityId id = w.spawn(2u, 1u, static_cast<RoomId>(i % kRooms),
					    static_cast<eng::s16>(i * 2u), 20);
		if (id != no_entity) {
			pred_ids[nd++] = id;
		}
	}
	for (eng::u8 i = 0; i < sc.social; ++i) {
		const EntityId id = w.spawn(3u, 0u, static_cast<RoomId>(i % kRooms),
					    static_cast<eng::s16>(i * 2u), 35);
		if (id != no_entity) {
			learn_ritual(w.find(id)->knowledge, RitualKind::Greeting, 255u);
			learn_ritual(w.find(id)->knowledge, RitualKind::Mourning, 200u);
		}
	}
	for (eng::u8 i = 0; i + 1u < np; i = static_cast<eng::u8>(i + 2u)) {
		set_relationship(w.find(prey_ids[i])->relationships, prey_ids[i + 1u],
				 RelationKind::Mate, 80, 80);
		set_relationship(w.find(prey_ids[i + 1u])->relationships, prey_ids[i],
				 RelationKind::Mate, 80, 80);
	}
	for (eng::u8 i = 0; i + 1u < nd; i = static_cast<eng::u8>(i + 2u)) {
		set_relationship(w.find(pred_ids[i])->relationships, pred_ids[i + 1u],
				 RelationKind::Mate, 80, 80);
		set_relationship(w.find(pred_ids[i + 1u])->relationships, pred_ids[i],
				 RelationKind::Mate, 80, 80);
	}
	// Variacion genetica.
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		w.creature(i).genome = Genome::random(rng);
		w.creature(i).personality = genome_to_personality(w.creature(i).genome);
		w.creature(i).senses = senses_from_genome(w.creature(i).genome);
		// Sentidos "de laboratorio": suficiente alcance para que haya encuentros.
		w.creature(i).senses.vision_range = 34u;
		w.creature(i).senses.hearing_range = 30u;
		w.creature(i).senses.smell_range = 20u;
		w.creature(i).senses.vision_arc = 170u;
		if (is_predator(w.creature(i).species)) {
			w.creature(i).personality.aggression = 85u;
			w.creature(i).personality.bravery = 80u;
		}
		w.creature(i).set_realized(true);
	}

	std::printf("=== scenario %s (seed=%u) ===\n", sc.name, static_cast<unsigned>(seed));
	Lab lab {};
	for (eng::u16 t = 1; t <= kTicks; ++t) {
		step(w, rng, sc, lab);
		const eng::u16 a = alive_count(w);
		if (a > max_population) {
			max_population = a;
		}
		if (t % kDigestEvery == 0u) {
			digest(sc.name, t, w, lab.ct);
		}
	}
		std::printf("\n");
	total_births += lab.ct.births;
	total_deaths += static_cast<eng::u32>(lab.ct.deaths_hunger) + lab.ct.deaths_age +
			lab.ct.deaths_predation;
	const eng::u16 end_alive = alive_count(w);
	if (end_alive < min_alive_end) {
		min_alive_end = end_alive;
	}
}

} // namespace

int main() {
	std::printf("Sim scenarios (laboratorio)\n");
	run_scenario(Scenario {"abundante", 20u, 2u, 6u, 220u, 30u}, 1001u);
	run_scenario(Scenario {"escaso", 20u, 2u, 6u, 30u, 40u}, 1002u);
	run_scenario(Scenario {"depredadores", 20u, 8u, 6u, 180u, 90u}, 1003u);
	run_scenario(Scenario {"manada", 24u, 6u, 12u, 170u, 70u}, 1004u);
	std::printf("poblacion maxima observada en un escenario: %u\n", max_population);
	std::printf("totales: nacimientos=%u muertes=%u poblacion minima final=%u\n",
		    static_cast<unsigned>(total_births), static_cast<unsigned>(total_deaths),
		    min_alive_end);
	// Invariantes suaves de un "mundo vivo": hubo poblacion, natalidad y mortalidad.
	const bool ok = max_population > 0u && total_births > 0u && total_deaths > 0u &&
			max_population <= kCapacity;
	if (ok) {
		std::printf("OK: Sim scenarios (mundo viable: natalidad y mortalidad observadas)\n");
		return 0;
	}
	std::printf("FALLOS: el ecosistema no mostro dinamica vital\n");
	return 1;
}
