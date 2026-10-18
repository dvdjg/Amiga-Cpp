#pragma once

/// \file interaction.hpp
/// **Núcleo de interacción de la GUI** (`eng::ui`): las primitivas comunes a **toda** interfaz
/// — aplicaciones (`eng::ui`: ventanas, menús, edición) y HUD de juego (`eng::hud`) — sin
/// arrastrar el árbol de ventanas, el *backing* ni el compositor. Es un **header hoja**
/// (solo tipos y `constexpr`): quien lo incluye paga únicamente lo que usa.
///
/// ## Por qué está aquí y no en el HUD
///
/// El *hover*, el *hit-test* por capas/fórmula, el anillo de foco con navegación **geométrica**
/// y la semántica de *click*/*activación* son **conceptos de interfaz de usuario**, no del
/// juego: el HUD **se construye sobre este núcleo** (dirección de dependencia `hud → ui`),
/// igual que los widgets de `eng::ui`. El HUD añade lo suyo (regiones, franja, medidores,
/// rejillas de inventario, puente RPG) y reutiliza estas primitivas tal cual.
///
/// ## Piezas
///
/// - **Formas de *hit*** (`HitRectLike`/`HitGridLike`, por concepto): rectángulos con capa
///   (mayor = delante) y rejillas con fórmula `(p − origen) / pitch`. Cada capa define sus
///   entradas (el HUD añade el `GridHandle`; la GUI, su `Widget`) — el núcleo solo lee los
///   campos que necesita.
/// - **`hit_test`**: punto → entrada (rect por capa; si no, rejilla por fórmula). La **misma**
///   función resuelve el ratón y el foco: una sola fuente de verdad (`HUD_SYSTEM.md` §7.1).
/// - **`FocusRing`**: anillo de foco navegable por **geometría** (centro contra centro, el eje
///   del movimiento manda; dentro de una rejilla mueve la celda y en el borde salta al vecino),
///   `next` en orden de empuje (`Tab`) y **activación** devuelta al llamador (el HUD selecciona
///   la celda; la GUI entrega el evento al widget).
///
/// Verificación: los casos de foco de **HOST-469** (HUD) y **HOST-225** (GUI) ejercitan este
/// núcleo desde ambos consumidores.

#include <eng/core/types/box.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::ui {

/// Concepto de entrada de *hit* rectangular: caja, id lógico y capa (mayor = delante).
template <class T>
concept HitRectLike = requires(const T& t) {
	t.box;
	t.id;
	t.layer;
};

/// Concepto de entrada de *hit* de rejilla: área, celdas, paso y primer id lógico.
template <class T>
concept HitGridLike = requires(const T& t) {
	t.area;
	t.cols;
	t.rows;
	t.pitch_x;
	t.pitch_y;
	t.first_id;
};

/// *Hit* resuelto: `kind` dice si tocó un rect o una celda de rejilla; `index` es el índice de
/// la entrada en su lista (el llamador lo mapea a su handle/widget) y `slot`/`id` la celda y
/// el id lógico (rect: su id; rejilla: `first_id + índice de celda`).
struct HitResult {
	enum class Kind : eng::u8 { None = 0, Rect = 1, Grid = 2 };
	Kind kind = Kind::None;
	eng::u16 index = 0u; ///< índice en la lista de rects o de rejillas
	eng::u16 slot = 0u;  ///< índice de celda dentro de la rejilla (0 si rect)
	eng::u16 id = 0u;    ///< id lógico (rect: su id; rejilla: `first_id + slot`)
	eng::u8 layer = 0u;  ///< capa del rect (0 si rejilla)
	[[nodiscard]] constexpr bool hit() const noexcept { return kind != Kind::None; }
};

