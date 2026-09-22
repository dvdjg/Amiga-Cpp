#pragma once

/// \file compositor.hpp
/// **Compositor con backing store** (`eng::ui`, G7): cada ventana tiene su `WindowBacking`; la
/// pantalla solo **compone** rectángulos (fondo → frente). Mover, redimensionar o cambiar Z **no**
/// invalida a las vecinas: se copian trozos ya rasterizados. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §14.
///
/// El compositor no pinta widgets: la app dibuja el contenido de una ventana en su
/// `backing.surface` (con `UiPainter`) cuando `needs_repaint`; `present()` solo hace las
/// **copies** de los backings a la pantalla sobre las regiones dañadas.

#include <eng/core/ptr.hpp>
#include <eng/core/types.hpp>
#include <eng/field/surface.hpp>
#include <eng/ui/backing.hpp>
#include <eng/ui/dirty.hpp>
#include <eng/ui/theme.hpp>

namespace eng::ui {

/// Ventana del compositor: su backing y su rect en pantalla.
struct CompWindow {
	WindowBacking backing {};
	Rect frame {}; ///< rect en pantalla (coincide con el área del backing)
};

class Compositor {
public:
	static constexpr eng::u8 kMaxWindows = 8u;

	/// Fija la superficie de pantalla destino (no propietario).
	void set_screen(eng::field::Surface& s) noexcept {
		m_screen = eng::Ref<eng::field::Surface>(s);
	}
	void set_desktop(eng::u8 color) noexcept { m_desktop = color; }

	/// Añade una ventana al frente. `nullptr` si el pool está lleno.
	[[nodiscard]] CompWindow* add() noexcept {
		if (m_count >= kMaxWindows) {
			return nullptr;
		}
		CompWindow& w = m_wins[m_count];
		m_order[m_count] = m_count;
		++m_count;
		return &w;
	}

	/// Sube `w` al frente (último en el orden de composición) y daña su rect.
	void raise(CompWindow& w) noexcept {
		eng::u8 p = 0u;
		while (p < m_count && &m_wins[m_order[p]] != &w) {
			++p;
		}
		if (p >= m_count || p == static_cast<eng::u8>(m_count - 1u)) {
			return;
		}
		const eng::u8 idx = m_order[p];
		for (eng::u8 j = p; j + 1u < m_count; ++j) {
			m_order[j] = m_order[j + 1u];
		}
		m_order[m_count - 1u] = idx;
		damage_screen(w.frame);
	}

	/// Mueve `w` a `(nx, ny)`: daña origen y destino, sin tocar el contenido de las demás.
	void move_window(CompWindow& w, eng::s16 nx, eng::s16 ny) noexcept {
		const Rect old = w.frame;
		w.frame.x = nx;
		w.frame.y = ny;
		damage_screen(eng::merge(old, w.frame));
	}

	/// Redimensiona `w` si cabe en su backing; marca solo a `w` para repintar su contenido.
	bool resize_window(CompWindow& w, eng::u16 nw, eng::u16 nh) noexcept {
		if (!w.backing.valid || nw > w.backing.width || nh > w.backing.height) {
			return false;
		}
		const Rect old = w.frame;
		w.frame.w = nw;
		w.frame.h = nh;
		w.backing.needs_repaint = true;
		damage_screen(eng::merge(old, w.frame));
		return true;
	}

	/// Marca una región de pantalla para recomponer (se fusiona con las existentes).
	void damage_screen(Rect r) noexcept { m_damage.add(r); }

	/// Compone las regiones dañadas: fondo + backings (de atrás hacia delante) y limpia el daño.
	void present() noexcept {
		if (!m_screen.valid()) {
			return;
		}
		eng::field::Surface& scr = *m_screen;
		for (eng::u8 d = 0u; d < m_damage.count; ++d) {
			const Rect region = m_damage.rects[d];
			for (eng::s16 y = region.y; y <= region.bottom(); ++y) {
				for (eng::s16 x = region.x; x <= region.right(); ++x) {
					scr.set_pixel(x, y, m_desktop);
				}
			}
			for (eng::u8 i = 0u; i < m_count; ++i) {
				CompWindow& w = m_wins[m_order[i]];
				if (!w.backing.valid) {
					continue;
				}
				const Rect I = eng::intersect(region, w.frame);
				if (I.empty()) {
					continue;
				}
				for (eng::s16 y = I.y; y <= I.bottom(); ++y) {
					for (eng::s16 x = I.x; x <= I.right(); ++x) {
						scr.set_pixel(x, y,
							      w.backing.pixel_at(
								      static_cast<eng::s16>(
									      x - w.frame.x),
								      static_cast<eng::s16>(
									      y - w.frame.y)));
					}
				}
			}
		}
		m_damage.clear();
	}

	[[nodiscard]] eng::u8 damage_count() const noexcept { return m_damage.count; }
	[[nodiscard]] eng::u8 window_count() const noexcept { return m_count; }

private:
	eng::Ref<eng::field::Surface> m_screen {};
	eng::u8 m_desktop = 0u;
	CompWindow m_wins[kMaxWindows] {};
	eng::u8 m_order[kMaxWindows] {}; ///< orden de composición (atrás -> frente), índices a m_wins
	eng::u8 m_count = 0u;
	DirtyList<8> m_damage {};
};

} // namespace eng::ui
