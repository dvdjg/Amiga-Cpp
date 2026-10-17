#pragma once

/// \file equipment.hpp
/// **Equipo** (`eng::rpg`): ranuras de equipo (cabeza, cuerpo, mano principal…) que apuntan
/// a instancias del `ItemStore`, más la recolección de sus modificadores de atributo.
///
/// Decisión de diseño: la **ranura no es un tipo** — es un índice. Cuántas hay y qué
/// etiqueta exige cada una es **dato del juego** (`required_tags`), de modo que el mismo
/// mecanismo sirve a un Zelda (tres ranuras) y a un CRPG (diez), sin duplicar la clase y sin
/// un `enum` de casos concretos en el engine (ver `CODING_STYLE.md` §Reglas obligatorias).
///
/// `equip_item` valida la etiqueta exigida por la ranura antes de aceptar (un objeto sin
/// `weapon` no entra en la mano); `gather_equipment_mods` expande los modificadores de cada
/// pieza (base, afijos y escala de mejora vía `ModSources`, `affix.hpp`) y alimenta
/// `recompute_stats` (`stats.hpp`).
///
/// Verificación: HOST-456. NO VERIFICADA por demo (vertical slice en
/// `docs/guides/roadmap/ROADMAP_RPG.md` M6).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/rpg/affix.hpp>
#include <eng/rpg/items.hpp>
#include <eng/rpg/stats.hpp>
#include <eng/rpg/types.hpp>

namespace eng::rpg {

/// Conjunto de ranuras de equipo. `slots` guarda el handle equipado (inválido = vacía) y
/// `required_tags` la etiqueta que la ranura exige (0 = cualquiera): configuración del juego
/// que sobrevive intacta a cargar partida (el save solo guarda los handles).
template <eng::u8 Slots>
struct Equipment {
	static_assert(Slots >= 1u, "Equipment: al menos una ranura");

	eng::util::Array<ItemHandle, Slots> slots {};        ///< pieza equipada por ranura (no_item = vacía)
	eng::util::Array<eng::u16, Slots> required_tags {};  ///< máscara de etiquetas exigida por ranura (0 = cualquiera)
};

/// Equipa `h` en `slot` si su tipo existe en el catálogo y cumple la etiqueta exigida por la
/// ranura. `true` si se equipó; en ese caso `replaced` recibe la pieza anterior (inválida si
/// la ranura estaba vacía) para devolverla al inventario. Rechazo: `false` y nada cambia.
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
[[nodiscard]] bool equip_item(Equipment<Slots>& equipment, const ItemStore<MaxItems>& store,
			      const ItemCatalog<MaxTypes>& catalog, eng::u8 slot, ItemHandle h,
			      ItemHandle& replaced) noexcept {
	replaced = ItemHandle {};
	if (slot >= Slots) {
		return false;
	}
	eng::Ref<const ItemInstance> inst = store.get(h);
	if (!inst.valid()) {
		return false;
	}
	eng::Ref<const ItemDef> def = catalog.at(inst->type);
	if (!def.valid() || !has_tags(def->tags, equipment.required_tags[slot])) {
		return false;
	}
	replaced = equipment.slots[slot];
	equipment.slots[slot] = h;
	return true;
}

/// Desequipa `slot` y devuelve la pieza (inválida si ya estaba vacía o el índice no existe).
/// No la mete en ningún inventario: el llamador decide su destino.
template <eng::u8 Slots>
[[nodiscard]] ItemHandle unequip_item(Equipment<Slots>& equipment, eng::u8 slot) noexcept {
	if (slot >= Slots) {
		return ItemHandle {};
	}
	const ItemHandle h = equipment.slots[slot];
	equipment.slots[slot] = ItemHandle {};
	return h;
}

/// Acumula en `out` los `StatMod` de todas las piezas equipadas: por cada pieza viva copia
/// su rango base (`mod_first/mod_count`) y el de sus afijos marcados (expansión de
/// `gather_item_mods`, con la escala de mejora de `ModSources`). Devuelve cuántos escribió;
/// si `out` se llena, escribe los que caben y **no** es un error (subir la capacidad del
/// array de mods, no perder silenciosamente: el conteo lo delata).
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
[[nodiscard]] eng::u16 gather_equipment_mods(const Equipment<Slots>& equipment,
					     const ItemStore<MaxItems>& store,
					     const ItemCatalog<MaxTypes>& catalog,
					     const ModSources& sources,
					     eng::Span<StatMod> out) noexcept {
	eng::u16 written = 0u;
	for (eng::u8 slot = 0; slot < Slots; ++slot) {
		eng::Ref<const ItemInstance> inst = store.get(equipment.slots[slot]);
		if (!inst.valid()) {
			continue;
		}
		eng::Ref<const ItemDef> def = catalog.at(inst->type);
		if (!def.valid()) {
			continue;
		}
		written = written + gather_item_mods(*inst, *def, sources, out.subspan(written));
	}
	return written;
}

} // namespace eng::rpg
