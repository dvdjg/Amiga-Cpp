#pragma once

/// \file chr.hpp
/// **Decodificación de tiles 2bpp → planar** (`eng::graphics`): convierte un tile tipo
/// NES/Game Boy (2 planos de bits, 8 filas cada uno) a **planos contiguos** de Amiga.
///
/// Layout de entrada (`chr`): `planes` bloques contiguos, cada uno de `h * (w/8)` bytes; cada
/// byte es una fila de `w` píxeles con el bit **MSB = píxel izquierdo**. Para 2bpp: bytes
/// `0..(h*w/8-1)` = plano 0, y el plano 1 a continuación (es el formato `CHR` de la NES).
///
/// Layout de salida (`dst`): `planes` planos contiguos de `row_bytes` bytes por fila, con
/// `plane_stride` bytes entre planos (`>= h*row_bytes`). Se copian exactamente `planes` planos
/// (el origen debe tener `planes` bloques).
/// Es la puerta para `IPatternCache`/`ISpriteEngine::define` de consumidores externos, sin que
/// el emulador toque el intercalado planar.
///
/// ```cpp
/// eng::graphics::chr_to_planar(chr16, planes, 8, 8, 2, 1, 8);  // tile 8x8 a 2 planos
/// ```

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// \param chr Bytes de origen (`planes` planos contiguos de `h*(w/8)`).
/// \param dst Destino planar (`planes` planos de `plane_stride`, `row_bytes` por fila).
/// \param w Ancho en píxeles (múltiplo de 8). \param h Alto en filas.
/// \param planes Planos de origen y destino (1..6).
/// \param row_bytes Bytes por fila de cada plano de destino (`>= w/8`).
/// \param plane_stride Bytes entre planos de destino (`>= h*row_bytes`).
/// \return `false` si algún argumento es inválido (punteros nulos, `w` no múltiplo de 8, etc.).
[[nodiscard]] inline bool chr_to_planar(const u8* chr, u8* dst, u16 w, u16 h, u8 planes,
					u16 row_bytes, u32 plane_stride) noexcept {
	if (chr == nullptr || dst == nullptr || planes == 0u || planes > 6u || w == 0u || h == 0u ||
	    (w & 7u) != 0u || row_bytes < static_cast<u16>(w / 8u) ||
	    plane_stride < static_cast<u32>(h) * row_bytes) {
		return false;
	}
	const u16 in_row = static_cast<u16>(w / 8u);   // bytes por fila en el origen
	const u16 in_plane = static_cast<u16>(h * in_row); // bytes por plano en el origen
	// Limpia el destino (los planos extra quedan a 0).
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
					chr[static_cast<u32>(p) * in_plane + so];
			}
		}
	}
	return true;
}

} // namespace eng::graphics
