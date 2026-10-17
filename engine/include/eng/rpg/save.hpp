#pragma once

/// \file save.hpp
/// **Serialización por componente** del núcleo RPG (`eng::rpg`): hechos, instancias de
/// objeto, inventario, equipo, atributos base y reloj de mundo, sobre los cursores seguros
/// `ByteWriter`/`ByteReader` (`eng/core/util/binary.hpp`, little-endian). Sin
/// `reinterpret_cast` a structs y sin `memcpy`: cada campo se escribe explícitamente y una
/// partida truncada falla con `false` en vez de corromper memoria.
///
/// Propiedades del formato:
/// - **determinista**: el mismo estado se serializa byte a byte igual (recorridos en orden
///   ascendente de hueco/id); dos `save_all` consecutivos se comparan en el test HOST-457;
/// - **por componente**: cada bloque puede evolucionar por separado; `kRpgSaveVersion` viaja
///   al principio y un save de otra versión se rechaza;
/// - el **orden canónico** es el de `save_all`: hechos → instancias → inventario → equipo →
///   atributos → reloj. El equipo guarda solo sus handles (`required_tags` es configuración
///   del juego, no estado); las stats guardan solo la base (la derivada se recalcula).
///
/// Qué NO entra aquí: el estado de los motores (quest en curso, combate activo) se
/// serializará con sus propios bloques cuando existan (`docs/guides/roadmap/ROADMAP_RPG.md`,
/// M5); el contenido (catálogos, tablas) nunca se guarda: es fichero del juego.
///
/// Verificación: HOST-457. NO VERIFICADA por demo (vertical slice en
/// `docs/guides/roadmap/ROADMAP_RPG.md` M6).

#include <eng/core/types/types.hpp>
#include <eng/core/util/binary.hpp>
#include <eng/rpg/clock.hpp>
#include <eng/rpg/equipment.hpp>
#include <eng/rpg/facts.hpp>
#include <eng/rpg/inventory.hpp>
#include <eng/rpg/items.hpp>
#include <eng/rpg/stats.hpp>

