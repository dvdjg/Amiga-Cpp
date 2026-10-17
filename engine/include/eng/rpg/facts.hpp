#pragma once

/// \file facts.hpp
/// **Hechos del mundo** (`eng::rpg`): el *blackboard* persistente de estado de historia del
/// que cuelgan quests, diálogos y reglas sociales.
///
/// Un hecho es una clave densa (`FactId`, asignada por el `enum` de contenido del juego) con
/// un valor `u16` (bool, contador, `EntityId`…). Las quests **no se llaman entre sí**: matar
/// al dragón escribe el hecho `dragon_dead`, y la puerta del pueblo, el comerciante y la
/// guardia reaccionan sin conocerse. Ese es el pegamento de un RPG "vivo".
///
/// **No confundir con los hechos del planificador**: `eng::ai`/`eng::sim` usan `Fact` como
/// índice de un `BitSet` local a un agente y efímero (estado de una búsqueda GOAP). El
/// `FactStore` es estado de partida, serializable y compartido por todos los sistemas.
///
/// El cambio se rastrea en dos niveles (evita reevaluar todas las quests cada frame):
/// - `epoch()`: contador global que sube con cada cambio efectivo — un consumidor que guarda
///   su último epoch puede salir en O(1) si nada cambió;
/// - `changed(id)`/`any_changed()`: bits de "tocado desde la última pasada" que el consumidor
///   limpia con `clear_changed()`.
///
/// Verificación: HOST-454. NO VERIFICADA por demo (vertical slice en
/// `docs/guides/roadmap/ROADMAP_RPG.md` M6).

#include <eng/core/types/types.hpp>
#include <eng/core/util/bitset.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/rpg/types.hpp>

namespace eng::rpg {

/// Almacén denso de hechos de mundo. `MaxFacts` es el número de claves del contenido
/// (enum del juego); las operaciones son O(1) sobre bits y valores de 16 bits.
template <eng::u16 MaxFacts>
class FactStore {
public:
	static_assert(MaxFacts >= 1u && MaxFacts <= 0xfffeu,
		      "FactStore: capacidad razonable de hechos (1..65534)");

	/// Capacidad de claves (tamaño del enum de hechos del juego).
	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return MaxFacts; }

	/// Escribe `value` en `id`. Solo cuenta como cambio si el valor difiere (una quest
	/// suscrita al hecho no se reevalúa por reescribir el mismo valor). `false` si `id`
	/// no existe en este almacén.
	bool set(FactId id, eng::u16 value) noexcept {
		if (id >= MaxFacts) {
			return false;
		}
		const bool first = !m_present.test(id);
		if (!first && m_values[id] == value) {
			return true; // sin cambio efectivo: no toca epoch ni dirty
		}
		m_values[id] = value;
		m_present.set(id);
		m_changed.set(id);
		++m_epoch;
		return true;
	}

	/// Marca el hecho como presente con valor 1 (bandera de historia típica).
	bool set_flag(FactId id) noexcept { return set(id, 1u); }

	/// Elimina `id` (pasa a "ausente"; `value` devolverá 0). Cuenta como cambio solo si
	/// estaba presente. `false` si `id` no existe en este almacén.
	bool clear(FactId id) noexcept {
		if (id >= MaxFacts) {
			return false;
		}
		if (!m_present.test(id)) {
			return true; // ya ausente: sin cambio efectivo
		}
		m_present.reset(id);
		m_values[id] = 0u;
		m_changed.set(id);
		++m_epoch;
		return true;
	}

	/// Suma saturada `delta` al valor (contadores: reputación acumulada, objetos reunidos).
	/// Un hecho ausente parte de 0 y pasa a presente; nunca desborda (`sat_add`).
	bool add(FactId id, eng::u16 delta) noexcept {
		if (id >= MaxFacts) {
			return false;
		}
		const eng::u16 next = eng::util::sat_add(m_values[id], delta);
		if (next == m_values[id] && m_present.test(id)) {
			return true; // saturado: sin cambio efectivo
		}
		m_values[id] = next;
		m_present.set(id);
		m_changed.set(id);
		++m_epoch;
		return true;
	}

