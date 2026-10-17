#pragma once

/// \file sim_bridge.hpp
/// **Puente `eng::rpg` ↔ `eng::sim`** (hito M1): las operaciones de la capa RPG que la
/// simulación interpreta. La regla del puente está en `docs/engine/architecture/RPG_LAYER.md`
/// §7: la simulación **propone** (una criatura tiene hambre), el RPG **interpreta** (come y
/// la alimenta); el puente no duplica estado — lee el objeto del RPG y escribe en las
/// necesidades de la sim.
///
/// Es el **único** sitio de `eng::rpg` que incluye `eng::sim`: el núcleo sigue siendo
/// independiente (un Zelda sin ecosistema no paga nada por este fichero si no lo incluye).
///
/// Verificación: HOST-459. NO VERIFICADA por demo (vertical slice en el roadmap, M6).

#include <eng/core/types/types.hpp>
#include <eng/rpg/combat.hpp>
#include <eng/rpg/inventory.hpp>
#include <eng/sim/needs.hpp>

namespace eng::rpg {

/// Comer: consume **una unidad** de `h` (que debe llevar la etiqueta `food`) y alimenta las
/// necesidades de `needs` con `nutrition` (puntos de saciedad). `false` sin efecto si el
/// handle no vive, su tipo no está en el catálogo o no es comida.
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
[[nodiscard]] bool eat_item(ItemBag<Slots>& bag, ItemStore<MaxItems>& store,
			    const ItemCatalog<MaxTypes>& catalog, ItemHandle h,
			    eng::sim::Needs& needs, eng::u8 nutrition) noexcept {
	eng::Ref<const ItemInstance> inst = store.get(h);
	if (!inst.valid()) {
		return false;
	}
	eng::Ref<const ItemDef> def = catalog.at(inst->type);
	if (!def.valid() || !has_tags(def->tags, item_tags::food)) {
		return false; // no es comida: no se consume nada
	}
	if (!consume(bag, store, h, 1u)) {
		return false;
	}
	eng::sim::feed(needs, nutrition);
	return true;
}

/// Descansar consumiendo un objeto (`rest`): consume una unidad de `h` y baja la fatiga de
/// `needs`. Mismo contrato que `eat_item` (el objeto no lleva etiqueta propia: lo decide el
/// juego pasando el handle).
template <eng::u8 Slots, eng::u8 MaxItems>
[[nodiscard]] bool rest_with_item(ItemBag<Slots>& bag, ItemStore<MaxItems>& store, ItemHandle h,
				  eng::sim::Needs& needs, eng::u8 rest_amount) noexcept {
	if (!consume(bag, store, h, 1u)) {
		return false;
	}
	eng::sim::rest(needs, rest_amount);
	return true;
}

/// **Daño con enlace a la simulación**: aplica `dealt` a la vitalidad del actor y lo
/// refleja en sus necesidades (herida y miedo suben; la simulación propone, el RPG
/// interpreta — `RPG_LAYER.md` §7). Devuelve el daño realmente quitado.
[[nodiscard]] inline eng::u16 hurt_actor(Vitality& vitality, eng::sim::Needs& needs,
					 eng::u16 dealt) noexcept {
	const eng::u16 done = apply_damage(vitality, dealt);
	if (done > 0u) {
		eng::sim::hurt(needs, static_cast<eng::u8>(done > 40u ? 40u : done));
		eng::sim::rise(needs, eng::sim::Need::Fear, 8u);
	}
	return done;
}

} // namespace eng::rpg