/// Resuelve el *hit* de `(x, y)` (coordenadas de la región/widget): **rectángulos por capa**
/// (mayor delante; empate = el último empujado, el orden de pintado) y, si ninguno toca,
/// **rejillas por fórmula** (`(p − origen) / pitch`, división entera) en orden de empuje
/// (gana la primera). Misma transformación que el pintado (`HUD_SYSTEM.md` §7.4).
template <HitRectLike R, HitGridLike G>
[[nodiscard]] constexpr HitResult hit_test(eng::Span<const R> rects, eng::Span<const G> grids,
					   eng::s16 x, eng::s16 y) noexcept {
	HitResult best {};
	bool found = false;
	eng::u8 best_layer = 0u;
	for (eng::usize i = 0u; i < rects.size(); ++i) {
		const R& r = rects[i];
		if (!r.box.contains(x, y)) {
			continue;
		}
		if (!found || r.layer >= best_layer) {
			best.kind = HitResult::Kind::Rect;
			best.index = static_cast<eng::u16>(i);
			best.id = r.id;
			best.layer = r.layer;
			best.slot = 0u;
			best_layer = r.layer;
			found = true;
		}
	}
	if (found) {
		return best;
	}
	for (eng::usize i = 0u; i < grids.size(); ++i) {
		const G& g = grids[i];
		if (g.cols == 0u || g.rows == 0u || g.pitch_x == 0u || g.pitch_y == 0u) {
			continue;
		}
		if (!g.area.contains(x, y) || x < g.area.x || y < g.area.y) {
			continue;
		}
		const eng::u16 col = static_cast<eng::u16>(static_cast<eng::u16>(x - g.area.x) /
							   g.pitch_x);
		const eng::u16 row = static_cast<eng::u16>(static_cast<eng::u16>(y - g.area.y) /
							   g.pitch_y);
		if (col >= g.cols || row >= g.rows) {
			continue;
		}
		const eng::u16 slot = static_cast<eng::u16>(
			static_cast<eng::u16>(row * g.cols) + col);
		best.kind = HitResult::Kind::Grid;
		best.index = static_cast<eng::u16>(i);
		best.slot = slot;
		best.id = static_cast<eng::u16>(g.first_id + slot);
		best.layer = 0u;
		return best;
	}
	return best;
}

/// **Anillo de foco** genérico: navegación geométrica + orden (`Tab`) sobre las **mismas**
/// listas del *hit-test*. El llamador aporta las listas (sus entradas con `box`/`layer` o
/// `area`/pitch) y actúa sobre el objetivo devuelto (el HUD selecciona la celda; la GUI pasa
/// el evento al widget). No impone layout ni árbol.
class FocusRing {
public:
	/// Objetivo enfocado: la entrada de rect o la celda de una rejilla.
	struct Target {
		enum class Kind : eng::u8 { None = 0, Rect = 1, Grid = 2 };
		Kind kind = Kind::None;
		eng::u16 index = 0u; ///< índice en la lista (rects o rejillas)
		eng::u16 slot = 0u;  ///< celda de la rejilla (0 si rect)
	};

	/// Resultado de una operación de foco.
	struct Result {
		bool moved = false;
		eng::u16 index = 0u;
		eng::u16 slot = 0u;
	};

	[[nodiscard]] const Target& target() const noexcept { return m_target; }
	[[nodiscard]] bool active() const noexcept { return m_target.kind != Target::Kind::None; }
	void reset() noexcept { m_target = Target {}; }

	/// Enfoca la primera entrada (el primer rect; si no, la primera rejilla).
	template <HitRectLike R, HitGridLike G>
	[[nodiscard]] bool focus_first(eng::Span<const R> rects, eng::Span<const G> grids) noexcept {
		if (!rects.empty()) {
			m_target = Target {};
			m_target.kind = Target::Kind::Rect;
			m_target.index = 0u;
			return true;
		}
		if (!grids.empty()) {
			m_target = Target {};
			m_target.kind = Target::Kind::Grid;
			m_target.index = 0u;
			m_target.slot = 0u;
			return true;
		}
		return false;
	}