namespace eng::rpg {

/// Versión del bloque de partida del núcleo RPG. Sube cuando cambia el formato; un save de
/// otra versión se rechaza en `load_all` (el juego decide migrar o descartar).
inline constexpr eng::u8 kRpgSaveVersion = 1u;

/// Serializa el almacén de hechos: contador `u16` + pares `(id, valor)` `u16` en orden
/// ascendente de id. `false` si el buffer no tiene sitio.
template <eng::u16 MaxFacts>
[[nodiscard]] bool save_fact_store(const FactStore<MaxFacts>& facts,
				   eng::util::ByteWriter& writer) noexcept {
	if (!writer.write_u16(facts.present_count())) {
		return false;
	}
	bool ok = true;
	facts.for_each_present([&writer, &ok](FactId id, eng::u16 value) {
		if (ok && (!writer.write_u16(id) || !writer.write_u16(value))) {
			ok = false;
		}
	});
	return ok;
}

/// Carga un almacén de hechos escrito por `save_fact_store`. `false` si los bytes faltan o
/// algún id no existe en este contenido (save de otra partida/contenido). En fallo, el
/// almacén queda a medio cargar: el juego descarta y recarga.
template <eng::u16 MaxFacts>
[[nodiscard]] bool load_fact_store(FactStore<MaxFacts>& facts,
				   eng::util::ByteReader& reader) noexcept {
	eng::u16 count = 0u;
	if (!reader.read_u16(count)) {
		return false;
	}
	facts.clear();
	for (eng::u16 i = 0; i < count; ++i) {
		eng::u16 id = 0u;
		eng::u16 value = 0u;
		if (!reader.read_u16(id) || !reader.read_u16(value)) {
			return false;
		}
		if (!facts.set(id, value)) {
			return false;
		}
	}
	facts.clear_changed(); // recién cargado: nada "cambió" en esta sesión
	return true;
}

/// Serializa las instancias vivas del pool: contador `u8` + por hueco `slot`, `generación` e
/// instancia (8 B). Recorre en orden ascendente de hueco: formato determinista.
template <eng::u8 MaxItems>
[[nodiscard]] bool save_item_store(const ItemStore<MaxItems>& store,
				   eng::util::ByteWriter& writer) noexcept {
	if (!writer.write_u8(store.live_count())) {
		return false;
	}
	bool ok = true;
	store.for_each_live([&writer, &ok](ItemHandle handle, const ItemInstance& inst) {
		if (!ok) {
			return;
		}
		ok = writer.write_u8(handle.slot) && writer.write_u8(handle.generation) &&
		     writer.write_u8(inst.type) && writer.write_u8(inst.qty) &&
		     writer.write_u8(inst.durability) && writer.write_u8(inst.upgrade) &&
		     writer.write_u16(inst.affixes) && writer.write_u16(inst.metadata);
	});
	return ok;
}

/// Carga instancias escritas por `save_item_store` restaurando slot y generación exactos
/// (los handles guardados en inventario/equipo siguen valiendo tras cargar).
template <eng::u8 MaxItems>
[[nodiscard]] bool load_item_store(ItemStore<MaxItems>& store,
				   eng::util::ByteReader& reader) noexcept {
	eng::u8 count = 0u;
	if (!reader.read_u8(count)) {
		return false;
	}
	store.clear();
	for (eng::u8 i = 0; i < count; ++i) {
		eng::u8 slot = 0u;
		eng::u8 generation = 0u;
		ItemInstance inst {};
		if (!reader.read_u8(slot) || !reader.read_u8(generation) ||
		    !reader.read_u8(inst.type) || !reader.read_u8(inst.qty) ||
		    !reader.read_u8(inst.durability) || !reader.read_u8(inst.upgrade) ||
		    !reader.read_u16(inst.affixes) || !reader.read_u16(inst.metadata)) {
			return false;
		}
		if (!store.restore(slot, generation, inst)) {
			return false; // hueco duplicado o fuera de rango: save corrupto
		}
	}
	return true;
}

/// Serializa el inventario: contador `u8` + pares `(slot, generación)` por entrada.
template <eng::u8 Slots>
[[nodiscard]] bool save_bag(const ItemBag<Slots>& bag, eng::util::ByteWriter& writer) noexcept {
	if (!writer.write_u8(bag.items.size())) {
		return false;
	}
	for (eng::usize i = 0; i < bag.items.size(); ++i) {
		if (!writer.write_u8(bag.items[i].slot) || !writer.write_u8(bag.items[i].generation)) {
			return false;
		}
	}
	return true;
}

/// Carga un inventario (los handles deben apuntar a instancias ya cargadas; `load_all` lo
/// verifica). Un contador mayor que la capacidad es un save corrupto.
template <eng::u8 Slots>
[[nodiscard]] bool load_bag(ItemBag<Slots>& bag, eng::util::ByteReader& reader) noexcept {
	eng::u8 count = 0u;
	if (!reader.read_u8(count) || count > Slots) {
		return false;
	}
	bag.items.clear();
	for (eng::u8 i = 0; i < count; ++i) {
		ItemHandle handle {};
		if (!reader.read_u8(handle.slot) || !reader.read_u8(handle.generation)) {
			return false;
		}
		if (!bag.items.push_back(handle)) {
			return false;
		}
	}
	return true;
}

/// Serializa las ranuras de equipo (solo handles: `required_tags` es configuración).
template <eng::u8 Slots>
[[nodiscard]] bool save_equipment(const Equipment<Slots>& equipment,
				  eng::util::ByteWriter& writer) noexcept {
	for (eng::u8 slot = 0; slot < Slots; ++slot) {
		if (!writer.write_u8(equipment.slots[slot].slot) ||
		    !writer.write_u8(equipment.slots[slot].generation)) {
			return false;
		}
	}
	return true;
}

/// Carga las ranuras de equipo (no toca `required_tags`).
template <eng::u8 Slots>
[[nodiscard]] bool load_equipment(Equipment<Slots>& equipment,
				  eng::util::ByteReader& reader) noexcept {
	for (eng::u8 slot = 0; slot < Slots; ++slot) {
		ItemHandle handle {};
		if (!reader.read_u8(handle.slot) || !reader.read_u8(handle.generation)) {
			return false;
		}
		equipment.slots[slot] = handle;
	}
	return true;
}

/// Serializa los atributos **base** (la derivada es caché y se recalcula al cargar).
template <eng::u8 MaxStats>
[[nodiscard]] bool save_stats(const StatSheet<MaxStats>& sheet,
			      eng::util::ByteWriter& writer) noexcept {
	for (eng::u8 s = 0; s < MaxStats; ++s) {
		if (!writer.write_s16(sheet.base[s])) {
			return false;
		}
	}
	return true;
}

/// Carga los atributos base y marca la derivada como pendiente de recalcular.
template <eng::u8 MaxStats>
[[nodiscard]] bool load_stats(StatSheet<MaxStats>& sheet, eng::util::ByteReader& reader) noexcept {
	for (eng::u8 s = 0; s < MaxStats; ++s) {
		if (!reader.read_s16(sheet.base[s])) {
			return false;
		}
	}
	sheet.derived_valid = false;
	return true;
}

/// Serializa el reloj de mundo: día `u16` + hora `u8` + minuto `u8`.
[[nodiscard]] inline bool save_clock(const WorldClock& clock, eng::util::ByteWriter& writer) noexcept {
	return writer.write_u16(clock.day) && writer.write_u8(clock.hour) &&
	       writer.write_u8(clock.minute);
}

/// Carga el reloj de mundo.
[[nodiscard]] inline bool load_clock(WorldClock& clock, eng::util::ByteReader& reader) noexcept {
	return reader.read_u16(clock.day) && reader.read_u8(clock.hour) &&
	       reader.read_u8(clock.minute);
}

/// Guarda la **partida** completa del núcleo: versión + hechos + instancias + inventario +
/// equipo + atributos + reloj (cada bloque con su verificación de espacio). El catálogo no se
/// serializa: es contenido del juego (fichero en disco), no estado de partida.
template <eng::u16 MaxFacts, eng::u8 BagSlots, eng::u8 EquipSlots, eng::u8 MaxItems,
	  eng::u8 MaxStats>
[[nodiscard]] bool save_all(const FactStore<MaxFacts>& facts, const ItemStore<MaxItems>& items,
			    const ItemBag<BagSlots>& bag, const Equipment<EquipSlots>& equipment,
			    const StatSheet<MaxStats>& stats, const WorldClock& clock,
			    eng::util::ByteWriter& writer) noexcept {
	return writer.write_u8(kRpgSaveVersion) && save_fact_store(facts, writer) &&
	       save_item_store(items, writer) && save_bag(bag, writer) &&
	       save_equipment(equipment, writer) && save_stats(stats, writer) &&
	       save_clock(clock, writer);
}

/// Carga una partida escrita por `save_all` y valida la coherencia: handles de inventario y
/// equipo deben existir en el pool cargado. `false` ante versión distinta, bytes truncados o
/// handles colgantes; en ese caso el juego descarta el estado parcial.
template <eng::u16 MaxFacts, eng::u8 BagSlots, eng::u8 EquipSlots, eng::u8 MaxItems,
	  eng::u8 MaxStats>
[[nodiscard]] bool load_all(FactStore<MaxFacts>& facts, ItemStore<MaxItems>& items,
			    ItemBag<BagSlots>& bag, Equipment<EquipSlots>& equipment,
			    StatSheet<MaxStats>& stats, WorldClock& clock,
			    eng::util::ByteReader& reader) noexcept {
	eng::u8 version = 0u;
	if (!reader.read_u8(version) || version != kRpgSaveVersion) {
		return false;
	}
	if (!load_fact_store(facts, reader) || !load_item_store(items, reader) ||
	    !load_bag(bag, reader) || !load_equipment(equipment, reader) ||
	    !load_stats(stats, reader) || !load_clock(clock, reader)) {
		return false;
	}
	// Coherencia referencial: el save no puede dejar handles colgando en inventario/equipo.
	for (eng::usize i = 0; i < bag.items.size(); ++i) {
		if (!items.get(bag.items[i]).valid()) {
			return false;
		}
	}
	for (eng::u8 slot = 0; slot < EquipSlots; ++slot) {
		const ItemHandle h = equipment.slots[slot];
		if (h.valid() && !items.get(h).valid()) {
			return false;
		}
	}
	return true;
}

} // namespace eng::rpg
