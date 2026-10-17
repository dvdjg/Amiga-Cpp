#pragma once

/// \file inventory.hpp
/// **Inventario** (`eng::rpg`): lista de handles con capacidad fija y apilado sobre pilas
/// compatibles. El contenedor no posee los objetos — viven en el `ItemStore` —: el
/// inventario solo dice "qué instancias lleva este actor", y por eso vale igual para una
/// mochila, un cofre o el botín de un enemigo (el `ItemBag` no sabe de quién es).
///
/// Reparto de responsabilidades (patrón del engine: el contenedor es tonto, las reglas son
/// funciones): `ItemBag::put/remove` manipulan entradas; `bag_add` aplica el apilado (usa
/// `merge_stack` de `items.hpp`); las consultas (`bag_count`, `bag_find_type`,
/// `bag_find_tag`, `bag_total_weight`) leen el store/catálogo sin duplicar estado.
///
/// Verificación: HOST-456. NO VERIFICADA por demo (vertical slice en
/// `docs/guides/roadmap/ROADMAP_RPG.md` M6).

#include <eng/core/types/types.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/rpg/items.hpp>

namespace eng::rpg {

/// Contenedor de objetos de capacidad fija. Las entradas son handles; el orden de inserción
/// se conserva (la UI de inventario muestra en ese orden: primero recogido, primero visto).
template <eng::u8 Slots>
struct ItemBag {
	static_assert(Slots >= 1u, "ItemBag: capacidad de al menos una entrada");

	/// Entradas del contenedor (handles vivos del `ItemStore`).
	eng::util::StaticVector<ItemHandle, Slots> items {};

	/// ¿El contenedor está lleno?
	[[nodiscard]] bool full() const noexcept { return items.full(); }

	/// Nº de entradas (pilas, no unidades).
	[[nodiscard]] eng::usize size() const noexcept { return items.size(); }

	/// Añade `h` tal cual (sin apilar). `false` si está lleno.
	bool put(ItemHandle h) noexcept { return items.push_back(h); }

	/// ¿Contiene exactamente el handle `h`?
	[[nodiscard]] bool contains(ItemHandle h) const noexcept {
		for (eng::usize i = 0; i < items.size(); ++i) {
			if (items[i] == h) {
				return true;
			}
		}
		return false;
	}

	/// Quita la entrada de `h` (no destruye la instancia en el store). `false` si no estaba.
	bool remove(ItemHandle h) noexcept {
		for (eng::usize i = 0; i < items.size(); ++i) {
			if (items[i] == h) {
				items.erase(i);
				return true;
			}
		}
		return false;
	}

