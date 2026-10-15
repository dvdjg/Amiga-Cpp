#pragma once

/// \file terrain_paint.hpp
/// **Pintor de piezas de terreno** (`eng::gfx::proc::terrain`): compone cada `TileSpec`
/// muestreando el `TextureAtlas` de materiales y sellando las primitivas de
/// `shapes.hpp`. Orden del pintor (algoritmo del pintor, fondo → primer plano):
///
/// ```text
///   base (hierba/cultivo/patio/piedra/cueva)      paint_base
///   + surcos de cultivo                            paint_crops
///   + lago (agua + orilla Foam)                    paint_lake
///   + cauce (agua + orilla; puente si Bridge)      paint_river
///   + árboles / rocas / montaña                    paint_trees/rocks/mountain
///   + casa / cueva                                 paint_house / paint_cave_mouth
///   + muralla / torre / puerta / musgo del patio   paint_wall/tower/gate/yard_moss
///   + franjas de borde suave (rampa + dither)      apply_edge_transition
/// ```
///
/// La continuidad entre vecinos es estructural: el agua entra/sale por el centro exacto
/// del borde (dos tramos casan), la muralla se centra en el borde, y la rampa de la
/// transición es idéntica en ambos lados del mismo id (+ dither Bayer espacial).
///
/// El azar sale de un `Xoroshiro64pp` sembrado con `spec.seed`: mismo seed = mismo tile
/// en host y en Amiga. Todo el coste es de **generación** (carga), nunca por frame.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-442`); sin demo que la ejercite en hardware.

#include <eng/core/math/noise.hpp>
#include <eng/core/math/random.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/evaluators.hpp>
#include <eng/graphics/proc/shapes.hpp>
#include <eng/graphics/proc/terrain_types.hpp>
#include <eng/graphics/proc/tiles.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::gfx::proc::terrain {

/// ¿Este borde pinta franja de transición suave? No la pintan `Grass` (llano), `Water`
/// (el cauce la sustituye) ni `Wall` (el tramo la sustituye): su continuidad es
/// estructural, no una franja de material representativo.
/// \param e id de borde. \return true si `apply_edge_transition` pinta su franja.
[[nodiscard]] constexpr bool edge_has_transition(Edge e) {
	switch (e) {
	case Edge::Path:
	case Edge::Forest:
	case Edge::Ridge:
	case Edge::Crop:
	case Edge::Cave: return true;
	default: return false;
	}
}

/// Material del atlas que representa un borde en su franja de transición (solo definido
/// para los bordes con transición; `Grass` como reserva).
/// \param e id de borde. \return material representativo.
[[nodiscard]] constexpr MaterialId edge_material(Edge e) {
	switch (e) {
	case Edge::Path: return MaterialId::Path;
	case Edge::Forest: return MaterialId::Foliage;
	case Edge::Ridge: return MaterialId::Rock;
	case Edge::Crop: return MaterialId::CropA;
	case Edge::Cave: return MaterialId::Cave;
	default: return MaterialId::Grass;
	}
}

/// Estampa un material del atlas donde `mask(x,y) != 0` (sello genérico: primitivas de
/// `shapes.hpp`, máscaras de agua, etc.). El píxel de material se toma en la misma `(x,y)`
/// del tile, así que la textura del sello es la del material (coherente con el fondo).
/// \tparam TW,TH geometría del tile. \tparam S escalar del atlas. \tparam MaskFn callable `bool/u8(u16,u16)`.
/// \param buf buffer destino. \param atlas atlas de materiales. \param id material a estampar.
/// \param variant variante del material. \param mask máscara de cobertura del sello.
template <eng::u16 TW, eng::u16 TH, class S, class MaskFn>
void stamp_material(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, MaterialId id,
		    eng::u8 variant, MaskFn&& mask) {
	const BitmapBuffer<TW, TH>& tex = atlas.get(id, variant);
	for (eng::u16 y = 0; y < TH; ++y) {
		for (eng::u16 x = 0; x < TW; ++x) {
			if (mask(x, y)) buf.at(x, y) = tex.at(x, y);
		}
	}
}

/// Copia completa de un tile de material (paso 1 del pintor: el fondo del tile).
/// \tparam TW,TH geometría del tile. \tparam S escalar del atlas.
/// \param buf destino. \param atlas atlas. \param id material base. \param variant variante.
template <eng::u16 TW, eng::u16 TH, class S>
void copy_material(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, MaterialId id,
		   eng::u8 variant) {
	const BitmapBuffer<TW, TH>& tex = atlas.get(id, variant);
	for (eng::u16 y = 0; y < TH; ++y) {
		for (eng::u16 x = 0; x < TW; ++x) {
			buf.at(x, y) = tex.at(x, y);
		}
	}
}

