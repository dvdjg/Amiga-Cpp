// ============================================================================
// Test HOST-174: jugador simulado y LOD (mundo rico cerca, barato lejos)
// ============================================================================
//
// Valida:
//   - `eng/sim/lod.hpp`: bandas de detalle por distancia y sonda de "cuesta CPU".
//   - `SimWorld::update_lod`: realized cerca / abstract a media / **dormant** lejos; el
//     dormido no consume (no avanza en el tick abstracto).
//   - `eng/sim/avatar.hpp`: intencion del jugador segun necesidades, movimiento/accion y
//     resumen de lo que percibe y de la carga.
//   - Aforo por region (capacidad del bioma).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/174_sim_player_lod

#include <cstdio>

#include <eng/sim/avatar.hpp>
#include <eng/sim/season.hpp>
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

using World = SimWorld<SimTraits, 32, 4, 4, 8>;

void test_lod() {
	check(band_for(2u, true) == LodBand::Realized && band_for(20u, true) == LodBand::Abstract &&
		      band_for(40u, true) == LodBand::Dormant,
	      "lod: bandas por distancia");
	check(lod_costs_cpu(LodBand::Realized) && lod_costs_cpu(LodBand::Abstract) &&
		      !lod_costs_cpu(LodBand::Dormant),
	      "lod: lo dormido no cuesta");

	World w;
	for (eng::u8 r = 0; r < 8; ++r) {
		w.link_rooms(r, static_cast<RoomId>(r + 1u));
	}
	w.set_biome(0u, BiomeKind::Plains);
	const EntityId avatar = w.spawn(9u, 0u, 0u, 5, 5);
	(void)w.spawn(1u, 0u, 0u, 7, 5);  // cerca
	(void)w.spawn(1u, 0u, 0u, 18, 5); // media distancia
	const EntityId far = w.spawn(1u, 0u, 0u, 60, 5);
	LodParams lp {};
	lp.realize_radius = 6u;
	lp.abstract_radius = 15u;
	w.set_lod_params(lp);
	w.update_lod(w.find(avatar)->x, w.find(avatar)->y, w.find(avatar)->room);
	check(w.realized_count() >= 1u && w.abstract_count() >= 1u && w.dormant_count() >= 1u,
	      "lod: update_lod reparte realized/abstract/dormant");
	check(w.find(far)->dormant(), "lod: lo lejano queda dormido");

	// El dormido no consume CPU: no avanza en el tick abstracto.
	const eng::u8 before = w.find(far)->needs.hunger;
	eng::Xoroshiro64pp rng {7u, 9u};
	for (int i = 0; i < 32; ++i) {
		w.tick_abstract(rng);
	}
	check(w.find(far)->needs.hunger == before, "lod: el dormido no se simula");

	// El jugador se acerca: lo lejano pasa a realized.
	w.find(avatar)->x = 58;
	w.find(avatar)->y = 5;
	w.update_lod(w.find(avatar)->x, w.find(avatar)->y, w.find(avatar)->room);
	check(w.find(far)->realized(), "lod: al acercarse, la criatura se realiza");
}

void test_capacity() {
	World w;
	w.set_biome(0u, BiomeKind::Desert); // aforo 5
	check(w.region_capacity(0u) == 5u, "aforo: capacidad del bioma");
	while (w.region_has_space(0u)) {
		if (w.spawn(1u, 0u, 0u, 0, 0) == no_entity) {
			break;
		}
	}
	check(w.region_population(0u) == 5u && !w.region_has_space(0u),
	      "aforo: la region se llena y deja de aceptar");
	check(w.region_capacity(1u) == biome_capacity(BiomeKind::Plains),
	      "aforo: cada region usa su bioma");
}