	/// Entrada `i` (precondición: `i < size()`).
	[[nodiscard]] ItemHandle at(eng::usize i) const noexcept { return items[i]; }
};

/// Añade la instancia `h` del store al contenedor **apilando** sobre pilas compatibles y
/// respetando `stack_max`: si la pila de origen lo excede, la **divide** en pilas completas
/// (creando instancias nuevas en el store) y deja el resto en la instancia de origen; si aún
/// sobra, entra como pila nueva. Devuelve las unidades que no pudieron entrar (0 = todo
/// dentro). No destruye nada del store: con resto > 0, el llamador decide (soltar en el
/// suelo, dejar en el cofre origen…). Con `h` ya presente o agotado, 0.
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
[[nodiscard]] eng::u8 bag_add(ItemBag<Slots>& bag, ItemStore<MaxItems>& store,
			      const ItemCatalog<MaxTypes>& catalog, ItemHandle h) noexcept {
	eng::Ref<ItemInstance> src = store.get(h);
	if (!src.valid() || src->qty == 0u) {
		return 0u;
	}
	eng::Ref<const ItemDef> def = catalog.at(src->type);
	if (!def.valid()) {
		return src->qty; // tipo sin catálogo: no se inventaría contenido roto
	}
	for (eng::usize i = 0; i < bag.items.size() && src->qty > 0u; ++i) {
		eng::Ref<ItemInstance> dst = store.get(bag.items[i]);
		if (!dst.valid() || dst.get() == src.get()) {
			continue; // entrada caducada (se deja a la limpieza del juego) o la propia pila
		}
		(void)merge_stack(*dst, *src, *def);
	}
	// Pilas nuevas respetando `stack_max`: una pila completa por entrada (instancias nuevas).
	const eng::u8 stack_cap = def->stack_max > 0u ? def->stack_max : 1u;
	while (stack_cap > 1u && src->qty > stack_cap) {
		if (bag.full()) {
			return src->qty;
		}
		ItemInstance chunk = *src; // mismo estado (durabilidad, mejora, afijos, metadatos)
		chunk.qty = stack_cap;
		const ItemHandle piece = store.spawn(chunk);
		if (!piece.valid()) {
			return src->qty; // pool lleno: el resto no entra
		}
		(void)bag.put(piece);
		src->qty = src->qty - stack_cap;
	}
	if (src->qty == 0u) {
		return 0u; // dividida del todo: el llamador destruye la instancia de origen
	}
	if (!bag.put(h)) {
		return src->qty; // sin hueco: no entra nada
	}
	return 0u;
}

/// Unidades totales de un tipo en el contenedor (suma `qty` de las pilas que lo contienen).
template <eng::u8 Slots, eng::u8 MaxItems>
[[nodiscard]] eng::u16 bag_count(const ItemBag<Slots>& bag, const ItemStore<MaxItems>& store,
				 ItemTypeId type) noexcept {
	eng::u16 total = 0u;
	for (eng::usize i = 0; i < bag.items.size(); ++i) {
		eng::Ref<const ItemInstance> inst = store.get(bag.items[i]);
		if (inst.valid() && inst->type == type) {
			// Plantilla explícita: `qty` es u8 y el acumulador u16 (deducción uniforme).
			total = eng::util::sat_add<eng::u16>(total, inst->qty);
		}
	}
	return total;
}

/// Primera pila de `type`; `no_item` si no hay ninguna.
template <eng::u8 Slots, eng::u8 MaxItems>
[[nodiscard]] ItemHandle bag_find_type(const ItemBag<Slots>& bag, const ItemStore<MaxItems>& store,
				       ItemTypeId type) noexcept {
	for (eng::usize i = 0; i < bag.items.size(); ++i) {
		eng::Ref<const ItemInstance> inst = store.get(bag.items[i]);
		if (inst.valid() && inst->type == type) {
			return bag.items[i];
		}
	}
	return no_item;
}

/// Primera pila cuyo tipo lleva **todas** las etiquetas de `mask` (p. ej. `item_tags::key`);
/// `no_item` si no hay ninguna. Es la consulta de las reglas ("¿lleva llave?", "¿lleva algo
/// que alumbre?").
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
[[nodiscard]] ItemHandle bag_find_tag(const ItemBag<Slots>& bag, const ItemStore<MaxItems>& store,
				      const ItemCatalog<MaxTypes>& catalog,
				      eng::u16 mask) noexcept {
	for (eng::usize i = 0; i < bag.items.size(); ++i) {
		eng::Ref<const ItemInstance> inst = store.get(bag.items[i]);
		if (!inst.valid()) {
			continue;
		}
		eng::Ref<const ItemDef> def = catalog.at(inst->type);
		if (def.valid() && has_tags(def->tags, mask)) {
			return bag.items[i];
		}
	}
	return no_item;
}

/// Peso total del contenido (saturado a `u16`): suma `weight × qty` de cada pila. Sirve para
/// límites de carga; el coste es O(nº de pilas) y se llama al cambiar el inventario, no por
/// frame.
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
[[nodiscard]] eng::u16 bag_total_weight(const ItemBag<Slots>& bag, const ItemStore<MaxItems>& store,
					const ItemCatalog<MaxTypes>& catalog) noexcept {
	eng::u16 total = 0u;
	for (eng::usize i = 0; i < bag.items.size(); ++i) {
		eng::Ref<const ItemInstance> inst = store.get(bag.items[i]);
		if (!inst.valid()) {
			continue;
		}
		eng::Ref<const ItemDef> def = catalog.at(inst->type);
		if (!def.valid()) {
			continue;
		}
		const eng::u16 line = def->weight * inst->qty;
		total = eng::util::sat_add(total, line);
	}
	return total;
}

/// Primera pila que satisface `(type, tags)` (consulta `item_matches`). `no_item` si no hay
/// ninguna. Es la búsqueda de las recetas, las llaves y las reglas de uso.
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
[[nodiscard]] ItemHandle bag_find_match(const ItemBag<Slots>& bag, const ItemStore<MaxItems>& store,
					const ItemCatalog<MaxTypes>& catalog, ItemTypeId type,
					eng::u16 tags) noexcept {
	for (eng::usize i = 0; i < bag.items.size(); ++i) {
		eng::Ref<const ItemInstance> inst = store.get(bag.items[i]);
		if (!inst.valid()) {
			continue;
		}
		eng::Ref<const ItemDef> def = catalog.at(inst->type);
		if (def.valid() && item_matches(*inst, *def, type, tags)) {
			return bag.items[i];
		}
	}
	return no_item;
}

/// Unidades totales que satisfacen `(type, tags)`, saturado a `u16`.
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
[[nodiscard]] eng::u16 bag_count_match(const ItemBag<Slots>& bag, const ItemStore<MaxItems>& store,
				       const ItemCatalog<MaxTypes>& catalog, ItemTypeId type,
				       eng::u16 tags) noexcept {
	eng::u16 total = 0u;
	for (eng::usize i = 0; i < bag.items.size(); ++i) {
		eng::Ref<const ItemInstance> inst = store.get(bag.items[i]);
		if (!inst.valid()) {
			continue;
		}
		eng::Ref<const ItemDef> def = catalog.at(inst->type);
		if (!def.valid() || !item_matches(*inst, *def, type, tags)) {
			continue;
		}
		total = eng::util::sat_add<eng::u16>(total, inst->qty);
	}
	return total;
}

/// Consume `qty` unidades que satisfagan `(type, tags)`, repartidas por las pilas **en orden
/// de entrada** (determinista). Transaccional: valida primero y no toca nada si no hay
/// bastante. Las instancias que quedan a 0 se destruyen en el store y sus entradas salen de
/// la bolsa. `false` si no había unidades suficientes.
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems>
bool bag_consume(ItemBag<Slots>& bag, ItemStore<MaxItems>& store,
		 const ItemCatalog<MaxTypes>& catalog, ItemTypeId type, eng::u16 tags,
		 eng::u8 qty) noexcept {
	if (qty == 0u) {
		return true;
	}
	if (bag_count_match(bag, store, catalog, type, tags) < qty) {
		return false;
	}
	eng::u8 pending = qty;
	for (eng::usize i = 0; i < bag.items.size() && pending > 0u;) {
		eng::Ref<ItemInstance> inst = store.get(bag.items[i]);
		if (!inst.valid()) {
			bag.items.erase(i);
			continue;
		}
		eng::Ref<const ItemDef> def = catalog.at(inst->type);
		if (!def.valid() || !item_matches(*inst, *def, type, tags)) {
			++i;
			continue;
		}
		const eng::u8 moved = inst->qty < pending ? inst->qty : pending;
		inst->qty = inst->qty - moved;
		pending = pending - moved;
		if (inst->qty == 0u) {
			(void)store.destroy(bag.items[i]);
			bag.items.erase(i);
		} else {
			++i;
		}
	}
	return true;
}

/// Consume `qty` unidades de la instancia `h` concreta (uso/consumo de un objeto concreto):
/// descuenta, destruye la instancia al llegar a 0 y quita su entrada de la bolsa. `false` sin
/// efecto si `h` no vive, no está en la bolsa o no hay unidades suficientes.
template <eng::u8 Slots, eng::u8 MaxItems>
bool consume(ItemBag<Slots>& bag, ItemStore<MaxItems>& store, ItemHandle h, eng::u8 qty = 1u) noexcept {
	if (qty == 0u) {
		return true;
	}
	eng::Ref<ItemInstance> inst = store.get(h);
	if (!inst.valid() || inst->qty < qty) {
		return false;
	}
	const eng::u8 left = inst->qty - qty;
	inst->qty = left;
	if (left == 0u) {
		(void)store.destroy(h);
		(void)bag.remove(h);
	}
	return true;
}

} // namespace eng::rpg
