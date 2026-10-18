#pragma once

/// \file focus.hpp
/// **Foco del HUD** (`eng::hud`): navegación por **todos** los elementos interactivos de una
/// región (botones/rects, celdas de rejilla, verbos) con teclado/joystick, activación y
/// *hover*. El anillo (navegación geométrica, `Tab`, activación) es **genérico de interfaz**
/// y vive en `eng::ui::FocusRing` (`eng/ui/interaction.hpp`); este fichero es el **adaptador
/// fino** al modelo del HUD: mapea el objetivo (índice de lista → `GridHandle`/id lógico) y
/// sincroniza el modelo (`grid_move_focus_to`/`grid_select`). El HUD **se construye sobre la
/// GUI**, no al contrario (`HUD_SYSTEM.md` §7.1).
///
/// ## Capas (una sola fuente de verdad por nivel)
///
/// ```text
///   modelo (model.hpp)      estado + GEOMETRIA (box, area/pitch de rejillas)
///        │  hits()/grid_hits()  (lo que empuja el juego: el mismo mapa del raton)
///        ▼
///   hit-test (input.hpp)    punto -> elemento (eng::ui::hit_test + mapeo a GridHandle)
///        ▼
///   entrada (HudInput)      eventos -> click/seleccion (raton) y pasos (joystick/accion)
///        ▼
///   foco (HudFocus)         adaptador de eng::ui::FocusRing + sincronizacion del modelo
/// ```
///
/// El **mismo** `push_hit`/`push_grid_hit` sirve al ratón y al anillo: no hay dos mapas que
/// puedan desincronizarse (`HUD_SYSTEM.md` §7.1). El foco es estado de **presentación**: el
/// juego lo pinta (marco, color) leyendo el modelo (`IconGrid::focus`, `Button::hover`).
///
/// ## Navegación
///
/// - Flechas/joystick: **geométrica** — dentro de una rejilla mueve el foco de celda
///   (`IconGrid::focus`); en el borde (o entre widgets) salta al elemento más cercano en esa
///   dirección (centro contra centro). `Tab`/`Shift+Tab` recorren el anillo en orden.
/// - `Fire`/`Enter`/`Espacio`: **activa** el elemento enfocado (selecciona la celda, o
///   devuelve el `id` del rect para que el juego lo resuelva — el mismo camino del click).
/// - Ratón: el *hover* (mover) y el click los resuelve `HudInput`; `HudFocus` solo sincroniza
///   el foco con el hover si el juego lo pide (`focus_from_hit`).

#include <eng/core/types/box.hpp>
#include <eng/core/types/types.hpp>
#include <eng/hud/input.hpp> // HudHit (el hit-test del que parte el foco)
#include <eng/hud/model.hpp>
#include <eng/ui/interaction.hpp> // eng::ui::FocusRing (el anillo genérico)

namespace eng::hud {

/// Elemento enfocado por `HudFocus`.
struct HudFocusTarget {
	enum class Kind : eng::u8 { None = 0, Rect = 1, Grid = 2 };
	Kind kind = Kind::None;
	eng::u16 id = 0u;           ///< id lógico del rect (para el juego)
	GridHandle grid {};         ///< rejilla enfocada (su `focus` es la celda)
	eng::u16 slot = kHudNoSlot; ///< celda enfocada (índice en la rejilla)
};

/// Resultado de una operación de foco.
struct HudFocusResult {
	bool moved = false;     ///< el foco cambió de elemento
	bool activated = false; ///< se activó el elemento enfocado
	eng::u16 id = 0u;       ///< id del rect activado (0 si rejilla)
	GridHandle grid {};     ///< rejilla afectada
	eng::u16 slot = kHudNoSlot; ///< celda afectada
};

/// **Anillo de foco** del HUD: adapta `eng::ui::FocusRing` al modelo (índice de lista →
/// `GridHandle`/id) y sincroniza el estado del modelo en cada operación.
class HudFocus {
public:
	/// Elemento enfocado (en el vocabulario del HUD).
	[[nodiscard]] const HudFocusTarget& target() const noexcept { return m_target; }
	/// ¿Hay foco?
	[[nodiscard]] bool active() const noexcept { return m_target.kind != HudFocusTarget::Kind::None; }
	/// Quita el foco (cierre de panel, cambio de región).
	void reset() noexcept {
		m_ring.reset();
		m_target = HudFocusTarget {};
	}

	/// Enfoca el **primer** elemento del modelo (el primer rect empujado o la primera
	/// rejilla). `false` si no hay elementos interactivos.
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] bool focus_first(Hud<R, W, S, D, H>& model) noexcept {
		if (!m_ring.focus_first(model.hits(), model.grid_hits())) {
			return false;
		}
		if (m_ring.target().kind == eng::ui::FocusRing::Target::Kind::Grid) {
			const HudGridHit& g = model.grid_hits()[m_ring.target().index];
			if (!g.grid.valid()) {
				m_ring.reset();
				return false;
			}
			// La celda de entrada es la que ya tenga el modelo.
			m_ring.focus_grid_cell(m_ring.target().index, model.grid(g.grid).focus);
		}
		map_target(model);
		return true;
	}

