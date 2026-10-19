#pragma once

/// \file say.hpp
/// **Cola de réplicas** (`eng::adv`): el actor dice líneas con una duración en VBlanks y la
/// cola las reproduce en orden, una detrás de otra, sin que el juego lleve temporizadores a
/// mano. El texto no vive aquí: la cola transporta el `text_id` del contenido (tabla del
/// juego, como los juegos convertidos) y el hablante; quien pinta consulta la tabla.
/// Diseño: `docs/engine/architecture/ADVENTURE_LAYER.md` §HUD de diálogo; hito A1 de
/// `docs/guides/roadmap/ROADMAP_ADVENTURE.md`.
///
/// ```text
///   push(12, actor, 150)  ->  [12,150][7,90]   tick() por VBlank
///                              ^current        al agotarse la primera, la segunda pasa a current
/// ```
///
/// No drena sola por tiempo de reloj: `tick()` se llama **una vez por VBlank** (el bucle de
/// la demo lo hace al cerrar el frame), de modo que la duración es determinista en tiempo
/// emulado aunque el host vaya a saltos.

#include <eng/core/types/types.hpp>

namespace eng::adv {

/// Cola FIFO de réplicas, capacidad fija (sin heap).
template <eng::u8 MaxLines>
class SayQueue {
	static_assert(MaxLines >= 1u, "SayQueue: capacidad de al menos una réplica");

public:
	/// Réplica encolada: texto del contenido + hablante + VBlanks restantes.
	struct Line {
		eng::u16 text_id {0u}; ///< id en la tabla de textos del contenido
		eng::u8 speaker {0u};  ///< actor que habla (0 = narrador)
		eng::u16 frames {0u};  ///< VBlanks que queda en pantalla
	};

	/// Encola una réplica de `frames` VBlanks. `false` si la cola está llena o `frames == 0`
	/// (una réplica de cero VBlanks sería invisible: es un error de contenido, no un caso).
	[[nodiscard]] bool push(eng::u16 text_id, eng::u8 speaker, eng::u16 frames) noexcept {
		if (m_count >= MaxLines || frames == 0u) {
			return false;
		}
		m_lines[m_tail].text_id = text_id;
		m_lines[m_tail].speaker = speaker;
		m_lines[m_tail].frames = frames;
		m_tail = static_cast<eng::u8>((m_tail + 1u) % MaxLines);
		++m_count;
		return true;
	}

	/// Avanza un VBlank: descuenta la réplica en curso y la retira al llegar a cero. La
	/// siguiente réplica empieza **en el VBlank siguiente** (no se le descuenta el tick de la
	/// retirada: su duración es exacta).
	void tick() noexcept {
		if (m_count == 0u) {
			return;
		}
		Line& cur = m_lines[m_head];
		--cur.frames;
		if (cur.frames == 0u) {
			m_head = static_cast<eng::u8>((m_head + 1u) % MaxLines);
			--m_count;
		}
	}

	/// Réplica en curso (`nullptr` si la cola está vacía).
	[[nodiscard]] const Line* current() const noexcept {
		return m_count != 0u ? &m_lines[m_head] : nullptr;
	}

	/// ¿Cola vacía?
	[[nodiscard]] bool empty() const noexcept { return m_count == 0u; }

	/// Réplicas encoladas (0..MaxLines).
	[[nodiscard]] eng::u8 size() const noexcept { return m_count; }

	/// Descarta todas las réplicas (p. ej. al cambiar de sala).
	void clear() noexcept {
		m_head = 0u;
		m_tail = 0u;
		m_count = 0u;
	}

private:
	Line m_lines[MaxLines] {}; ///< anillo FIFO (head..tail-1 válidas)
	eng::u8 m_head = 0u;       ///< próxima a reproducir
	eng::u8 m_tail = 0u;       ///< próxima libre
	eng::u8 m_count = 0u;      ///< réplicas encoladas
};

} // namespace eng::adv
