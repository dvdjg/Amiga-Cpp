// ============================================================================
// Test HOST-454: nucleo de mundo RPG (`eng::rpg`) -- hechos y reloj de mundo
// ============================================================================
//
// Valida `engine/include/eng/rpg/{types,facts,clock}.hpp`:
//
//   1) `FactStore`: semantica de "cambio efectivo" (epoch y bit changed solo suben cuando
//      el valor cambia de verdad), saturacion de `add`, consultas fuera de rango seguras,
//      `present_count`, `for_each_present` en orden ascendente y `clear` total.
//   2) `FactSet`: hechos pegados a una entidad (pertenencia, sin duplicados, capacidad).
//   3) `WorldClock`: acarreo minuto -> hora -> dia con division nativa (`divmod32`/`divu_w`),
//      `advance_hours`/`advance_days`, saturacion del dia, `set_time` con recorte y
//      `minutes_of_day`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/454_rpg_kernel

#include <cstdio>

#include <eng/rpg/clock.hpp>
#include <eng/rpg/facts.hpp>

namespace {

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_fact_store() {
	eng::rpg::FactStore<8> facts;
	check(facts.present_count() == 0u, "hechos: almacen vacio al inicio");
	check(!facts.has(3u), "hechos: ausente al inicio");
	check(facts.value(3u) == 0u, "hechos: valor 0 si ausente");
	check(facts.value_or(3u, 7u) == 7u, "hechos: value_or usa respaldo si ausente");
	check(facts.epoch() == 0u, "hechos: epoch inicial 0");

	check(facts.set(3u, 7u), "hechos: set valido");
	check(facts.has(3u) && facts.value(3u) == 7u, "hechos: set guarda valor");
	check(facts.epoch() == 1u, "hechos: set efectivo sube epoch");
	check(facts.changed(3u), "hechos: bit changed tras set");
	check(facts.any_changed(), "hechos: any_changed tras set");

	check(facts.set(3u, 7u), "hechos: re-set del mismo valor");
	check(facts.epoch() == 1u, "hechos: mismo valor no sube epoch");

	check(facts.set(3u, 8u), "hechos: set de valor nuevo");
	check(facts.epoch() == 2u, "hechos: valor nuevo sube epoch");
	check(facts.set_flag(3u) && facts.value(3u) == 1u, "hechos: set_flag");

	check(facts.clear(3u), "hechos: clear de presente");
	check(!facts.has(3u) && facts.value(3u) == 0u, "hechos: tras clear queda ausente");
	check(facts.epoch() == 4u, "hechos: clear efectivo sube epoch");
	check(facts.clear(3u), "hechos: clear de ausente");
	check(facts.epoch() == 4u, "hechos: clear de ausente no sube epoch");

	check(facts.add(5u, 10u), "hechos: add sobre ausente");
	check(facts.has(5u) && facts.value(5u) == 10u, "hechos: add crea el valor");
	const eng::u32 after_add = facts.epoch();
	check(facts.add(5u, 0xffffu), "hechos: add saturado");
	check(facts.value(5u) == 0xffffu, "hechos: add satura al maximo u16");
	check(facts.epoch() == after_add + 1u, "hechos: saturacion efectiva sube epoch");
	check(facts.add(5u, 3u), "hechos: add sobre valor saturado");
	check(facts.value(5u) == 0xffffu && facts.epoch() == after_add + 1u,
	      "hechos: sin cambio efectivo no sube epoch");

	check(!facts.set(8u, 1u), "hechos: set fuera de rango falla");
	check(!facts.clear(99u), "hechos: clear fuera de rango falla");
	check(!facts.add(0xffffu, 1u), "hechos: add fuera de rango falla");
	check(!facts.has(8u) && facts.value(8u) == 0u, "hechos: consulta fuera de rango segura");

	check(facts.present_count() == 1u, "hechos: present_count cuenta");
	facts.clear_changed();
	check(!facts.any_changed() && !facts.changed(3u), "hechos: clear_changed limpia bits");
	check(facts.epoch() == after_add + 1u, "hechos: clear_changed no toca epoch");

	(void)facts.set(1u, 0x11u);
	(void)facts.set(6u, 0x66u);
	eng::u16 ids[8] = {};
	eng::u16 vals[8] = {};
	eng::u8 n = 0u;
	facts.for_each_present([&](eng::rpg::FactId id, eng::u16 value) {
		ids[n] = id;
		vals[n] = value;
		++n;
	});
	check(n == 3u && ids[0] == 1u && ids[1] == 5u && ids[2] == 6u,
	      "hechos: for_each_present recorre en orden ascendente");
	check(vals[0] == 0x11u && vals[1] == 0xffffu && vals[2] == 0x66u,
	      "hechos: for_each_present entrega los valores");

	facts.clear();
	check(facts.present_count() == 0u && facts.epoch() == 0u && !facts.any_changed(),
	      "hechos: clear total vuelve a cero");
}

void test_fact_set() {
	eng::rpg::FactSet<3> set;
	check(!set.has(10u), "factset: vacio al inicio");
	check(set.add(10u), "factset: add");
	check(set.has(10u), "factset: has tras add");
	check(!set.add(10u), "factset: duplicado rechazado");
	check(set.add(11u) && set.add(12u), "factset: llenando hasta capacidad");
	check(!set.add(13u), "factset: capacidad finita");
	check(set.remove(11u), "factset: remove de presente");
	check(!set.remove(11u), "factset: remove de ausente");
	check(set.add(13u), "factset: hueco reutilizado tras remove");
	check(set.has(10u) && !set.has(11u) && set.has(12u) && set.has(13u),
	      "factset: contenido final correcto");
}

void test_clock() {
	eng::rpg::WorldClock clock;
	check(clock.day == 1u && clock.hour == 8u && clock.minute == 0u,
	      "reloj: arranca el dia 1 a las 08:00");
	check(eng::rpg::minutes_of_day(clock) == 480u, "reloj: minutes_of_day 08:00");

	eng::rpg::advance_minutes(clock, 16u * 60u); // 08:00 + 16 h = 00:00 del dia 2
	check(clock.day == 2u && clock.hour == 0u && clock.minute == 0u,
	      "reloj: cruza medianoche con acarreo de dia");
	eng::rpg::advance_minutes(clock, 61u); // 01:01
	check(clock.day == 2u && clock.hour == 1u && clock.minute == 1u,
	      "reloj: acarreo de hora");

	// 61 + 100000 = 100061 min = 69 dias + 701 min = 11 h 41 min (division nativa).
	eng::rpg::advance_minutes(clock, 100000u);
	check(clock.day == 71u && clock.hour == 11u && clock.minute == 41u,
	      "reloj: delta grande por divmod32/divu_w");

	eng::rpg::advance_hours(clock, 13u); // 11:41 + 13 h = 00:41 del dia 72
	check(clock.day == 72u && clock.hour == 0u && clock.minute == 41u,
	      "reloj: advance_hours");

	eng::rpg::advance_days(clock, 3u); // conserva la hora del dia
	check(clock.day == 75u && clock.hour == 0u && clock.minute == 41u,
	      "reloj: advance_days conserva hora");

	clock.day = 65000u;
	eng::rpg::advance_days(clock, 1000u);
	check(clock.day == eng::rpg::kLastDay, "reloj: dia saturado al maximo");

	eng::rpg::set_time(clock, 30u, 90u);
	check(clock.hour == 23u && clock.minute == 59u, "reloj: set_time recorta al rango");
	eng::rpg::set_time(clock, 6u, 30u);
	check(clock.hour == 6u && clock.minute == 30u &&
		  eng::rpg::minutes_of_day(clock) == 390u,
	      "reloj: set_time valido");
}

} // namespace

int main() {
	std::printf("RPG kernel (hechos, reloj de mundo):\n");
	test_fact_store();
	test_fact_set();
	test_clock();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG kernel (FactStore/FactSet/WorldClock)\n");
	return 0;
}
