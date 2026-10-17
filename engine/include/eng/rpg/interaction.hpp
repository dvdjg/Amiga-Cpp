#pragma once

/// \file interaction.hpp
/// **Interacción con el mundo** (`eng::rpg`, hito M1): cerraduras que se abren con la llave
/// correcta, recoger del suelo y soltar. Cada operación valida, muta (bolsa/instancias/
/// cerradura) y devuelve un resultado explícito; las reglas que las encadenan (UI, quests,
/// IA) viven fuera. Diseño: `docs/engine/architecture/RPG_LAYER.md` §5.1.
///
/// La llave demuestra el modelo de datos de M0 trabajando junto: la **etiqueta** `key` dice
/// que el objeto abre cerraduras y el **metadato de instancia** dice *cuál* (`metadata ==
/// Lock::key_meta`); no hay una clase `LlaveDelAlmacén`.
///
/// Verificación: HOST-459. NO VERIFICADA por demo (vertical slice en el roadmap, M6).

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/rpg/inventory.hpp>
#include <eng/rpg/world.hpp>

namespace eng::rpg {

/// Conjunto de cerraduras del mundo (puertas, cofres). Son **contenido**: se crean al cargar
/// el mapa y no se reciclan; `add` devuelve el índice que la entidad guarda en `data`.
template <eng::u8 MaxLocks>
class LockSet {
public:
	static_assert(MaxLocks >= 1u, "LockSet: capacidad de al menos una cerradura");

	/// Una cerradura: qué llave la abre y si ya está abierta.
	struct Lock {
		eng::u16 key_meta = 0u; ///< metadato de la llave que la abre (`ItemInstance::metadata`)
		bool open = false;      ///< estado persistente (los hechos de historia lo reflejan aparte)
	};

	/// Añade una cerradura para la llave con metadato `key_meta`. `0xff` si está llena.
	[[nodiscard]] eng::u8 add(eng::u16 key_meta) noexcept {
		if (m_count >= MaxLocks) {
			return 0xffu;
		}
		m_locks[m_count].key_meta = key_meta;
		m_locks[m_count].open = false;
		const eng::u8 index = m_count;
		++m_count;
		return index;
	}

	/// ¿Existe la cerradura `index` y está abierta?
	[[nodiscard]] bool is_open(eng::u8 index) const noexcept {
		return index < m_count && m_locks[index].open;
	}

	/// Abre la cerradura `index` con la llave `key`. Requisitos: la llave vive en el store,
	/// su tipo lleva la etiqueta `key` y su `metadata` coincide con `key_meta`. Idempotente
	/// (una cerradura abierta devuelve `true` sin consumir nada). La llave **no se consume**.
	template <eng::u8 MaxTypes, eng::u8 MaxItems>
	[[nodiscard]] bool try_open(eng::u8 index, ItemHandle key, const ItemStore<MaxItems>& store,
				    const ItemCatalog<MaxTypes>& catalog) noexcept {
		if (index >= m_count) {
			return false;
		}
		Lock& lock = m_locks[index];
		if (lock.open) {
			return true;
		}
		eng::Ref<const ItemInstance> inst = store.get(key);
		if (!inst.valid()) {
			return false;
		}
		eng::Ref<const ItemDef> def = catalog.at(inst->type);
		if (!def.valid() || !has_tags(def->tags, item_tags::key)) {
			return false;
		}
		if (inst->metadata != lock.key_meta) {
			return false;
		}
		lock.open = true;
		return true;
	}

private:
	eng::util::Array<Lock, MaxLocks> m_locks {}; ///< cerraduras (0..m_count-1 válidas)
	eng::u8 m_count = 0u;                        ///< cerraduras registradas
};

/// Recoge la entrada `index` del suelo hacia la bolsa, apilando. Devuelve las **unidades que
/// quedaron en el suelo** (0 = recogido del todo y entrada retirada). Una entrada cuya
/// instancia ya no vive se retira (limpieza) y devuelve 0.
template <eng::u8 Slots, eng::u8 MaxTypes, eng::u8 MaxItems, eng::u16 MaxGround>
[[nodiscard]] eng::u8 pick_up(GroundItems<MaxGround>& ground, eng::usize index, ItemBag<Slots>& bag,
			      ItemStore<MaxItems>& store,
			      const ItemCatalog<MaxTypes>& catalog) noexcept {
	if (index >= ground.size()) {
		return 0u;
	}
	const ItemHandle h = ground.at(index).item;
	if (!store.get(h).valid()) {
		(void)ground.take_at(index); // entrada caducada: limpiar
		return 0u;
	}
	const eng::u8 leftover = bag_add(bag, store, catalog, h);
	if (leftover == 0u) {
		(void)ground.take_at(index);
	}
	return leftover;
}

/// Suelta `h` del inventario al suelo (sin destruir la instancia). `false` si el suelo está
/// lleno o `h` no estaba en la bolsa; en ese caso nada cambia.
template <eng::u8 Slots, eng::u16 MaxGround>
[[nodiscard]] bool drop_item(ItemBag<Slots>& bag, GroundItems<MaxGround>& ground, ItemHandle h,
			     eng::s16 x, eng::s16 y, eng::u8 room = 0u) noexcept {
	if (ground.full()) {
		return false;
	}
	if (!bag.remove(h)) {
		return false;
	}
	if (!ground.drop(h, x, y, room)) {
		(void)bag.put(h); // devolución: no perder el objeto
		return false;
	}
	return true;
}

} // namespace eng::rpg
