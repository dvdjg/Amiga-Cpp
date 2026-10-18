#pragma once

/// \file focus.hpp
/// **Foco del HUD** (`eng::hud`): navegación por **todos** los elementos interactivos de una
/// región (botones/rects, celdas de rejilla, verbos) con teclado/joystick, activación y
/// *hover*. Es la capa que convierte el HUD en «una pantalla donde funciona el sistema de
/// GUI» (`HUD_SYSTEM.md` §7), **sin atar la interfaz**: no impone layout — cualquier
/// combinación de widgets con geometría participa, y el juego decide qué pinta.
///
/// ## Capas (una sola fuente de verdad por nivel)
///
/// ```text
///   modelo (model.hpp)      estado + GEOMETRIA (box, area/pitch de rejillas)
///        │  hits()/grid_hits()  (lo que empuja el juego: el mismo mapa del raton)
///        ▼
///   hit-test (input.hpp)    punto -> elemento (rect por capa, celda por formula)
///        ▼
///   entrada (HudInput)      eventos -> click/seleccion (raton) y pasos (joystick/accion)
///        ▼
///   foco (HudFocus)         anillo navegable por geometria + activacion + hover
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

/// **Anillo de foco** del HUD: estado (el elemento enfocado) + navegación geométrica.
class HudFocus {
public:
	/// Elemento enfocado.
	[[nodiscard]] const HudFocusTarget& target() const noexcept { return m_target; }
	/// ¿Hay foco?
	[[nodiscard]] bool active() const noexcept { return m_target.kind != HudFocusTarget::Kind::None; }
	/// Quita el foco (cierre de panel, cambio de región).
	void reset() noexcept { m_target = HudFocusTarget {}; }

