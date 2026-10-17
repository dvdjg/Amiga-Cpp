#pragma once

/// \file world.hpp
/// **Entidades de mundo** (`eng::rpg`): actores, cofres, escenarios, disparadores y monturas
/// con la composición mínima de un CRPG. Diseño en `docs/engine/architecture/RPG_LAYER.md`
/// §5.1 y `docs/guides/roadmap/ROADMAP_RPG.md` (hito M1).
///
/// Decisiones:
/// - **Una `struct` con `kind` + máscara de verbos + índice de componente**, no un ECS
///   genérico: lo que un RPG necesita por entidad cabe en 10 bytes y se serializa tal cual.
/// - `data` es el **índice de la tabla del componente por tipo** (p. ej. contenedor o
///   cerradura) o un parámetro libre del juego: evita un `union` de capacidades y mantiene
///   la entidad POD.
/// - Los ids siguen el convenio de `eng::sim`: **monótonos y no reutilizados** (`0xffff` =
///   ninguno); el hueco del pool sí se recicla, con id nuevo.
///
/// `GroundItems` es la lista de objetos con posición (suelo o botín): el puente entre la
/// instancia del `ItemStore` y el mapa. Las operaciones de interacción viven en
/// `interaction.hpp`.
///
/// Verificación: HOST-458. NO VERIFICADA por demo (vertical slice en el roadmap, M6).

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/rpg/items.hpp>

namespace eng::rpg {

/// Identificador de entidad de mundo (personajes, cofres, puertas, estaciones de trabajo,
/// monturas). `u16`: miles de entidades por mundo sin apretar el presupuesto.
using EntityId = eng::u16;

/// Valor "sin entidad".
inline constexpr EntityId no_entity = 0xffffu;

/// Tipo de entidad: qué *es* en el mundo (los componentes y verbos concretos van aparte).
enum class EntityKind : eng::u8 {
	Actor = 0,   ///< personaje (jugador o NPC)
	Container,   ///< cofre, barril, mochila de suelo
	Scenery,     ///< yunque, fogata, puerta, cartel
	Trigger,     ///< volumen de evento (entrada de región, trampa)
	Mount,       ///< montura
	Count,
};

/// **Verbos de interacción** (máscara de `WorldEntity::verbs`): qué se puede hacer con la
/// entidad. Las reglas de UI y el sistema de intenciones consultan capacidades, no el tipo.
namespace interaction {
inline constexpr eng::u16 inspect = 1u << 0u; ///< mirar (descripción)
inline constexpr eng::u16 take = 1u << 1u;    ///< coger del contenedor/suelo
inline constexpr eng::u16 open = 1u << 2u;    ///< abrir (puerta, cofre)
inline constexpr eng::u16 talk = 1u << 3u;    ///< hablar (diálogo)
inline constexpr eng::u16 use = 1u << 4u;     ///< usar (estación de trabajo, mecanismo)
inline constexpr eng::u16 rest = 1u << 5u;    ///< descansar/dormir (cama, fogata)
inline constexpr eng::u16 steal = 1u << 6u;   ///< intentar robar (propiedad ajena)
inline constexpr eng::u16 mount = 1u << 7u;   ///< montar
} // namespace interaction

/// Banderas de estado de la entidad (utilidad del juego: colisión, visibilidad).
namespace entity_flags {
inline constexpr eng::u8 hidden = 0x01u; ///< no se dibuja
inline constexpr eng::u8 solid = 0x02u;  ///< bloquea el paso
} // namespace entity_flags

/// Entidad de mundo (10 bytes). `kind` dice qué es; `verbs` qué se puede hacer; `data`
/// indexa la tabla del componente por tipo (contenedor, cerradura…) o es un parámetro
/// libre del juego (p. ej. variante visual).
struct WorldEntity {
	EntityId id = no_entity;            ///< identidad estable (no se reutiliza)
	EntityKind kind = EntityKind::Scenery; ///< clase de entidad
	eng::u16 verbs = 0u;                ///< máscara de verbos disponibles (`interaction`)
	eng::s16 x = 0;                     ///< posición en el mapa (unidades del juego)
	eng::s16 y = 0;                     ///< posición en el mapa (unidades del juego)
	eng::u8 room = 0u;                  ///< región/celda del mapa
	eng::u8 data = 0u;                  ///< índice de componente por tipo o parámetro libre
	eng::u8 flags = 0u;                 ///< máscara de estado (`entity_flags`)
};

/// Pool de entidades de capacidad fija, sin heap. Los huecos se reciclan con **id nuevo**
/// (los `EntityId` viejos dejan de existir: no hay handles colgantes silenciosos).
template <eng::u8 MaxEntities>
class EntityStore {
public:
	static_assert(MaxEntities >= 1u && MaxEntities <= 0xfeu,
		      "EntityStore: hasta 254 entidades (no_entity está reservado)");

	/// Capacidad de entidades simultáneas.
	[[nodiscard]] static constexpr eng::u8 capacity() noexcept { return MaxEntities; }

	/// Crea una entidad en el primer hueco libre. Devuelve `no_entity` si está lleno.
	[[nodiscard]] EntityId spawn(EntityKind kind, eng::s16 x, eng::s16 y, eng::u8 room = 0u,
				     eng::u16 verbs = 0u, eng::u8 flags = 0u) noexcept {
		for (eng::u8 i = 0; i < MaxEntities; ++i) {
			Slot& s = m_slots[i];
			if (s.live) {
				continue;
			}
			s.e = WorldEntity {m_next_id, kind, verbs, x, y, room, 0u, flags};
			s.live = true;
			++m_live;
			++m_next_id;
			return s.e.id;
		}
		return no_entity;
	}

