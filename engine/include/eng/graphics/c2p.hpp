#pragma once

/// \file c2p.hpp
/// chunky 4bpp -> planar (capa portable, correcta y verificable).
///
/// Convierte un buffer "chunky" (1 byte por pixel, 4 bits de color en el nibble bajo)
/// al formato planar de 4 bitplanes que lee el DMA de Agnus.
///
/// Hay dos implementaciones del mismo concepto:
///   - este header: version C++ naive, O(planos*w*h), correcta por construccion y
///     testeable en host. Suficiente para efectos CPU de tamano modesto a 50 fps.
///   - `support/c2p_1x1_4.s`: el c2p de Kalms/Scout (1999), con writes de word y la
///     desintercalacion por mascaras (tres fases $0f0f0f0f/$00ff00ff/$55555555 y
///     $33333333). Es la rutina optimizada para el hot path; se mantiene en asm y
///     pendiente de validacion contra esta version (misma semantica) antes de usarla
///     por defecto. Regla del roadmap: el asm del origen se conserva en `support/`.
///
/// Convencion planar Amiga (importante): el pixel mas a la izquierda de un grupo de 8
/// es el bit MAS significativo (bit 7) del byte, igual que el fetch de Agnus.
/// Por eso la escritura usa `1u << (7 - bit_en_grupo)`.

#include <eng/core/types.hpp>

namespace eng::graphics {

/// Convierte una imagen chunky de 4bpp a 4 bitplanes planares.
///
///   - `width_px`:  ancho en pixels (no hace falta multiplo de 16 en esta version).
///   - `height_px`: alto en pixels.
///   - `plane_stride_bytes`: paso en bytes entre el inicio de cada bitplane (p. ej.
///     `bytes_per_row * height` en un layout separate).
///   - `chunky`:    entrada, un byte por pixel; se usa el nibble bajo (indice 0..15).
///   - `planes`:    salida; plano p (0..3) a `planes + p*plane_stride_bytes`.
///
/// Sin heap, sin STL: bucles puros. Los buffers deben ser validos y del tamano
/// correcto; la capa de contenedor valida, esta no.
inline void c2p_1x1_4(
	unsigned long width_px,
	unsigned long height_px,
	unsigned long plane_stride_bytes,
	const void* chunky,
	void* planes
) {
	const u32 w = static_cast<u32>(width_px);
	const u32 h = static_cast<u32>(height_px);
	const u32 stride = static_cast<u32>(plane_stride_bytes);
	const u8* src_px = static_cast<const u8*>(chunky);
	u8* row_start[4];
	for (u8 p = 0; p < 4; ++p) {
		row_start[p] = static_cast<u8*>(planes) + static_cast<u32>(p) * stride;
	}

	for (u32 y = 0; y < h; ++y) {
		// bytes de la fila actual: src_px va contiguo (un byte por pixel).
		const u8* srow = src_px + static_cast<u32>(y) * w;
		const u32 row_bytes = w >> 3u; // bytes por fila en un plano (width/8)

		for (u32 x = 0; x < w;) {
			// Grupo de hasta 8 pixels -> un byte por plano.
			u8 out[4] = { 0, 0, 0, 0 };
			const u32 group_x = x;
			for (u32 k = 0; k < 8 && x < w; ++k, ++x) {
				const u8 index = static_cast<u8>(srow[x] & 0xfu); // nibble bajo
				const u8 bit = static_cast<u8>(1u << (7u - static_cast<u8>(k)));
				for (u8 p = 0; p < 4; ++p) {
					if ((index & (1u << p)) != 0u) {
						out[p] = static_cast<u8>(out[p] | bit);
					}
				}
			}
			const u32 byte_x = group_x >> 3u; // byte dentro de la fila planar
			const u32 off = static_cast<u32>(y) * row_bytes + byte_x;
			for (u8 p = 0; p < 4; ++p) {
				row_start[p][off] = out[p];
			}
		}
	}
}

} // namespace eng::graphics