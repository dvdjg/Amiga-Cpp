#pragma once

/// \file layer.hpp
/// **Capa de dibujo** (`eng::scene::DrawLayer`): la **política de capa** — los objetos se describen
/// en el **setup** con una forma estática (tipo/geometría/color) y una **posición dinámica** que el
/// juego actualiza por frame. `emit` vuelca la capa a una cola de intención
/// (`graphics::IntentQueue`).
///
/// Separación de responsabilidades: la **cola** y el **vocabulario** (`DrawIntent`) son el
/// **mecanismo** (`eng::graphics`); la **capa** (qué dibuja cada objeto y qué cambia por frame) es
/// **política de escena**, y vive aquí. No es una cola: es una **receta de capa**.
/// Ver `docs/engine/architecture/INTENT_PLANNER.md` §9.

#include <eng/core/types/types.hpp>
#include <eng/graphics/intent_queue.hpp>

namespace eng::scene {

using eng::graphics::DrawIntent;

/// **Capa de dibujo**: objetos descritos **en el setup** (forma invariante) con una posición
/// **dinámica** por frame. `emit` recorre y encola con el dato del frame — **solo lo dinámico**
/// cambia (regla de coste): sin reconstruir, sin asignar.
template <eng::u16 N>
class DrawLayer {
public:
	[[nodiscard]] constexpr eng::u16 count() const noexcept { return m_count; }

	/// Alta de un objeto (setup): la **forma** base (kind/geometría/color; `x`/`y` se ignoran) y su
	/// posición inicial.
	constexpr bool add(const DrawIntent& shape, eng::s16 x, eng::s16 y) noexcept {
		if (m_count >= N) {
			return false;
		}
		m_shape[m_count] = shape;
		m_x[m_count] = x;
		m_y[m_count] = y;
		++m_count;
		return true;
	}

	/// **Frame**: mueve un objeto (solo el dato dinámico).
	constexpr void move(eng::u16 i, eng::s16 x, eng::s16 y) noexcept {
		m_x[i] = x;
		m_y[i] = y;
	}

	/// **Frame**: reproduce la capa en la cola con las posiciones actuales (recorrido mínimo).
	template <class Queue>
	void emit(Queue& queue) const noexcept {
		for (eng::u16 i = 0u; i < m_count; ++i) {
			DrawIntent d = m_shape[i];
			d.x = m_x[i];
			d.y = m_y[i];
			queue.enqueue(d);
		}
	}

private:
	DrawIntent m_shape[N] {};
	eng::s16 m_x[N] {};
	eng::s16 m_y[N] {};
	eng::u16 m_count = 0u;
};

} // namespace eng::scene