	/// Sincroniza el foco con un *hit* del ratón (hover): si toca una celda de rejilla, el
	/// foco pasa a ella; si toca un rect, al rect. `false` si el hit no es interactivo.
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] bool focus_from_hit(Hud<R, W, S, D, H>& model, const HudHit& hit) noexcept {
		if (!hit.hit) {
			return false;
		}
		if (hit.grid.valid()) {
			const eng::Span<const HudGridHit> grids = model.grid_hits();
			for (eng::usize i = 0u; i < grids.size(); ++i) {
				if (grids[i].grid.index == hit.grid.index) {
					m_ring.focus_grid_cell(static_cast<eng::u16>(i), hit.slot);
					(void)model.grid_move_focus_to(hit.grid, hit.slot);
					map_target(model);
					return true;
				}
			}
			return false;
		}
		const eng::Span<const HudHitRect> rects = model.hits();
		for (eng::usize i = 0u; i < rects.size(); ++i) {
			if (rects[i].id == hit.id) {
				m_ring.focus_rect(static_cast<eng::u16>(i));
				map_target(model);
				return true;
			}
		}
		return false;
	}

	/// Mueve el foco en `(dx, dy)` (flechas/joystick). Dentro de una rejilla mueve su celda;
	/// en el borde (o entre widgets) salta al elemento **más cercano en esa dirección**
	/// (centro contra centro; el eje del movimiento manda). Devuelve si se movió.
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] HudFocusResult move(Hud<R, W, S, D, H>& model, eng::s8 dx, eng::s8 dy) noexcept {
		HudFocusResult out {};
		const eng::ui::FocusRing::Result r = m_ring.move(model.hits(), model.grid_hits(), dx, dy);
		if (!r.moved) {
			return out;
		}
		map_target(model);
		sync_model(model);
		out.moved = true;
		out.id = m_target.id;
		out.grid = m_target.grid;
		out.slot = m_target.slot;
		return out;
	}

	/// **Activa** el elemento enfocado (`Fire`/`Enter`/`Espacio`): en una rejilla selecciona
	/// la celda; en un rect devuelve su `id` (el juego resuelve, igual que con el click).
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] HudFocusResult activate(Hud<R, W, S, D, H>& model) noexcept {
		HudFocusResult out {};
		switch (m_target.kind) {
		case HudFocusTarget::Kind::None:
			return out;
		case HudFocusTarget::Kind::Grid:
			if (m_target.grid.valid()) {
				const eng::u16 slot = model.grid(m_target.grid).focus;
				out.activated = true;
				out.grid = m_target.grid;
				out.slot = slot;
				out.id = slot;
				(void)model.grid_select(m_target.grid, slot);
			}
			return out;
		case HudFocusTarget::Kind::Rect:
			out.activated = true;
			out.id = m_target.id;
			return out;
		}
		return out;
	}

	/// Recorre el anillo en orden de empuje (`Tab`): pasa al siguiente rect/rejilla.
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] HudFocusResult next(Hud<R, W, S, D, H>& model, bool forward) noexcept {
		HudFocusResult out {};
		const eng::ui::FocusRing::Result r =
			m_ring.next(model.hits(), model.grid_hits(), forward);
		if (!r.moved) {
			return out;
		}
		map_target(model);
		sync_model(model);
		out.moved = true;
		out.id = m_target.id;
		out.grid = m_target.grid;
		out.slot = m_target.slot;
		return out;
	}

private:
	/// Traduce el objetivo del anillo (índice de lista) al vocabulario del HUD
	/// (`GridHandle`/id lógico). Si el índice no resuelve (lista cambiada), desactiva el foco.
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	void map_target(const Hud<R, W, S, D, H>& model) noexcept {
		m_target = HudFocusTarget {};
		const eng::ui::FocusRing::Target& t = m_ring.target();
		if (t.kind == eng::ui::FocusRing::Target::Kind::Rect) {
			if (t.index < model.hits().size()) {
				m_target.kind = HudFocusTarget::Kind::Rect;
				m_target.id = model.hits()[t.index].id;
			}
			return;
		}
		if (t.kind == eng::ui::FocusRing::Target::Kind::Grid) {
			if (t.index < model.grid_hits().size()) {
				const HudGridHit& g = model.grid_hits()[t.index];
				if (g.grid.valid()) {
					m_target.kind = HudFocusTarget::Kind::Grid;
					m_target.grid = g.grid;
					m_target.slot = t.slot;
				}
			}
		}
	}

	/// Sincroniza el modelo con el foco (la rejilla enfocada mueve su celda `focus`).
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	void sync_model(Hud<R, W, S, D, H>& model) noexcept {
		if (m_target.kind == HudFocusTarget::Kind::Grid && m_target.grid.valid()) {
			(void)model.grid_move_focus_to(m_target.grid, m_target.slot);
		}
	}

	eng::ui::FocusRing m_ring {};
	HudFocusTarget m_target {};
};

} // namespace eng::hud
