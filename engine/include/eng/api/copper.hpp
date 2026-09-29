#pragma once

/// \file copper.hpp
/// **Fachada de Copper de alto nivel** (`eng::Copper`): construye la copperlist por **intención**
/// (`wait_line` + acción) sin nombrar registros ni `WAIT`/`MOVE`. Es general (juegos y
/// consumidores externos); envuelve el `copper::Scheduler` del `copper::Plan` del `Scene`.
///
/// El ciclo de vida (doble buffer, `begin`/`commit` e instalación) lo lleva el `Scene`/`Device`
/// (`Scene::begin_build`/`end_build`, `app.present()`, `device.commit_copper`); esta fachada solo
/// **emite** la parte que cambia por líneas/colores.
///
/// ```cpp
/// eng::Copper c { scene.scheduler() };
/// c.set_palette(pal);
/// c.wait_line(120);
/// c.set_color(1, 0x0f00);
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>

namespace eng {

/// Constructor de copperlist orientado a intención sobre un `copper::Scheduler`.
class Copper {
public:
	explicit constexpr Copper(copper::Scheduler& sched) noexcept : m_sched(sched) {}

	/// Espera a la línea raster `line` (0..255) antes de los MOVEs siguientes.
	void wait_line(u16 line) noexcept { m_sched.wait_line(static_cast<u8>(line)); }

	/// Fija `COLOR<index>` (0..31) al color RGB444 `0x0RGB`.
	void set_color(u8 index, u16 color) noexcept {
		m_sched.move(copper::color_register(index), color);
	}

	/// Emite un tramo de paleta (`first..first+count`).
	void set_palette(eng::PaletteWords colors, u8 first = 0u, u8 count = 32u) noexcept {
		m_sched.emit_palette(colors, first, count);
	}

	/// Scroll fino del playfield 1 (`BPLCON1`): delay de 0..15 en cada nibble.
	void set_scroll(u16 bplcon1) noexcept { m_sched.move(copper::Register::BPLCON1, bplcon1); }

	/// Palabras de Copper escritas hasta ahora (presupuesto consumido; el libre lo da el `Plan`).
	[[nodiscard]] u16 words_used() const noexcept { return m_sched.words_used(); }

private:
	copper::Scheduler& m_sched;
};

} // namespace eng