/// Paso 1 del pintor: elige el material base por rasgo dominante (cultivo, patio, piedra
/// de montaña, cueva; hierba por defecto) y lo copia con la variante del tile.
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param spec pieza.
/// \param atlas atlas. \param tile_x,tile_y posición en el mapa (elige variante).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_base(BitmapBuffer<TW, TH>& buf, const TileSpec& spec, const TextureAtlas<TW, TH, S>& atlas,
		eng::u16 tile_x, eng::u16 tile_y) {
	MaterialId base = MaterialId::Grass;
	if (has(spec.features, Feature::Crops)) base = MaterialId::CropA;
	if (has(spec.features, Feature::CastleYard)) base = MaterialId::Cobble;
	if (has(spec.features, Feature::Mountain)) base = MaterialId::Stone;
	if (has(spec.features, Feature::CaveMouth)) base = MaterialId::Cave;
	copy_material(buf, atlas, base, atlas.variant_at(tile_x, tile_y, base));
}

/// Estampa los surcos de cultivo (`CropRows`): el marcador 1 elige `CropA` y el 2
/// `CropB` (dos materiales que comparten escala para verse como un campo con dos tonos).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino.
/// \param atlas atlas. \param rng RNG de la pieza (espaciado, orientación, seed).
/// \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_crops(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas,
		 eng::Xoroshiro64pp& rng, eng::u16 tile_x, eng::u16 tile_y) {
	CropRows<S> rows {};
	rows.color_a = 1u;
	rows.color_b = 2u;
	rows.spacing = 3u;
	rows.seed = rng.next();
	rows.horizontal = (rng.next() & 1u) != 0u;
	const BitmapBuffer<TW, TH>& a =
		atlas.get(MaterialId::CropA, atlas.variant_at(tile_x, tile_y, MaterialId::CropA));
	const BitmapBuffer<TW, TH>& b =
		atlas.get(MaterialId::CropB, atlas.variant_at(tile_x, tile_y, MaterialId::CropB));
	for (eng::u16 y = 0; y < TH; ++y) {
		for (eng::u16 x = 0; x < TW; ++x) {
			const eng::u8 c = rows(x, y);
			if (c == rows.color_a) {
				buf.at(x, y) = a.at(x, y);
			} else if (c == rows.color_b) {
				buf.at(x, y) = b.at(x, y);
			}
		}
	}
}

/// Estampa un lago: primero la orilla de espuma (`Foam`, radios +1) y luego el agua
/// (`Water`), de modo que queda un anillo de orilla. El contorno es irregular (ruido).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino.
/// \param atlas atlas. \param rng RNG (centro, radios y seed del contorno).
/// \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_lake(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas,
		eng::Xoroshiro64pp& rng, eng::u16 tile_x, eng::u16 tile_y) {
	LakeBlob<S> water {};
	water.cx = eng::u16(8u + rng.next_mod(16u));
	water.cy = eng::u16(8u + rng.next_mod(16u));
	water.rx = eng::u16(6u + rng.next_mod(6u));
	water.ry = eng::u16(5u + rng.next_mod(5u));
	water.color = 1u;
	water.seed = rng.next();
	const eng::u8 vw = atlas.variant_at(tile_x, tile_y, MaterialId::Water);
	const eng::u8 vf = atlas.variant_at(tile_x ^ 1u, tile_y, MaterialId::Foam);
	LakeBlob<S> shore = water;
	shore.rx = eng::u16(water.rx + 1u);
	shore.ry = eng::u16(water.ry + 1u);
	stamp_material(buf, atlas, MaterialId::Foam, vf, shore);
	stamp_material(buf, atlas, MaterialId::Water, vw, water);
}

