#pragma once

/// \file backing.hpp
/// **Backing store de ventana** (`eng::ui`, G7): un lienzo planar contiguo (Chip RAM en Amiga)
/// con su `Surface` de dibujo. Los widgets pintan sobre el backing; el compositor copia trozos ya
/// rasterizados a la pantalla, de modo que mover/redimensionar/cambiar Z **no** invalida a las
/// vecinas. Ver `docs/engine/architecture/GUI_LIBRARY.md` §14.1.
///
/// No posee memoria: se enlaza con `bind(memory, bytes, w, h, depth)` (el pool la reserva).

#include <eng/core/types.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/surface.hpp>

namespace eng::ui {

struct WindowBacking {
	eng::field::ContiguousPlayfield playfield {};
	eng::field::Surface surface {};
	eng::u16 width = 0u;
	eng::u16 height = 0u;
	eng::u8 depth = 0u;
	bool valid = false;
	bool needs_repaint = true; ///< el contenido debe redibujarse al backing

	/// Enlaza el backing a `bytes` de memoria (planos contiguos) y prepara su `Surface`.
	bool bind(eng::u8* memory, eng::u32 bytes, eng::u16 w, eng::u16 h, eng::u8 d) noexcept {
		if (!playfield.bind_raw(memory, bytes, w, h, d)) {
			return false;
		}
		width = w;
		height = h;
		depth = d;
		valid = true;
		needs_repaint = true;
		surface = eng::field::Surface {playfield,
					       eng::field::SurfaceRect {0, 0, w, h}};
		return true;
	}

	/// Color del píxel `(x, y)` del backing (mapeo planar, MSB primero).
	[[nodiscard]] eng::u8 pixel_at(eng::s16 x, eng::s16 y) const noexcept {
		const eng::u8* base = playfield.bitplanes().data();
		const eng::u16 rb = playfield.bytes_per_row();
		const eng::u32 ps = playfield.plane_stride();
		const eng::u8 np = playfield.planes();
		eng::u8 c = 0u;
		for (eng::u8 p = 0u; p < np; ++p) {
			const eng::u16* word = reinterpret_cast<const eng::u16*>(
				base + static_cast<eng::u32>(p) * ps +
				static_cast<eng::u32>(y) * rb +
				static_cast<eng::u32>(x / 16) * 2u);
			const eng::u8 bit =
				static_cast<eng::u8>((*word >> (15u - (x & 15))) & 1u);
			c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
		}
		return c;
	}
};

} // namespace eng::ui
