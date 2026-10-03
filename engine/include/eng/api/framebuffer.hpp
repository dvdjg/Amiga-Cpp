#pragma once

/// \file framebuffer.hpp
/// **Framebuffer indexado de nivel A** (`eng::IndexedDisplay`): resuelve el patrón *chunky→planar*
/// de un emulador o de un efecto por píxel. El juego **escribe índices** (1 byte/píxel, `0..15`) en
/// un buffer **lineal** y llama `present()`; el engine hace la conversión **C2P por Blitter** a los
/// bitplanes y publica el buffer en VBlank (swap de copperlist). El juego **no ve** planos
/// (`BPLxPT`), módulos ni `Scene` — solo índices.
///
/// ```cpp
/// struct Game {
///     eng::IndexedDisplay<4, 2> fb;                 // 16 colores, doble buffer
///     void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
///         fb.init(backend, 256, 240, palette, 16);  // reserva escena + chunky
///     }
///     void update(eng::amiga::AmigaBackend&, eng::GameContext&) {
///         eng::Span<eng::u8> px = fb.framebuffer(); // escribe índices (1 byte/píxel)
///         for (...) px[y * fb.width() + x] = color_index;
///     }
///     void render(eng::amiga::AmigaBackend&, eng::GameContext&) { fb.present(); }
/// };
/// ```
///
/// Por debajo: `graphics::composition::Scene` (bitplanes + copperlist en Chip, doble buffer) y
/// `graphics::c2p_1x1_4` (transposición 4bpp → 4 planos, la referencia portable del asm de Kalms).
/// Es el nivel A del camino del emulador NES (`docs/engine/NES_CONSUMER_GUIDE.md` §1.2); la demo
/// `demos/techniques/amiga/c2p/061_c2p_chunky_4bpl` es el nivel B equivalente.
///
/// Los `static_cast` a `u32`/`usize` son de **frontera**: dimensiones `u16` promovidas para
/// dimensionar reservas y vistas (`u16·u16` desbordaría el `int` de promoción).

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/c2p.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/palette.hpp>
#include <eng/memory/memory_manager.hpp>

namespace eng {

/// Ver doc del fichero. `Planes` = profundidad (4 = paleta de 16 índices). `Buffers` = 1 (simple) o
/// 2/3 (doble/triple): con doble, la CPU escribe el frame siguiente mientras el actual se convierte
/// y publica.
template <eng::u8 Planes = 4u, eng::u8 Buffers = 2u>
class IndexedDisplay {
public:
	static_assert(Planes == 4u, "el C2P por tiling cubre 4 planos (16 colores)");
	static_assert(Buffers >= 1u && Buffers <= 3u, "1..3 buffers de display");

	/// Reserva la **escena planar** (`Planes` bitplanes, `Buffers` buffers, copperlist en Chip) y
	/// los buffers **chunky** (1 por buffer de display). `palette` = palabras RGB444 con `colors`
	/// entradas (se emiten al arrancar). `false` si no cabe o la config no es válida para OCS.
	template <class Backend>
	[[nodiscard]] bool init(Backend& backend, eng::u16 width, eng::u16 height,
				eng::PaletteWords palette, eng::u8 colors) noexcept {
		namespace comp = eng::graphics::composition;
		auto& mm = backend.memory_manager();
		comp::SceneResources res = comp::planar(width, height, Planes);
		res.buffers = Buffers;
		res.copper_bytes = 8192u; // display + paleta + margen
		if (!comp::compose(m_scene, mm, res, comp::ocs_a500, comp::display(res),
				   comp::palette(palette, 0u, colors))) {
			return false;
		}
		for (eng::u8 b = 0u; b < Buffers; ++b) {
			m_chunky[b] = mm.chip().template reserve<eng::ChunkyTag>(
				static_cast<eng::u32>(width) * height, 4u);
			if (!m_chunky[b].valid()) {
				return false;
			}
		}
		m_w = width;
		m_h = height;
		m_scene.takeover(backend);
		return true;
	}

	/// **Framebuffer del buffer trasero**: escribe un índice (0..15) por píxel, fila a fila
	/// (`px[y * width() + x]`). Válido hasta el `present()` de este frame.
	[[nodiscard]] eng::Span<eng::u8> framebuffer() noexcept {
		return eng::Span<eng::u8> {m_chunky[m_scene.back_index()].view.data(),
					   static_cast<eng::usize>(m_w) * m_h};
	}

	/// **Publica el frame**: C2P del buffer chunky trasero a los bitplanes + swap de copperlist
	/// (VBlank). Llámalo **una vez por frame** (típicamente en `render`).
	void present() noexcept {
		const eng::u8 b = m_scene.back_index();
		eng::graphics::c2p_1x1_4(m_w, m_h, m_scene.plane_bytes(),
					 m_chunky[b].view.as_const(), m_scene.back());
		m_scene.commit();
	}

	[[nodiscard]] eng::u16 width() const noexcept { return m_w; }
	[[nodiscard]] eng::u16 height() const noexcept { return m_h; }
	[[nodiscard]] constexpr eng::u8 planes() const noexcept { return Planes; }
	[[nodiscard]] constexpr eng::u8 buffer_count() const noexcept { return Buffers; }
	/// La escena que el helper posee (para efectos/enlaces avanzados).
	[[nodiscard]] eng::graphics::composition::Scene& scene() noexcept { return m_scene; }
	[[nodiscard]] const eng::graphics::composition::Scene& scene() const noexcept { return m_scene; }

private:
	eng::graphics::composition::Scene m_scene {};
	eng::Block<eng::ChunkyTag> m_chunky[Buffers] {};
	eng::u16 m_w = 0u;
	eng::u16 m_h = 0u;
};

} // namespace eng
