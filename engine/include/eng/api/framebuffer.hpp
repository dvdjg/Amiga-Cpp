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

/// Ver doc del fichero. `Planes` = profundidad (4 = 16 índices, 5 = 32, 6 = 64). `Buffers` = 1
/// (simple) o 2/3 (doble/triple): con doble, la CPU escribe el frame siguiente mientras el actual se
/// convierte y publica.
///
/// ⚠️ **Estado**: el camino de **4 planos** está verificado en hardware (demo 061_indexed_display);
/// el **C2P** de 5/6 planos (`c2p_1x1_naive`) está verificado en host (HOST-367), pero el **display**
/// de 5/6 planos aún **no renderiza** en hardware (bug abierto en la configuración de planos del
/// `Scene` para profundidades ≠ 4). Usa 4 planos hasta cerrarlo.
template <eng::u8 Planes = 4u, eng::u8 Buffers = 2u>
class IndexedDisplay {
public:
	static_assert(Planes >= 4u && Planes <= 6u, "4..6 planos (16..64 colores; 5 = 32 como la NES)");
	static_assert(Buffers >= 1u && Buffers <= 3u, "1..3 buffers de display");

	/// Reserva la **escena planar** (`Planes` bitplanes, `Buffers` buffers, copperlist en Chip) y
	/// los buffers **chunky** (1 por buffer de display). `palette` = palabras RGB444 con `colors`
	/// entradas (se emiten al arrancar). `false` si no cabe o la config no es válida para OCS.
	///
	/// `row_repeat` (`≥1`, divisor de `height`) muestra cada fila lógica `row_repeat` veces por
	/// Copper: el framebuffer chunky es de `width × (height / row_repeat)` y **cuesta
	/// `1/row_repeat` el fill por frame** (el truco de las demos 061/080). `1` = sin repetición.
	template <class Backend>
	[[nodiscard]] bool init(Backend& backend, eng::u16 width, eng::u16 height,
				eng::PaletteWords palette, eng::u8 colors,
				eng::u8 row_repeat = 1u) noexcept {
		namespace comp = eng::graphics::composition;
		if (row_repeat == 0u || (height % row_repeat) != 0u) {
			return false;
		}
		auto& mm = backend.memory_manager();
		const eng::u16 rows = static_cast<eng::u16>(height / row_repeat);
		comp::SceneResources res = comp::planar(width, height, Planes);
		res.buffers = Buffers;
		res.rows = rows; // bitmap de `rows` filas; `row_repeat` las cuadruplica en pantalla
		res.copper_bytes = 16384u; // display + paleta (hasta 64) + row_repeat + margen
		const bool composed =
			(row_repeat == 1u)
				? comp::compose(m_scene, mm, res, comp::ocs_a500, comp::display(res),
						comp::palette(palette, 0u, colors))
				: comp::compose(m_scene, mm, res, comp::ocs_a500, comp::display(res),
						comp::palette(palette, 0u, colors),
						comp::row_repeat(row_repeat, 0x2cu, 0u));
		if (!composed) {
			return false;
		}
		for (eng::u8 b = 0u; b < Buffers; ++b) {
			m_chunky[b] = mm.chip().template reserve<eng::ChunkyTag>(
				static_cast<eng::u32>(width) * rows, 4u);
			if (!m_chunky[b].valid()) {
				return false;
			}
		}
		m_w = width;
		m_h = height;
		m_rows = rows;
		m_scene.takeover(backend);
		return true;
	}

	/// **Framebuffer del buffer trasero**: escribe un índice (0..15) por píxel, fila a fila
	/// (`px[y * width() + x]`, `y < rows()`). Válido hasta el `present()` de este frame.
	[[nodiscard]] eng::Span<eng::u8> framebuffer() noexcept {
		return eng::Span<eng::u8> {m_chunky[m_scene.back_index()].view.data(),
					   static_cast<eng::usize>(m_w) * m_rows};
	}

	/// **Publica el frame**: C2P del buffer chunky trasero a los bitplanes + swap de copperlist
	/// (VBlank). Llámalo **una vez por frame** (típicamente en `render`).
	void present() noexcept {
		const eng::u8 b = m_scene.back_index();
		// 4 planos -> C2P por *merge* (`c2p_1x1_4`, el rapido); 5/6 -> transposicion generica
		// (`c2p_1x1_naive`, correcta; su port de Kalms `c2p_1x1_5/6` queda como optimizacion).
		if constexpr (Planes == 4u) {
			eng::graphics::c2p_1x1_4(m_w, m_rows, m_scene.plane_bytes(),
						 m_chunky[b].view.as_const(), m_scene.back());
		} else {
			eng::graphics::c2p_1x1_naive(m_w, m_rows, Planes, m_scene.plane_bytes(),
						     m_chunky[b].view.as_const(), m_scene.back());
		}
		m_scene.commit();
	}

	[[nodiscard]] eng::u16 width() const noexcept { return m_w; }
	/// Alto de la **pantalla** en líneas (el del display, no el del framebuffer).
	[[nodiscard]] eng::u16 height() const noexcept { return m_h; }
	/// Filas del **framebuffer** chunky (`height / row_repeat`): es lo que se escribe por frame.
	[[nodiscard]] eng::u16 rows() const noexcept { return m_rows; }
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
	eng::u16 m_rows = 0u;
};

} // namespace eng
