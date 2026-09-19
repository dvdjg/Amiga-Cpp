#pragma once

/// \file blackboard.hpp
/// `eng::ai::Blackboard<Key, Value, MaxKeys>`: **memoria compartida** de la IA, de
/// capacidad fija y sin heap. Un `enum` denso de claves (`[0, MaxKeys)`) indexa un
/// array de valores y un `BitSet` de presencia; `find` es `O(1)` (indice directo).
///
/// Pensado para que los sistemas (percepcion, decision, planificacion) lean y escriban
/// creencias del agente sin acoplarse: "ultima posicion conocida", "objetivo actual",
/// "municion", "tiempo desde el aviso". Los valores pueden ser un struct del juego.
///
/// Para difundir un cambio se usa `eng::util::Event` en paralelo (el blackboard no
/// emite: es almacenamiento puro, para no imponer el coste de un emisor a quien no lo
/// necesita). Ver `docs/engine/architecture/GAME_AI_LIBRARY.md`.
///
/// Uso:
///   enum class Belief : eng::u16 { Target, LastSeen, Ammo, Max };
///   eng::ai::Blackboard<Belief, eng::s32, (eng::usize)Belief::Max> bb;
///   bb.set(Belief::Ammo, 30);
///   if (const eng::s32* ammo = bb.find(Belief::Ammo)) { ... }
///
/// ```text
///   enum Key [0, MaxKeys)              Blackboard (sin heap)                  sistemas
///   ────────────────────               ────────────────────                   ────────
///   Target, LastSeen, Ammo … ───────► [ values[MaxKeys] ][ present BitSet ]
///                                           ▲    │
///                      set(key, v) ─────────┘    └──────► find(key) → const Value* (O(1), índice directo)
///
///   difundir cambios = eng::util::Event APARTE (el blackboard no emite)
/// ```
///
/// Verificación: HOST-111.

#include <eng/core/types.hpp>
#include <eng/core/util/bitset.hpp>

namespace eng::ai {

template <class Key, class Value, eng::usize MaxKeys>
class Blackboard {
	static_assert(MaxKeys > 0u, "Blackboard: MaxKeys debe ser mayor que 0");

public:
	[[nodiscard]] static constexpr eng::usize capacity() noexcept { return MaxKeys; }
	[[nodiscard]] constexpr eng::usize size() const noexcept { return m_present.count(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_present.none(); }

	/// Escribe (o sobrescribe) la creencia `key`.
	constexpr void set(Key key, const Value& value) noexcept {
		const eng::usize i = index(key);
		m_values[i] = value;
		m_present.set(i);
	}

	[[nodiscard]] constexpr bool contains(Key key) const noexcept {
		return m_present.test(index(key));
	}

	/// Puntero al valor, o `nullptr` si la creencia no está puesta.
	[[nodiscard]] constexpr Value* find(Key key) noexcept {
		const eng::usize i = index(key);
		return m_present.test(i) ? &m_values[i] : nullptr;
	}
	[[nodiscard]] constexpr const Value* find(Key key) const noexcept {
		const eng::usize i = index(key);
		return m_present.test(i) ? &m_values[i] : nullptr;
	}

	/// Valor de `key`, o `fallback` si no está puesta.
	[[nodiscard]] constexpr Value get_or(Key key, const Value& fallback) const noexcept {
		const Value* p = find(key);
		return p != nullptr ? *p : fallback;
	}

	/// Borra la creencia. `false` si no estaba puesta.
	constexpr bool erase(Key key) noexcept {
		const eng::usize i = index(key);
		if (!m_present.test(i)) {
			return false;
		}
		m_present.reset(i);
		return true;
	}

	constexpr void clear() noexcept { m_present.reset(); }

private:
	[[nodiscard]] static constexpr eng::usize index(Key key) noexcept {
		return static_cast<eng::usize>(key);
	}

	Value m_values[MaxKeys] {};
	eng::util::BitSet<MaxKeys> m_present {};
};

} // namespace eng::ai