	/// Fija el foco en una entrada rectangular (p. ej. desde un *hover* del ratón).
	void focus_rect(eng::u16 rect_index) noexcept {
		m_target = Target {};
		m_target.kind = Target::Kind::Rect;
		m_target.index = rect_index;
	}

	/// Fija el foco en una celda de rejilla (p. ej. desde un *hover* del ratón).
	void focus_grid_cell(eng::u16 grid_index, eng::u16 slot) noexcept {
		m_target = Target {};
		m_target.kind = Target::Kind::Grid;
		m_target.index = grid_index;
		m_target.slot = slot;
	}

	/// Mueve el foco en `(dx, dy)`: dentro de la rejilla mueve la celda (si cabe); en el borde
	/// (o entre entradas) salta al **más cercano en esa dirección** (centro contra centro; el
	/// eje del movimiento manda). `moved` indica si cambió.
	template <HitRectLike R, HitGridLike G>
	[[nodiscard]] Result move(eng::Span<const R> rects, eng::Span<const G> grids, eng::s8 dx,
				  eng::s8 dy) noexcept {
		Result out {};
		if (m_target.kind == Target::Kind::Grid && m_target.index < grids.size()) {
			const G& g = grids[m_target.index];
			const eng::s16 nc = static_cast<eng::s16>(m_target.slot % g.cols) + dx;
			const eng::s16 nr = static_cast<eng::s16>(m_target.slot / g.cols) + dy;
			if (nc >= 0 && nc < static_cast<eng::s16>(g.cols) && nr >= 0 &&
			    nr < static_cast<eng::s16>(g.rows)) {
				const eng::u16 next = static_cast<eng::u16>(
					static_cast<eng::u16>(nr * g.cols) + static_cast<eng::u16>(nc));
				if (next != m_target.slot) {
					m_target.slot = next;
					out.moved = true;
					out.index = m_target.index;
					out.slot = next;
					return out;
				}
			}
		}
		eng::s16 cx = 0;
		eng::s16 cy = 0;
		if (!current_center(rects, grids, cx, cy)) {
			return out;
		}
		bool found = false;
		eng::s32 best = 0;
		Target best_t {};
		for (eng::usize i = 0u; i < rects.size(); ++i) {
			const R& r = rects[i];
			if (m_target.kind == Target::Kind::Rect && m_target.index == i) {
				continue;
			}
			const eng::s16 rx = static_cast<eng::s16>(r.box.x + r.box.w / 2);
			const eng::s16 ry = static_cast<eng::s16>(r.box.y + r.box.h / 2);
			if (!in_direction(cx, cy, rx, ry, dx, dy)) {
				continue;
			}
			const eng::s32 d = distance(cx, cy, rx, ry);
			if (!found || d < best) {
				best = d;
				found = true;
				best_t = Target {};
				best_t.kind = Target::Kind::Rect;
				best_t.index = static_cast<eng::u16>(i);
			}
		}
		for (eng::usize i = 0u; i < grids.size(); ++i) {
			const G& g = grids[i];
			if (g.cols == 0u || g.rows == 0u) {
				continue;
			}
			if (m_target.kind == Target::Kind::Grid && m_target.index == i) {
				continue;
			}
			const eng::s16 gx = static_cast<eng::s16>(g.area.x + g.area.w / 2);
			const eng::s16 gy = static_cast<eng::s16>(g.area.y + g.area.h / 2);
			if (!in_direction(cx, cy, gx, gy, dx, dy)) {
				continue;
			}
			const eng::s32 d = distance(cx, cy, gx, gy);
			if (!found || d < best) {
				best = d;
				found = true;
				best_t = Target {};
				best_t.kind = Target::Kind::Grid;
				best_t.index = static_cast<eng::u16>(i);
				best_t.slot = nearest_cell(g, cx, cy);
			}
		}
		if (!found) {
			return out;
		}
		m_target = best_t;
		out.moved = true;
		out.index = m_target.index;
		out.slot = m_target.slot;
		return out;
	}

