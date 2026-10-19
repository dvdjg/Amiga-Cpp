#pragma once

/// \file pointer.hpp
/// **Puntero de ratón del sistema de GUI** (`eng::ui`): la **flecha clásica** (cuerpo +
/// contorno negro de 16×16) sobre `HardwareCursor` — sprite de hardware, no ensucia el
/// framebuffer ni genera *damage* —, con seguimiento del ratón y recorte a los límites de
/// pantalla. `App` lo integra en el display (`GameDisplay::hardware_cursor`): reserva la
/// estructura DMA en Chip RAM, compone la etapa de Copper que emite `SPR0PT` + `SPREN` y lo
/// expone con `App::cursor()` para moverlo desde la entrada.
///
/// Verificación: **HOST-301** (estructura, emisión, flecha y controlador); demos 215/303/306.

#include <eng/core/types/types.hpp>
#include <eng/ui/hardware_cursor.hpp>

namespace eng::ui {

/// Lado del sprite (= lado de la flecha).
inline constexpr eng::u16 kPointerSize = HardwareCursor::kSize;

/// Bits de la **flecha clásica**: cuerpo (color 1, blanco) y contorno (color 2, negro) de
/// 8 vecinos menos el cuerpo. El contorno se **calcula** del cuerpo para que no se
/// desincronicen si cambia la forma.
struct PointerBits {
	eng::u16 dat[kPointerSize] {};
	eng::u16 datb[kPointerSize] {};
};

/// Calcula la flecha clásica (cuerpo relleno + contorno de 8 vecinos). Constexpr: la tabla
/// vive en ROM.
[[nodiscard]] constexpr PointerBits make_pointer_bits() noexcept {
	PointerBits b {};
	// Cuerpo: flecha rellena clásica (bit 15 = píxel más a la izquierda).
	constexpr eng::u16 body[kPointerSize] = {
		0x8000u, 0xc000u, 0xe000u, 0xf000u, 0xf800u, 0xfc00u, 0xfe00u, 0xff00u,
		0xff80u, 0xff00u, 0xf000u, 0xd800u, 0x8c00u, 0x0c00u, 0x0600u, 0x0000u,
	};
	for (eng::u16 r = 0u; r < kPointerSize; ++r) {
		b.dat[r] = body[r];
	}
	// Contorno: dilatación 8-vecina (arriba/abajo/fila actual desplazada) menos el cuerpo.
	for (eng::u16 r = 0u; r < kPointerSize; ++r) {
		const eng::u16 up = (r > 0u) ? body[r - 1u] : 0u;
		const eng::u16 cur = body[r];
		const eng::u16 dn = (r + 1u < kPointerSize) ? body[r + 1u] : 0u;
		eng::u16 dil = static_cast<eng::u16>(up | cur | dn);
		dil = static_cast<eng::u16>(dil | static_cast<eng::u16>(dil << 1) |
					    static_cast<eng::u16>(dil >> 1));
		b.datb[r] = static_cast<eng::u16>(dil & static_cast<eng::u16>(~cur));
	}
	return b;
}

/// Flecha clásica precalculada (cuerpo + contorno).
inline constexpr PointerBits kPointerArrow = make_pointer_bits();

/// **Puntero de GUI**: `HardwareCursor` + posición con recorte. La app lo mueve con los
/// deltas del ratón (`poll_mouse`) o con posiciones absolutas si su entrada es absoluta.
/// Oculto se aparca fuera de pantalla: `SPREN` es global para todos los sprites y no debe
/// tocarse para esconder solo el puntero.
struct Pointer {
	HardwareCursor cursor {};
	eng::s16 x = 0;      ///< posición actual (recortada)
	eng::s16 y = 0;
	eng::s16 max_x = 319; ///< límites inclusivos del recorte
	eng::s16 max_y = 255;
	bool hidden = false;

	/// Enlaza al buffer **Chip RAM** (`>= HardwareCursor::kBytes`) y aplica la flecha clásica.
	bool bind(eng::u8* chip, eng::u32 bytes) noexcept {
		if (!cursor.bind(chip, bytes)) {
			return false;
		}
		cursor.set_bitmap(kPointerArrow.dat, kPointerArrow.datb);
		return true;
	}

	/// Fija los límites del recorte (ancho/alto del display) y recorta la posición actual.
	void set_bounds(eng::s16 width, eng::s16 height) noexcept {
		max_x = (width > 0) ? static_cast<eng::s16>(width - 1) : 0;
		max_y = (height > 0) ? static_cast<eng::s16>(height - 1) : 0;
		clamp();
	}

	/// Coloca el puntero en una posición absoluta, recortando a los límites.
	void move_to(eng::s16 nx, eng::s16 ny) noexcept {
		x = nx;
		y = ny;
		clamp();
	}

	/// Desplazamiento relativo (el ratón de Amiga entrega deltas).
	void move_by(eng::s16 dx, eng::s16 dy) noexcept {
		move_to(static_cast<eng::s16>(x + dx), static_cast<eng::s16>(y + dy));
	}

	/// Muestra u oculta el puntero. Oculto vacía el bitmap (sprite transparente) en vez de
	/// moverlo fuera: VSTART/VSTOP son campos de 8 bits y la pantalla canónica ocupa las 256
	/// líneas, así que no hay posición vertical "fuera" dentro del campo.
	void set_hidden(bool h) noexcept {
		if (h == hidden) {
			return;
		}
		hidden = h;
		apply_bits();
	}

	/// Emite `SPR0PT` + `DMACON` (SPREN) a la copperlist (una vez, al componer el display).
	void emit_into(eng::copper::Scheduler& s) const noexcept {
		if (cursor.valid()) {
			cursor.emit_into(s);
		}
	}

	[[nodiscard]] bool valid() const noexcept { return cursor.valid(); }

private:
	void clamp() noexcept {
		if (x < 0) {
			x = 0;
		}
		if (y < 0) {
			y = 0;
		}
		if (x > max_x) {
			x = max_x;
		}
		if (y > max_y) {
			y = max_y;
		}
		apply_position();
	}

	/// Posición en la estructura DMA (camino caliente del movimiento: 2 palabras).
	void apply_position() noexcept {
		if (cursor.valid()) {
			cursor.set_position(x, y);
		}
	}

	/// Bitmap (solo al enlazar o al mostrar/ocultar: 32 palabras).
	void apply_bits() noexcept {
		if (cursor.valid()) {
			cursor.set_bitmap(hidden ? nullptr : kPointerArrow.dat,
					  hidden ? nullptr : kPointerArrow.datb);
		}
	}
};

} // namespace eng::ui