/// Estampa el cauce: por cada borde `Water` hay un punto de conexión en el **centro** del
/// borde; el cauce une dos puntos con un segmento ondulado (recto si son opuestos, en
/// diagonal si son adyacentes), y con los cuatro bordes de agua se cruzan dos cauces
/// (cruce/ensanche). Primero la orilla (`Foam`, semi-ancho +1), luego el agua: queda el
/// anillo de orilla visible. Con `Feature::Bridge`, encima va el puente.
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param spec pieza.
/// \param atlas atlas. \param rng RNG (ancho y ondulación del cauce).
/// \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_river(BitmapBuffer<TW, TH>& buf, const TileSpec& spec, const TextureAtlas<TW, TH, S>& atlas,
		 eng::Xoroshiro64pp& rng, eng::u16 tile_x, eng::u16 tile_y) {
	eng::u16 wx[4] {};
	eng::u16 wy[4] {};
	eng::u8 wn = 0;
	if (spec.edges.n == Edge::Water) {
		wx[wn] = TW / 2u;
		wy[wn] = 0u;
		++wn;
	}
	if (spec.edges.e == Edge::Water) {
		wx[wn] = TW - 1u;
		wy[wn] = TH / 2u;
		++wn;
	}
	if (spec.edges.s == Edge::Water) {
		wx[wn] = TW / 2u;
		wy[wn] = TH - 1u;
		++wn;
	}
	if (spec.edges.w == Edge::Water) {
		wx[wn] = 0u;
		wy[wn] = TH / 2u;
		++wn;
	}
	if (wn == 0u) return;
	const eng::u16 w = eng::u16(3u + rng.next_mod(3u)); // semi-ancho 3..5
	const eng::u32 seed = rng.next();
	const eng::u8 vw = atlas.variant_at(tile_x, tile_y, MaterialId::Water);
	const eng::u8 vf = atlas.variant_at(tile_x, tile_y ^ 1u, MaterialId::Foam);
	const eng::u16 x1 = (wn >= 2u) ? wx[1] : eng::u16(TW / 2u);
	const eng::u16 y1 = (wn >= 2u) ? wy[1] : eng::u16(TH / 2u);
	RiverStroke<S> shore {wx[0], wy[0], x1, y1, eng::u16(w + 1u), 1u, seed, 3u};
	stamp_material(buf, atlas, MaterialId::Foam, vf, shore);
	RiverStroke<S> stroke {wx[0], wy[0], x1, y1, w, 1u, seed, 3u};
	stamp_material(buf, atlas, MaterialId::Water, vw, stroke);
	if (wn >= 4u) {
		RiverStroke<S> shore2 {wx[2], wy[2], wx[3], wy[3], eng::u16(w + 1u), 1u, seed ^ 1u, 3u};
		stamp_material(buf, atlas, MaterialId::Foam, vf, shore2);
		RiverStroke<S> stroke2 {wx[2], wy[2], wx[3], wy[3], w, 1u, seed ^ 1u, 3u};
		stamp_material(buf, atlas, MaterialId::Water, vw, stroke2);
	}
	if (has(spec.features, Feature::Bridge)) {
		const BitmapBuffer<TW, TH>& path =
			atlas.get(MaterialId::Path, atlas.variant_at(tile_x, tile_y, MaterialId::Path));
		const BitmapBuffer<TW, TH>& wood =
			atlas.get(MaterialId::Wood, atlas.variant_at(tile_x, tile_y, MaterialId::Wood));
		const eng::u16 y0 = TH / 2u >= 2u ? eng::u16(TH / 2u - 2u) : 0u;
		const eng::u16 y1b = TH / 2u + 2u < TH ? eng::u16(TH / 2u + 2u) : eng::u16(TH - 1u);
		for (eng::u16 y = y0; y <= y1b; ++y) {
			for (eng::u16 x = 0; x < TW; ++x) {
				buf.at(x, y) = path.at(x, y); // camino de acceso a ambos lados
			}
		}
		const eng::u16 bw = eng::u16(w + 1u);
		const eng::u16 bx = eng::u16(TW / 2u >= bw ? TW / 2u - bw : 0u);
		const eng::u16 ex = eng::u16(TW / 2u + bw < TW ? TW / 2u + bw : TW - 1u);
		for (eng::u16 y = y0; y <= y1b; ++y) {
			for (eng::u16 x = bx; x <= ex; ++x) {
				buf.at(x, y) = wood.at(x, y); // tablones sobre el agua
			}
		}
	}
}

/// Estampa un bosque: `2 + rng mod 4` árboles (`TreeCanopy`, marcador 1 = copa, 2 =
/// tronco; hojas = `Foliage`, tronco = `Trunk`) con centro, radio y semilla por árbol.
/// Se aplican en orden para que unos tapen a otros (algoritmo del pintor).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino.
/// \param atlas atlas. \param rng RNG (nº, posición, radio y seed por árbol).
/// \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_trees(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas,
		 eng::Xoroshiro64pp& rng, eng::u16 tile_x, eng::u16 tile_y) {
	const eng::u8 ntrees = eng::u8(2u + rng.next_mod(4u));
	const BitmapBuffer<TW, TH>& fol =
		atlas.get(MaterialId::Foliage, atlas.variant_at(tile_x, tile_y, MaterialId::Foliage));
	const BitmapBuffer<TW, TH>& trk =
		atlas.get(MaterialId::Trunk, atlas.variant_at(tile_x, tile_y, MaterialId::Trunk));
	for (eng::u8 t = 0; t < ntrees; ++t) {
		TreeCanopy<S> tree {};
		tree.cx = eng::u16(4u + rng.next_mod(TW - 8u));
		tree.cy = eng::u16(4u + rng.next_mod(TH - 8u));
		tree.r2 = eng::u16(20u + rng.next_mod(30u));
		tree.foliage = 1u;
		tree.trunk = 2u;
		tree.draw_trunk = true;
		tree.seed = rng.next();
		for (eng::u16 y = 0; y < TH; ++y) {
			for (eng::u16 x = 0; x < TW; ++x) {
				const eng::u8 c = tree(x, y);
				if (c == tree.foliage) {
					buf.at(x, y) = fol.at(x, y);
				} else if (c == tree.trunk) {
					buf.at(x, y) = trk.at(x, y);
				}
			}
		}
	}
}

