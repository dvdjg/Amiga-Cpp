#pragma once

/// \file terrain_place.hpp
/// **Colocador de mundos de terreno** (`eng::gfx::proc::terrain`): compone un mapa a partir
/// del `TerrainTileSet` **por casación de bordes** (`can_place`), con un trazado
/// determinista de río (con meandros), un camino con puente donde cruza el cauce, un
/// castillo con torre y puerta, y relleno de naturaleza (bosque, rocas, sierra, lago,
/// aldea, cueva). Todo el azar sale de un `Xoroshiro64pp` sembrado: **misma semilla =
/// mismo mapa**, en host y en Amiga.
///
/// ```text
///   TerrainTileSet (piezas pintadas) ──place_world(seed)──► TerrainLayout<W,H>
///        │  casación de bordes (n==vecino.s, w==vecino.e)      cells[y*W+x] = índice
///        ▼
///   cada unión interna se verifica (layout_is_connected) y el mapa se puede
///   envolver como eng::playfield::TileLayerMap (cells/width/height) sin conversión.
/// ```
///
/// Estructura del mapa (coordenadas relativas a W×H, `W >= 8`, `H >= 6`):
///
/// ```text
///   fila 0        cueva (borde superior) + sierra
///   columnas 1..  río de norte a sur con meandros (curvas derivadas con apply_transform)
///   fila H-2      camino horizontal; puente donde cruza el río
///   derecha       castillo 3×3 (esquinas + muros + torre + puerta) con patio de hierba
///   resto         hierba llana con bosques, rocas, un lago y casas
/// ```
///
/// El colocador **no** duplica piezas: las orientaciones que el catálogo no trae (curvas
/// de río, camino) se derivan con `TerrainTileSet::add_variant` (`apply_transform` píxel +
/// `transform_edges`), la vía barata de variedad del sistema Wang.
///
/// ## Estado de verificación: VERIFICADA (demo 119)
/// `place_world`/`layout_is_connected` se ejercitan en hardware en carga (mundo 10×7 con
/// río, puente, castillo, aldea); tests host `HOST-442`/`HOST-443`.

#include <eng/core/math/random.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/proc/terrain.hpp>

namespace eng::gfx::proc::terrain {

/// **Mapa colocado**: rejilla row-major de índices a las piezas del set (`cells[y*W+x]`,
/// el mismo orden que `eng::playfield::TileLayerMap`: `x + y*width`).
/// \tparam W ancho del mapa en tiles. \tparam H alto del mapa en tiles.
template <eng::u16 W, eng::u16 H>
struct TerrainLayout {
	/// Índices de pieza, fila a fila.
	eng::u16 cells[W * H] {};
	/// Ancho del mapa en tiles.
	static constexpr eng::u16 width = W;
	/// Alto del mapa en tiles.
	static constexpr eng::u16 height = H;