	/// Enfoca el **primer** elemento del modelo (el primer rect empujado o la primera
	/// rejilla). `false` si no hay elementos interactivos.
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] bool focus_first(Hud<R, W, S, D, H>& model) noexcept {
		const eng::Span<const HudHitRect> rects = model.hits();
		if (!rects.empty()) {
			m_target = HudFocusTarget {};
			m_target.kind = HudFocusTarget::Kind::Rect;
			m_target.id = rects[0].id;
			return true;
		}
		const eng::Span<const HudGridHit> grids = model.grid_hits();
		if (!grids.empty() && grids[0].grid.valid()) {
			m_target = HudFocusTarget {};
			m_target.kind = HudFocusTarget::Kind::Grid;
			m_target.grid = grids[0].grid;
			m_target.slot = model.grid(grids[0].grid).focus;
			return true;
		}
		return false;
	}

	/// Sincroniza el foco con un *hit* del ratón (hover): si toca una celda de rejilla, el
	/// foco pasa a ella; si toca un rect, al rect. `false` si el hit no es interactivo.
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] bool focus_from_hit(Hud<R, W, S, D, H>& model, const HudHit& hit) noexcept {
		if (!hit.hit) {
			return false;
		}
		m_target = HudFocusTarget {};
		if (hit.grid.valid()) {
			m_target.kind = HudFocusTarget::Kind::Grid;
			m_target.grid = hit.grid;
			m_target.slot = hit.slot;
			(void)model.grid_move_focus_to(hit.grid, hit.slot);
			return true;
		}
		m_target.kind = HudFocusTarget::Kind::Rect;
		m_target.id = hit.id;
		return true;
	}

	/// Mueve el foco en `(dx, dy)` (flechas/joystick). Dentro de una rejilla mueve su celda;
	/// en el borde (o entre widgets) salta al elemento **más cercano en esa dirección**
	/// (centro contra centro; el eje del movimiento manda). Devuelve si se movió.
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] HudFocusResult move(Hud<R, W, S, D, H>& model, eng::s8 dx, eng::s8 dy) noexcept {
		HudFocusResult out {};
		if (m_target.kind == HudFocusTarget::Kind::Grid && m_target.grid.valid()) {
			if (model.grid_move_focus(m_target.grid, dx, dy)) {
				m_target.slot = model.grid(m_target.grid).focus;
				out.moved = true;
				out.grid = m_target.grid;
				out.slot = m_target.slot;
				return out;
			}
		}
		// Salto entre elementos: centro del actual contra los centros de los demás.
		eng::s16 cx = 0;
		eng::s16 cy = 0;
		if (!current_center(model, cx, cy)) {
			return out;
		}
		eng::s32 best = 0;
		HudFocusTarget best_t {};
		const eng::Span<const HudHitRect> rects = model.hits();
		for (eng::usize i = 0u; i < rects.size(); ++i) {
			const HudHitRect& r = rects[i];
			if (m_target.kind == HudFocusTarget::Kind::Rect && r.id == m_target.id) {
				continue;
			}
			const eng::s16 rx = static_cast<eng::s16>(r.box.x + r.box.w / 2);
			const eng::s16 ry = static_cast<eng::s16>(r.box.y + r.box.h / 2);
			if (!in_direction(cx, cy, rx, ry, dx, dy)) {
				continue;
			}
			const eng::s32 d = distance(cx, cy, rx, ry);
			if (best_t.kind == HudFocusTarget::Kind::None || d < best) {
				best = d;
				best_t = HudFocusTarget {};
				best_t.kind = HudFocusTarget::Kind::Rect;
				best_t.id = r.id;
			}
		}
		const eng::Span<const HudGridHit> grids = model.grid_hits();
		for (eng::usize i = 0u; i < grids.size(); ++i) {
			const HudGridHit& g = grids[i];
			if (!g.grid.valid() || g.cols == 0u || g.rows == 0u) {
				continue;
			}
			// El candidato se mide por el **centro de su caja** (como los rects); la celda de
			// entrada se calcula al aterrizar (la más cercana a la posición de origen).
			const eng::s16 gx = static_cast<eng::s16>(g.area.x + g.area.w / 2);
			const eng::s16 gy = static_cast<eng::s16>(g.area.y + g.area.h / 2);
			if (m_target.kind == HudFocusTarget::Kind::Grid &&
			    g.grid.index == m_target.grid.index) {
				continue;
			}
			if (!in_direction(cx, cy, gx, gy, dx, dy)) {
				continue;
			}
			const eng::s32 d = distance(cx, cy, gx, gy);
			if (best_t.kind == HudFocusTarget::Kind::None || d < best) {
				best = d;
				best_t = HudFocusTarget {};
				best_t.kind = HudFocusTarget::Kind::Grid;
				best_t.grid = g.grid;
				best_t.slot = nearest_cell(g, cx, cy);
			}
		}
		if (best_t.kind == HudFocusTarget::Kind::None) {
			return out;
		}
		m_target = best_t;
		if (m_target.kind == HudFocusTarget::Kind::Grid && m_target.grid.valid()) {
			(void)model.grid_move_focus_to(m_target.grid, m_target.slot);
		}
		out.moved = true;
		out.grid = m_target.grid;
		out.slot = m_target.slot;
		out.id = m_target.id;
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
		const eng::usize n_rects = model.hits().size();
		const eng::usize n_grids = model.grid_hits().size();
		const eng::usize total = n_rects + n_grids;
		if (total == 0u) {
			return out;
		}
		eng::usize idx = 0u;
		if (m_target.kind == HudFocusTarget::Kind::Rect) {
			const eng::Span<const HudHitRect> rects = model.hits();
			for (eng::usize i = 0u; i < rects.size(); ++i) {
				if (rects[i].id == m_target.id) {
					idx = i;
					break;
				}
			}
		} else if (m_target.kind == HudFocusTarget::Kind::Grid) {
			idx = n_rects;
			const eng::Span<const HudGridHit> grids = model.grid_hits();
			for (eng::usize i = 0u; i < grids.size(); ++i) {
				if (grids[i].grid.index == m_target.grid.index) {
					idx = n_rects + i;
					break;
				}
			}
		}
		idx = forward ? (idx + 1u) % total : (idx + total - 1u) % total;
		if (idx < n_rects) {
			m_target = HudFocusTarget {};
			m_target.kind = HudFocusTarget::Kind::Rect;
			m_target.id = model.hits()[idx].id;
		} else {
			const HudGridHit& g = model.grid_hits()[idx - n_rects];
			if (!g.grid.valid()) {
				return out;
			}
			m_target = HudFocusTarget {};
			m_target.kind = HudFocusTarget::Kind::Grid;
			m_target.grid = g.grid;
			m_target.slot = model.grid(g.grid).focus;
		}
		out.moved = true;
		out.grid = m_target.grid;
		out.slot = m_target.slot;
		out.id = m_target.id;
		return out;
	}

