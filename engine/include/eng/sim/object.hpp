#pragma once

/// \file object.hpp
/// **Objetos del mundo y ejecución del dominio** (`eng::sim`). Une la planificación
/// (`planner.hpp`/`domain.hpp`) con el inventario (`inventory.hpp`): cada paso de un plan
/// se traduce en un efecto material concreto.
///
/// - `ItemStore`: objetos presentes en el mundo (id, tipo, cantidad, room, posición,
///   portador). Capacidad fija, sin heap.
/// - `execute_domain_action`: aplica un paso (`SimActionKind`) al inventario y, si hace
///   falta, al mundo: `Forage` y `Gather` añaden recursos, `Eat` consume comida,
///   `CraftTool` convierte material en herramienta y `Build` consume herramienta+material
///   y **deja una estructura en el mundo**.
///
/// Con esto el ciclo queda cerrado: la criatura planifica para conseguir algo y luego lo
/// consigue de verdad. El juego decide además los efectos secundarios (hambre, memoria) a
/// partir del resultado.
///
/// Verificación: HOST-157.

#include <eng/core/types/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/domain.hpp>
#include <eng/sim/inventory.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Máximo de objetos simultáneos en el mundo.
inline constexpr eng::u8 kMaxWorldItems = 24u;

/// Objeto instanciado en el mundo.
struct Item {
	EntityId id = no_entity;
	ItemKind kind = ItemKind::Food;
	eng::u8 amount = 1;
	RoomId room = no_room;
	eng::s16 x = 0;
	eng::s16 y = 0;
	EntityId holder = no_entity; ///< quién lo lleva (`no_entity` = en el suelo)
};

/// Almacén de objetos del mundo.
struct ItemStore {
	eng::util::StaticVector<Item, kMaxWorldItems> items {};
	eng::u16 next_id = 1u;

	/// Deja un objeto en el mundo; devuelve su id o `no_entity` si no cabe.
	constexpr EntityId spawn(ItemKind kind, RoomId room, eng::s16 x, eng::s16 y,
				 eng::u8 amount = 1u) noexcept {
		if (items.full()) {
			return no_entity;
		}
		Item it {};
		it.id = next_id++;
		it.kind = kind;
		it.amount = amount;
		it.room = room;
		it.x = x;
		it.y = y;
		(void)items.push_back(it);
		return it.id;
	}

	[[nodiscard]] constexpr Item* find(EntityId id) noexcept {
		for (eng::usize i = 0; i < items.size(); ++i) {
			if (items[i].id == id) {
				return &items[i];
			}
		}
		return nullptr;
	}
	[[nodiscard]] constexpr const Item* find(EntityId id) const noexcept {
		for (eng::usize i = 0; i < items.size(); ++i) {
			if (items[i].id == id) {
				return &items[i];
			}
		}
		return nullptr;
	}

	constexpr bool remove(EntityId id) noexcept {
		for (eng::usize i = 0; i < items.size(); ++i) {
			if (items[i].id == id) {
				items.erase(i);
				return true;
			}
		}
		return false;
	}

	/// Cuántos objetos de un tipo hay en el mundo.
	[[nodiscard]] constexpr eng::u16 count_kind(ItemKind kind) const noexcept {
		eng::u16 n = 0u;
		for (eng::usize i = 0; i < items.size(); ++i) {
			if (items[i].kind == kind) {
				++n;
			}
		}
		return n;
	}
};

/// Resultado de ejecutar una acción del dominio.
enum class ActionResult : eng::u8 {
	Done = 0,
	MissingResource = 1,
	Full = 2,
	Unknown = 3,
};

/// Ejecuta materialmente un paso del dominio sobre el inventario y el mundo.
[[nodiscard]] constexpr ActionResult execute_domain_action(Inventory& inv, SimActionKind a,
							   ItemStore* store, RoomId room,
							   eng::s16 x, eng::s16 y) noexcept {
	switch (a) {
		case SimActionKind::Forage:
			return inv.add(ItemKind::Food, 1u) == 1u ? ActionResult::Done
								 : ActionResult::Full;
		case SimActionKind::Eat:
			return inv.remove(ItemKind::Food, 1u) ? ActionResult::Done
							      : ActionResult::MissingResource;
		case SimActionKind::Gather:
			return inv.add(ItemKind::Material, 1u) == 1u ? ActionResult::Done
								     : ActionResult::Full;
		case SimActionKind::CraftTool:
			if (!inv.remove(ItemKind::Material, 1u)) {
				return ActionResult::MissingResource;
			}
			(void)inv.add(ItemKind::Tool, 1u);
			return ActionResult::Done;
		case SimActionKind::Build:
			if (!inv.has(ItemKind::Tool, 1u) || !inv.has(ItemKind::Material, 1u)) {
				return ActionResult::MissingResource;
			}
			(void)inv.remove(ItemKind::Tool, 1u);
			(void)inv.remove(ItemKind::Material, 1u);
			if (store != nullptr) {
				(void)store->spawn(ItemKind::Shelter, room, x, y);
			}
			return ActionResult::Done;
		default:
			return ActionResult::Unknown;
	}
}

} // namespace eng::sim
