#pragma once

/// \file input.hpp
/// **Entrada del HUD** (`eng::hud`): `HudInput` consume `eng::ui::UiEvent` (el contrato de
/// eventos compartido con `eng::ui`) contra las listas de *hit-test* del modelo y produce
/// **selección y foco** (`HUD_SYSTEM.md` §7). No es un segundo despachador: es un consumidor
/// del evento, y la política de consumo es explícita por región (`Policy::capture_input`).
///
/// Subconjunto vigente (H2, hasta que G9 cierre el contrato de `eng::ui`): `MouseMove`,
/// `MouseDown`, `MouseUp`, `PointerCancel`, `JoyButton` y `Action`. `KeyDown`/`KeyUp`/`Tick`
/// no se consumen aquí (la repetición la aporta el servicio de entrada o el `Tick`).
///
/// Verificación: **HOST-469** (prioridad por capa, rejilla con pitch no múltiplo de 16,
/// *click* dentro/fuera, foco con bordes de rejilla y consumo según `capture_input`).

#include <eng/core/types/box.hpp>
#include <eng/core/types/types.hpp>
#include <eng/hud/model.hpp>
#include <eng/ui/event.hpp>

namespace eng::hud {

/// Bits de `UiEvent::joy_buttons` (contrato del puente `os::Msg` → `UiEvent`,
/// `ui_bridge.hpp`): direcciones en los bits 0..3 y fuego en el bit 8.
inline constexpr eng::u16 kHudJoyUp = 1u << 0u;
inline constexpr eng::u16 kHudJoyDown = 1u << 1u;
inline constexpr eng::u16 kHudJoyLeft = 1u << 2u;
inline constexpr eng::u16 kHudJoyRight = 1u << 3u;
inline constexpr eng::u16 kHudJoyFire = 1u << 8u;

/// *Hit* resuelto de un punto (coordenadas de la región): entrada rectangular (por capa) o
/// celda de rejilla (por fórmula).
struct HudHit {
	bool hit = false;            ///< el punto toca una entrada interactiva
	eng::u16 id = 0u;            ///< id lógico (`first_id` + índice en la rejilla; rect: su id)
	GridHandle grid {};          ///< rejilla tocada (vacío si el *hit* es rectangular)
	eng::u16 slot = kHudNoSlot;  ///< **índice** del slot en la rejilla (para el modelo)
	eng::u8 layer = 0u;          ///< capa de la entrada rectangular (mayor = delante)
};

/// Resuelve el *hit* de `(x, y)` en coordenadas de la región contra las listas del modelo.
///
/// Prioridad: **rectángulos por capa** (mayor delante; empate = el último empujado, el orden
/// de pintado) y, si ninguno toca, **rejillas por fórmula** (`(p − origen) / pitch`, división
/// entera: el *pitch* no tiene que ser múltiplo de 16) en orden de empuje (gana la primera).
/// La transformación a coordenadas de región es responsabilidad del llamador (§7.4): el
/// *hit-test* y el pintado comparten la misma, no hay dos caminos que se desincronicen.
template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
[[nodiscard]] constexpr HudHit hud_hit_test(const Hud<R, W, S, D, H>& model, eng::s16 x,
					    eng::s16 y) noexcept {
	HudHit best {};
	bool found = false;
	const eng::Span<const HudHitRect> rects = model.hits();
	for (eng::usize i = 0u; i < rects.size(); ++i) {
		const HudHitRect& r = rects[i];
		if (!r.box.contains(x, y)) {
			continue;
		}
		if (!found || r.layer >= best.layer) {
			best.hit = true;
			best.id = r.id;
			best.layer = r.layer;
			best.grid = GridHandle {};
			best.slot = kHudNoSlot;
			found = true;
		}
	}
	if (found) {
		return best;
	}
	const eng::Span<const HudGridHit> grids = model.grid_hits();
	for (eng::usize i = 0u; i < grids.size(); ++i) {
		const HudGridHit& g = grids[i];
		if (g.cols == 0u || g.rows == 0u || g.pitch_x == 0u || g.pitch_y == 0u) {
			continue;
		}
		if (!g.area.contains(x, y) || x < g.area.x || y < g.area.y) {
			continue;
		}
		const eng::u16 col = static_cast<eng::u16>(
			static_cast<eng::u16>(x - g.area.x) / g.pitch_x);
		const eng::u16 row = static_cast<eng::u16>(
			static_cast<eng::u16>(y - g.area.y) / g.pitch_y);
		if (col >= g.cols || row >= g.rows) {
			continue;
		}
		// Índice del slot = fila*columnas + columna (la multiplicación ocurre **en el
		// evento**, nunca por frame ni por píxel, §5.4). `slot` es el índice que consumen
		// las llamadas del modelo (`grid_select`); `id` es el lógico (`first_id` + índice).
		const eng::u16 index = static_cast<eng::u16>(
			static_cast<eng::u16>(row * g.cols) + col);
		best.hit = true;
		best.id = static_cast<eng::u16>(g.first_id + index);
		best.grid = g.grid;
		best.slot = index;
		best.layer = 0u;
		return best;
	}
	return best;
}

/// **Consumidor de entrada del HUD** (H2): máquina de puntero mínima (*hover* implícito,
/// *down*/*up* en el mismo objetivo = *click*), navegación de foco por rejilla con joystick
/// (`Fire` = *click* del foco) y `UiAction` semántica. Una instancia por región de HUD.
///
/// La **selección** y el **foco** viven en el modelo (`IconGrid`): `HudInput` solo los mueve
/// (`grid_select`/`grid_move_focus`) y marca *dirty*; el presentador pinta el estado.
class HudInput {
public:
	/// Política de la región (§7.5): con `capture_input` todo evento dentro de la región se
	/// consume (panel modal); sin ella, solo se consume con *hit* interactivo y el resto pasa
	/// al juego. La política es explícita; no se deduce del orden de dibujo.
	struct Policy {
		bool capture_input = false; ///< la región captura la entrada (modal)

