#pragma once

/// \file hud_bridge.hpp
/// **Puente RPG -> HUD** (H5): convierte bolsas/inventario de `eng::rpg` en vistas
/// `HudSlotView` para un `IconGrid` del HUD — el caso de **aventuras gráficas y CRPG**
/// (inventario, equipo; `ADVENTURE_LAYER.md` §3 y `RPG_LAYER.md`). Vive en `eng/rpg` (usa
/// ambos lados) para **no invertir dependencias**: `eng::hud` no conoce el RPG y el juego
/// solo llama a estas funciones al refrescar la rejilla.
///
/// El **icono** de cada tipo sale de una tabla del juego (`icon_of_type[type]`, 0 = sin
/// icono): el catálogo del RPG es contenido puro y no lleva arte; el HUD pinta
/// `HudSlotView::icon_frame` con su atlas.
///
/// Uso típico (setup de la rejilla + refresco por evento):
///
///   static const eng::u16 kIcons[] = {0u, 3u, 5u, /* ... por ItemTypeId */};
///   eng::hud::HudSlotView views[24] {};
///   const eng::u8 n = eng::rpg::hud_slots_from_bag(bag, store, kIcons, views, &equipment);
///   (void)hud.bind_grid(grid, eng::Span<const eng::hud::HudSlotView> {views, n});

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/hud/model.hpp>
#include <eng/rpg/equipment.hpp>
#include <eng/rpg/inventory.hpp>
#include <eng/rpg/items.hpp>

namespace eng::rpg {

/// Icono de `type` según la tabla del juego (`0` si no hay tabla o el tipo está fuera).
[[nodiscard]] constexpr eng::u16 hud_icon_for(eng::Span<const eng::u16> icon_of_type,
					      ItemTypeId type) noexcept {
	return type < icon_of_type.size() ? icon_of_type[type] : 0u;
}

/// Convierte la bolsa `bag` (instancias en `store`) en vistas de slot del HUD. Devuelve
/// cuántas escribió (`min(out.size(), bag.items.size())`); los slots restantes de `out`
/// quedan **vacíos** (así la rejilla puede tener más celdas que objetos). Con `equipped`
/// (opcional) las piezas equipadas se marcan `kHudSlotEquipped`.
///
/// `item_id` es el **id lógico** del objeto en el HUD: `type + 1` (0 = vacío), de modo que
/// el juego distingue "sin objeto" de "tipo 0" y puede mapear de vuelta al catálogo.
template <eng::u8 Slots, eng::u8 MaxItems, eng::u8 EqSlots = 1u>
[[nodiscard]] eng::u8 hud_slots_from_bag(const ItemBag<Slots>& bag, const ItemStore<MaxItems>& store,
					 eng::Span<const eng::u16> icon_of_type,
					 eng::Span<eng::hud::HudSlotView> out,
					 const Equipment<EqSlots>* equipped = nullptr) noexcept {
	const eng::usize used = bag.items.size() < out.size() ? bag.items.size() : out.size();
	for (eng::usize i = 0u; i < out.size(); ++i) {
		eng::hud::HudSlotView& v = out[i];
		v = eng::hud::HudSlotView {};
		if (i >= used) {
			v.flags = eng::hud::kHudSlotEmpty;
			continue;
		}
		const ItemHandle h = bag.items[i];
		const eng::Ref<const ItemInstance> inst = store.get(h);
		if (!inst.valid() || inst->qty == 0u) {
			v.flags = eng::hud::kHudSlotEmpty;
			continue;
		}
		v.item_id = static_cast<eng::u16>(inst->type) + 1u; // 0 = vacío
		v.icon_frame = hud_icon_for(icon_of_type, inst->type);
		v.qty = inst->qty;
		v.flags = eng::hud::kHudSlotEnabled;
		if (equipped != nullptr) {
			for (eng::u8 s = 0u; s < EqSlots; ++s) {
				if (equipped->slots[s] == h) {
					v.flags = static_cast<eng::u8>(v.flags |
								       eng::hud::kHudSlotEquipped);
				}
			}
		}
	}
	return static_cast<eng::u8>(used);
}

} // namespace eng::rpg