/// Estampa peñascos (`RockBlob`, marcador 1 → material `Rock`).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino.
/// \param atlas atlas. \param rng RNG (nº, posición, radio y seed por roca).
/// \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_rocks(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas,
		 eng::Xoroshiro64pp& rng, eng::u16 tile_x, eng::u16 tile_y) {
	const eng::u8 nrocks = eng::u8(1u + rng.next_mod(3u));
	const BitmapBuffer<TW, TH>& rock =
		atlas.get(MaterialId::Rock, atlas.variant_at(tile_x, tile_y, MaterialId::Rock));
	for (eng::u8 r = 0; r < nrocks; ++r) {
		RockBlob<S> blob {};
		blob.cx = eng::u16(3u + rng.next_mod(TW - 6u));
		blob.cy = eng::u16(3u + rng.next_mod(TH - 6u));
		blob.r2 = eng::u16(12u + rng.next_mod(20u));
		blob.color = 1u;
		blob.seed = rng.next();
		for (eng::u16 y = 0; y < TH; ++y) {
			for (eng::u16 x = 0; x < TW; ++x) {
				if (blob(x, y) == blob.color) buf.at(x, y) = rock.at(x, y);
			}
		}
	}
}

/// Estampa la silueta de montaña: un perfil de colina (`Profile1D`, amplitudes del
/// diseño: 9/16 del alto con base 1/4) rellena con `Rock` y una cima de `Snow` (3 filas
/// donde el perfil supera un tercio de su amplitud). Sin ruido por píxel: lookup puro.
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param atlas atlas.
/// \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_mountain(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, eng::u16 tile_x,
		    eng::u16 tile_y) {
	const eng::u16 amp = eng::u16((eng::u32(TH) * 9u) / 16u);
	const eng::u16 base = eng::u16(TH / 4u);
	const Profile1D<TW> profile = Profile1D<TW>::hill(amp, base);
	const BitmapBuffer<TW, TH>& rock =
		atlas.get(MaterialId::Rock, atlas.variant_at(tile_x, tile_y, MaterialId::Rock));
	const BitmapBuffer<TW, TH>& snow =
		atlas.get(MaterialId::Snow, atlas.variant_at(tile_x, tile_y, MaterialId::Snow));
	const eng::u16 snow_top = eng::u16(base + amp / 3u); // cima nevada sobre un tercio
	for (eng::u16 x = 0; x < TW; ++x) {
		const eng::u16 h = profile.y[x];
		const eng::u16 y0 = h < TH ? eng::u16(TH - h) : 0u;
		for (eng::u16 y = y0; y < TH; ++y) {
			const bool snowy = h > snow_top && y < y0 + 3u;
			buf.at(x, y) = snowy ? snow.at(x, y) : rock.at(x, y);
		}
	}
}

/// Estampa una casa (`HouseFootprint`): marcador 1 = pared (`Wood`), 2 = tejado (`Roof`),
/// 3 = puerta (`Path`).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino.
/// \param atlas atlas. \param rng RNG (posición de la casa).
/// \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_house(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas,
		 eng::Xoroshiro64pp& rng, eng::u16 tile_x, eng::u16 tile_y) {
	HouseFootprint house {};
	house.x0 = eng::u16(6u + rng.next_mod(8u));
	house.y0 = eng::u16(8u + rng.next_mod(6u));
	house.w = 14u;
	house.h = 12u;
	house.wall = 1u;
	house.roof = 2u;
	house.door = 3u;
	const BitmapBuffer<TW, TH>& wall =
		atlas.get(MaterialId::Wood, atlas.variant_at(tile_x, tile_y, MaterialId::Wood));
	const BitmapBuffer<TW, TH>& roof =
		atlas.get(MaterialId::Roof, atlas.variant_at(tile_x, tile_y, MaterialId::Roof));
	const BitmapBuffer<TW, TH>& door =
		atlas.get(MaterialId::Path, atlas.variant_at(tile_x, tile_y, MaterialId::Path));
	for (eng::u16 y = 0; y < TH; ++y) {
		for (eng::u16 x = 0; x < TW; ++x) {
			const eng::u8 c = house(x, y);
			if (c == house.wall) {
				buf.at(x, y) = wall.at(x, y);
			} else if (c == house.roof) {
				buf.at(x, y) = roof.at(x, y);
			} else if (c == house.door) {
				buf.at(x, y) = door.at(x, y);
			}
		}
	}
}

