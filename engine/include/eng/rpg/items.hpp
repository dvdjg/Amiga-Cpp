#pragma once

/// \file items.hpp
/// **Objetos con instancia** (`eng::rpg`): el modelo de objetos del jugador, más rico que el
/// del ecosistema (`eng::sim::Inventory` guarda pilas `(ItemKind, cantidad)` para la IA de
/// criaturas; aquí un objeto tiene identidad, durabilidad, mejora, afijos y metadatos —
/// ver `docs/engine/architecture/RPG_LAYER.md` §Qué se reutiliza).
///
/// Tres piezas:
/// - `ItemDef`: la fila del **catálogo** (contenido del juego, típicamente en ROM): etiquetas,
///   peso, tamaño de pila, durabilidad, efecto de uso y rango de modificadores de atributo.
/// - `ItemInstance`: 8 bytes de estado mutable (cantidad, durabilidad, mejora, afijos,
///   metadatos) — "esta espada concreta", no "una espada".
/// - `ItemStore`: pool de instancias de capacidad fija con huecos reciclados y **handles con
///   generación** (`ItemHandle`, 2 bytes): reutilizar un hueco invalida los handles viejos
///   (mismo patrón que `TaskHandle`/`TimerHandle` del engine), de modo que una referencia
///   muerta nunca apunta a otra instancia distinta.
///
/// Verificación: HOST-455. NO VERIFICADA por demo (vertical slice en
/// `docs/guides/roadmap/ROADMAP_RPG.md` M6).

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/rpg/types.hpp>

namespace eng::rpg {

/// Referencia a una instancia del `ItemStore`. `slot` indexa el hueco; `generation` distingue
/// su vida al reciclarlo. `{0xff, 0}` = sin objeto (el default).
struct ItemHandle {
	eng::u8 slot = 0xffu;      ///< índice del hueco (0xff = ninguno)
	eng::u8 generation = 0u;   ///< vida del hueco (0 = inválido; sube al liberar/reutilizar)

	/// ¿El handle puede apuntar a una instancia? (la liveness la confirma el store).
	[[nodiscard]] constexpr bool valid() const noexcept {
		return slot != 0xffu && generation != 0u;
	}

	/// Igualdad por contenido: mismo hueco y misma generación (identidad de instancia).
	[[nodiscard]] friend constexpr bool operator==(ItemHandle a, ItemHandle b) noexcept {
		return a.slot == b.slot && a.generation == b.generation;
	}

	/// Desigualdad (negación de la igualdad).
	[[nodiscard]] friend constexpr bool operator!=(ItemHandle a, ItemHandle b) noexcept {
		return !(a == b);
	}
};

/// Handle vacío (sin objeto); alias del default para legibilidad en firmas.
inline constexpr ItemHandle no_item {};

/// Definición de tipo de objeto (una fila del catálogo). Solo describe el *tipo*; el estado
/// mutable vive en `ItemInstance`. Layout pensado para tabla compacta leíble por CPU.
struct ItemDef {
	eng::u16 tags = 0u;            ///< máscara de etiquetas (`item_tags`); las reglas consultan capacidades
	eng::u8 weight = 0u;           ///< peso por unidad en unidad de juego (0 = sin peso)
	eng::u8 stack_max = 1u;        ///< máximo por pila (1 = no apilable)
	eng::u8 durability_max = 0u;   ///< usos máximos (0 = indestructible / sin durabilidad)
	eng::u8 use_effect = 0u;       ///< id de efecto de uso (comer, leer, abrir; 0 = ninguno)
	eng::u8 mod_first = 0u;        ///< primer índice en la tabla de `StatMod` del juego
	eng::u8 mod_count = 0u;        ///< nº de `StatMod` de este tipo (equipo/materiales)
};

/// Instancia de objeto (8 bytes): "esta espada", no "una espada". Dos instancias del mismo
/// tipo con el mismo estado son apilables (`stack_compatible`); una mejora o un afijo las
/// separa.
struct ItemInstance {
	ItemTypeId type = no_item_type; ///< tipo (índice al `ItemCatalog`)
	eng::u8 qty = 0u;               ///< unidades de la pila (1..stack_max; 0 = consumida)
	eng::u8 durability = 0u;        ///< usos restantes (0 = sin durabilidad; con `durability_max`, desgaste)
	eng::u8 upgrade = 0u;           ///< nivel de mejora (0 = sin mejorar; forja/encantamiento)
	eng::u16 affixes = 0u;          ///< máscara de afijos permanentes (bit i = afijo i del juego)
	eng::u16 metadata = 0u;         ///< dato del juego por instancia (llave→puerta, firma del dueño, variante)
};

/// Catálogo de tipos de objeto: tabla de contenido con capacidad fija (normalmente se llena
/// una vez en el setup desde ROM y no se toca más).
template <eng::u8 MaxTypes>
struct ItemCatalog {
	static_assert(MaxTypes >= 1u, "ItemCatalog: capacidad de al menos un tipo");

