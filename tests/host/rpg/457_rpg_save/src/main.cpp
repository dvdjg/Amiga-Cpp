// ============================================================================
// Test HOST-457: serializacion del nucleo RPG (`eng::rpg/save.hpp`)
// ============================================================================
//
// Valida `engine/include/eng/rpg/save.hpp`:
//
//   1) Round-trip completo: hechos + instancias (con huecos de generacion) + inventario +
//      equipo + atributos base + reloj, con los handles del inventario/equipo intactos
//      tras cargar (slot y generacion exactos).
//   2) Determinismo: dos saves del mismo estado son byte a byte iguales; un save tras
//      load_all reproduce el original (el formato no guarda epoch ni cachés).
//   3) Robustez: version distinta, stream truncado, stream vacio y handles colgantes se
//      rechazan con `false` (comparado con un stream manual bien formado que si carga).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/457_rpg_save

#include <cstdio>

#include <eng/rpg/save.hpp>

namespace {

using namespace eng::rpg;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

bool bytes_equal(const eng::u8* a, const eng::u8* b, eng::usize count) {
	for (eng::usize i = 0; i < count; ++i) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

/// Estado de partida completo: dos tipos (espada/pocima), un hueco de generacion reciclado,
/// bolsa con dos pilas, espada equipada, atributos base y reloj avanzado.
struct GameState {
	ItemCatalog<4> catalog {};
	FactStore<16> facts {};
	ItemStore<8> store {};
	ItemBag<4> bag {};
	Equipment<2> equipment {};
	StatSheet<3> stats {};
	WorldClock clock {};

	ItemHandle sword {};
	ItemHandle potion {};
	ItemHandle doomed {}; ///< pocima destruida cuyo hueco recicla `potion` (generacion nueva)

	GameState() {
		ItemDef sword_def {};
		sword_def.tags = item_tags::weapon;
		sword_def.weight = 3u;
		sword_def.durability_max = 100u;
		const ItemTypeId sword_type = catalog.add(sword_def);
		ItemDef potion_def {};
		potion_def.tags = item_tags::food;
		potion_def.stack_max = 5u;
		const ItemTypeId potion_type = catalog.add(potion_def);

		(void)facts.set(2u, 42u);
		(void)facts.set(7u, 1u);
		(void)facts.add(9u, 5u);

		ItemInstance sword_inst {};
		sword_inst.type = sword_type;
		sword_inst.qty = 1u;
		sword_inst.durability = 100u;
		sword = store.spawn(sword_inst);

		ItemInstance first_potion {};
		first_potion.type = potion_type;
		first_potion.qty = 3u;
		doomed = store.spawn(first_potion);
		(void)store.destroy(doomed); // deja un hueco con generacion usada

		ItemInstance second_potion {};
		second_potion.type = potion_type;
		second_potion.qty = 2u;
		potion = store.spawn(second_potion); // recicla el hueco (generacion nueva)

		(void)bag_add(bag, store, catalog, sword);
		(void)bag_add(bag, store, catalog, potion);

		equipment.required_tags[0] = item_tags::weapon;
		ItemHandle replaced {};
		(void)equip_item(equipment, store, catalog, 0u, sword, replaced);

		stats.base[0] = 11;
		stats.base[1] = -7;
		stats.base[2] = 300;

		advance_minutes(clock, 6u * 60u + 25u); // 14:25
		advance_days(clock, 2u);                // dia 3
	}
};

void test_round_trip() {
	GameState game;
	check(game.potion.slot == game.doomed.slot && game.potion.generation != game.doomed.generation,
	      "estado: el hueco se reciclo con generacion nueva");

	eng::u8 buffer[512] = {};
	eng::util::ByteWriter writer {eng::Span<eng::u8> {buffer, 512u}};
	check(save_all(game.facts, game.store, game.bag, game.equipment, game.stats, game.clock,
		       writer),
	      "save: guarda el estado completo");
	const eng::usize size = writer.position();
	check(size > 0u && size < 512u, "save: tamano razonable");

	eng::u8 buffer2[512] = {};
	eng::util::ByteWriter writer2 {eng::Span<eng::u8> {buffer2, 512u}};
	check(save_all(game.facts, game.store, game.bag, game.equipment, game.stats, game.clock,
		       writer2),
	      "save: segundo guardado");
	check(writer2.position() == size && bytes_equal(buffer, buffer2, size),
	      "save: determinista (byte a byte)");

	// Estado mutado: la carga debe reponerlo todo.
	(void)game.facts.set(2u, 999u);
	(void)game.facts.clear(7u);
	(void)game.store.destroy(game.sword);
	(void)game.store.destroy(game.potion);
	game.bag.items.clear();
	game.equipment.slots[0] = ItemHandle {};
	game.stats.base[0] = -1;
	game.clock.day = 1u;
	game.clock.hour = 0u;
	game.clock.minute = 0u;

	eng::util::ByteReader reader {eng::Span<const eng::u8> {buffer, size}};
	check(load_all(game.facts, game.store, game.bag, game.equipment, game.stats, game.clock,
		       reader),
	      "load: carga la partida");

	check(game.facts.value(2u) == 42u && game.facts.has(7u) && game.facts.value(9u) == 5u,
	      "load: hechos restaurados");
	check(game.facts.present_count() == 3u && !game.facts.any_changed(),
	      "load: hechos sin cambios pendientes");
	check(game.store.live_count() == 2u, "load: instancias restauradas");
	check(game.store.get(game.sword).valid() && game.store.get(game.sword)->durability == 100u,
	      "load: espada viva con su estado");
	check(game.store.get(game.potion).valid() && game.store.get(game.potion)->qty == 2u,
	      "load: pocima restaurada");
	check(!game.store.get(game.doomed).valid(), "load: handle destruido sigue caducado");
	check(game.bag.size() == 2u && game.bag.contains(game.sword) &&
		  game.bag.contains(game.potion),
	      "load: bolsa restaurada con los mismos handles");
	check(game.equipment.slots[0] == game.sword, "load: equipo restaurado");
	check(game.stats.base[0] == 11 && game.stats.base[1] == -7 && game.stats.base[2] == 300,
	      "load: atributos base restaurados");
	check(!game.stats.derived_valid, "load: derivada pendiente de recalcular");
	check(game.clock.day == 3u && game.clock.hour == 14u && game.clock.minute == 25u,
	      "load: reloj restaurado");

	eng::u8 buffer3[512] = {};
	eng::util::ByteWriter writer3 {eng::Span<eng::u8> {buffer3, 512u}};
	check(save_all(game.facts, game.store, game.bag, game.equipment, game.stats, game.clock,
		       writer3),
	      "save: re-guarda tras load");
	check(writer3.position() == size && bytes_equal(buffer, buffer3, size),
	      "save: byte-identico tras load (formato canonico)");

	// Version distinta.
	eng::u8 bad_version[512] = {};
	for (eng::usize i = 0; i < size; ++i) {
		bad_version[i] = buffer[i];
	}
	bad_version[0] = 99u;
	eng::util::ByteReader bad_reader {eng::Span<const eng::u8> {bad_version, size}};
	check(!load_all(game.facts, game.store, game.bag, game.equipment, game.stats, game.clock,
			bad_reader),
	      "load: version distinta rechazada");

	// Stream truncado y vacio.
	eng::util::ByteReader cut_reader {eng::Span<const eng::u8> {buffer, size - 1u}};
	check(!load_all(game.facts, game.store, game.bag, game.equipment, game.stats, game.clock,
			cut_reader),
	      "load: stream truncado rechazado");
	eng::util::ByteReader empty_reader {eng::Span<const eng::u8> {buffer, 0u}};
	check(!load_all(game.facts, game.store, game.bag, game.equipment, game.stats, game.clock,
			empty_reader),
	      "load: stream vacio rechazado");
}

/// Construye a mano un save completo minimo: 0 hechos, 1 instancia en el hueco 0
/// (generacion 1) y una bolsa que la referencia con el slot indicado. Compartido por el
/// control positivo (slot 0, existe) y el negativo (slot 3, hueco colgante).
bool build_min_save(eng::u8* buffer, eng::usize capacity, eng::u8 bag_slot,
		    eng::usize& out_size) {
	eng::util::ByteWriter w {eng::Span<eng::u8> {buffer, capacity}};
	bool ok = w.write_u8(kRpgSaveVersion);
	ok = ok && w.write_u16(0u);                          // hechos: ninguno
	ok = ok && w.write_u8(1u);                           // instancias: una
	ok = ok && w.write_u8(0u) && w.write_u8(1u);         // slot 0, generacion 1
	ok = ok && w.write_u8(0u) && w.write_u8(1u);         // tipo 0, cantidad 1
	ok = ok && w.write_u8(0u) && w.write_u8(0u);         // durabilidad, mejora
	ok = ok && w.write_u16(0u) && w.write_u16(0u);       // afijos, metadatos
	ok = ok && w.write_u8(1u);                           // bolsa: una entrada
	ok = ok && w.write_u8(bag_slot) && w.write_u8(1u);   // handle de la entrada de bolsa
	ok = ok && w.write_u8(0xffu) && w.write_u8(0u);      // equipo ranura 0 vacia
	ok = ok && w.write_u8(0xffu) && w.write_u8(0u);      // equipo ranura 1 vacia
	ok = ok && w.write_s16(0) && w.write_s16(0) && w.write_s16(0); // atributos base
	ok = ok && w.write_u16(1u) && w.write_u8(0u) && w.write_u8(0u); // reloj
	out_size = w.position();
	return ok;
}

void test_referential_integrity() {
	// Control positivo: el handle de la bolsa apunta a la instancia viva -> carga.
	eng::u8 buffer[64] = {};
	eng::usize size = 0u;
	check(build_min_save(buffer, 64u, 0u, size), "stream manual: bien formado");

	FactStore<4> facts {};
	ItemStore<4> store {};
	ItemBag<2> bag {};
	Equipment<2> equipment {};
	StatSheet<3> stats {};
	WorldClock clock {};
	eng::util::ByteReader good {eng::Span<const eng::u8> {buffer, size}};
	check(load_all(facts, store, bag, equipment, stats, clock, good),
	      "stream manual: carga (control positivo)");
	check(store.live_count() == 1u && bag.size() == 1u, "stream manual: estado coherente");

	// Mismo stream con el handle de la bolsa apuntando a un hueco inexistente: rechazo.
	eng::u8 dangling[64] = {};
	eng::usize dangling_size = 0u;
	check(build_min_save(dangling, 64u, 3u, dangling_size) && dangling_size == size,
	      "stream colgante: mismo tamano que el bueno");
	eng::util::ByteReader bad {eng::Span<const eng::u8> {dangling, dangling_size}};
	FactStore<4> facts2 {};
	ItemStore<4> store2 {};
	ItemBag<2> bag2 {};
	Equipment<2> equipment2 {};
	StatSheet<3> stats2 {};
	WorldClock clock2 {};
	check(!load_all(facts2, store2, bag2, equipment2, stats2, clock2, bad),
	      "stream manual: handle colgante rechazado");
}

} // namespace

int main() {
	std::printf("RPG save (serializacion por componente):\n");
	test_round_trip();
	test_referential_integrity();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG save (round-trip, determinismo, robustez)\n");
	return 0;
}
