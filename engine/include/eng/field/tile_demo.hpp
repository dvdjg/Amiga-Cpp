#pragma once

/// \file tile_demo.hpp
/// Utilidades COMUNES para demos de campos de tiles.
///
/// Unifica el código que las demos de `TileFieldController` duplicaban (106,
/// 102, 107): paleta, glifos, seno Q16, cámaras, generación de tileset (con
/// soporte de tiles ANCHOS múltiplo de 16) y construcción de config.
///
/// La abstracción de playfield en sí (el "código común de cualquier juego") es
/// `TileFieldController` + `DpfDisplayComposer` (ver `tile_field.hpp` y
/// `dpf_composer.hpp`): un controlador por playfield, dual o single, con su
/// mapa, framebuffer y cámara independientes. Este header solo evita duplicar
/// la generación de assets y el movimiento de las demos.

#include <eng/field/tile_field.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::field::demo {

/// Paleta por defecto (32 colores). En dual 3+3: PF1 usa 0..7 (0 transparente)
/// y PF2 8..15 (8 transparente). En single 5 planos se usan los 32 colores.
constexpr eng::u16 kPalette[32] {
	// PF1 (primer plano): registros 0..7. Color 0 = transparente.
	0x000, 0xf0c, 0x0cf, 0xff0, 0xf80, 0x84f, 0xf44, 0xfff,
	// PF2 (fondo): registros 8..15. Color 0 (reg 8) = transparente.
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xfff,
};

/// Hash pseudoaleatorio determinista por celda (x, y, semilla).
constexpr eng::u32 cell_hash(eng::u32 x, eng::u32 y, eng::u32 seed) {
	eng::u32 h = seed ^ (x * 0x9e3779b9u) ^ (y * 0x85ebca6bu);
	h ^= h >> 16u;
	h *= 0x7feb352du;
	h ^= h >> 15u;
	h *= 0x846ca68bu;
	h ^= h >> 16u;
	return h;
}

/// Glifo hexadecimal 5x7 (para distinguir tiles).
constexpr eng::u8 hex_glyph_row(eng::u8 glyph, eng::u8 row) {
	constexpr eng::u8 rows[] {
		0x0eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0eu, // 0
		0x04u, 0x0cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0eu, // 1
		0x0eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1fu, // 2
		0x1eu, 0x01u, 0x01u, 0x0eu, 0x01u, 0x01u, 0x1eu, // 3
		0x02u, 0x06u, 0x0au, 0x12u, 0x1fu, 0x02u, 0x02u, // 4
		0x1fu, 0x10u, 0x10u, 0x1eu, 0x01u, 0x01u, 0x1eu, // 5
		0x0eu, 0x10u, 0x10u, 0x1eu, 0x11u, 0x11u, 0x0eu, // 6
		0x1fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u, // 7
		0x0eu, 0x11u, 0x11u, 0x0eu, 0x11u, 0x11u, 0x0eu, // 8
		0x0eu, 0x11u, 0x11u, 0x0fu, 0x01u, 0x01u, 0x0eu, // 9
		0x0eu, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u, // A
		0x1eu, 0x11u, 0x11u, 0x1eu, 0x11u, 0x11u, 0x1eu, // B
		0x0eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0eu, // C
		0x1eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1eu, // D
		0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x1fu, // E
		0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x10u, // F
	};
	return rows[static_cast<eng::u16>(glyph & 0x0fu) * 7u + (row % 7u)];
}

constexpr eng::u16 row_mask_range(eng::u8 left, eng::u8 width) {
	eng::u16 mask = 0;
	for (eng::u8 i = 0; i < width; ++i) {
		mask |= static_cast<eng::u16>(0x8000u >> (left + i));
	}
	return mask;
}

constexpr eng::u16 glyph_row_mask(eng::u8 glyph, eng::u8 y) {
	if (y < 1u || y >= 15u) {
		return 0;
	}
	const eng::u8 glyph_y = static_cast<eng::u8>((y - 1u) / 2u);
	const eng::u8 bits = hex_glyph_row(glyph, glyph_y);
	eng::u16 mask = 0;
	for (eng::u8 glyph_x = 0; glyph_x < 5u; ++glyph_x) {
		if ((bits & (1u << (4u - glyph_x))) != 0u) {
			mask |= row_mask_range(static_cast<eng::u8>(3u + glyph_x * 2u), 2);
		}
	}
	return mask;
}