	/// Entidad viva de `id`; `nullptr` si no existe.
	[[nodiscard]] eng::Ref<WorldEntity> find(EntityId id) noexcept {
		eng::Ref<Slot> s = slot_of(id);
		return s.valid() ? eng::Ref<WorldEntity>(s->e) : nullptr;
	}

	/// Versión de solo lectura de `find`.
	[[nodiscard]] eng::Ref<const WorldEntity> find(EntityId id) const noexcept {
		eng::Ref<const Slot> s = slot_of(id);
		return s.valid() ? eng::Ref<const WorldEntity>(s->e) : nullptr;
	}

	/// Elimina la entidad (el id deja de existir; el hueco se recicla). `false` si no existe.
	bool remove(EntityId id) noexcept {
		eng::Ref<Slot> s = slot_of(id);
		if (!s.valid()) {
			return false;
		}
		s->live = false;
		--m_live;
		return true;
	}

	/// Entidades vivas (telemetría y presupuesto de contenido).
	[[nodiscard]] eng::u8 live_count() const noexcept { return m_live; }

	/// Vacía el pool (partida nueva). Los ids reinician su cuenta.
	void clear() noexcept {
		for (eng::u8 i = 0; i < MaxEntities; ++i) {
			m_slots[i].live = false;
			m_slots[i].e = WorldEntity {};
		}
		m_live = 0u;
		m_next_id = 1u;
	}

	/// Recorre las entidades vivas llamando `fn(WorldEntity&)` (mutación permitida).
	template <class Fn>
	void for_each(Fn&& fn) {
		for (eng::u8 i = 0; i < MaxEntities; ++i) {
			if (m_slots[i].live) {
				fn(m_slots[i].e);
			}
		}
	}

	/// Recorrido de solo lectura de las entidades vivas.
	template <class Fn>
	void for_each(Fn&& fn) const {
		for (eng::u8 i = 0; i < MaxEntities; ++i) {
			if (m_slots[i].live) {
				fn(m_slots[i].e);
			}
		}
	}

private:
	/// Hueco del pool: entidad + liveness (el id vive dentro de la entidad).
	struct Slot {
		WorldEntity e {};   ///< entidad almacenada (indefinida si `!live`)
		bool live = false;  ///< ¿hay entidad viva en el hueco?
	};

	/// Hueco de la entidad `id`, o `nullptr` si no existe viva.
	[[nodiscard]] eng::Ref<Slot> slot_of(EntityId id) noexcept {
		for (eng::u8 i = 0; i < MaxEntities; ++i) {
			if (m_slots[i].live && m_slots[i].e.id == id) {
				return m_slots[i];
			}
		}
		return nullptr;
	}

	/// Versión de solo lectura de `slot_of` (para `find` const).
	[[nodiscard]] eng::Ref<const Slot> slot_of(EntityId id) const noexcept {
		for (eng::u8 i = 0; i < MaxEntities; ++i) {
			if (m_slots[i].live && m_slots[i].e.id == id) {
				return m_slots[i];
			}
		}
		return nullptr;
	}

	Slot m_slots[MaxEntities] {}; ///< huecos del pool
	eng::u8 m_live = 0u;          ///< entidades vivas
	eng::u16 m_next_id = 1u;      ///< siguiente id (monótono; 0xffff reservado)
};

/// Objeto (instancia del `ItemStore`) colocado en el mundo: suelo, botín de un cofre
/// derramado, recompensa. La posición es en unidades del juego, como la de las entidades.
template <eng::u16 MaxGround>
class GroundItems {
public:
	static_assert(MaxGround >= 1u, "GroundItems: capacidad de al menos un objeto");

	/// Entrada del suelo: instancia + posición.
	struct Entry {
		ItemHandle item {}; ///< instancia viva del `ItemStore`
		eng::s16 x = 0;     ///< posición en el mapa
		eng::s16 y = 0;     ///< posición en el mapa
		eng::u8 room = 0u;  ///< región/celda
	};

	/// Coloca `item` en el suelo. `false` si la lista está llena.
	bool drop(ItemHandle item, eng::s16 x, eng::s16 y, eng::u8 room = 0u) noexcept {
		return m_entries.push_back(Entry {item, x, y, room});
	}

	/// Extrae la entrada `index` (no destruye la instancia). `no_item` si el índice no existe.
	[[nodiscard]] ItemHandle take_at(eng::usize index) noexcept {
		if (index >= m_entries.size()) {
			return no_item;
		}
		const ItemHandle h = m_entries[index].item;
		m_entries.erase(index);
		return h;
	}

	/// Nº de entradas.
	[[nodiscard]] eng::usize size() const noexcept { return m_entries.size(); }

	/// ¿Está llena?
	[[nodiscard]] bool full() const noexcept { return m_entries.full(); }

	/// Entrada `index` (precondición: `index < size()`).
	[[nodiscard]] Entry& at(eng::usize index) noexcept { return m_entries[index]; }

	/// Versión de solo lectura de `at`.
	[[nodiscard]] const Entry& at(eng::usize index) const noexcept { return m_entries[index]; }

	/// Vacía la lista (no destruye instancias: el juego decide qué hacer con ellas).
	void clear() noexcept { m_entries.clear(); }

private:
	eng::util::StaticVector<Entry, MaxGround> m_entries {}; ///< entradas del suelo
};

} // namespace eng::rpg