		/// Deriva la política del **contrato de región** (`HudRegionSpec::capture_input`): la
		/// fuente única del dato es el descriptor, no una copia en la demo.
		[[nodiscard]] static constexpr Policy from_region(const HudRegionSpec& spec) noexcept {
			return Policy {spec.capture_input};
		}
	};

	/// Resultado de consumir un evento.
	struct Result {
		bool consumed = false;          ///< el evento no debe llegar al juego
		bool clicked = false;           ///< *click* completado (*down* + *up* mismo objetivo, o `Fire`)
		bool selection_changed = false; ///< la rejilla cambió de selección
		bool focus_changed = false;     ///< la rejilla cambió de foco
		eng::u16 id = 0u;               ///< id del rect clicado (0 si rejilla)
		GridHandle grid {};             ///< rejilla afectada (si aplica)
		eng::u16 slot = kHudNoSlot;     ///< slot afectado (si aplica)
	};

	/// Rejilla que recibe la navegación (joystick/teclado); la fija el juego en el *setup*.
	void set_focus_grid(GridHandle g) noexcept { m_focus_grid = g; }
	/// Rejilla de navegación vigente.
	[[nodiscard]] GridHandle focus_grid() const noexcept { return m_focus_grid; }
	/// ¿Hay un *down* pendiente de *up*?
	[[nodiscard]] bool pressed() const noexcept { return m_pressed; }
	/// Reinicia el estado del puntero (cambio de escena, pérdida de foco).
	void reset() noexcept { clear_press(); }

