// ============================================================================
// Test HOST-461: afijos y mejora de objetos (`eng::rpg/affix.hpp`)
// ============================================================================
//
// Valida el progreso del objeto concreto (hito M2):
//
//   1) `apply_affix`: el afijo se acepta por **etiquetas** del tipo, se marca en la máscara
//      de la instancia y rechaza duplicados, tipos incompatibles y bits fuera de rango.
//   2) `gather_item_mods`: expansión base + afijos, **escala de mejora** aditiva en `add_pct`
//      (aritmética entera exacta) y truncación delatada por el conteo.
//   3) Integración con el equipo y `recompute_stats`: una espada mejorada y con afijo sube
//      el ataque derivado por las fases fijas del pipeline, sin tocar el combate.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/461_rpg_affix

#include <cstdio>

#include <eng/rpg/affix.hpp>
#include <eng/rpg/equipment.hpp>

namespace {

using namespace eng::rpg;

enum : eng::u8 { kAtk = 0u, kDef = 1u };

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Fixture {
	ItemCatalog<3> catalog {};
	ItemStore<4> store {};
	eng::util::Array<StatMod, 2> def_mods {};    // [0] = espada +3 atk
	eng::util::Array<StatMod, 1> affix_mods {};  // [0] = "de fuego" +2 atk
	eng::util::Array<AffixDef, 2> affixes {};
	ModSources sources {};
	ItemTypeId sword = no_item_type;
	ItemTypeId potion = no_item_type;

	Fixture() {
		ItemDef sword_def {};
		sword_def.tags = item_tags::weapon;
		sword_def.durability_max = 100u;
		sword_def.mod_first = 0u;
		sword_def.mod_count = 1u;
		sword = catalog.add(sword_def);
		ItemDef potion_def {};
		potion_def.tags = item_tags::food;
		potion = catalog.add(potion_def);

		def_mods[0] = StatMod {kAtk, 3, 0};
		affix_mods[0] = StatMod {kAtk, 2, 0};
		affixes[0] = AffixDef {item_tags::weapon, 0u, 1u};  // de fuego: solo armas
		affixes[1] = AffixDef {item_tags::armor, 0u, 1u};   // de muro: solo armaduras

		sources.def_mods = def_mods.span();
		sources.affixes = affixes.span();
		sources.affix_mods = affix_mods.span();
	}

	ItemHandle spawn_sword(eng::u8 upgrade = 0u) {
		ItemInstance inst {};
		inst.type = sword;
		inst.qty = 1u;
		inst.durability = 100u;
		inst.upgrade = upgrade;
		return store.spawn(inst);
	}
};

void test_apply_affix() {
	Fixture fx;
	const ItemDef& sword_def = *fx.catalog.at(fx.sword);
	const ItemDef& potion_def = *fx.catalog.at(fx.potion);

	ItemInstance sword {};
	sword.type = fx.sword;
	check(apply_affix(sword, sword_def, 0u, fx.affixes[0]), "afijo: arma acepta el de fuego");
	check(sword.affixes == 0x0001u, "afijo: bit marcado en la mascara");
	check(!apply_affix(sword, sword_def, 0u, fx.affixes[0]), "afijo: duplicado rechazado");
	check(!apply_affix(sword, sword_def, 1u, fx.affixes[1]),
	      "afijo: el de armaduras no entra en un arma");
	check(!apply_affix(sword, sword_def, 16u, fx.affixes[0]), "afijo: bit fuera de rango");

	ItemInstance food {};
	food.type = fx.potion;
	check(!apply_affix(food, potion_def, 0u, fx.affixes[0]),
	      "afijo: la pocima no acepta el de fuego");
	check(food.affixes == 0u, "afijo: rechazo sin marca");

	// Afijo sin restriccion (allowed_tags = 0) entra en cualquier cosa.
	AffixDef any {};
	any.mod_count = 0u;
	check(apply_affix(food, potion_def, 5u, any), "afijo: sin restriccion entra");
	check(food.affixes == 0x0020u, "afijo: bit 5");
}

void test_gather_and_upgrade() {
	Fixture fx;

	ItemInstance base {};
	base.type = fx.sword;
	eng::util::Array<StatMod, 4> out {};
	check(gather_item_mods(base, *fx.catalog.at(fx.sword), fx.sources, out.span()) == 1u,
	      "mods: solo el mod base");
	check(out[0].stat == kAtk && out[0].add == 3 && out[0].add_pct == 0, "mods: +3 ataque");

	const ItemDef& sword_def = *fx.catalog.at(fx.sword);
	(void)apply_affix(base, sword_def, 0u, fx.affixes[0]);
	check(gather_item_mods(base, sword_def, fx.sources, out.span()) == 2u,
	      "mods: base + afijo");
	check(out[1].stat == kAtk && out[1].add == 2, "mods: +2 del afijo");

	// Mejora: cada mod gana +10 % por nivel (en `add_pct`), recortado a ±100.
	check(reinforce(base, 3u, 10u), "mejora: sube a nivel 3");
	check(base.upgrade == 3u, "mejora: nivel guardado");
	check(!reinforce(base, 5u, 3u), "mejora: tope respetado");
	fx.sources.upgrade_step_pct = 10u;
	check(gather_item_mods(base, sword_def, fx.sources, out.span()) == 2u, "mejora: sigue habiendo 2");
	check(out[0].add_pct == 30 && out[1].add_pct == 30, "mejora: +30 % por nivel 3");
	fx.sources.upgrade_step_pct = 60u;
	(void)gather_item_mods(base, sword_def, fx.sources, out.span());
	check(out[0].add_pct == 100, "mejora: porcentaje recortado a +100");

	// Truncación: un `out` que no cabe delata el conteo (no es silencioso).
	eng::util::Array<StatMod, 1> tiny {};
	fx.sources.upgrade_step_pct = 0u;
	check(gather_item_mods(base, sword_def, fx.sources, tiny.span()) == 1u,
	      "mods: out lleno devuelve lo escrito");

	// Rango de afijo mal formado: corta sin leer fuera.
	Fixture broken;
	broken.affixes[0].mod_first = 9u;
	ItemInstance with {};
	with.type = broken.sword;
	(void)apply_affix(with, *broken.catalog.at(broken.sword), 0u, broken.affixes[0]);
	check(gather_item_mods(with, *broken.catalog.at(broken.sword), broken.sources, out.span()) == 1u,
	      "mods: rango de afijo mal formado corta");
}

void test_equipment_integration() {
	Fixture fx;
	fx.sources.upgrade_step_pct = 10u;
	Equipment<1> eq {};
	eq.required_tags[0] = item_tags::weapon;

	const ItemHandle h = fx.spawn_sword(3u); // nivel 3
	eng::Ref<ItemInstance> inst = fx.store.get(h);
	(void)apply_affix(*inst, *fx.catalog.at(fx.sword), 0u, fx.affixes[0]);
	ItemHandle replaced {};
	check(equip_item(eq, fx.store, fx.catalog, 0u, h, replaced), "integracion: espada equipada");

	eng::util::Array<StatMod, 4> mods {};
	const eng::u16 written = gather_equipment_mods(eq, fx.store, fx.catalog, fx.sources, mods.span());
	check(written == 2u, "integracion: base + afijo recogidos");

	StatSheet<2> sheet {};
	sheet.base[kAtk] = 5;
	recompute_stats(sheet, mods.span());
	// (5 + 3 + 2) × (100 + 30 + 30) / 100 = 16 (el porcentaje se acumula por mod, y el
	// pipeline de stats lo recorta a ±100 tras sumarlo).
	check(derived_stat(sheet, kAtk) == 16, "integracion: ataque derivado con mejora y afijo");
}

} // namespace

int main() {
	std::printf("RPG affix (afijos, mejora, recoleccion de mods):\n");
	test_apply_affix();
	test_gather_and_upgrade();
	test_equipment_integration();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG affix (apply_affix/gather_item_mods/reinforce)\n");
	return 0;
}
