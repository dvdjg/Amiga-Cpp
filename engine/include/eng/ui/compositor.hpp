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

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/frame_plan.hpp>
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
	///
	/// **CPU**: el fondo por `Surface::fill_rect` y los backings píxel a píxel. Es el camino
	/// agnóstico (sin `FramePlan`).
	void present() noexcept { compose(nullptr); }

	/// Igual, pero **acelerada**: el fondo entra por `fill_rect` (rasterizador: Blitter si el
	/// `Surface` lo tiene instalado) y cada intersección **alineada a palabra** (destino `x`, ancho
	/// y origen `sx` múltiplos de 16) se copia con `Surface::blit` (`CopyRect` por Blitter). El
	/// `plan` lo ejecuta el backend; las intersecciones no alineadas caen al bucle de píxeles.
	void present_blit(eng::graphics::FramePlan& plan) noexcept { compose(plan); }

	[[nodiscard]] eng::u8 damage_count() const noexcept { return m_damage.count; }
	[[nodiscard]] eng::u8 window_count() const noexcept { return m_count; }

private:
	/// Compone con o sin plan (`Ref` no válida = solo CPU). El fondo va por `fill_rect`; cada
	/// intersección intenta el blit planar y, si no procede, cae al bucle de píxeles.
	void compose(eng::Ref<eng::graphics::FramePlan> plan) noexcept {
		if (!m_screen.valid()) {
			return;
		}
		eng::field::Surface& scr = *m_screen;
		for (eng::u8 d = 0u; d < m_damage.count; ++d) {
			const Rect region = m_damage.rects[d];
			scr.fill_rect(region.x, region.y, region.w, region.h, m_desktop);
			for (eng::u8 i = 0u; i < m_count; ++i) {
				CompWindow& w = m_wins[m_order[i]];
				if (!w.backing.valid) {
					continue;
				}
				const Rect I = eng::intersect(region, w.frame);
				if (I.empty()) {
					continue;
				}
				if (plan.valid() && blit_backing(*plan, scr, w, I)) {
					continue;
				}
				for (eng::s16 y = I.y; y <= I.bottom(); ++y) {
					for (eng::s16 x = I.x; x <= I.right(); ++x) {
						scr.set_pixel(x, y,
							      w.backing.pixel_at(
								      static_cast<eng::s16>(x - w.frame.x),
								      static_cast<eng::s16>(y - w.frame.y)));
					}
				}
			}
		}
		m_damage.clear();
	}

	/// Copia la intersección `I` del backing de `w` a la pantalla con `Surface::blit`
	/// (`CopyRect` por Blitter). Solo cuando destino `I.x`, ancho `I.w` y origen `sx` son
	/// **múltiplos de 16** (el blit planar no desplaza bits); si no, `false` → CPU.
	[[nodiscard]] static bool blit_backing(eng::graphics::FramePlan& plan,
					       eng::field::Surface& scr, const CompWindow& w,
					       const Rect& I) noexcept {
		if ((I.x & 15) != 0 || (I.w & 15) != 0) {
			return false;
		}
		const eng::s16 sx = static_cast<eng::s16>(I.x - w.frame.x);
		const eng::s16 sy = static_cast<eng::s16>(I.y - w.frame.y);
		if (sx < 0 || sy < 0 || (sx & 15) != 0) {
			return false;
		}
		const eng::u16 rb = w.backing.playfield.bytes_per_row();
		const eng::u32 ps = w.backing.playfield.plane_stride();
		const eng::u8 np = w.backing.playfield.planes();
		if (rb == 0u || ps == 0u || np == 0u) {
			return false;
		}
		const eng::u16* base =
			reinterpret_cast<const eng::u16*>(w.backing.playfield.bitplanes().data());
		const eng::u16* src = base + static_cast<eng::u32>(sy) * (rb / 2u) +
				      static_cast<eng::u32>(sx / 16);
		const eng::u32 need = static_cast<eng::u32>(np - 1u) * (ps / 2u) +
				      static_cast<eng::u32>(I.h - 1u) * (rb / 2u) +
				      static_cast<eng::u32>(I.w / 16u);
		return scr.blit(plan, eng::Span<const eng::u16>(src, need), I.x, I.y, I.w, I.h,
				rb, ps, np);
	}

	eng::Ref<eng::field::Surface> m_screen {};
	eng::u8 m_desktop = 0u;
	CompWindow m_wins[kMaxWindows] {};
	eng::u8 m_order[kMaxWindows] {}; ///< orden de composición (atrás -> frente), índices a m_wins
	eng::u8 m_count = 0u;
	DirtyList<8> m_damage {};
};

} // namespace eng::ui