private:
	/// Centro del elemento enfocado (rect: su caja; rejilla: su celda con foco).
	template <eng::u16 R, eng::u16 W, eng::u16 S, eng::u16 D, eng::u16 H>
	[[nodiscard]] bool current_center(const Hud<R, W, S, D, H>& model, eng::s16& x,
					  eng::s16& y) const noexcept {
		if (m_target.kind == HudFocusTarget::Kind::Rect) {
			const eng::Span<const HudHitRect> rects = model.hits();
			for (eng::usize i = 0u; i < rects.size(); ++i) {
				if (rects[i].id == m_target.id) {
					x = static_cast<eng::s16>(rects[i].box.x + rects[i].box.w / 2);
					y = static_cast<eng::s16>(rects[i].box.y + rects[i].box.h / 2);
					return true;
				}
			}
			return false;
		}
		if (m_target.kind == HudFocusTarget::Kind::Grid) {
			const eng::Span<const HudGridHit> grids = model.grid_hits();
			for (eng::usize i = 0u; i < grids.size(); ++i) {
				if (grids[i].grid.index == m_target.grid.index) {
					const eng::u16 slot = model.grid(m_target.grid).focus;
					x = cell_center_x(grids[i], slot);
					y = cell_center_y(grids[i], slot);
					return true;
				}
			}
		}
		return false;
	}

	/// ¿`(px,py)` cae en la dirección `(dx,dy)` desde `(cx,cy)`? El eje del movimiento manda:
	/// en horizontal exige `px` en el lado y descarta los que caen "detrás" en ese eje.
	[[nodiscard]] static bool in_direction(eng::s16 cx, eng::s16 cy, eng::s16 px, eng::s16 py,
					       eng::s8 dx, eng::s8 dy) noexcept {
		const eng::s32 ax = static_cast<eng::s32>(px) - cx;
		const eng::s32 ay = static_cast<eng::s32>(py) - cy;
		if (dx > 0 && ax <= 0) return false;
		if (dx < 0 && ax >= 0) return false;
		if (dy > 0 && ay <= 0) return false;
		if (dy < 0 && ay >= 0) return false;
		return true;
	}

	/// Distancia al cuadrado (ponderada por el eje del movimiento para favorecer la misma
	/// fila/columna).
	[[nodiscard]] static eng::s32 distance(eng::s16 cx, eng::s16 cy, eng::s16 px,
					       eng::s16 py) noexcept {
		const eng::s32 ax = static_cast<eng::s32>(px) - cx;
		const eng::s32 ay = static_cast<eng::s32>(py) - cy;
		return ax * ax + ay * ay;
	}

	[[nodiscard]] static eng::u16 nearest_cell(const HudGridHit& g, eng::s16 cx,
						   eng::s16 cy) noexcept {
		const eng::s16 rx = static_cast<eng::s16>(cx - g.area.x);
		const eng::s16 ry = static_cast<eng::s16>(cy - g.area.y);
		eng::u16 col = rx <= 0 ? 0u : static_cast<eng::u16>(rx / g.pitch_x);
		eng::u16 row = ry <= 0 ? 0u : static_cast<eng::u16>(ry / g.pitch_y);
		if (col >= g.cols) col = static_cast<eng::u16>(g.cols - 1u);
		if (row >= g.rows) row = static_cast<eng::u16>(g.rows - 1u);
		return static_cast<eng::u16>(row * g.cols + col);
	}

	[[nodiscard]] static eng::s16 cell_center_x(const HudGridHit& g, eng::u16 slot) noexcept {
		const eng::u16 col = static_cast<eng::u16>(slot % g.cols);
		return static_cast<eng::s16>(g.area.x + col * g.pitch_x + g.pitch_x / 2);
	}

	[[nodiscard]] static eng::s16 cell_center_y(const HudGridHit& g, eng::u16 slot) noexcept {
		const eng::u16 row = static_cast<eng::u16>(slot / g.cols);
		return static_cast<eng::s16>(g.area.y + row * g.pitch_y + g.pitch_y / 2);
	}

	HudFocusTarget m_target {};
};

} // namespace eng::hud