	eng::u8 count = 0u;                         ///< tipos registrados (los válidos son 0..count-1)
	eng::util::Array<ItemDef, MaxTypes> defs {};///< definiciones (las no registradas quedan a cero)

	/// ¿`type` está registrado?
	[[nodiscard]] constexpr bool valid(ItemTypeId type) const noexcept {
		return type < count;
	}

	/// Definición de un tipo registrado; `nullptr` si no existe (precondición: `valid`).
	[[nodiscard]] constexpr eng::Ref<const ItemDef> at(ItemTypeId type) const noexcept {
		return valid(type) ? &defs[type] : nullptr;
	}

	/// Registra una definición nueva. Devuelve su `ItemTypeId` o `no_item_type` si está lleno.
	[[nodiscard]] ItemTypeId add(const ItemDef& def) noexcept {
		if (count >= MaxTypes) {
			return no_item_type;
		}
		defs[count] = def;
		const ItemTypeId id = count;
		++count;
		return id;
	}
};

/// Pool de instancias de objetos con capacidad fija. Sin heap: `MaxItems` huecos (≤ 254)
/// se reciclan al destruir, subiendo la generación para invalidar handles viejos.
template <eng::u8 MaxItems>
class ItemStore {
public:
	static_assert(MaxItems >= 1u && MaxItems <= 0xfeu,
		      "ItemStore: hasta 254 huecos (0xff está reservado para 'sin objeto')");

	/// Capacidad de instancias simultáneas.
	[[nodiscard]] static constexpr eng::u8 capacity() noexcept { return MaxItems; }

	/// Crea una instancia en el primer hueco libre (recicla el hueco con generación nueva).
	/// Devuelve un handle inválido si el pool está lleno.
	[[nodiscard]] ItemHandle spawn(const ItemInstance& inst) noexcept {
		for (eng::u8 i = 0; i < MaxItems; ++i) {
			Slot& s = m_slots[i];
			if (s.live) {
				continue;
			}
			if (s.generation == 0u) {
				s.generation = 1u; // primer uso del hueco
			}
			s.inst = inst;
			s.live = true;
			++m_live;
			return ItemHandle {i, s.generation};
		}
		return ItemHandle {};
	}

	/// Instancia viva de `h`; `nullptr` si el handle es inválido o caducó (hueco reciclado).
	[[nodiscard]] eng::Ref<ItemInstance> get(ItemHandle h) noexcept {
		if (h.slot >= MaxItems) {
			return nullptr;
		}
		Slot& s = m_slots[h.slot];
		if (!s.live || s.generation != h.generation) {
			return nullptr;
		}
		return s.inst;
	}

	/// Versión de solo lectura de `get`.
	[[nodiscard]] eng::Ref<const ItemInstance> get(ItemHandle h) const noexcept {
		if (h.slot >= MaxItems) {
			return nullptr;
		}
		const Slot& s = m_slots[h.slot];
		if (!s.live || s.generation != h.generation) {
			return nullptr;
		}
		return s.inst;
	}

	/// Destruye la instancia de `h` (el hueco pasa a libre y su generación sube). `false` si
	/// el handle ya era inválido.
	bool destroy(ItemHandle h) noexcept {
		if (h.slot >= MaxItems) {
			return false;
		}
		Slot& s = m_slots[h.slot];
		if (!s.live || s.generation != h.generation) {
			return false;
		}
		s.live = false;
		s.generation = next_generation(s.generation);
		--m_live;
		return true;
	}