constexpr eng::u16 variant_marker_mask(eng::u8 variant, eng::u8 y) {
	const eng::u8 marker_size = static_cast<eng::u8>(2u + (variant & 1u));
	eng::u16 mask = 0;
	if (y < marker_size) {
		mask |= row_mask_range(1, marker_size);
	}
	if (variant >= 2u && y >= static_cast<eng::u8>(15u - marker_size)) {
		mask |= row_mask_range(static_cast<eng::u8>(15u - marker_size), marker_size);
	}
	return mask;
}

/// Fila planar (1 word de 16px) de un tile simbólico de un playfield.
///
/// `base` es el índice del primer color del playfield (0 para PF1, 8 para PF2).
/// El tile combina fondo (tramado al 50% si `transparent_bg`), borde, glifo
/// hexadecimal y marcador de variante.
constexpr eng::u16 pf_plane_row(
	eng::u8 glyph,
	eng::u8 variant,
	eng::u8 y,
	eng::u8 plane,
	eng::u8 base,
	bool transparent_bg
) {
	constexpr eng::u8 bg_colors[] {1, 2, 3, 4};
	constexpr eng::u8 ink_colors[] {7, 6, 5, 7};
	constexpr eng::u8 border_colors[] {5, 3, 1, 6};
	const eng::u8 bg = static_cast<eng::u8>(base + bg_colors[variant & 3u]);
	const eng::u8 ink = static_cast<eng::u8>(base + ink_colors[variant & 3u]);
	const eng::u8 border = static_cast<eng::u8>(base + border_colors[variant & 3u]);

	const eng::u16 border_mask = (y == 0u || y == 15u) ? 0xffffu : 0x8001u;
	const eng::u16 marker_mask = static_cast<eng::u16>(variant_marker_mask(variant, y) & ~border_mask);
	const eng::u16 glyph_mask = static_cast<eng::u16>(glyph_row_mask(glyph, y) & ~(border_mask | marker_mask));
	const eng::u16 bg_mask = static_cast<eng::u16>(~(border_mask | marker_mask | glyph_mask));

	eng::u16 row = 0;
	if (transparent_bg) {
		const eng::u16 checker = (y & 1u) == 0u ? 0xaaaa : 0x5555;
		if ((bg & (1u << plane)) != 0u) {
			row |= static_cast<eng::u16>(bg_mask & checker);
		}
	} else if ((bg & (1u << plane)) != 0u) {
		row |= bg_mask;
	}
	if ((border & (1u << plane)) != 0u) {
		row |= border_mask;
	}
	if ((ink & (1u << plane)) != 0u) {
		row |= static_cast<eng::u16>(marker_mask | glyph_mask);
	}
	return row;
}

/// Construye el tileset de un campo con tiles de `tile_width` px (múltiplo de
/// 16). Cada tile ocupa `[plano][filas x words_por_fila]` words contiguos, y el
/// controlador lo copia con un único blit `TileBlockCopy` (words_por_fila = 2
/// para 32px, 3 para 48px, etc.).
///
/// `words_per_tile` = `tile_planes * tile_size * words_por_fila`.
/// El tile `fully_transparent_tile` (si >= 0) se escribe todo a 0.
void build_tile_cache(
	eng::Span<eng::u16> words,
	eng::u16 tile_count,
	eng::u16 tile_size,
	eng::u16 tile_width,
	eng::u8 tile_planes,
	eng::u8 base,
	bool transparent_bg,
	eng::s16 fully_transparent_tile = -1
) {
	const eng::u16 words_per_row = static_cast<eng::u16>(tile_width / 16u);
	const eng::u32 words_per_plane = static_cast<eng::u32>(tile_size) * words_per_row;
	const eng::u32 words_per_tile = words.size() / tile_count;
	for (eng::u16 tile = 0; tile < tile_count; ++tile) {
		const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
		const eng::u8 variant = static_cast<eng::u8>((tile >> 4u) & 3u);
		const bool fully_transparent = fully_transparent_tile >= 0 &&
			static_cast<eng::u16>(fully_transparent_tile) == tile;
		for (eng::u8 plane = 0; plane < tile_planes; ++plane) {
			for (eng::u16 row = 0; row < tile_size; ++row) {
				const eng::u32 base_idx = static_cast<eng::u32>(tile) * words_per_tile +
					static_cast<eng::u32>(plane) * words_per_plane +
					static_cast<eng::u32>(row) * words_per_row;
				// En un tile ancho, la mitad izquierda es el glifo y las
				// siguientes words repiten un patrón de variante para que se vea
				// que el tile ancho se copia completo.
				const eng::u16 left = fully_transparent
					? 0
					: pf_plane_row(glyph, variant, static_cast<eng::u8>(row), plane, base, transparent_bg);
				for (eng::u16 w = 0; w < words_per_row; ++w) {
					words.at(base_idx + w) = fully_transparent
						? 0
						: (w == 0 ? left : (left ^ 0x0ff0u)); // patrón de variante
				}
			}
		}
	}
}