void test_player() {
	World w;
	for (eng::u8 r = 0; r < 8; ++r) {
		w.link_rooms(r, static_cast<RoomId>(r + 1u));
	}
	w.set_biome(0u, BiomeKind::Forest); // comida 80
	const EntityId avatar = w.spawn(9u, 0u, 0u, 5, 5);
	const EntityId ally = w.spawn(1u, 0u, 0u, 8, 5);

	// Intencion segun necesidades.
	Needs hungry {};
	hungry.hunger = 200u;
	check(intent_for(hungry) == PlayerIntent::Feed, "jugador: con hambre busca comida");
	Needs cold {};
	cold.exposure = 220u;
	check(intent_for(cold) == PlayerIntent::Shelter, "jugador: expuesto busca refugio");
	Needs tired {};
	tired.fatigue = 220u;
	check(intent_for(tired) == PlayerIntent::Rest, "jugador: cansado descansa");

	// Accion: come y baja el hambre.
	w.find(avatar)->needs.hunger = 200u;
	eng::Xoroshiro64pp rng {3u, 4u};
	const PlayerIntent it = player_step(w, avatar, rng);
	check(it == PlayerIntent::Feed && w.find(avatar)->needs.hunger < 200u,
	      "jugador: come en region con comida");

	// Percepcion y resumen de carga.
	LodParams lp {};
	w.set_lod_params(lp);
	w.update_lod(w.find(avatar)->x, w.find(avatar)->y, w.find(avatar)->room);
	SenseTarget targets[8];
	for (eng::usize i = 0; i < w.creature_count(); ++i) {
		const World::Creature& c = w.creature(i);
		targets[i] = SenseTarget {c.id, c.room, c.x, c.y, 40u, 40u, 20u, 0u, 0u, true,
					  TrackerKind::Friend};
	}
	Observation obs[8];
	const eng::u8 seen = w.sense(avatar, eng::Span<const SenseTarget> {targets, w.creature_count()},
				     eng::Span<Observation> {obs, 8});
	w.integrate_senses(avatar, eng::Span<const Observation> {obs, seen});
	const PlayerView view = player_view(w, avatar, seen);
	check(view.observed > 0u, "jugador: percibe el mundo a su alrededor");
	check(view.realized + view.abstract + view.dormant == static_cast<eng::u8>(w.creature_count()),
	      "jugador: el resumen cubre todas las criaturas");
	check(confidence_of(w.find(avatar)->trackers, ally, TrackerKind::Friend) > 0u,
	      "jugador: registra a quien ve");
}

void test_lod_wake() {
	World w;
	for (eng::u8 r = 0; r < 8; ++r) {
		w.link_rooms(r, static_cast<RoomId>(r + 1u));
	}
	w.set_biome(0u, BiomeKind::Plains);
	const EntityId a = w.spawn(1u, 0u, 0u, 3, 5);
	const EntityId b = w.spawn(1u, 0u, 0u, 6, 5);
	const EntityId far = w.spawn(1u, 0u, 0u, 60, 5);
	LodParams lp {};
	lp.realize_radius = 10u;
	lp.abstract_radius = 20u;
	lp.wake_per_frame = 1u;
	lp.wake_step = 80u;
	w.set_lod_params(lp);

	// Presupuesto de despertar: solo una criatura se realiza por frame.
	w.update_lod(0, 5, 0u);
	check(w.realized_count() == 1u, "lod: wake_per_frame limita las transiciones");
	w.update_lod(0, 5, 0u);
	check(w.realized_count() == 2u, "lod: el resto se realiza en el frame siguiente");

	// Despertar gradual: la criatura no decide hasta completar la transición.
	check(!w.find(a)->lod_ready(), "lod: recien realizada no esta lista");
	eng::Xoroshiro64pp rng {1u, 2u};
	for (int i = 0; i < 4; ++i) {
		w.tick_realized(rng);
	}
	check(w.find(a)->lod_ready() && w.find(b)->lod_ready(),
	      "lod: la transicion se completa sin pop");
	check(w.find(far)->dormant(), "lod: lo lejano sigue dormido");
}

void test_dynamic_capacity() {
	World w;
	w.set_biome(0u, BiomeKind::Plains); // base 12
	check(w.region_capacity(0u) == 12u, "aforo: en verano, capacidad base");

	w.set_season(Season::Winter);
	check(w.region_capacity(0u) == 8u, "aforo: en invierno sostiene menos");

	w.climate().set(0u, HazardKind::Storm, 200u);
	check(w.region_capacity(0u) == 4u, "aforo: con tormenta extrema, la mitad");

	w.climate().set(0u, HazardKind::None, 0u);
	w.set_season(Season::Spring);
	check(w.region_capacity(0u) == 13u, "aforo: en primavera sostiene mas");
	check(w.region_base_capacity(0u) == 12u, "aforo: la base del bioma no cambia");
}

} // namespace

int main() {
	std::printf("Sim player lod:\n");
	test_lod();
	test_capacity();
	test_player();
	test_lod_wake();
	test_dynamic_capacity();

	if (g_fail == 0u) {
		std::printf("OK: Sim player lod (bandas, wake, aforo dinamico, avatar, percepcion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