/// Estampa la boca de cueva (`CaveMouth`): marcador 1 = interior (`Cave`), 2 = roca
/// (`Rock`).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param atlas atlas.
/// \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_cave_mouth(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas,
		      eng::u16 tile_x, eng::u16 tile_y) {
	CaveMouth cave {};
	cave.cx = eng::u16(TW / 2u);
	cave.cy = eng::u16(TH - 8u);
	cave.rx = 7u;
	cave.ry = 5u;
	cave.dark = 1u;
	cave.rock = 2u;
	const BitmapBuffer<TW, TH>& dark =
		atlas.get(MaterialId::Cave, atlas.variant_at(tile_x, tile_y, MaterialId::Cave));
	const BitmapBuffer<TW, TH>& rock =
		atlas.get(MaterialId::Rock, atlas.variant_at(tile_x, tile_y, MaterialId::Rock));
	for (eng::u16 y = 0; y < TH; ++y) {
		for (eng::u16 x = 0; x < TW; ++x) {
			const eng::u8 c = cave(x, y);
			if (c == cave.dark) {
				buf.at(x, y) = dark.at(x, y);
			} else if (c == cave.rock) {
				buf.at(x, y) = rock.at(x, y);
			}
		}
	}
}

/// Deriva la orientación del tramo de muralla de sus bordes `Wall`: esquinas si dos
/// bordes adyacentes son muro, recta vertical (N/S) o recta horizontal (E/W) en el resto.
/// \param e bordes de la pieza. \return dirección del `WallSegment`.
[[nodiscard]] constexpr WallSegment::Dir wall_dir(EdgeSides<Edge> e) {
	const bool n = e.n == Edge::Wall;
	const bool s = e.s == Edge::Wall;
	const bool w = e.w == Edge::Wall;
	const bool east = e.e == Edge::Wall;
	if (n && east) return WallSegment::Dir::NorthEast;
	if (n && w) return WallSegment::Dir::NorthWest;
	if (s && east) return WallSegment::Dir::SouthEast;
	if (s && w) return WallSegment::Dir::SouthWest;
	if (n || s) return WallSegment::Dir::North; // franja vertical (continúa al norte/sur)
	return WallSegment::Dir::East;            // franja horizontal (continúa al este/oeste)
}

/// Estampa el tramo de muralla (`WallSegment`): marcador 1 = cuerpo (`Wall`) y 2 =
/// almena (`Marble`, la piedra clara). La orientación sale de `wall_dir`.
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param spec pieza.
/// \param atlas atlas. \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_wall(BitmapBuffer<TW, TH>& buf, const TileSpec& spec, const TextureAtlas<TW, TH, S>& atlas,
		eng::u16 tile_x, eng::u16 tile_y) {
	WallSegment wall {};
	wall.dir = wall_dir(spec.edges);
	wall.color = 1u;
	wall.battlement = 2u;
	wall.thickness = 5u;
	wall.center_x = eng::u16(TW / 2u);
	wall.center_y = eng::u16(TH / 2u);
	wall.tile_w = TW;
	wall.tile_h = TH;
	wall.edge_band = 3u;
	const BitmapBuffer<TW, TH>& body =
		atlas.get(MaterialId::Wall, atlas.variant_at(tile_x, tile_y, MaterialId::Wall));
	const BitmapBuffer<TW, TH>& top =
		atlas.get(MaterialId::Marble, atlas.variant_at(tile_x, tile_y, MaterialId::Marble));
	for (eng::u16 y = 0; y < TH; ++y) {
		for (eng::u16 x = 0; x < TW; ++x) {
			const eng::u8 c = wall(x, y);
			if (c == wall.color) {
				buf.at(x, y) = body.at(x, y);
			} else if (c == wall.battlement) {
				buf.at(x, y) = top.at(x, y);
			}
		}
	}
}

/// Estampa la torre (`TowerTop`): marcador 1 = anillo (`Wall`), 2 = tejado (`Roof`),
/// 3 = merlones (`Marble`).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param atlas atlas.
/// \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_tower(BitmapBuffer<TW, TH>& buf, const TextureAtlas<TW, TH, S>& atlas, eng::u16 tile_x,
		 eng::u16 tile_y) {
	TowerTop tower {};
	tower.cx = eng::u16(TW / 2u);
	tower.cy = eng::u16(TH / 2u);
	tower.r2 = 70u;
	tower.wall = 1u;
	tower.roof = 2u;
	tower.merlon = 3u;
	tower.ring = 20u;
	const BitmapBuffer<TW, TH>& ring =
		atlas.get(MaterialId::Wall, atlas.variant_at(tile_x, tile_y, MaterialId::Wall));
	const BitmapBuffer<TW, TH>& roof =
		atlas.get(MaterialId::Roof, atlas.variant_at(tile_x, tile_y, MaterialId::Roof));
	const BitmapBuffer<TW, TH>& merlon =
		atlas.get(MaterialId::Marble, atlas.variant_at(tile_x, tile_y, MaterialId::Marble));
	for (eng::u16 y = 0; y < TH; ++y) {
		for (eng::u16 x = 0; x < TW; ++x) {
			const eng::u8 c = tower(x, y);
			if (c == tower.wall) {
				buf.at(x, y) = ring.at(x, y);
			} else if (c == tower.roof) {
				buf.at(x, y) = roof.at(x, y);
			} else if (c == tower.merlon) {
				buf.at(x, y) = merlon.at(x, y);
			}
		}
	}
}