	/// Instancias vivas (telemetría, presupuesto de contenido).
	[[nodiscard]] eng::u8 live_count() const noexcept { return m_live; }

	/// Vuelve al pool vacío (partida nueva o carga de partida antes de restaurar).
	void clear() noexcept {
		for (eng::u8 i = 0; i < MaxItems; ++i) {
			m_slots[i].inst = ItemInstance {};
			m_slots[i].generation = 0u;
			m_slots[i].live = false;
		}
		m_live = 0u;
	}

	/// Restaura una instancia exacta (slot + generación) durante la carga de partida. Solo
	/// para `eng/rpg/save.hpp`: los jets de gameplay usan `spawn`.
	[[nodiscard]] bool restore(eng::u8 slot, eng::u8 generation, const ItemInstance& inst) noexcept {
		if (slot >= MaxItems || generation == 0u) {
			return false;
		}
		Slot& s = m_slots[slot];
		if (s.live) {
			return false; // hueco ya ocupado: save corrupto o doble carga
		}
		s.inst = inst;
		s.generation = generation;
		s.live = true;
		++m_live;
		return true;
	}

	/// Recorre los huecos vivos en orden ascendente, llamando `fn(handle, instancia)`.
	/// Lo usan la serialización (`save.hpp`) y las consultas de juego.
	template <class Fn>
	void for_each_live(Fn&& fn) const {
		for (eng::u8 i = 0; i < MaxItems; ++i) {
			const Slot& s = m_slots[i];
			if (s.live) {
				fn(ItemHandle {i, s.generation}, s.inst);
			}
		}
	}

private:
	/// Hueco del pool: instancia + generación (vida del hueco) + liveness.
	struct Slot {
		ItemInstance inst {};    ///< instancia almacenada (indefinida si `!live`)
		eng::u8 generation = 0u; ///< vida del hueco (0 = virgen; sube al liberar)
		bool live = false;       ///< ¿hay instancia viva en el hueco?
	};

	/// Siguiente generación de un hueco, saltando el 0 (reservado a "inválido").
	[[nodiscard]] static eng::u8 next_generation(eng::u8 generation) noexcept {
		const eng::u8 next = generation + 1u;
		return next == 0u ? 1u : next;
	}

	Slot m_slots[MaxItems] {}; ///< huecos del pool
	eng::u8 m_live = 0u;       ///< instancias vivas (capacidad rápida de consulta)
};

/// ¿La instancia satisface la consulta `(type, tags)`? `type != no_item_type` exige ese tipo
/// exacto; `tags != 0` exige que la definición los cubra todos. Es la consulta que usan el
/// inventario (recetas, llaves) y el comercio: capacidades, no clases.
[[nodiscard]] constexpr bool item_matches(const ItemInstance& inst, const ItemDef& def,
					  ItemTypeId type, eng::u16 tags) noexcept {
	return (type == no_item_type || inst.type == type) && has_tags(def.tags, tags);
}

/// ¿Dos instancias son la misma pila? Mismo tipo y mismo estado: una espada encantada o
/// desgastada no se funde con una lisa (evita perder afijos o repartir durabilidad).
[[nodiscard]] constexpr bool stack_compatible(const ItemInstance& a, const ItemInstance& b) noexcept {
	return a.type == b.type && a.durability == b.durability && a.upgrade == b.upgrade &&
	       a.affixes == b.affixes && a.metadata == b.metadata;
}

/// Fusiona `src` en `dst` si el tipo es apilable (`stack_max > 1`) y son compatibles: mueve
/// unidades hasta llenar la pila. Devuelve las unidades que quedan en `src` (0 = fusionado
/// del todo y `src` debe retirarse). No destruye nada: el llamador decide.
[[nodiscard]] constexpr eng::u8 merge_stack(ItemInstance& dst, ItemInstance& src,
					    const ItemDef& def) noexcept {
	if (def.stack_max <= 1u || !stack_compatible(dst, src)) {
		return src.qty;
	}
	const eng::u8 space = def.stack_max > dst.qty ? def.stack_max - dst.qty : 0u;
	const eng::u8 moved = space < src.qty ? space : src.qty;
	dst.qty = dst.qty + moved;
	src.qty = src.qty - moved;
	return src.qty;
}

} // namespace eng::rpg
