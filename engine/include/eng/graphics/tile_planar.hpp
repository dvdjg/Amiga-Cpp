#pragma once

/// \file tile_planar.hpp
/// **Decodificación de tiles indexados → planos de bits** (`eng::graphics`): convierte una imagen
/// con `planes` planos de bits **secuenciales** (plano 0 fila a fila, plano 1 a continuación…) a
/// **planos contiguos** con fila alineada y `plane_stride` propio — el formato que consumen el
/// Blitter y los drivers de tiles.
///
/// Es un decoder de assets **genérico** (cualquier gráfico de N bits por píxel empaquetado por
/// planos: fondos, sprites, fuentes), independiente de la fuente y del consumidor.
///
/// ```cpp
/// eng::graphics::decode_2bpp_planar(tile16, planes, 8, 8, 2, 1, 8);   // tile 8x8, 2 planos
/// ```

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// \param src Origen: `planes` planos secuenciales, cada uno de `h*(w/8)` bytes; cada byte es
///        una fila de `w` píxeles con el bit **MSB = píxel izquierdo**.
/// \param dst Destino planar: `planes` planos de `plane_stride`, `row_bytes` por fila.
/// \param w Ancho en píxeles (múltiplo de 8). \param h Alto en filas.
/// \param planes Planos de origen y destino (1..6).
/// \param row_bytes Bytes por fila de cada plano de destino (`>= w/8`).
/// \param plane_stride Bytes entre planos de destino (`>= h*row_bytes`).
/// \return `false` si algún argumento es inválido (punteros nulos, `w` no múltiplo de 8, etc.).
[[nodiscard]] inline bool decode_2bpp_planar(const u8* src, u8* dst, u16 w, u16 h, u8 planes,
					     u16 row_bytes, u32 plane_stride) noexcept {
	if (src == nullptr || dst == nullptr || planes == 0u || planes > 6u || w == 0u || h == 0u ||
	    (w & 7u) != 0u || row_bytes < static_cast<u16>(w / 8u) ||
	    plane_stride < static_cast<u32>(h) * row_bytes) {
		return false;
	}
	const u16 in_row = static_cast<u16>(w / 8u);       // bytes por fila en el origen
	const u16 in_plane = static_cast<u16>(h * in_row); // bytes por plano en el origen
	// Limpia el destino (el padding de fila y planos extra quedan a 0).
	for (u8 p = 0; p < planes; ++p) {
		u8* base = dst + static_cast<u32>(p) * plane_stride;
		for (u32 i = 0; i < static_cast<u32>(h) * row_bytes; ++i) {
			base[i] = 0u;
		}
	}
	for (u16 y = 0; y < h; ++y) {
		for (u16 b = 0; b < in_row; ++b) {
			const u32 so = static_cast<u32>(y) * in_row + b;
			for (u8 p = 0; p < planes; ++p) {
				dst[static_cast<u32>(p) * plane_stride + static_cast<u32>(y) * row_bytes + b] =
					src[static_cast<u32>(p) * in_plane + so];
			}
		}
	}
	return true;
}

} // namespace eng::graphics