/// Abre la puerta en el tramo de muralla: hueco (índice 0, transparente) centrado en el
/// paso del muro con jambas de `Wall` a ambos lados. La orientación (muro vertical u
/// horizontal) se deduce de los bordes `Wall`; la puerta complementa al tramo.
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param spec pieza.
/// \param atlas atlas. \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_gate(BitmapBuffer<TW, TH>& buf, const TileSpec& spec, const TextureAtlas<TW, TH, S>& atlas,
		eng::u16 tile_x, eng::u16 tile_y) {
	const bool vertical = spec.edges.n == Edge::Wall || spec.edges.s == Edge::Wall;
	const BitmapBuffer<TW, TH>& jamb =
		atlas.get(MaterialId::Wall, atlas.variant_at(tile_x, tile_y, MaterialId::Wall));
	const eng::u16 cx = TW / 2u;
	const eng::u16 cy = TH / 2u;
	for (eng::u16 dy = 0; dy < 8u; ++dy) {
		for (eng::u16 dx = 0; dx < 6u; ++dx) {
			const eng::u16 x = vertical ? eng::u16(cx - 3u + dx) : eng::u16(cx - 4u + dx);
			const eng::u16 y = vertical ? eng::u16(cy - 4u + dy) : eng::u16(cy - 3u + dy);
			buf.at(x, y) = 0u; // hueco del paso (índice transparente)
		}
	}
	for (eng::u16 d = 0; d < 10u; ++d) {
		const eng::u16 x = vertical ? eng::u16(cx - 4u) : eng::u16(cx - 5u + d);
		const eng::u16 y = vertical ? eng::u16(cy - 5u + d) : eng::u16(cy - 4u);
		buf.at(x, y) = jamb.at(x, y);
		const eng::u16 x2 = vertical ? eng::u16(cx + 3u) : x;
		const eng::u16 y2 = vertical ? y : eng::u16(cy + 3u);
		buf.at(x2, y2) = jamb.at(x2, y2);
	}
}

/// Siembra musgo en el patio (`CastleYard`): una máscara de ruido de baja frecuencia
/// (celdas de 1/4 del tile) deja pasar el material `Moss` entre los adoquines. La inversa
/// de la celda se calcula una vez por tile (1 división, no por píxel).
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param spec pieza.
/// \param atlas atlas. \param tile_x,tile_y posición en el mapa (variantes de material).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_yard_moss(BitmapBuffer<TW, TH>& buf, const TileSpec& spec,
		     const TextureAtlas<TW, TH, S>& atlas, eng::u16 tile_x, eng::u16 tile_y) {
	using eng::math::div_norm;
	using eng::math::mul_norm;
	using eng::math::scalar_const;
	using eng::math::scalar_traits;
	using eng::math::value_noise2;
	const eng::u16 cell = TW >= 8u ? eng::u16(TW / 4u) : 2u;
	const S inv = div_norm(scalar_traits<S>::one(), scalar_traits<S>::from_int(cell));
	const S thr = scalar_const<S>::from(0.62);
	const BitmapBuffer<TW, TH>& moss =
		atlas.get(MaterialId::Moss, atlas.variant_at(tile_x, tile_y, MaterialId::Moss));
	for (eng::u16 y = 0; y < TH; ++y) {
		for (eng::u16 x = 0; x < TW; ++x) {
			const S n = value_noise2(mul_norm(scalar_traits<S>::from_int(x), inv),
						 mul_norm(scalar_traits<S>::from_int(y), inv),
						 spec.seed + 77u);
			if (n > thr) buf.at(x, y) = moss.at(x, y);
		}
	}
}