	/// Consume un evento sobre `model`. `ev.x`/`ev.y` van en **coordenadas de la región**.
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] Result handle(Hud<R, W, S, D, H>& model, const Policy& policy,
				    const eng::ui::UiEvent& ev) noexcept {
		using eng::ui::UiEventKind;
		switch (ev.kind) {
		case UiEventKind::MouseMove:
			return on_move(model, policy, ev);
		case UiEventKind::MouseDown:
			return on_down(model, policy, ev);
		case UiEventKind::MouseUp:
			return on_up(model, policy, ev);
		case UiEventKind::PointerCancel:
			return on_pointer_cancel(policy);
		case UiEventKind::JoyButton:
			return on_joy(model, ev);
		case UiEventKind::Action:
			return on_action(model, policy, ev);
		default:
			// KeyDown/KeyUp/Tick: el HUD no los consume (la repetición es del servicio).
			return {};
		}
	}

private:
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] Result on_move(const Hud<R, W, S, D, H>& model, const Policy& policy,
				     const eng::ui::UiEvent& ev) const noexcept {
		const HudHit h = hud_hit_test(model, ev.x, ev.y);
		Result out {};
		out.consumed = policy.capture_input || h.hit;
		out.id = h.id;
		out.grid = h.grid;
		out.slot = h.slot;
		return out;
	}

	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] Result on_down(const Hud<R, W, S, D, H>& model, const Policy& policy,
				     const eng::ui::UiEvent& ev) noexcept {
		const HudHit h = hud_hit_test(model, ev.x, ev.y);
		Result out {};
		if (h.hit) {
			m_pressed = true;
			m_press_id = h.id;
			m_press_grid = h.grid;
			m_press_slot = h.slot;
			out.consumed = true;
			out.id = h.id;
			out.grid = h.grid;
			out.slot = h.slot;
			return out;
		}
		// Sin *hit*: un panel modal consume igualmente (el juego no ve el *click* dentro).
		out.consumed = policy.capture_input;
		return out;
	}

	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] Result on_up(Hud<R, W, S, D, H>& model, const Policy& policy,
				   const eng::ui::UiEvent& ev) noexcept {
		Result out {};
		if (!m_pressed) {
			// *up* suelto: solo lo consume un panel modal (evita *clicks* fuera del panel).
			out.consumed = policy.capture_input;
			return out;
		}
		const HudHit h = hud_hit_test(model, ev.x, ev.y);
		const bool same = h.hit && h.id == m_press_id && h.slot == m_press_slot &&
				  h.grid.index == m_press_grid.index;
		out.consumed = true;
		out.id = h.hit ? h.id : m_press_id;
		out.grid = h.hit ? h.grid : m_press_grid;
		out.slot = h.hit ? h.slot : m_press_slot;
		if (same) {
			out.clicked = true;
			if (h.grid.valid()) {
				out.selection_changed = model.grid_select(h.grid, h.slot);
			}
		}
		clear_press();
		return out;
	}

	[[nodiscard]] Result on_pointer_cancel(const Policy& policy) noexcept {
		Result out {};
		out.consumed = m_pressed || policy.capture_input;
		clear_press();
		return out;
	}

	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] Result on_joy(Hud<R, W, S, D, H>& model, const eng::ui::UiEvent& ev) noexcept {
		Result out {};
		const eng::u16 b = ev.joy_buttons;
		if ((b & kHudJoyFire) != 0u) {
			// `Fire` = *click* del **foco** de la rejilla de navegación (sin ratón).
			if (m_focus_grid.valid()) {
				const eng::u16 slot = model.grid(m_focus_grid).focus;
				out.consumed = true;
				out.clicked = true;
				out.grid = m_focus_grid;
				out.slot = slot;
				out.id = slot;
				out.selection_changed = model.grid_select(m_focus_grid, slot);
			}
			return out;
		}
		const eng::s8 dx = static_cast<eng::s8>(((b & kHudJoyRight) != 0u ? 1 : 0) -
							((b & kHudJoyLeft) != 0u ? 1 : 0));
		const eng::s8 dy = static_cast<eng::s8>(((b & kHudJoyDown) != 0u ? 1 : 0) -
							((b & kHudJoyUp) != 0u ? 1 : 0));
		if ((dx != 0 || dy != 0) && m_focus_grid.valid()) {
			out.consumed = true;
			out.focus_changed = model.grid_move_focus(m_focus_grid, dx, dy);
			out.grid = m_focus_grid;
		}
		return out;
	}

	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] Result on_action(Hud<R, W, S, D, H>& model, const Policy& policy,
				       const eng::ui::UiEvent& ev) noexcept {
		using eng::ui::UiAction;
		Result out {};
		eng::s8 dx = 0;
		eng::s8 dy = 0;
		switch (ev.action) {
		case UiAction::MoveLeft: dx = -1; break;
		case UiAction::MoveRight: dx = 1; break;
		case UiAction::MoveUp: dy = -1; break;
		case UiAction::MoveDown: dy = 1; break;
		case UiAction::Accept:
		case UiAction::Activate:
			if (m_focus_grid.valid()) {
				const eng::u16 slot = model.grid(m_focus_grid).focus;
				out.consumed = true;
				out.clicked = true;
				out.grid = m_focus_grid;
				out.slot = slot;
				out.id = slot;
				out.selection_changed = model.grid_select(m_focus_grid, slot);
			}
			return out;
		case UiAction::Cancel:
			return on_pointer_cancel(policy);
		default:
			return {};
		}
		if (m_focus_grid.valid()) {
			out.consumed = true;
			out.focus_changed = model.grid_move_focus(m_focus_grid, dx, dy);
			out.grid = m_focus_grid;
		}
		return out;
	}

	void clear_press() noexcept {
		m_pressed = false;
		m_press_id = 0u;
		m_press_grid = GridHandle {};
		m_press_slot = kHudNoSlot;
	}

	bool m_pressed = false;          ///< *down* pendiente de *up*
	eng::u16 m_press_id = 0u;        ///< objetivo del *down* (rect)
	GridHandle m_press_grid {};      ///< objetivo del *down* (rejilla)
	eng::u16 m_press_slot = kHudNoSlot; ///< objetivo del *down* (slot)
	GridHandle m_focus_grid {};      ///< rejilla que recibe la navegación
};

} // namespace eng::hud
