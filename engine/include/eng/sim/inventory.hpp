#pragma once

/// \file inventory.hpp
/// **Inventario** de una criatura (`eng::sim`): pilas de objetos con etiquetas, sin heap
/// y de capacidad fija. Es la pieza que cierra el bucle objeto↔planificación: los pasos
/// del dominio GOAP (`Forage`/`Gather`/`CraftTool`/`Build`...) mueven estas pilas
/// (`object.hpp`), y la decisión puede leerlas para saber qué tiene el agente.
///
/// - `ItemKind`: comida, material, herramienta, estructura, ficha.
/// - `item_tags`: qué se puede hacer con un objeto (comestible, fabricable, herramienta,
///   estructura, valioso). Evita ramificar por tipo en la lógica.
/// - `Inventory`: hasta `kMaxItemStacks` pilas `(tipo, cantidad)`; `add`/`remove`/`count`.
///
/// Verificación: HOST-157 (y HOST-155 para el dominio).

#include <eng/core/types/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Tipo de objeto.
enum class ItemKind : eng::u8 {
	Food = 0,
	Material = 1,
	Tool = 2,
	Shelter = 3,
	Token = 4,
	Count = 5,
};

inline constexpr eng::usize item_kinds = static_cast<eng::usize>(ItemKind::Count);

/// Etiquetas de objeto (bitfield): capacidades que habilitan acciones.
namespace item_tag {
inline constexpr eng::u8 edible = 0x01u;
inline constexpr eng::u8 craftable = 0x02u;
inline constexpr eng::u8 tool = 0x04u;
inline constexpr eng::u8 structure = 0x08u;
inline constexpr eng::u8 valuable = 0x10u;
} // namespace item_tag

/// Etiquetas de cada tipo.
[[nodiscard]] constexpr eng::u8 item_tags(ItemKind k) noexcept {
	switch (k) {
		case ItemKind::Food: return item_tag::edible;
		case ItemKind::Material: return item_tag::craftable;
		case ItemKind::Tool: return item_tag::tool;
		case ItemKind::Shelter: return item_tag::structure;
		case ItemKind::Token: return item_tag::valuable;
		default: return 0u;
	}
}

/// ¿El objeto tiene una etiqueta?
[[nodiscard]] constexpr bool has_tag(ItemKind k, eng::u8 tag) noexcept {
	return (item_tags(k) & tag) != 0u;
}

/// Nombre legible (diagnóstico; sin heap).
[[nodiscard]] constexpr const char* item_name(ItemKind k) noexcept {
	switch (k) {
		case ItemKind::Food: return "food";
		case ItemKind::Material: return "material";
		case ItemKind::Tool: return "tool";
		case ItemKind::Shelter: return "shelter";
		case ItemKind::Token: return "token";
		default: return "?";
	}
}

/// Pila de objetos del mismo tipo.
struct ItemStack {
	ItemKind kind = ItemKind::Food;
	eng::u8 count = 0;
};

/// Capacidad por defecto de pilas distintas.
inline constexpr eng::u8 kMaxItemStacks = 4u;

/// Inventario de una criatura.
struct Inventory {
	eng::util::StaticVector<ItemStack, kMaxItemStacks> stacks {};

	[[nodiscard]] constexpr eng::u8 count(ItemKind k) const noexcept {
		for (eng::usize i = 0; i < stacks.size(); ++i) {
			if (stacks[i].kind == k) {
				return stacks[i].count;
			}
		}
		return 0u;
	}

	[[nodiscard]] constexpr bool has(ItemKind k, eng::u8 amount) const noexcept {
		return count(k) >= amount;
	}

	/// Añade `amount` (saturado a 255). Devuelve cuánto se añadió de verdad.
	constexpr eng::u8 add(ItemKind k, eng::u8 amount) noexcept {
		for (eng::usize i = 0; i < stacks.size(); ++i) {
			if (stacks[i].kind == k) {
				const eng::u8 before = stacks[i].count;
				stacks[i].count = u8_sat_add(stacks[i].count, amount);
				return static_cast<eng::u8>(stacks[i].count - before);
			}
		}
		if (stacks.full()) {
			return 0u;
		}
		(void)stacks.push_back(ItemStack {k, amount});
		return amount;
	}

	/// Quita `amount`; `false` si no había bastante.
	constexpr bool remove(ItemKind k, eng::u8 amount) noexcept {
		for (eng::usize i = 0; i < stacks.size(); ++i) {
			if (stacks[i].kind != k) {
				continue;
			}
			if (stacks[i].count < amount) {
				return false;
			}
			stacks[i].count = static_cast<eng::u8>(stacks[i].count - amount);
			if (stacks[i].count == 0u) {
				stacks.erase(i);
			}
			return true;
		}
		return false;
	}
};

} // namespace eng::sim