/// Franja de **borde suave**: por cada lado con `edge_has_transition`, mezcla el material
/// del borde (muestreado en el atlas) con el contenido actual del tile usando la rampa
/// `smoothstep` (`ramp[0]` = interior, `ramp[bw-1]` = exterior) y `blend_index` (dither).
/// Las esquinas funden las dos rampas (peso medio; material del lado dominante). Se
/// aplica al final, de modo que las franjas integran los sellos (árboles, rocas) en el
/// borde del bosque o la cresta.
/// \tparam TW,TH geometría. \tparam S escalar. \param buf destino. \param spec pieza.
/// \param atlas atlas. \param tile_x,tile_y posición en el mapa (variantes de material).
/// \param border_w ancho de franja (4/6/8; se ajusta a la rampa disponible).
template <eng::u16 TW, eng::u16 TH, class S>
void apply_edge_transition(BitmapBuffer<TW, TH>& buf, const TileSpec& spec,
			   const TextureAtlas<TW, TH, S>& atlas, eng::u16 tile_x, eng::u16 tile_y,
			   eng::u8 border_w = 6u) {
	const eng::u8* ramp = kRamp6.t;
	eng::u8 bw = 6u;
	if (border_w <= 4u) {
		ramp = kRamp4.t;
		bw = 4u;
	} else if (border_w > 6u) {
		ramp = kRamp8.t;
		bw = 8u;
	}
	if (eng::u16(bw) * 2u > TW || eng::u16(bw) * 2u > TH) return; // sin sitio para la franja
	const EdgeSides<Edge> e = spec.edges;
	const bool tn = edge_has_transition(e.n);
	const bool te = edge_has_transition(e.e);
	const bool ts = edge_has_transition(e.s);
	const bool tw = edge_has_transition(e.w);
	if (tn) {
		const BitmapBuffer<TW, TH>& tex =
			atlas.get(edge_material(e.n), atlas.variant_at(tile_x, tile_y, edge_material(e.n)));
		for (eng::u16 y = 0; y < bw; ++y) {
			const eng::u8 alpha = ramp[bw - 1u - y]; // y=0 es el borde exterior
			for (eng::u16 x = 0; x < TW; ++x) {
				buf.at(x, y) = blend_index(buf.at(x, y), tex.at(x, y), alpha, x, y);
			}
		}
	}
	if (ts) {
		const BitmapBuffer<TW, TH>& tex =
			atlas.get(edge_material(e.s), atlas.variant_at(tile_x, tile_y, edge_material(e.s)));
		for (eng::u16 i = 0; i < bw; ++i) {
			const eng::u16 y = eng::u16(TH - bw + i);
			const eng::u8 alpha = ramp[i]; // i crece hacia el borde exterior (abajo)
			for (eng::u16 x = 0; x < TW; ++x) {
				buf.at(x, y) = blend_index(buf.at(x, y), tex.at(x, y), alpha, x, y);
			}
		}
	}
	if (tw) {
		const BitmapBuffer<TW, TH>& tex =
			atlas.get(edge_material(e.w), atlas.variant_at(tile_x, tile_y, edge_material(e.w)));
		for (eng::u16 x = 0; x < bw; ++x) {
			const eng::u8 alpha = ramp[bw - 1u - x]; // x=0 es el borde exterior
			for (eng::u16 y = 0; y < TH; ++y) {
				buf.at(x, y) = blend_index(buf.at(x, y), tex.at(x, y), alpha, x, y);
			}
		}
	}
	if (te) {
		const BitmapBuffer<TW, TH>& tex =
			atlas.get(edge_material(e.e), atlas.variant_at(tile_x, tile_y, edge_material(e.e)));
		for (eng::u16 i = 0; i < bw; ++i) {
			const eng::u16 x = eng::u16(TW - bw + i);
			const eng::u8 alpha = ramp[i]; // i crece hacia el borde exterior (derecha)
			for (eng::u16 y = 0; y < TH; ++y) {
				buf.at(x, y) = blend_index(buf.at(x, y), tex.at(x, y), alpha, x, y);
			}
		}
	}
	// Esquinas: solo si los dos lados adyacentes tienen franja (peso medio; material del
	// lado con más peso en ese píxel).
	for (eng::u16 j = 0; j < bw; ++j) {
		for (eng::u16 i = 0; i < bw; ++i) {
			const eng::u8 w_n = ramp[bw - 1u - j]; // peso del lado norte (y=j)
			const eng::u8 w_s = ramp[j];           // peso del lado sur (y=TH-bw+j)
			const eng::u8 w_w = ramp[bw - 1u - i]; // peso del lado oeste (x=i)
			const eng::u8 w_e = ramp[i];           // peso del lado este (x=TW-bw+i)
			const eng::u16 xe = eng::u16(TW - bw + i);
			const eng::u16 ys = eng::u16(TH - bw + j);
			if (tn && tw) {
				const BitmapBuffer<TW, TH>& tex =
					atlas.get(edge_material(w_n > w_w ? e.n : e.w),
						  atlas.variant_at(tile_x, tile_y,
								   edge_material(w_n > w_w ? e.n : e.w)));
				const eng::u8 wa = eng::u8((eng::u16(w_n) + w_w) >> 1u);
				buf.at(i, j) = blend_index(buf.at(i, j), tex.at(i, j), wa, i, j);
			}
			if (tn && te) {
				const BitmapBuffer<TW, TH>& tex =
					atlas.get(edge_material(w_n > w_e ? e.n : e.e),
						  atlas.variant_at(tile_x, tile_y,
								   edge_material(w_n > w_e ? e.n : e.e)));
				const eng::u8 wa = eng::u8((eng::u16(w_n) + w_e) >> 1u);
				buf.at(xe, j) = blend_index(buf.at(xe, j), tex.at(xe, j), wa, xe, j);
			}
			if (ts && tw) {
				const BitmapBuffer<TW, TH>& tex =
					atlas.get(edge_material(w_s > w_w ? e.s : e.w),
						  atlas.variant_at(tile_x, tile_y,
								   edge_material(w_s > w_w ? e.s : e.w)));
				const eng::u8 wa = eng::u8((eng::u16(w_s) + w_w) >> 1u);
				buf.at(i, ys) = blend_index(buf.at(i, ys), tex.at(i, ys), wa, i, ys);
			}
			if (ts && te) {
				const BitmapBuffer<TW, TH>& tex =
					atlas.get(edge_material(w_s > w_e ? e.s : e.e),
						  atlas.variant_at(tile_x, tile_y,
								   edge_material(w_s > w_e ? e.s : e.e)));
				const eng::u8 wa = eng::u8((eng::u16(w_s) + w_e) >> 1u);
				buf.at(xe, ys) = blend_index(buf.at(xe, ys), tex.at(xe, ys), wa, xe, ys);
			}
		}
	}
}

