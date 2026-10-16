#pragma once

/// \file struct_paint.hpp
/// **Pintores de estructuras 2×2** (`eng::gfx::proc::terrain`): casa grande (variantes A/B
/// con la puerta espejada) y árbol grande (copa continua `SoftCircleMask` + tronco).
/// Cada celda de la estructura pinta su **cuadrante** del diseño global de `2·TW × 2·TH`
/// (el generador asigna la MISMA seed a las cuatro piezas), de modo que las partes casan
/// por construcción; el resto del catálogo de piezas vive en `terrain_paint.hpp`, que
/// incluye esta cabecera y despacha por `TileSpec`.
///
/// Tests: `HOST-448` (casa A/B con puerta y ventanas, copa/tronco continuos).

#include <eng/core/types/types.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/shapes.hpp>
#include <eng/graphics/proc/terrain_types.hpp>

namespace eng::gfx::proc::terrain {
/// Cuadrante de una **estructura 2×2** (casa grande, árbol grande): la celda del
/// rasgo correspondiente pinta su parte del diseño global (2·TW × 2·TH píxeles).
enum class StructPart : eng::u8 { NorthWest = 0, NorthEast = 1, SouthWest = 2, SouthEast = 3 };

/// ¿La máscara lleva alguna parte de la **casa grande** (variante A o B)?
/// \param f máscara de rasgos. \return true si es una celda de casa 2×2.
[[nodiscard]] constexpr bool is_big_house(Feature f) {
	return has(f, Feature::HouseA_NW) || has(f, Feature::HouseA_NE) || has(f, Feature::HouseA_SW) ||
	       has(f, Feature::HouseA_SE) || has(f, Feature::HouseB_NW) || has(f, Feature::HouseB_NE) ||
	       has(f, Feature::HouseB_SW) || has(f, Feature::HouseB_SE);
}
/// ¿La casa grande es de la variante **B** (puerta espejada al cuadrante SE)?
/// \param f máscara de rasgos de una celda de casa. \return true si es variante B.
[[nodiscard]] constexpr bool big_house_door_east(Feature f) {
	return has(f, Feature::HouseB_NW) || has(f, Feature::HouseB_NE) || has(f, Feature::HouseB_SW) ||
	       has(f, Feature::HouseB_SE);
}
/// Cuadrante que ocupa esta celda de la casa grande (el bit presente en la máscara).
/// \param f máscara de rasgos de una celda de casa. \return cuadrante.
[[nodiscard]] constexpr StructPart big_house_part(Feature f) {
	if (has(f, Feature::HouseA_NE) || has(f, Feature::HouseB_NE)) return StructPart::NorthEast;
	if (has(f, Feature::HouseA_SW) || has(f, Feature::HouseB_SW)) return StructPart::SouthWest;
	if (has(f, Feature::HouseA_SE) || has(f, Feature::HouseB_SE)) return StructPart::SouthEast;
	return StructPart::NorthWest;
}
/// ¿La máscara lleva alguna parte del **árbol grande** 2×2?
/// \param f máscara de rasgos. \return true si es una celda del árbol.
[[nodiscard]] constexpr bool is_big_tree(Feature f) {
	return has(f, Feature::TreeBig_NW) || has(f, Feature::TreeBig_NE) ||
	       has(f, Feature::TreeBig_SW) || has(f, Feature::TreeBig_SE);
}
/// Cuadrante que ocupa esta celda del árbol grande.
/// \param f máscara de rasgos de una celda del árbol. \return cuadrante.
[[nodiscard]] constexpr StructPart big_tree_part(Feature f) {
	if (has(f, Feature::TreeBig_NE)) return StructPart::NorthEast;
	if (has(f, Feature::TreeBig_SW)) return StructPart::SouthWest;
	if (has(f, Feature::TreeBig_SE)) return StructPart::SouthEast;
	return StructPart::NorthWest;
}

/// Estampa un **cuadrante de la casa grande 2×2** (diseño global de `2·TW × 2·TH`):
/// tejado con veta clara (filas 0..`7/16`), alero oscuro, pared de madera, ventanas
/// de mármol a ambos lados y **puerta** abajo en el cuadrante que marca la variante
/// (A: SW; B: SE). Cada celda dibuja solo su cuadrado del diseño, de modo que las
/// cuatro piezas **casan por construcción**; los materiales se muestrean con variante
/// fija 0 (texturas tileables: el patrón continúa entre cuadrantes).
/// \tparam TW,TH lado del tile (el diseño es el doble). \tparam S escalar del atlas.
/// \param buf destino (se estampa encima del material base del pintor). \param part
///        cuadrante de esta celda. \param door_east true = variante B (puerta a la derecha).
/// \param atlas atlas de materiales. \param tile_x,tile_y posición (variante de material fija 0).
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void paint_big_house(BitmapBuffer<TW, TH>& buf, StructPart part, bool door_east,
			       const TextureAtlas<TW, TH, S>& atlas, eng::u16, eng::u16) {
	const eng::u16 gw = eng::u16(2u * TW); // ancho del diseño global (2 tiles)
	const eng::u16 gh = eng::u16(2u * TH);
	const eng::u16 ox = (part == StructPart::NorthEast || part == StructPart::SouthEast) ? TW : 0u;
	const eng::u16 oy = (part == StructPart::SouthWest || part == StructPart::SouthEast) ? TH : 0u;
	// Geometría proporcional (exacta a 32×32 global): tejado 14/32, alero 1, pared el
	// resto; ventanas 18..20/32; puerta 24..31/32; jambas y anchos a 1/16 del global.
	const eng::u16 roof_h = eng::u16((gh * 14u) >> 5u);
	const eng::u16 roof_x0 = eng::u16(gw / 16u);
	const eng::u16 roof_x1 = eng::u16(gw - gw / 16u);
	const eng::u16 ridge0 = eng::u16((gh * 6u) >> 5u);
	const eng::u16 ridge1 = eng::u16((gh * 7u) >> 5u);
	const eng::u16 wall_x0 = eng::u16((gw * 3u) >> 5u);
	const eng::u16 wall_x1 = eng::u16(gw - ((gw * 3u) >> 5u));
	const eng::u16 win_y0 = eng::u16((gh * 18u) >> 5u);
	const eng::u16 win_y1 = eng::u16((gh * 20u) >> 5u);
	const eng::u16 win_x0 = eng::u16((gw * 5u) >> 5u);
	const eng::u16 win_x1 = eng::u16((gw * 8u) >> 5u);
	const eng::u16 door_y0 = eng::u16((gh * 24u) >> 5u);
	const eng::u16 door_x0 = door_east ? eng::u16(gw - ((gw * 10u) >> 5u)) : eng::u16((gw * 5u) >> 5u);
	const eng::u16 door_x1 = door_east ? eng::u16(gw - ((gw * 5u) >> 5u)) : eng::u16((gw * 10u) >> 5u);
	const BitmapBuffer<TW, TH>& roof = atlas.get(MaterialId::Roof, 0u);
	const BitmapBuffer<TW, TH>& ridge = atlas.get(MaterialId::Marble, 0u);
	const BitmapBuffer<TW, TH>& wall = atlas.get(MaterialId::Wood, 0u);
	const BitmapBuffer<TW, TH>& trim = atlas.get(MaterialId::Marble, 0u);
	const BitmapBuffer<TW, TH>& door = atlas.get(MaterialId::Path, 0u);
	const BitmapBuffer<TW, TH>& shade = atlas.get(MaterialId::Cave, 0u);
	for (eng::u16 ly = 0u; ly < TH; ++ly) {
		for (eng::u16 lx = 0u; lx < TW; ++lx) {
			const eng::u16 gx = eng::u16(ox + lx);
			const eng::u16 gy = eng::u16(oy + ly);
			// Fuera del contorno de la casa no se escribe (queda el material base).
			const bool in_roof = gy < roof_h && gx >= roof_x0 && gx <= roof_x1;
			const bool in_wall = gy >= roof_h && gx >= wall_x0 && gx <= wall_x1;
			if (!in_roof && !in_wall) continue;
			const bool window = in_wall && gy >= win_y0 && gy <= win_y1 &&
					    ((gx >= win_x0 && gx <= win_x1) ||
					     (gx >= eng::u16(gw - win_x1) && gx <= eng::u16(gw - win_x0)));
			const bool door_px = in_wall && gy >= door_y0 && gx >= door_x0 && gx <= door_x1;
			const bool ridge_px = in_roof && gy >= ridge0 && gy <= ridge1;
			const bool eave_px = in_roof && gy == eng::u16(roof_h - 1u);
			if (door_px) {
				buf.at(lx, ly) = door.at(lx, ly);
			} else if (window) {
				buf.at(lx, ly) = trim.at(lx, ly);
			} else if (in_roof) {
				buf.at(lx, ly) = ridge_px ? ridge.at(lx, ly)
						 : eave_px  ? shade.at(lx, ly)
							    : roof.at(lx, ly);
			} else {
				buf.at(lx, ly) = wall.at(lx, ly);
			}
		}
	}
}

/// Estampa un **cuadrante del árbol grande 2×2**: copa `SoftCircleMask` (disco
/// irregular determinista) centrada en el medio del diseño global y tronco en las
/// columnas centrales (mitad izquierda en SW, derecha en SE). La silueta se evalúa en
/// coordenadas **globales** con el seed de la estructura (el generador asigna el
/// MISMO seed a las cuatro piezas) → la copa es continua entre cuadrantes.
/// \tparam TW,TH lado del tile. \tparam S escalar del atlas. \param buf destino.
/// \param part cuadrante. \param seed seed de la estructura. \param atlas atlas.
/// \param tile_x,tile_y posición (no usada: variante de material fija 0).
template <eng::u16 TW, eng::u16 TH, class S>
constexpr void paint_big_tree(BitmapBuffer<TW, TH>& buf, StructPart part, eng::u32 seed,
			      const TextureAtlas<TW, TH, S>& atlas, eng::u16, eng::u16) {
	const eng::u16 gw = eng::u16(2u * TW);
	const eng::u16 gh = eng::u16(2u * TH);
	const eng::u16 ox = (part == StructPart::NorthEast || part == StructPart::SouthEast) ? TW : 0u;
	const eng::u16 oy = (part == StructPart::SouthWest || part == StructPart::SouthEast) ? TH : 0u;
	const SoftCircleMask<S> blob = [&]() constexpr {
		SoftCircleMask<S> b {};
		b.cx = TW;		  // centro del 2×2 (coordenadas globales)
		b.cy = TH;
		const eng::u16 r0 = eng::u16((TW * 11u) >> 4u); // 11/16·TW: núcleo opaco
		const eng::u16 r1 = eng::u16((TW * 15u) >> 4u); // 15/16·TW: borde irregular
		b.r2_min = eng::u16(r0 * r0);
		b.r2_max = eng::u16(r1 * r1);
		b.seed = seed;
		return b;
	}();
	const eng::u16 trunk_x0 = eng::u16(TW - (TW >> 3u));
	const eng::u16 trunk_x1 = eng::u16(TW + (TW >> 3u) - 1u);
	const eng::u16 trunk_y0 = eng::u16((gh * 22u) >> 5u);
	const BitmapBuffer<TW, TH>& foliage = atlas.get(MaterialId::Foliage, 0u);
	const BitmapBuffer<TW, TH>& rim = atlas.get(MaterialId::Cave, 0u);
	const BitmapBuffer<TW, TH>& trunk = atlas.get(MaterialId::Trunk, 0u);
	for (eng::u16 ly = 0u; ly < TH; ++ly) {
		for (eng::u16 lx = 0u; lx < TW; ++lx) {
			const eng::u16 gx = eng::u16(ox + lx);
			const eng::u16 gy = eng::u16(oy + ly);
			if (blob(gx, gy) != 0u) {
				// Banda exterior (borde irregular): dither oscuro → copa con volumen.
				const eng::s32 dx = eng::s32(gx) - eng::s32(blob.cx);
				const eng::s32 dy = eng::s32(gy) - eng::s32(blob.cy);
				const eng::u32 d2 = eng::u32(dx * dx + dy * dy);
				const bool edge = d2 * 2u > eng::u32(blob.r2_min) + eng::u32(blob.r2_max);
				const bool dark = edge && (((gx + gy) & 1u) != 0u);
				buf.at(lx, ly) = dark ? rim.at(lx, ly) : foliage.at(lx, ly);
			}
			if (gy >= trunk_y0 && gx >= trunk_x0 && gx <= trunk_x1) {
				buf.at(lx, ly) = trunk.at(lx, ly);
			}
		}
	}
}
} // namespace eng::gfx::proc::terrain
