#pragma once

/// \file planar_patch.hpp
/// **Generador de patrones planares en compilación** (`eng::graphics`): convierte una
/// función `pixel(y, x) -> color` (índice 0..15) en las palabras que el Blitter copia tal
/// cual. Formato: **interleaved 4bpp por fila de píxel** — `[plano0..plano3]` de la fila 0,
/// luego `[plano0..plano3]` de la fila 1… — el mismo orden físico del bitmap de pantalla,
/// así que una copia es **un solo job** sin máscara ni shift (`copy_now` con `cmod = 0`).
///
/// Promovido al engine desde las demos RPG (001/400): genera hojas de sprites, tiles y
/// tiras de HUD en ROM para copiarlas a Chip en el setup, sin assets externos.
///
/// Restricciones (del formato): `W` múltiplo de 16; `Words == (W/16) × 4 × H`; cada pixel
/// usa 4 bits (color 0..15).
///
/// Verificación: HOST-464.

#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>

namespace eng::graphics {

/// Construye el bloque planar de un parche `W`×`H` con `pixel(y, x)`; `Words` debe ser
/// `(W/16) × 4 × H` palabras. `constexpr`: el parche nace en ROM sin código de runtime.
template <eng::usize Words, eng::u8 W, eng::u8 H, class Fn>
[[nodiscard]] constexpr eng::util::Array<eng::u16, Words> make_planar_patch(Fn pixel) {
	static_assert(W % 16u == 0u, "make_planar_patch: el ancho debe ser multiplo de 16 px");
	static_assert(Words == (W / 16u) * 4u * H,
		      "make_planar_patch: Words = (W/16) x 4 planos x H filas");
	eng::util::Array<eng::u16, Words> out {};
	for (eng::u8 y = 0u; y < H; ++y) {
		for (eng::u8 x = 0u; x < W; ++x) {
			const eng::u8 color = static_cast<eng::u8>(pixel(y, x) & 0x0fu);
			if (color == 0u) {
				continue; // fondo (plano a 0)
			}
			for (eng::u8 plane = 0u; plane < 4u; ++plane) {
				if ((color & static_cast<eng::u8>(1u << plane)) != 0u) {
					// Índice: fila de pixel y, plano `plane`, palabra `x/16` dentro de la
					// fila; bit MSB = píxel izquierdo de la palabra.
					const eng::usize index =
					    static_cast<eng::usize>(y * 4u + plane) * (W / 16u) + (x >> 4u);
					out[index] = static_cast<eng::u16>(
					    out[index] | static_cast<eng::u16>(0x8000u >> (x & 15u)));
				}
			}
		}
	}
	return out;
}

} // namespace eng::graphics