/// **Pinta una pieza de terreno completa** en `buf` (ver el diagrama del fichero): elige
/// la base, estampa los rasgos en orden de pintor (cultivo, lago, río+puente, árboles,
/// rocas, montaña, casa, cueva, muralla, torre, puerta, musgo del patio) y cierra con la
/// transición de bordes. Todo el azar sale de un `Xoroshiro64pp` sembrado con
/// `spec.seed`: la misma pieza da el mismo resultado en host y Amiga.
/// \tparam TW,TH geometría del tile. \tparam S escalar del ruido (el del atlas).
/// \param buf destino (se sobrescribe completo). \param spec pieza a pintar.
/// \param atlas atlas de materiales. \param tile_x,tile_y posición en el mapa (variantes).
/// \param border_w ancho de la franja de borde suave (4/6/8).
template <eng::u16 TW, eng::u16 TH, class S>
void paint_terrain_tile(BitmapBuffer<TW, TH>& buf, const TileSpec& spec,
			const TextureAtlas<TW, TH, S>& atlas, eng::u16 tile_x = 0u, eng::u16 tile_y = 0u,
			eng::u8 border_w = 6u) {
	eng::Xoroshiro64pp rng {spec.seed, spec.seed ^ 0xC0FFEEu};
	paint_base(buf, spec, atlas, tile_x, tile_y);
	if (has(spec.features, Feature::Crops)) paint_crops(buf, atlas, rng, tile_x, tile_y);
	if (has(spec.features, Feature::Lake)) paint_lake(buf, atlas, rng, tile_x, tile_y);
	// El cauce se pinta siempre que haya bordes de agua (el catálogo lo empareja con River).
	const bool water_edges = spec.edges.n == Edge::Water || spec.edges.e == Edge::Water ||
				 spec.edges.s == Edge::Water || spec.edges.w == Edge::Water;
	if (water_edges) paint_river(buf, spec, atlas, rng, tile_x, tile_y);
	if (has(spec.features, Feature::Trees)) paint_trees(buf, atlas, rng, tile_x, tile_y);
	if (has(spec.features, Feature::Rocks)) paint_rocks(buf, atlas, rng, tile_x, tile_y);
	if (has(spec.features, Feature::Mountain)) paint_mountain(buf, atlas, tile_x, tile_y);
	if (has(spec.features, Feature::House)) paint_house(buf, atlas, rng, tile_x, tile_y);
	if (has(spec.features, Feature::CaveMouth)) paint_cave_mouth(buf, atlas, tile_x, tile_y);
	if (has(spec.features, Feature::WallSeg)) paint_wall(buf, spec, atlas, tile_x, tile_y);
	if (has(spec.features, Feature::Tower)) paint_tower(buf, atlas, tile_x, tile_y);
	if (has(spec.features, Feature::Gate)) paint_gate(buf, spec, atlas, tile_x, tile_y);
	if (has(spec.features, Feature::CastleYard)) paint_yard_moss(buf, spec, atlas, tile_x, tile_y);
	apply_edge_transition(buf, spec, atlas, tile_x, tile_y, border_w);
}

} // namespace eng::gfx::proc::terrain
