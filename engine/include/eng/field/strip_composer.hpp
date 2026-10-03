#pragma once

/// \file strip_composer.hpp
/// **Compositor del scroller de tiras** (`StripComposer<Geom>`): emite la copperlist **una vez**
/// (con handles de las words de `BPLxPT`/`BPLCON1` y del split) y por frame **parchea** solo los
/// valores de `strip_copper_values` (fine scroll, punteros de la ventana y split), sin re-emitir.
/// Es el camino de scroll rápido (referencia en `docs/debugging/investigaciones/consulta-scroll-*`).
///
/// El llamador posee el anillo; `set_ring(base)` da su dirección (Chip) y `patch(c)` aplica el plan.
/// `build()` emite en ambos bloques del doble buffer; `install()` publica `COP1LC`.

#include <eng/core/types/types.hpp>
#include <eng/field/strip_scroller.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/double_buffer.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/palette.hpp>
#include <eng/memory/memory_manager.hpp>

namespace eng::field {

/// Valores de display del compositor de tiras (cabecera estatica + paleta).
inline constexpr eng::u16 kStripDiwStrt = 0x2c81u; ///< DIWSTRT (vpos 0x2c, hpos 0x81)
inline constexpr eng::u16 kStripDiwStop = 0x2cc1u; ///< DIWSTOP base (se ajusta por alto)
inline constexpr eng::u16 kStripDdfStrt = 0x0030u; ///< DDFSTRT con la palabra extra de scroll
inline constexpr eng::u16 kStripDdfStop = 0x00d0u; ///< DDFSTOP estandar 320

template <class Geom>
class StripComposer {
public:
	bool init(MemoryManager& mem, const eng::PaletteWords& palette, u32 copper_bytes = 1024u) noexcept {
		m_palette = palette;
		m_ok = m_copper.begin(mem, copper_bytes);
		return m_ok;
	}

	/// Fija la direccion base (Chip) del anillo (los `BPLxPT` se calculan sobre ella).
	constexpr void set_ring(const eng::u16* base) noexcept { m_base = base; }

	/// Emite la lista completa en **ambos** bloques del doble buffer (una vez).
	bool build() noexcept {
		if (!m_ok) return false;
		if (!emit()) return false;
		m_copper.flip();
		if (!emit()) return false;
		m_copper.flip();
		m_built = true;
		return true;
	}

	/// **Parchea** en el bloque inactivo los valores de Copper (BPLxPT de la ventana + BPLCON1 +
	/// split) y lo publica. No re-emite la lista.
	bool patch(const StripCopper& c) noexcept {
		if (!m_built || m_base == nullptr) return false;
		eng::u16* const w = m_copper.inactive_words();
		w[m_bplcon1_handle + 1u] = c.bplcon1;
		for (eng::u8 p = 0u; p < Geom::planes; ++p) {
			const eng::u32 addr = base_addr(c.pt_byte[p]);
			w[m_pt_handle[p][0] + 1u] = static_cast<eng::u16>(addr >> 16u);
			w[m_pt_handle[p][1] + 1u] = static_cast<eng::u16>(addr & 0xffffu);
			if constexpr (Geom::split_vertical) {
				w[m_split_pt_handle[p][0] + 1u] = static_cast<eng::u16>(addr >> 16u);
				w[m_split_pt_handle[p][1] + 1u] = static_cast<eng::u16>(addr & 0xffffu);
			}
		}
		m_copper.flip();
		return true;
	}

	template <class Backend>
	void takeover(Backend& backend) const noexcept {
		if (m_built) m_copper.takeover(backend);
	}
	template <class Backend>
	void install(Backend& backend) const noexcept {
		if (m_built) m_copper.install(backend);
	}

	[[nodiscard]] constexpr bool ok() const noexcept { return m_ok; }
	[[nodiscard]] constexpr bool built() const noexcept { return m_built; }
	[[nodiscard]] constexpr eng::u16 bplcon1_handle() const noexcept { return m_bplcon1_handle; }
	[[nodiscard]] constexpr eng::u16 pt_handle(eng::u8 plane, eng::u8 hl) const noexcept {
		return m_pt_handle[plane][hl];
	}
	[[nodiscard]] const eng::u16* debug_active_words() const noexcept { return m_copper.active_words(); }

private:
	[[nodiscard]] eng::u32 base_addr(eng::u32 off) const noexcept {
		return reinterpret_cast<eng::uintptr>(m_base) + off;
	}

	bool emit() noexcept {
		copper::SchedulerT<false> sched { m_copper.inactive_block() };
		sched.move(copper::Register::DMACON, static_cast<eng::u16>(copper::DmaSetClear |
									    copper::DmaMaster |
									    copper::DmaCopper |
									    copper::DmaBitplane));
		sched.move(copper::Register::BPLCON0,
			   static_cast<eng::u16>(0x0200u | (static_cast<eng::u16>(Geom::planes) << 12u)));
		m_bplcon1_handle = sched.move_at(copper::Register::BPLCON1, 0u);
		sched.move(copper::Register::BPLCON2, 0u);
		sched.move(copper::Register::BPL1MOD, Geom::bpl_mod);
		sched.move(copper::Register::BPL2MOD, Geom::bpl_mod);
		sched.move(copper::Register::DIWSTRT, kStripDiwStrt);
		sched.move(copper::Register::DIWSTOP, kStripDiwStop);
		sched.move(copper::Register::DDFSTRT, kStripDdfStrt);
		sched.move(copper::Register::DDFSTOP, kStripDdfStop);
		sched.emit_palette(m_palette, 0u, 32u);
		for (eng::u8 p = 0u; p < Geom::planes; ++p) {
			m_pt_handle[p][0] = sched.move_at(copper::bitplane_pointer_high_register(p), 0u);
			m_pt_handle[p][1] = sched.move_at(copper::bitplane_pointer_low_register(p), 0u);
		}
		if constexpr (Geom::split_vertical) {
			// Split: la linea de corte es `0x2c + viewport_h`. Si cruza la 255 (OCS VPOS de 8
			// bits), se usa la secuencia two-WAIT ($ffdf,$fffe + $0001,$fffe).
			if constexpr (Geom::split_crosses_255) {
				sched.wait_raw(0xffu, 0xdfu, 0xfffeu);
				sched.wait_raw(0x00u, 0x01u, 0xfffeu);
			} else {
				sched.wait_raw(static_cast<eng::u16>(Geom::split_line), 0x01u, 0xfffeu);
			}
			for (eng::u8 p = 0u; p < Geom::planes; ++p) {
				m_split_pt_handle[p][0] =
					sched.move_at(copper::bitplane_pointer_high_register(p), 0u);
				m_split_pt_handle[p][1] =
					sched.move_at(copper::bitplane_pointer_low_register(p), 0u);
			}
		}
		sched.end();
		m_ok = m_ok && sched.ok();
		return m_ok;
	}

	copper::DoubleBuffer m_copper {};
	eng::PaletteWords m_palette {};
	const eng::u16* m_base = nullptr;
	eng::u16 m_bplcon1_handle = 0u;
	eng::u16 m_pt_handle[8][2] {};
	eng::u16 m_split_pt_handle[8][2] {};
	bool m_ok = false;
	bool m_built = false;
};

} // namespace eng::field