	/// ¿El hecho está presente?
	[[nodiscard]] bool has(FactId id) const noexcept {
		return id < MaxFacts && m_present.test(id);
	}

	/// Valor del hecho (0 si ausente; usa `has` para distinguir "0" de "sin hecho").
	[[nodiscard]] eng::u16 value(FactId id) const noexcept {
		return id < MaxFacts ? m_values[id] : 0u;
	}

	/// Valor del hecho con respaldo explícito si la clave es ausente o inválida.
	[[nodiscard]] eng::u16 value_or(FactId id, eng::u16 fallback) const noexcept {
		return has(id) ? m_values[id] : fallback;
	}

	/// Contador global de cambios efectivos (envuelve a los 2^32; **comparar por
	/// desigualdad**, nunca por ">"): los consumidores guardan su último epoch y salen en
	/// O(1) cuando no hay nada nuevo que evaluar.
	[[nodiscard]] eng::u32 epoch() const noexcept { return m_epoch; }

	/// ¿`id` cambió desde el último `clear_changed()`?
	[[nodiscard]] bool changed(FactId id) const noexcept {
		return id < MaxFacts && m_changed.test(id);
	}

	/// ¿Cambió algún hecho desde el último `clear_changed()`? (reaseguro de bajo coste).
	[[nodiscard]] bool any_changed() const noexcept { return m_changed.any(); }

	/// Limpia los bits de cambio (lo llama el sistema que ya procesó el frame). No toca
	/// `epoch`: los consumidores con snapshot propio no dependen de esta limpieza.
	void clear_changed() noexcept { m_changed.reset(); }

	/// Número de hechos presentes (telemetría, save y tests).
	[[nodiscard]] eng::u16 present_count() const noexcept {
		return m_present.count();
	}

	/// Vuelve al estado "todo ausente, epoch 0" (partida nueva o carga).
	void clear() noexcept {
		m_present.reset();
		m_changed.reset();
		for (eng::u16 i = 0; i < MaxFacts; ++i) {
			m_values[i] = 0u;
		}
		m_epoch = 0u;
	}

	/// Recorre los hechos presentes en orden ascendente de id, llamando `fn(id, value)`.
	/// Lo usa la serialización (`eng/rpg/save.hpp`) para un formato determinista.
	template <class Fn>
	void for_each_present(Fn&& fn) const {
		for (eng::u16 i = 0; i < MaxFacts; ++i) {
			if (m_present.test(i)) {
				fn(i, m_values[i]);
			}
		}
	}

private:
	eng::u16 m_values[MaxFacts] {};          ///< valor por clave (0 si ausente)
	eng::util::BitSet<MaxFacts> m_present {};///< claves con hecho (distinguir "0" de "ausente")
	eng::util::BitSet<MaxFacts> m_changed {};///< claves tocadas desde `clear_changed()`
	eng::u32 m_epoch = 0u;                   ///< cambios efectivos acumulados (envuelve)
};

/// Conjunto de hechos **pegados a una entidad** (no un almacén de valores): la lista pequeña
/// que un actor u objeto lleva consigo — `witnessed_theft` de un guardia, `soulbound` de un
/// arma, "esta puerta ya se abrió con permiso". Búsqueda lineal: la lista es corta.
template <eng::u8 Max>
struct FactSet {
	static_assert(Max >= 1u, "FactSet: capacidad de al menos un hecho");

	/// Hechos del conjunto (sin duplicados).
	eng::util::StaticVector<FactId, Max> facts {};

	/// ¿El conjunto contiene `id`?
	[[nodiscard]] bool has(FactId id) const noexcept {
		for (eng::usize i = 0; i < facts.size(); ++i) {
			if (facts[i] == id) {
				return true;
			}
		}
		return false;
	}

	/// Añade `id` si no está. `false` si ya estaba o si el conjunto está lleno.
	bool add(FactId id) noexcept {
		if (has(id) || facts.full()) {
			return false;
		}
		return facts.push_back(id);
	}

	/// Quita `id`. `false` si no estaba.
	bool remove(FactId id) noexcept {
		for (eng::usize i = 0; i < facts.size(); ++i) {
			if (facts[i] == id) {
				facts.erase(i);
				return true;
			}
		}
		return false;
	}
};

} // namespace eng::rpg