/// Seno de 64 pasos exactos (amplitud 64).
constexpr eng::s16 sin64(eng::u8 index) {
	constexpr eng::s16 t[] {
		0, 6, 12, 18, 24, 31, 36, 41,
		45, 49, 53, 56, 59, 61, 63, 64,
		64, 64, 63, 61, 59, 56, 53, 49,
		45, 41, 36, 31, 24, 18, 12, 6,
		0, -6, -12, -18, -24, -31, -36, -41,
		-45, -49, -53, -56, -59, -61, -63, -64,
		-64, -64, -63, -61, -59, -56, -53, -49,
		-45, -41, -36, -31, -24, -18, -12, -6,
	};
	return t[index & 63u];
}

/// Seno interpolado con precisión Q16 (escala 65536 = 1.0) para movimiento
/// sub-pixel suave (conserva la fracción; el acumulador de resto de la demo
/// convierte a píxeles enteros).
constexpr eng::s32 sin_smooth(eng::u32 frame_index, eng::u32 period) {
	const eng::u32 ph = (frame_index * 4096u) / period;
	const eng::u8 i = static_cast<eng::u8>((ph >> 6) & 63u);
	const eng::u8 frac = static_cast<eng::u8>(ph & 63u);
	const eng::s32 s0 = sin64(i);
	const eng::s32 s1 = sin64(static_cast<eng::u8>(i + 1u) & 63u);
	return (s0 * static_cast<eng::s32>(64 - frac) + s1 * static_cast<eng::s32>(frac)) * 1024;
}

/// Cámara de fondo: scroll infinito diagonal constante en ambos ejes
/// (2 px/frame X, 1 px/frame Y).
constexpr eng::s32 bg_scroll_x(eng::u32 frame_index) { return static_cast<eng::s32>(frame_index) * 2; }
constexpr eng::s32 bg_scroll_y(eng::u32 frame_index) { return static_cast<eng::s32>(frame_index); }

/// Cámara de primer plano: Lissajous en SUB-PÍXELES (Q16) que recorre más de
/// una pantalla en cada eje (0..2*vw, 0..2*vh) cruzando páginas en ambos
/// sentidos. Devuelve px*65536. `vw/vh` es el viewport.
struct CameraQ16 {
	eng::s32 x = 0;
	eng::s32 y = 0;
};

constexpr CameraQ16 fg_lissajous_camera(
	eng::u32 frame_index,
	eng::u32 vw,
	eng::u32 vh,
	eng::u32 period_x,
	eng::u32 period_y
) {
	const eng::s32 sx = sin_smooth(frame_index + 3u * period_x / 4u, period_x) * static_cast<eng::s32>(vw) / 64;
	const eng::s32 sy = sin_smooth(frame_index + 3u * period_y / 4u, period_y) * static_cast<eng::s32>(vh) / 64;
	return {
		static_cast<eng::s32>(vw) * 65536 + sx,
		static_cast<eng::s32>(vh) * 65536 + sy,
	};
}

} // namespace eng::field::demo