	/// Recorre el anillo en orden de empuje (rects y luego rejillas; circular).
	template <HitRectLike R, HitGridLike G>
	[[nodiscard]] Result next(eng::Span<const R> rects, eng::Span<const G> grids,
				  bool forward) noexcept {
		Result out {};
		const eng::usize total = rects.size() + grids.size();
		if (total == 0u) {
			return out;
		}
		eng::usize idx = 0u;
		if (m_target.kind == Target::Kind::Rect) {
			idx = m_target.index < rects.size() ? m_target.index : 0u;
		} else if (m_target.kind == Target::Kind::Grid) {
			idx = rects.size() + (m_target.index < grids.size() ? m_target.index : 0u);
		}
		idx = forward ? (idx + 1u) % total : (idx + total - 1u) % total;
		m_target = Target {};
		if (idx < rects.size()) {
			m_target.kind = Target::Kind::Rect;
			m_target.index = static_cast<eng::u16>(idx);
		} else {
			m_target.kind = Target::Kind::Grid;
			m_target.index = static_cast<eng::u16>(idx - rects.size());
			m_target.slot = 0u;
		}
		out.moved = true;
		out.index = m_target.index;
		out.slot = m_target.slot;
		return out;
	}

	/// Centro (en la lista) del objetivo enfocado: la caja del rect o la celda de la rejilla.
	template <HitRectLike R, HitGridLike G>
	[[nodiscard]] bool current_center(eng::Span<const R> rects, eng::Span<const G> grids,
					  eng::s16& x, eng::s16& y) const noexcept {
		if (m_target.kind == Target::Kind::Rect && m_target.index < rects.size()) {
			const R& r = rects[m_target.index];
			x = static_cast<eng::s16>(r.box.x + r.box.w / 2);
			y = static_cast<eng::s16>(r.box.y + r.box.h / 2);
			return true;
		}
		if (m_target.kind == Target::Kind::Grid && m_target.index < grids.size()) {
			const G& g = grids[m_target.index];
			x = cell_center_x(g, m_target.slot);
			y = cell_center_y(g, m_target.slot);
			return true;
		}
		return false;
	}

private:
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

	[[nodiscard]] static eng::s32 distance(eng::s16 cx, eng::s16 cy, eng::s16 px,
					       eng::s16 py) noexcept {
		const eng::s32 ax = static_cast<eng::s32>(px) - cx;
		const eng::s32 ay = static_cast<eng::s32>(py) - cy;
		return ax * ax + ay * ay;
	}

	template <HitGridLike G>
	[[nodiscard]] static eng::u16 nearest_cell(const G& g, eng::s16 cx, eng::s16 cy) noexcept {
		const eng::s16 rx = static_cast<eng::s16>(cx - g.area.x);
		const eng::s16 ry = static_cast<eng::s16>(cy - g.area.y);
		eng::u16 col = rx <= 0 ? 0u : static_cast<eng::u16>(rx / g.pitch_x);
		eng::u16 row = ry <= 0 ? 0u : static_cast<eng::u16>(ry / g.pitch_y);
		if (col >= g.cols) col = static_cast<eng::u16>(g.cols - 1u);
		if (row >= g.rows) row = static_cast<eng::u16>(g.rows - 1u);
		return static_cast<eng::u16>(row * g.cols + col);
	}

	template <HitGridLike G>
	[[nodiscard]] static eng::s16 cell_center_x(const G& g, eng::u16 slot) noexcept {
		const eng::u16 col = static_cast<eng::u16>(slot % g.cols);
		return static_cast<eng::s16>(g.area.x + col * g.pitch_x + g.pitch_x / 2);
	}

	template <HitGridLike G>
	[[nodiscard]] static eng::s16 cell_center_y(const G& g, eng::u16 slot) noexcept {
		const eng::u16 row = static_cast<eng::u16>(slot / g.cols);
		return static_cast<eng::s16>(g.area.y + row * g.pitch_y + g.pitch_y / 2);
	}

	Target m_target {};
};

} // namespace eng::ui