	/// Pieza de la celda `(x, y)`. \param x columna 0..W-1. \param y fila 0..H-1.
	/// \return índice a `TerrainTileSet::tiles`/`specs`.
	[[nodiscard]] constexpr eng::u16 at(eng::u16 x, eng::u16 y) const { return cells[y * W + x]; }
	/// Pieza de la celda `(x, y)` (mutable). \param x,y celda. \return referencia al índice.
	[[nodiscard]] constexpr eng::u16& at(eng::u16 x, eng::u16 y) { return cells[y * W + x]; }
	/// Vista mutable de las celdas para construir un `TileLayerMap` (`map.cells = l.span()`).
	/// \return span de `W*H` índices.
	[[nodiscard]] constexpr eng::Span<eng::u16> span() { return {cells, eng::usize(W) * H}; }
};

/// ¿Todas las uniones internas del mapa casan? (`a.e == b.w` y `a.s == b.n` en cada
/// par adyacente). Es el contrato que cumple `place_world` y la verificación que un
/// colocador propio debe pasar antes de mostrarse.
/// \tparam W,H mapa. \tparam TW,TH,MaxTiles set. \param l mapa colocado. \param set set.
/// \return true si cada vecino derecho/inferior de cada celda casa por el borde compartido.
template <eng::u16 W, eng::u16 H, eng::u16 TW, eng::u16 TH, eng::u16 MaxTiles>
[[nodiscard]] bool layout_is_connected(const TerrainLayout<W, H>& l,
				       const TerrainTileSet<TW, TH, MaxTiles>& set) {
	for (eng::u16 y = 0; y < H; ++y) {
		for (eng::u16 x = 0; x < W; ++x) {
			if (x > 0u && !set.can_place(l.at(x - 1u, y), l.at(x, y), Side::East)) return false;
			if (y > 0u && !set.can_place(l.at(x, y - 1u), l.at(x, y), Side::South)) return false;
		}
	}
	return true;
}

namespace place_detail {

/// ¿Las cuatro caras de la celda que limitan con piezas del mapa son `Grass`? (una celda
/// de borde cuenta como válida). Es la condición para colocar una pieza de bordes verdes
/// (hierba, árboles, casa, lago…) sin romper ninguna casación.
/// \tparam Set tipo del set. \param set set. \param l mapa en construcción.
/// \param x,y celda a comprobar. \return true si puede alojar una pieza de bordes verdes.
template <class Set, eng::u16 W, eng::u16 H>
[[nodiscard]] inline bool green_neighbors_ok(const Set& set, const TerrainLayout<W, H>& l,
					     eng::u16 x, eng::u16 y) {
	if (x > 0u && set.specs[l.at(x - 1u, y)].edges.e != Edge::Grass) return false;
	if (x + 1u < W && set.specs[l.at(x + 1u, y)].edges.w != Edge::Grass) return false;
	if (y > 0u && set.specs[l.at(x, y - 1u)].edges.s != Edge::Grass) return false;
	if (y + 1u < H && set.specs[l.at(x, y + 1u)].edges.n != Edge::Grass) return false;
	return true;
}

} // namespace place_detail

/// **Coloca un mundo completo** en `out` (ver el diagrama del fichero). El set debe venir
/// de `TerrainTileSet::generate` (catálogos completos) y tener capacidad para las 3
/// variantes de curva que el río deriva (`MaxTiles >= catálogo + 12 + 4`); si no cabe
/// alguna variante, el río usa la curva base y la validación final fallará de forma
/// visible (`false`), nunca silenciosa.
/// \tparam W,H tamaño del mapa en tiles (`W >= 8`, `H >= 6`).
/// \tparam TW,TH,MaxTiles geometría/capacidad del set.
/// \param out mapa a rellenar (se sobrescribe completo).
/// \param set set de piezas (se le añaden las variantes de curva necesarias).
/// \param seed semilla (misma semilla = mismo mapa, host y Amiga).
/// \return true si el mapa queda conectado por casación (contrato de `layout_is_connected`).
template <eng::u16 W, eng::u16 H, eng::u16 TW, eng::u16 TH, eng::u16 MaxTiles>
[[nodiscard]] bool place_world(TerrainLayout<W, H>& out, TerrainTileSet<TW, TH, MaxTiles>& set,
			       eng::u32 seed) {
	static_assert(W >= 8u && H >= 6u, "el mapa minimo del colocador es 8x6");
	using eng::Xoroshiro64pp;
	using place_detail::green_neighbors_ok;

	// Piezas base del catálogo (el colocador falla de forma visible si falta alguna).
	const eng::u16 grass = set.find([](const TileSpec& s) {
		return s.features == Feature::None && s.edges.n == Edge::Grass &&
		       s.edges.e == Edge::Grass && s.edges.s == Edge::Grass && s.edges.w == Edge::Grass;
	});
	const eng::u16 road_h = set.find([](const TileSpec& s) {
		return s.features == Feature::None && s.edges.e == Edge::Path && s.edges.w == Edge::Path &&
		       s.edges.n == Edge::Grass;
	});
	const eng::u16 river_v = set.find([](const TileSpec& s) {
		return s.edges.n == Edge::Water && s.edges.s == Edge::Water && s.edges.e == Edge::Grass;
	});
	const eng::u16 curve_ne = set.find([](const TileSpec& s) {
		return s.edges.n == Edge::Water && s.edges.e == Edge::Water && s.edges.s == Edge::Grass &&
		       s.edges.w == Edge::Grass;
	});
	const eng::u16 bridge = set.find([](const TileSpec& s) {
		return has(s.features, Feature::Bridge);
	});
	const eng::u16 wall_v = set.find([](const TileSpec& s) {
		return !has(s.features, Feature::Tower) && has(s.features, Feature::WallSeg) &&
		       s.edges.n == Edge::Wall && s.edges.s == Edge::Wall && s.edges.e == Edge::Grass;
	});
	const eng::u16 wall_h = set.find([](const TileSpec& s) {
		return has(s.features, Feature::WallSeg) && s.edges.e == Edge::Wall &&
		       s.edges.w == Edge::Wall && s.edges.n == Edge::Grass &&
		       !has(s.features, Feature::Gate);
	});
	// Esquina noroeste del anillo (muro por E y S; el codo va por el centro).
	const eng::u16 corner_tl = set.find([](const TileSpec& s) {
		return s.edges.n == Edge::Grass && s.edges.e == Edge::Wall && s.edges.s == Edge::Wall &&
		       s.edges.w == Edge::Grass;
	});
	// Esquina noreste del anillo (muro por S y W).
	const eng::u16 corner_tr = set.find([](const TileSpec& s) {
		return s.edges.n == Edge::Grass && s.edges.e == Edge::Grass && s.edges.s == Edge::Wall &&
		       s.edges.w == Edge::Wall;
	});
	// Esquina suroeste del anillo (muro por N y E).
	const eng::u16 corner_bl = set.find([](const TileSpec& s) {
		return s.edges.n == Edge::Wall && s.edges.e == Edge::Wall && s.edges.s == Edge::Grass &&
		       s.edges.w == Edge::Grass;
	});
	// Esquina sureste del anillo (muro por N y W).
	const eng::u16 corner_br = set.find([](const TileSpec& s) {
		return s.edges.n == Edge::Wall && s.edges.e == Edge::Grass && s.edges.s == Edge::Grass &&
		       s.edges.w == Edge::Wall;
	});
	const eng::u16 gate = set.find([](const TileSpec& s) { return has(s.features, Feature::Gate); });
	// Torre sobre una recta vertical (mismo borde que el muro recto + torre).
	const eng::u16 tower = set.find([](const TileSpec& s) {
		return has(s.features, Feature::Tower) && s.edges.e == Edge::Grass;
	});
	const eng::u16 trees = set.find([](const TileSpec& s) { return has(s.features, Feature::Trees); });
	const eng::u16 rocks = set.find([](const TileSpec& s) { return has(s.features, Feature::Rocks); });
	// Lago: borde todo `Grass` (una charca suelta en el claro, no en el cauce).
	const eng::u16 lake = set.find([](const TileSpec& s) {
		return has(s.features, Feature::Lake) && s.edges.n == Edge::Grass &&
		       s.edges.e == Edge::Grass && s.edges.s == Edge::Grass && s.edges.w == Edge::Grass;
	});
	// Sierra: la pieza de montaña "pura" (la variante con crestas tiene bordes Ridge).
	const eng::u16 mountain = set.find([](const TileSpec& s) {
		return has(s.features, Feature::Mountain) && s.edges.n == Edge::Grass && s.edges.s == Edge::Grass;
	});
	const eng::u16 cave = set.find([](const TileSpec& s) { return has(s.features, Feature::CaveMouth); });
	const eng::u16 house = set.find([](const TileSpec& s) { return has(s.features, Feature::House); });
	if (grass >= set.count || road_h >= set.count || river_v >= set.count || curve_ne >= set.count ||
	    bridge >= set.count || wall_v >= set.count || wall_h >= set.count || gate >= set.count ||
	    tower >= set.count || trees >= set.count || rocks >= set.count || lake >= set.count ||
	    mountain >= set.count || cave >= set.count || house >= set.count) {
		return false; // catálogo incompleto: mejor no colocar un mapa roto
	}
	// Variantes de curva que el catálogo no trae (baratas: píxel transformado + bordes).
	const eng::u16 curve_se = set.add_variant(curve_ne, Transform::Rot90); // n=G,e=W,s=W,w=G
	const eng::u16 curve_sw = set.add_variant(curve_ne, Transform::Rot180); // n=G,e=G,s=W,w=W
	const eng::u16 curve_nw = set.add_variant(curve_ne, Transform::Rot270); // n=W,e=G,s=G,w=W

	Xoroshiro64pp rng {seed, seed ^ 0x5EEDu};

	// 1) Relleno base: hierba llana en todo el mapa.
	for (eng::u16 y = 0; y < H; ++y) {
		for (eng::u16 x = 0; x < W; ++x) out.at(x, y) = grass;
	}

	// 2) Río: camino de norte a sur con meandros (sin cambio de columna en la fila del
	//    camino, para que el puente caiga en un tramo vertical).
	const eng::u16 road_row = eng::u16(H - 2u);
	const eng::u16 castle_c0 = eng::u16(W - 4u); // el castillo ocupa las 3 ultimas columnas - 1
	const eng::u16 river_max = eng::u16(castle_c0 - 2u);
	bool is_river[W * H] {};
	eng::u16 rc = eng::u16(W / 4u); // la entrada del rio, a un cuarto del ancho
	eng::u16 next_shift_row = 1u;   // primer meandro permitido (nunca en la fila 0)
	for (eng::u16 r = 0; r < H; ++r) {
		is_river[r * W + rc] = true;
		// Sin meandro en la fila del camino NI en la de encima: el tramo del puente y su
		// aproximación son verticales, así el camino y el cauce casan sin costuras.
		const bool shift_ok = r + 1u < H && r != road_row && r + 1u != road_row;
		if (shift_ok && r >= next_shift_row && eng::chance(rng, 1u, 2u)) {
			const bool right = (rng.next() & 1u) != 0u;
			const eng::u16 nc = right ? eng::u16(rc + 1u) : eng::u16(rc - 1u);
			if (nc >= 1u && nc <= river_max) {
				is_river[r * W + nc] = true; // meandro: tramo horizontal en esta fila
				rc = nc;
				next_shift_row = eng::u16(r + 2u); // deja al menos una fila recta
			}
		}
	}
	// Pieza de cada celda de río por máscara de lados de agua (N=1, E=2, S=4, W=8).
	for (eng::u16 y = 0; y < H; ++y) {
		for (eng::u16 x = 0; x < W; ++x) {
			if (!is_river[y * W + x]) continue;
			eng::u8 mask = 0u;
			if (y == 0u || is_river[(y - 1u) * W + x]) mask = eng::u8(mask | 1u);
			if (x + 1u < W && is_river[y * W + x + 1u]) mask = eng::u8(mask | 2u);
			if (y + 1u < H && is_river[(y + 1u) * W + x]) mask = eng::u8(mask | 4u);
			if (x > 0u && is_river[y * W + x - 1u]) mask = eng::u8(mask | 8u);
			if (mask == 3u) out.at(x, y) = curve_ne;        // agua N y E
			else if (mask == 6u) out.at(x, y) = curve_se;   // agua E y S
			else if (mask == 12u) out.at(x, y) = curve_sw;  // agua S y W
			else if (mask == 9u) out.at(x, y) = curve_nw;   // agua W y N
			else out.at(x, y) = river_v; // N|S y (por construcción) ningún otro caso
		}
	}

	// 3) Camino horizontal: rectas y puente en el cruce con el río.
	for (eng::u16 x = 0; x < W; ++x) {
		out.at(x, road_row) = is_river[road_row * W + x] ? bridge : road_h;
	}

	// 4) Castillo 3x3 a la derecha: esquinas, muros, torre y puerta; patio de hierba.
	const eng::u16 cr0 = 1u;
	const eng::u16 cc = castle_c0;
	out.at(cc, cr0) = corner_tl;
	out.at(eng::u16(cc + 2u), cr0) = corner_tr;
	out.at(cc, eng::u16(cr0 + 2u)) = corner_bl;
	out.at(eng::u16(cc + 2u), eng::u16(cr0 + 2u)) = corner_br;
	out.at(eng::u16(cc + 1u), cr0) = wall_h;                      // muro superior
	out.at(cc, eng::u16(cr0 + 1u)) = tower;                      // torre en el muro oeste
	out.at(eng::u16(cc + 2u), eng::u16(cr0 + 1u)) = wall_v;      // muro este
	out.at(eng::u16(cc + 1u), eng::u16(cr0 + 2u)) = gate;        // puerta al sur
	// El patio (centro) queda con hierba llana heredada del relleno.

	// 5) Naturaleza y aldea (solo donde las cuatro caras son verdes).
	const eng::u16 cave_x = 0u, cave_y = 0u;
	if (green_neighbors_ok(set, out, cave_x, cave_y)) out.at(cave_x, cave_y) = cave;
	if (green_neighbors_ok(set, out, 0u, 1u)) out.at(0u, 1u) = mountain;
	if (green_neighbors_ok(set, out, eng::u16(W - 1u), eng::u16(H - 1u)))
		out.at(eng::u16(W - 1u), eng::u16(H - 1u)) = lake;
	// Dos casas en celdas verdes elegidas por el RNG sobre los candidatos del borde
	// superior/inferior (lejos de la estructura).
	eng::u8 houses_left = 2u;
	for (eng::u16 y = 0; y < H && houses_left > 0u; ++y) {
		for (eng::u16 x = 0; x < W && houses_left > 0u; ++x) {
			if (x < 1u || x >= eng::u16(castle_c0 - 1u)) continue; // deja sitio al río/castillo
			if (x < 2u && y < 2u) continue;                        // esquina de la cueva/sierra
			if (eng::chance(rng, 1u, 5u) && green_neighbors_ok(set, out, x, y)) {
				out.at(x, y) = house;
				--houses_left;
			}
		}
	}
	// Bosque/rocas dispersos.
	for (eng::u16 y = 0; y < H; ++y) {
		for (eng::u16 x = 0; x < W; ++x) {
			if (out.at(x, y) != grass) continue;
			if (eng::chance(rng, 1u, 7u) && green_neighbors_ok(set, out, x, y)) {
				out.at(x, y) = (eng::chance(rng, 1u, 4u)) ? rocks : trees;
			}
		}
	}

	// 6) Contrato: todas las uniones internas casan.
	return layout_is_connected(out, set);
}

} // namespace eng::gfx::proc::terrain
