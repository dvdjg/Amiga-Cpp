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
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/intent_queue.hpp>
#include <eng/graphics/sprite_asset.hpp>

namespace eng::scene {

using eng::graphics::DrawIntent;

/// **Ejecutor de la intención de dibujo** hacia el plan del frame: compila un `DrawIntent` de tipo
/// `Sprite` con `Sprite::draw` (el plan decide CPU/Blitter). Es el puente **intención → `FramePlan`**
/// (el sumidero), apto como `Executor` de una `IntentQueue` (`ready`/`run`).
///
/// Recuerda el **rectángulo previo por objeto** (`DrawIntent::id`) para el borrado por caja y el
/// **save-under** (`BobErase::RestoreUnder`): restaura la caja previa, guarda la nueva y dibuja.
/// El buffer de guardado (opcional) se liga en el constructor; sin él, `RestoreUnder` se rechaza.
class SpritePlanExecutor {
public:
	/// Máximo de objetos con estado previo (slot de `DrawIntent::id`).
	static constexpr eng::u8 kMaxObjects = 16u;

	constexpr SpritePlanExecutor() noexcept = default;

	/// Liga el plan (el sumidero), la geometría de destino y, opcionalmente, el buffer del
	/// save-under (`save_words_per_row`/`save_height` = su capacidad; vacío = sin save-under).
	/// No propietario; los buffers deben vivir más que el ejecutor.
	constexpr SpritePlanExecutor(eng::graphics::FramePlan& plan,
				     const eng::graphics::BobTarget& target,
				     eng::Span<eng::u16> save = {},
				     eng::u16 save_words_per_row = 0u,
				     eng::u16 save_height = 0u) noexcept {
		bind(plan, target, save, save_words_per_row, save_height);
	}

	/// Liga/religa el plan, el destino y el buffer de guardado (p. ej. tras reservar memoria).
	constexpr void bind(eng::graphics::FramePlan& plan, const eng::graphics::BobTarget& target,
			    eng::Span<eng::u16> save = {}, eng::u16 save_words_per_row = 0u,
			    eng::u16 save_height = 0u) noexcept {
		m_plan = plan;
		m_target = target;
		m_save = save;
		m_save_words_per_row = save_words_per_row;
		m_save_height = save_height;
	}

	/// Liga el `copper::Plan` (opcional) para volcar las **necesidades de copper** de cada
	/// intención, ancladas a la Y del objeto. `display_top` es la primera línea del display.
	constexpr void bind_copper(eng::copper::Plan& plan, eng::s16 display_top) noexcept {
		m_copper = plan;
		m_display_top = display_top;
	}

	[[nodiscard]] constexpr bool ready() const noexcept { return m_plan.valid(); }

	/// Compila una intención de dibujo al plan (`Sprite::draw`); `false` si no es un sprite válido.
	///
	/// La **política** viaja en la intención: `erase` (fondo) restaura/borra la caja previa y `draw`
	/// (transparencia) elige el minterm; el asset solo aporta la hoja/geometría.
	bool run(const DrawIntent& item) noexcept {
		if (!m_plan.valid() || !m_target.valid()) {
			return false;
		}
		if (item.kind != eng::graphics::DrawKind::Sprite || !item.sheet.valid()) {
			return false;
		}
		eng::graphics::Bob bob = item.sheet->bob();
		bob.draw = item.draw;
		bob.erase = item.erase;
		const bool track = item.id < kMaxObjects;
		const eng::graphics::DirtyRect cur {
			item.x, item.y, static_cast<eng::s16>(item.x + static_cast<eng::s16>(bob.width)),
			static_cast<eng::s16>(item.y + static_cast<eng::s16>(bob.height))};
		if (track && item.erase == eng::graphics::BobErase::RestoreUnder) {
			const eng::graphics::DirtyRect prev = m_prev[item.id];
			if (prev.valid() &&
			    !eng::graphics::bob_restore_box(*m_plan, bob, prev.width(), prev.height(),
							    prev.left, prev.top, *m_target, m_save,
							    m_save_words_per_row, m_save_height)) {
				return false;
			}
			if (!eng::graphics::bob_save_box(*m_plan, bob, cur.width(), cur.height(), cur.left,
							 cur.top, *m_target, m_save, m_save_words_per_row,
							 m_save_height)) {
				return false;
			}
			m_prev[item.id] = cur;
		} else if (track && item.erase == eng::graphics::BobErase::ClearRect) {
			const eng::graphics::DirtyRect prev = m_prev[item.id];
			if (prev.valid() &&
			    !eng::graphics::bob_erase_box(*m_plan, bob, prev.width(), prev.height(), prev.left,
							  prev.top, *m_target)) {
				return false;
			}
			m_prev[item.id] = cur;
		}
		// Necesidades de copper del objeto, ancladas a su Y (si hay plan de copper ligado).
		if (m_copper.valid() && !item.copper.empty()) {
			m_copper->add_anchored(item.copper.data(), item.copper.size(),
					       static_cast<eng::s32>(m_display_top) + item.y, 0u,
					       static_cast<eng::u8>(item.id));
		}
		return eng::graphics::bob_draw(*m_plan, bob, item.frame, item.x, item.y, *m_target);
	}

private:
	eng::Ref<eng::graphics::FramePlan> m_plan {};
	eng::Ref<const eng::graphics::BobTarget> m_target {};
	eng::Span<eng::u16> m_save {};
	eng::u16 m_save_words_per_row = 0u;
	eng::u16 m_save_height = 0u;
	eng::Ref<eng::copper::Plan> m_copper {};
	eng::s16 m_display_top = 0;
	eng::graphics::DirtyRect m_prev[kMaxObjects] {};
};

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
			d.id = static_cast<eng::u8>(i); // slot para el estado previo del ejecutor
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
