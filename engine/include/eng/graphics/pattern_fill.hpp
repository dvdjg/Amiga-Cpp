#pragma once

/// \file pattern_fill.hpp
/// **Relleno de un rectángulo con un patrón planar de varias filas** (un "tablero"
/// real) sobre el `FramePlan`: un `PatternFill` por fila del patrón.
///
/// El Blitter solo puede repetir **una fila** de patrón con un módulo de A constante
/// (ver `BlitJobKind::PatternFill`). Para un patrón de `ph` filas se emiten `ph` jobs:
/// el job `k` cubre las filas destino `y+k, y+k+ph, y+k+2ph, ...` con A apuntando a la
/// fila `k` del patrón (módulo de A `-words*2`, el destino salta `ph` filas con su
/// módulo). El resultado es el patrón teselado en las dos direcciones.
///
/// ```cpp
/// // tablero 16x2 en Chip RAM (el Blitter no lee .rodata)
/// add_rect_pattern(plan, BlitDest {plane0}, row_bytes, x_word, y, words, rows,
///                  pattern, pattern_words, pattern_bytes, /*pattern_rows=*/2, plane_bytes);
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::graphics {

/// Añade a `plan` el relleno de un rect con un patrón planar de `pattern_rows` filas.
///
/// - `dst` = base del plano destino; `dst_row_bytes` = bytes por fila del destino;
/// - `x_word`/`y` = esquina superior izquierda en words de 16 px / filas;
/// - `words`/`rows` = tamaño del rect en words / filas;
/// - `pattern` = filas contiguas del patrón en Chip RAM (`pattern_words` por fila);
/// - `pattern_plane_stride` = bytes de un plano del patrón; `planes` = planos.
///
/// Devuelve `false` si el plan se llena o los argumentos no valen.
[[nodiscard]] inline bool add_rect_pattern(FramePlan& plan, BlitDest dst, u16 dst_row_bytes,
					   u16 x_word, u16 y, u16 words, u16 rows,
					   const u16* pattern, u16 pattern_words,
					   u32 pattern_plane_stride, u8 pattern_rows,
					   u32 dst_plane_stride_bytes, u8 planes) {
	if (pattern == nullptr || dst.words == nullptr || pattern_rows == 0u || words == 0u ||
	    rows == 0u || planes == 0u) {
		return false;
	}
	const s16 a_mod = static_cast<s16>(-static_cast<s32>(words) * 2);
	const s16 d_mod = static_cast<s16>(static_cast<s32>(pattern_rows) * dst_row_bytes -
					   static_cast<s32>(words) * 2);
	eng::u8* base = reinterpret_cast<eng::u8*>(dst.words);
	for (u8 k = 0u; k < pattern_rows; ++k) {
		if (k >= rows) {
			break;
		}
		const u16 rows_k = static_cast<u16>((rows - k + pattern_rows - 1u) / pattern_rows);
		BlitJob job {};
		job.source = BlitSource(pattern + static_cast<u32>(k) * pattern_words);
		job.destination = BlitDest(reinterpret_cast<u16*>(
			base + static_cast<eng::u32>(y + k) * dst_row_bytes +
			static_cast<eng::u32>(x_word) * 2u));
		job.words_per_row = words;
		job.height = rows_k;
		job.bitplane_count = planes;
		job.source_plane_stride_bytes = pattern_plane_stride;
		job.destination_plane_stride_bytes = dst_plane_stride_bytes;
		job.source_modulo_bytes = a_mod;
		job.destination_modulo_bytes = d_mod;
		job.minterm = 0xFCu; // D = A | D
		if (!plan.add_pattern_fill(job)) {
			return false;
		}
	}
	return true;
}

} // namespace eng::graphics
