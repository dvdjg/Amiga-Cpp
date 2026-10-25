#pragma once

/// \file cpu_blit.hpp
/// Copia CPU de un `Bob`/`Visual` planar sobre un `BobTarget`.
///
/// Es la materialización de **actores CPU** en A500: no encola `BlitJob`, no toca
/// registros y no usa heap. La ruta rápida copia palabra a palabra (16 bits), que es
/// el ancho del bus de Chip en OCS/ECS; la ruta de 32 bits para 68020+ se añadirá en
/// la misma interfaz cuando el backend declare el bus ancho.
///
/// Soporta:
///   - destino planar o interleaved (`BobTarget::layout`);
///   - X no alineada a 16 (desplazamiento de ventana con la palabra de guarda);
///   - `Opaque` y `Or`/`And` por palabra;
///   - `CookieCut` con máscara separable o máscara derivada de los planos (transparencia
///     por píxel; es la ruta lenta, no la de un sprite opaco).
///
/// No recorta: igual que el Blitter, exige que la caja quepa en el destino. Un objeto
/// que cruza un borde debe resolverlo el llamador (el motor de actores ya devuelve
/// `Clipped` en ese caso).

#include <eng/core/types/types.hpp>
#include <eng/graphics/bob.hpp>

namespace eng::graphics {

/// Resultado de una copia CPU (contabilidad simple, sin heap).
struct CpuBlitResult {
	eng::u16 rows = 0u;   ///< filas copiadas
	eng::u32 words = 0u;  ///< palabras de destino escritas (todas las filas/planos)
	bool ok = false;      ///< `false` = geometría/máscara no representable
};

namespace cpu_blit_detail {

/// Palabras de 16 px que cubre una anchura en píxeles (redondeo hacia arriba).
constexpr eng::u16 words_of(eng::u16 width) noexcept {
	return static_cast<eng::u16>((width + 15u) / 16u);
}

/// Fila `row` del plano `plane` de la hoja, en bytes; respeta el frame y el layout.
inline const eng::u8* plane_row(const eng::u8* base, const Bob& bob, eng::u32 row_bytes,
				eng::u32 frame_off, eng::u8 plane, eng::u16 row) noexcept {
	if (bob.layout == BobLayout::Interleaved) {
		return base + frame_off +
		       static_cast<eng::u32>(row) * row_bytes * bob.planes +
		       static_cast<eng::u32>(plane) * row_bytes;
	}
	return base + frame_off + static_cast<eng::u32>(plane) * bob.height * row_bytes +
	       static_cast<eng::u32>(row) * row_bytes;
}

/// Ventana de 16 bits `i` de una fila con desplazamiento fino (`x & 15`).
/// La palabra anterior aporta los bits altos; `words` evita leer fuera de la guarda.
inline eng::u16 shift_word(const eng::u16* row, eng::u16 i, eng::u8 shift,
			   eng::u16 words) noexcept {
	if (shift == 0u) {
		return row[i];
	}
	const eng::u16 prev = static_cast<eng::u16>(i > 0u && i - 1u < words ? row[i - 1u] : 0u);
	return static_cast<eng::u16>((row[i] >> shift) |
				     static_cast<eng::u16>(prev << (16u - shift)));
}

/// Aplica la ventana de 16 bits `v` con la máscara por píxel `m` sobre `d`.
/// La máscara es un bit por píxel: la selección es una operación de palabra,
/// sin bucle por bit (el mismo criterio que el minterm `$CA` del Blitter).
inline eng::u16 shift_word_left(const eng::u16* row, eng::u16 i, eng::u8 shift,
				eng::u16 words) noexcept {
	if (shift == 0u) {
		return row[i];
	}
	const eng::u16 next = static_cast<eng::u16>(i + 1u < words ? row[i + 1u] : 0u);
	return static_cast<eng::u16>((row[i] << shift) |
				     static_cast<eng::u16>(next >> (16u - shift)));
}

constexpr eng::u16 blend_masked(eng::u16 d, eng::u16 v, eng::u16 m) noexcept {
	return static_cast<eng::u16>((d & static_cast<eng::u16>(~m)) | (v & m));
}

} // namespace cpu_blit_detail

/// Borra una caja `w x h` en `(x,y)` del destino (todos los planos), por CPU.
[[nodiscard]] inline bool cpu_clear_box(const BobTarget& target, eng::s16 x, eng::s16 y,
					eng::u16 w, eng::u16 h) noexcept {
	if (target.height == 0u || target.row_bytes == 0u || target.plane_count == 0u ||
	    w == 0u || h == 0u || x < 0 || y < 0) {
		return false;
	}
	const eng::u16 words =
		cpu_blit_detail::words_of(static_cast<eng::u16>(w + static_cast<eng::u16>(x & 15)));
	if (words == 0u ||
	    static_cast<eng::u32>(x & ~15) + static_cast<eng::u32>(words) * 16u >
		    static_cast<eng::u32>(target.width) ||
	    static_cast<eng::u32>(y) + h > target.height) {
		return false;
	}
	const eng::u32 row_step = target.interleaved()
					  ? static_cast<eng::u32>(target.row_bytes) * target.plane_count
					  : target.row_bytes;
	const eng::u32 plane_step = target.interleaved() ? target.row_bytes
							 : target.plane_pointer_step();
	const eng::u16 left = static_cast<eng::u16>(x >> 4);
	for (eng::u8 p = 0u; p < target.plane_count; ++p) {
		eng::u8* base = target.data() + static_cast<eng::u32>(p) * plane_step +
				static_cast<eng::u32>(y) * row_step;
		for (eng::u16 row = 0u; row < h; ++row) {
			eng::u16* d = reinterpret_cast<eng::u16*>(base + static_cast<eng::u32>(row) * row_step);
			for (eng::u16 i = 0u; i < words; ++i) {
				d[left + i] = 0u;
			}
		}
	}
	return true;
}

/// Dibuja un frame de `bob` en `(x,y)` sobre `target` con la política de `bob.draw`.
/// Copia una caja del destino a un buffer de save-under planar.
[[nodiscard]] inline bool cpu_save_box(const BobTarget& target, eng::s16 x, eng::s16 y,
				       eng::u16 w, eng::u16 h,
				       eng::ChipView<eng::BobTag> save,
				       eng::u16 save_words_per_row,
				       eng::u16 save_height) noexcept {
	if (target.height == 0u || target.row_bytes == 0u || target.plane_count == 0u ||
	    w == 0u || h == 0u || x < 0 || y < 0 || save_words_per_row == 0u ||
	    save_height < h || save.empty()) {
		return false;
	}
	const eng::u16 words = cpu_blit_detail::words_of(
		static_cast<eng::u16>(w + static_cast<eng::u16>(x & 15)));
	if (words == 0u || save_words_per_row < words ||
	    static_cast<eng::u32>(x & ~15) + static_cast<eng::u32>(words) * 16u >
		    static_cast<eng::u32>(target.width) ||
	    static_cast<eng::u32>(y) + h > target.height ||
	    save.size() < static_cast<eng::u32>(save_words_per_row) * save_height * 2u *
				  target.plane_count) {
		return false;
	}
	const eng::u16 left = static_cast<eng::u16>(x >> 4);
	const eng::u32 target_row_step = target.interleaved()
						 ? static_cast<eng::u32>(target.row_bytes) * target.plane_count
						 : target.row_bytes;
	const eng::u32 target_plane_step = target.interleaved()
						  ? target.row_bytes
						  : target.plane_pointer_step();
	const eng::u32 save_row_step = static_cast<eng::u32>(save_words_per_row) * 2u;
	const eng::u32 save_plane_step = save_row_step * save_height;
	eng::u8* save_data = const_cast<eng::u8*>(save.data());
	for (eng::u8 p = 0u; p < target.plane_count; ++p) {
		for (eng::u16 row = 0u; row < h; ++row) {
			const eng::u16* src = reinterpret_cast<const eng::u16*>(
				target.data() + static_cast<eng::u32>(p) * target_plane_step +
				static_cast<eng::u32>(y + row) * target_row_step);
			eng::u16* dst = reinterpret_cast<eng::u16*>(
				save_data + static_cast<eng::u32>(p) * save_plane_step +
				static_cast<eng::u32>(row) * save_row_step);
			for (eng::u16 i = 0u; i < words; ++i) dst[i] = src[left + i];
		}
	}
	return true;
}

/// Restaura una caja guardada por `cpu_save_box`.
[[nodiscard]] inline bool cpu_restore_box(const BobTarget& target, eng::s16 x, eng::s16 y,
					  eng::u16 w, eng::u16 h,
					  eng::ChipView<eng::BobTag> save,
					  eng::u16 save_words_per_row,
					  eng::u16 save_height) noexcept {
	if (target.height == 0u || target.row_bytes == 0u || target.plane_count == 0u ||
	    w == 0u || h == 0u || x < 0 || y < 0 || save_words_per_row == 0u ||
	    save_height < h || save.empty()) {
		return false;
	}
	const eng::u16 words = cpu_blit_detail::words_of(
		static_cast<eng::u16>(w + static_cast<eng::u16>(x & 15)));
	if (words == 0u || save_words_per_row < words ||
	    static_cast<eng::u32>(x & ~15) + static_cast<eng::u32>(words) * 16u >
		    static_cast<eng::u32>(target.width) ||
	    static_cast<eng::u32>(y) + h > target.height ||
	    save.size() < static_cast<eng::u32>(save_words_per_row) * save_height * 2u *
				  target.plane_count) {
		return false;
	}
	const eng::u16 left = static_cast<eng::u16>(x >> 4);
	const eng::u32 target_row_step = target.interleaved()
						 ? static_cast<eng::u32>(target.row_bytes) * target.plane_count
						 : target.row_bytes;
	const eng::u32 target_plane_step = target.interleaved()
						  ? target.row_bytes
						  : target.plane_pointer_step();
	const eng::u32 save_row_step = static_cast<eng::u32>(save_words_per_row) * 2u;
	const eng::u32 save_plane_step = save_row_step * save_height;
	const eng::u8* save_data = save.data();
	for (eng::u8 p = 0u; p < target.plane_count; ++p) {
		for (eng::u16 row = 0u; row < h; ++row) {
			const eng::u16* src = reinterpret_cast<const eng::u16*>(
				save_data + static_cast<eng::u32>(p) * save_plane_step +
				static_cast<eng::u32>(row) * save_row_step);
			eng::u16* dst = reinterpret_cast<eng::u16*>(
				target.data() + static_cast<eng::u32>(p) * target_plane_step +
				static_cast<eng::u32>(y + row) * target_row_step);
			for (eng::u16 i = 0u; i < words; ++i) dst[left + i] = src[i];
		}
	}
	return true;
}

/// Dibuja un frame de `bob` en `(x,y)` sobre `target` con la política de `bob.draw`.
[[nodiscard]] inline CpuBlitResult cpu_draw_bob(const BobTarget& target, const Bob& bob,
						eng::u8 frame, eng::s16 x, eng::s16 y) noexcept {
	CpuBlitResult r {};
	if (!bob_detail::valid(bob, target) || frame >= bob.frame_count ||
	    target.height == 0u || bob.width == 0u || bob.height == 0u ||
	    bob.planes > target.plane_count) {
		return r;
	}
	if (bob.draw == BobDraw::CookieCut && bob.mask_pack == BobMaskPack::InterleavedPair) {
		return r; // la hoja par se materializa como BOB del Blitter, no como copia CPU
	}
	const eng::s16 skip_x = x < 0 ? static_cast<eng::s16>(-x) : 0;
	const eng::s16 skip_y = y < 0 ? static_cast<eng::s16>(-y) : 0;
	const eng::s16 dst_x = x < 0 ? 0 : x;
	const eng::s16 dst_y = y < 0 ? 0 : y;
	if (skip_x >= static_cast<eng::s16>(bob.width) ||
	    skip_y >= static_cast<eng::s16>(bob.height) || dst_x >= target.width ||
	    dst_y >= target.height) {
		return r;
	}
	const eng::u16 width = static_cast<eng::u16>(
		(bob.width - static_cast<eng::u16>(skip_x)) <
				(target.width - static_cast<eng::u16>(dst_x))
			? (bob.width - static_cast<eng::u16>(skip_x))
			: (target.width - static_cast<eng::u16>(dst_x)));
	const eng::u16 height = static_cast<eng::u16>(
		(bob.height - static_cast<eng::u16>(skip_y)) <
				(target.height - static_cast<eng::u16>(dst_y))
			? (bob.height - static_cast<eng::u16>(skip_y))
			: (target.height - static_cast<eng::u16>(dst_y)));
	if (width == 0u || height == 0u) {
		return r;
	}
	const eng::u8 shift = x < 0 ? static_cast<eng::u8>(skip_x & 15)
				    : static_cast<eng::u8>(dst_x & 15);
	const eng::u16 left = x < 0 ? 0u : static_cast<eng::u16>(dst_x >> 4);
	const eng::u16 src_word_offset =
		x < 0 ? static_cast<eng::u16>(skip_x >> 4) : 0u;
	const eng::u16 words = cpu_blit_detail::words_of(
		static_cast<eng::u16>(shift + width));
	if (words == 0u ||
	    static_cast<eng::u32>(dst_x) + static_cast<eng::u32>(width) >
		    static_cast<eng::u32>(target.width) ||
	    static_cast<eng::u32>(dst_y) + height > target.height) {
		return r;
	}
	const eng::u32 row_bytes = bob_detail::sheet_row_of(bob);
	const eng::u16 base_words = static_cast<eng::u16>((bob.width + 15u) / 16u);
	const bool guarded_row = row_bytes >= static_cast<eng::u32>(base_words + 1u) * 2u;
	const eng::u16 src_words = static_cast<eng::u16>(base_words + (guarded_row ? 1u : 0u));
	if (row_bytes < static_cast<eng::u32>(src_words) * 2u) {
		return r;
	}
	const bool has_mask = bob.draw == BobDraw::CookieCut && !bob.mask.empty() &&
			      bob.mask_pack == BobMaskPack::SeparatePlane;
	const eng::u32 frame_off = static_cast<eng::u32>(frame) * bob.frame_stride;
	const eng::u8* sheet = bob.sheet.address(frame_off).cptr();
	const eng::u8* mask_base = has_mask ? bob.mask.data() : nullptr;
	const eng::u32 mask_off = has_mask && bob.mask.size() > frame_off ? frame_off : 0u;

	const eng::u32 dst_row_step = target.interleaved()
					      ? static_cast<eng::u32>(target.row_bytes) * target.plane_count
					      : target.row_bytes;
	const eng::u32 dst_plane_step = target.interleaved()
						? target.row_bytes
						: target.plane_pointer_step();

	// Ruta rápida A500: copia alineada y opaca = `move.w` por palabra, sin
	// desplazamiento ni mezcla por píxel. Es el caso de un fondo/objeto CPU fijo.
	if (x >= 0 && shift == 0u && bob.draw == BobDraw::Opaque) {
		for (eng::u16 row = 0u; row < height; ++row) {
			for (eng::u8 p = 0u; p < bob.planes; ++p) {
				const eng::u16* src = reinterpret_cast<const eng::u16*>(
					cpu_blit_detail::plane_row(sheet, bob, row_bytes, 0u, p,
								   static_cast<eng::u16>(row + skip_y)));
				eng::u16* dst = reinterpret_cast<eng::u16*>(
					target.data() + static_cast<eng::u32>(p) * dst_plane_step +
					static_cast<eng::u32>(dst_y + row) * dst_row_step);
				for (eng::u16 i = 0u; i < words; ++i) {
					dst[left + i] = src[i];
					++r.words;
				}
			}
			++r.rows;
		}
		r.ok = true;
		return r;
	}
	for (eng::u16 row = 0u; row < height; ++row) {
		const eng::u32 source_row = static_cast<eng::u32>(row + skip_y);
		const eng::u8* mask_row = has_mask
			? mask_base + mask_off + source_row * row_bytes
			: nullptr;
		for (eng::u8 p = 0u; p < bob.planes; ++p) {
			const eng::u16* src = reinterpret_cast<const eng::u16*>(
				cpu_blit_detail::plane_row(sheet, bob, row_bytes, 0u, p,
							   static_cast<eng::u16>(source_row))) + src_word_offset;
			eng::u16* dst = reinterpret_cast<eng::u16*>(
				target.data() + static_cast<eng::u32>(p) * dst_plane_step +
				static_cast<eng::u32>(dst_y + row) * dst_row_step);
			for (eng::u16 i = 0u; i < words; ++i) {
				const eng::u16 v = x < 0
					? cpu_blit_detail::shift_word_left(src, i, shift, src_words)
					: cpu_blit_detail::shift_word(src, i, shift, src_words);
				eng::u16 m = 0u;
				if (bob.draw == BobDraw::CookieCut) {
					if (has_mask) {
						const eng::u16* mrow = reinterpret_cast<const eng::u16*>(mask_row);
						m = x < 0
							? cpu_blit_detail::shift_word_left(mrow, i, shift, src_words)
							: cpu_blit_detail::shift_word(mrow, i, shift, src_words);
					} else {
						eng::u16 art = 0u;
						for (eng::u8 q = 0u; q < bob.planes; ++q) {
							const eng::u16* arow =
								reinterpret_cast<const eng::u16*>(
									cpu_blit_detail::plane_row(
										sheet, bob, row_bytes, 0u, q,
										static_cast<eng::u16>(source_row)));
							art = static_cast<eng::u16>(
								art | (x < 0
									? cpu_blit_detail::shift_word_left(
										arow + src_word_offset, i, shift, src_words)
									: cpu_blit_detail::shift_word(
										arow, i, shift, src_words)));
						}
						m = art;
					}
				}
				eng::u16& d = dst[left + i];
				switch (bob.draw) {
				case BobDraw::Opaque:
					d = v;
					break;
				case BobDraw::Or:
					d = static_cast<eng::u16>(d | v);
					break;
				case BobDraw::And:
					d = static_cast<eng::u16>(d & v);
					break;
				case BobDraw::CookieCut:
					d = cpu_blit_detail::blend_masked(d, v, m);
					break;
				}
				++r.words;
			}
		}
		++r.rows;
	}
	r.ok = true;
	return r;
}

} // namespace eng::graphics
