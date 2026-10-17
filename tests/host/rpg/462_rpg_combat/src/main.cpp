// ============================================================================
// Test HOST-462: conflicto -- vitalidad, pipeline de dano y enlace con la simulacion
// ============================================================================
//
// Valida `engine/include/eng/rpg/{combat,sim_bridge}.hpp` (hito M3):
//
//   1) `Vitality`: dano/cura saturados, muerte por HP 0.
//   2) `resolve_damage` por **fases fijas**: mods del atacante, del defensor y resistencias
//      por etiqueta (solo las filas cuyas etiquetas tocan el ataque), recorte del factor a
//      [0x, 2x], minimo 1 con payload positivo (salvo inmunidad total) y saturacion a u16.
//   3) `hurt_actor`: el dano se refleja en `eng::sim::Needs` (herida y miedo).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/462_rpg_combat

#include <cstdio>

#include <eng/rpg/combat.hpp>
#include <eng/rpg/sim_bridge.hpp>

namespace {

using namespace eng::rpg;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_vitality() {
	Vitality v {10u, 20u};
	check(v.alive(), "vitalidad: vivo con hp 10");
	check(heal(v, 15u) == 10u && v.hp == 20u, "vitalidad: cura saturada al maximo");
	check(heal(v, 5u) == 0u, "vitalidad: sin hueco no cura");
	check(apply_damage(v, 7u) == 7u && v.hp == 13u, "vitalidad: dano exacto");
	check(apply_damage(v, 99u) == 13u && v.hp == 0u, "vitalidad: dano saturado a 0");
	check(!v.alive(), "vitalidad: hp 0 = derrotado");
}

void test_pipeline() {
	const eng::util::Array<CombatMod, 2> attacker {{CombatMod {damage_tags::slash, 50},
							CombatMod {damage_tags::fire, 90}}};
	const eng::util::Array<CombatMod, 2> defender {{CombatMod {damage_tags::physical, -20},
							CombatMod {damage_tags::cold, 90}}};
	const eng::util::Array<Resistance, 2> resists {{Resistance {damage_tags::physical, 25},
							Resistance {damage_tags::fire, 100}}};

	CombatTables none {};
	DamageInput slash {10u, static_cast<eng::u16>(damage_tags::physical | damage_tags::slash)};

	check(resolve_damage(slash, none) == 10u, "pipeline: sin tablas, dano base");

	CombatTables att {};
	att.attacker_mods = attacker.span();
	check(resolve_damage(slash, att) == 15u, "pipeline: fase 1 (+50% del atacante)");
	// El mod de fuego no toca a un ataque sin la etiqueta fire.
	const DamageInput slash_only {10u, damage_tags::slash};
	check(resolve_damage(slash_only, att) == 15u, "pipeline: solo filas con etiquetas presentes");

	CombatTables both {};
	both.attacker_mods = attacker.span();
	both.defender_mods = defender.span();
	check(resolve_damage(slash, both) == 13u, "pipeline: fase 2 (-20% del defensor)");

	CombatTables full {};
	full.attacker_mods = attacker.span();
	full.defender_mods = defender.span();
	full.resistances = resists.span();
	check(resolve_damage(slash, full) == 11u, "pipeline: fase 3 (+50 -20 -25 = +5%)");

	// Inmunidad total: una resistencia de +100 anula el ataque (ni el minimo de 1).
	const DamageInput fire {10u, static_cast<eng::u16>(damage_tags::fire | damage_tags::magic)};
	CombatTables immune {};
	immune.resistances = resists.span();
	check(resolve_damage(fire, immune) == 0u, "pipeline: inmunidad total devuelve 0");

	// Recorte de la fase a +/-100: tres mods de +60 suman 100 (no 180).
	const eng::util::Array<CombatMod, 3> stacking {{CombatMod {damage_tags::blunt, 60},
							CombatMod {damage_tags::blunt, 60},
							CombatMod {damage_tags::blunt, 60}}};
	CombatTables cap {};
	cap.attacker_mods = stacking.span();
	const DamageInput blunt {10u, damage_tags::blunt};
	check(sum_mods(stacking.span(), damage_tags::blunt) == 100, "pipeline: fase recortada a +100");
	check(resolve_damage(blunt, cap) == 20u, "pipeline: factor 2x");

	// Minimo 1 con payload positivo (salvo inmunidad): base 1 con +1% efectivo.
	const eng::util::Array<Resistance, 1> wall {{Resistance {damage_tags::slash, 99}}};
	CombatTables thin {};
	thin.resistances = wall.span();
	const DamageInput one {1u, damage_tags::slash};
	check(resolve_damage(one, thin) == 1u, "pipeline: minimo 1 con payload positivo");

	// Saturacion a u16.
	CombatTables huge {};
	huge.attacker_mods = stacking.span();
	const DamageInput big {60000u, damage_tags::blunt};
	check(resolve_damage(big, huge) == 0xffffu, "pipeline: saturacion a u16");

	// Payload 0 no hace dano.
	const DamageInput zero {0u, damage_tags::blunt};
	check(resolve_damage(zero, huge) == 0u, "pipeline: payload 0 no hace dano");
}

void test_needs_bridge() {
	Vitality v {12u, 12u};
	eng::sim::Needs needs {};
	check(hurt_actor(v, needs, 3u) == 3u && v.hp == 9u, "puente: el dano baja la vitalidad");
	check(needs.injury == 3u, "puente: la herida de la sim sube");
	check(needs.fear == 8u, "puente: el miedo de la sim sube");
	const eng::u16 done = hurt_actor(v, needs, 50u);
	check(done == 9u && v.hp == 0u && !v.alive(), "puente: golpe mortal");
	check(needs.injury == 12u, "puente: la herida acumula (3 + 9)");

	// Tope por golpe: un dano enorme sube la herida como maximo 40 por impacto.
	Vitality big {60u, 60u};
	eng::sim::Needs needs2 {};
	check(hurt_actor(big, needs2, 60u) == 60u && needs2.injury == 40u,
	      "puente: la herida por golpe se limita a 40");
}

} // namespace

int main() {
	std::printf("RPG combat (vitalidad, pipeline, puente sim):\n");
	test_vitality();
	test_pipeline();
	test_needs_bridge();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG combat (Vitality/resolve_damage/hurt_actor)\n");
	return 0;
}
