// Demo 121 — PROC STRUCTURES: cocido procedural + motor de tiles X-Limited.
// ----------------------------------------------------------------------------
// Tutorial de la **cadena completa de un mundo de tiles** con el engine:
//
//   1. **Generación procedural EN COMPILACIÓN** (`constexpr`, como la 119/120): el
//      terreno sale de los **stamps** sobre un `OccupancyMap` y las transiciones de los
//      **16 tiles de esquinas** (dual-grid, materiales del atlas); encima se colocan
//      **estructuras multi-tile** por casación de bordes (Wang): caminos con curvas/T/
//      puente, muralla de castillo (esquinas, torres, puerta, patio), **casas grandes
//      2×2** (variantes A/B con la puerta espejada) y **árboles grandes 2×2** (copa
//      continua + tronco). Cada estructura se pinta por **partes** que casan por
//      construcción (ver `terrain_paint.hpp` y HOST-448).
//   2. **Cocido a tileset indexado** (`bake_layered_map`, `tile_bake.hpp`): cada celda
//      se compone UNA vez (suelo → decal → overhead con las capas) y las celdas
//      idénticas se **deduplican**: el mundo queda en ~50 piezas únicas + una rejilla
//      de índices — exactamente el formato del motor de tiles.
//   3. **Fondo pintado por el MOTOR DE TILES con el Blitter** (`XlimitedScene`, el
//      corkscrew X-Limited): `scene.fill()` blitea las 260 celdas del mundo desde el
//      banco interleaved (cero compose por píxel, CERO C2P; la 119/120 pagaban ~1 s de
//      C2P del chunky en carga). La cámara es fija (el scroll llega solo).
//   4. **Actor con Blitter y cookie-cut planar**: un bicho (blanco 31, color exclusivo)
//      camina por el carril y **pasa por debajo de las copas**. Por frame se encolan
//      DOS jobs al `FramePlan`: (a) restaurar la banda del carril desde una copia de
//      fondo en Chip (`add_world_bitmap`) y (b) dibujar el actor desplazado
//      (`add_world_bitmap_masked`; 16 variantes de subpíxel precomputadas y máscara =
//      `actor & ~copas`, de modo que los píxeles de copa no se escriben).
//   5. **HUD de 1 bitplane** con nombre + versión + contador de frames por palabras
//      (`Surface::draw_text_words`, norma del repo).
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/121_proc_corner_layers --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/121_proc_corner_layers --warp
#include <eng/api/api.hpp>
#include <eng/core/math/minifloat.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/corner_tiles.hpp>
#include <eng/graphics/proc/layered_map.hpp>
#include <eng/graphics/proc/shapes.hpp>
#include <eng/graphics/proc/terrain_paint.hpp>
#include <eng/graphics/proc/tile_bake.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

using eng::s16;
using eng::s32;
using eng::u8;
using eng::u16;
using eng::u32;
using MF = eng::math::MiniFloat16;
namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
namespace playfield = eng::playfield;

// -----------------------------------------------------------------------------
// Geometría: campo 320×208 (20×13 tiles de 16 px) + HUD de 48 px; carril del actor
// en la fila 9 (y=144). El bitmap X-Limited es un anillo de 288 líneas (120).
// -----------------------------------------------------------------------------
constexpr u16 kViewportW = 320u;
constexpr u16 kViewportH = 256u;
constexpr u16 kHudH = 48u;
constexpr u16 kMainH = u16(kViewportH - kHudH); // 208 = 13 filas de tile
constexpr u8 kPlanes = 5u;                      // 32 colores del atlas
constexpr u16 kTile = 16u;
constexpr u16 kMapW = 20u;
constexpr u16 kMapH = 13u;
constexpr u16 kDisplayH = u16(kViewportH + 2u * kTile); // anillo 288
constexpr u16 kActorY = 144u;                            // fila 9
static_assert(kMainH == kMapH * kTile, "el campo visible es kMapH filas");
constexpr u16 kActorMinX = 32u;
constexpr u16 kActorMaxX = 272u;
constexpr u16 kActorWords = 2u;                 // ventana planar (16 px + subpíxel)
constexpr u16 kActorRows = 16u;
constexpr u32 kActorBandWords = kActorWords * kActorRows * 5u; // 160
constexpr u16 kLaneWordCells = 18u;             // carril x 0..287 (actor hasta 287)
constexpr u32 kLaneBandWords = kLaneWordCells * kActorRows * 5u; // 1440

constexpr playfield::ScrollConsts kScrollConsts {
	/*tile_width=*/kTile,
	/*tile_height=*/kTile,
	/*display_height=*/kDisplayH,
	/*display_planelines=*/u16(kDisplayH * kPlanes),
	/*planes=*/kPlanes,
};

constexpr u32 kAtlasSeed = 0x7E5A1u;

// -----------------------------------------------------------------------------
// Assets EN COMPILACIÓN: atlas, terreno por esquinas y piezas de estructura
// -----------------------------------------------------------------------------
[[nodiscard]] constexpr proc::TextureAtlas<kTile, kTile, MF> make_world_atlas() {
	proc::TextureAtlas<kTile, kTile, MF> a {};
	a.generate(kAtlasSeed);
	return a;
}
inline constexpr proc::TextureAtlas<kTile, kTile, MF> kWorldAtlas = make_world_atlas();

/// **Catálogo de piezas de estructura del mundo** (Wang por bordes y rasgos): caminos
/// (recta H/V, curvas, T, fondo de saco), puente sobre el cauce, muralla del castillo
/// (rectas, esquinas, torre, puerta, patio) y las partes de las estructuras 2×2
/// (casa A/B y árbol grande, 4 partes cada una). Cada spec es una pieza de 16×16;
/// colocándolas por casación de bordes (el lado compartido tiene el MISMO id) forman
/// redes coherentes: el camino encadena, la muralla cierra el anillo, las partes de la
/// casa/árbol casan por construcción (mismo seed por estructura).
namespace wp {
using terr::Edge;
using terr::Feature;
// Índices simbólicos dentro de `kPieces` (el mundo los referencia por nombre).
enum : eng::u16 {
	PathH = 0,
	PathV,
	PathCornerWN, // conecta Oeste y Norte
	PathCornerSE, // conecta Sur y Este
	PathStubN,    // fondo de saco hacia el norte (bajo la puerta)
	Bridge,       // agua N/S + camino E/W
	WallV,
	WallH,
	CornerSE, // muro por Sur y Este (pieza superior izquierda del anillo)
	CornerSW, // muro por Sur y Oeste
	CornerNE, // muro por Norte y Este
	CornerNW, // muro por Norte y Oeste
	Tower,    // torre sobre muro vertical
	Gate,     // puerta en el muro horizontal
	Yard,     // patio adoquinado con musgo
	HouseA_NW,
	HouseA_NE,
	HouseA_SW,
	HouseA_SE,
	HouseB_NW,
	HouseB_NE,
	HouseB_SW,
	HouseB_SE,
	TreeBig_NW,
	TreeBig_NE,
	TreeBig_SW,
	TreeBig_SE,
	Trees,      // arbolitos sueltos
	TreesRocks, // arbolitos con peñascos
	Count,
};
} // namespace wp

using terr::Edge;
using terr::Feature;

/// Semilla por estructura (las cuatro partes de casa/árbol COMPARTEN seed: la copa y la
/// geometría del diseño global se derivan de él → continuidad en las costuras).
constexpr u32 kHouseASeed = 0x40A1u;
constexpr u32 kHouseBSeed = 0x40B2u;
constexpr u32 kTreeSeed0 = 0x7EEE0u;
constexpr u32 kTreeSeed1 = 0x7EEE1u;

inline constexpr terr::TileSpec kPieces[wp::Count] = {
	// --- Caminos (borde `Path` por los lados conectados; el pintor dibuja la franja).
	{.edges = {Edge::Grass, Edge::Path, Edge::Grass, Edge::Path}, .features = Feature::None, .seed = 0x301u},
	{.edges = {Edge::Path, Edge::Grass, Edge::Path, Edge::Grass}, .features = Feature::None, .seed = 0x302u},
	{.edges = {Edge::Path, Edge::Grass, Edge::Grass, Edge::Path}, .features = Feature::None, .seed = 0x303u},
	{.edges = {Edge::Grass, Edge::Path, Edge::Path, Edge::Grass}, .features = Feature::None, .seed = 0x304u},
	{.edges = {Edge::Path, Edge::Grass, Edge::Grass, Edge::Grass}, .features = Feature::None, .seed = 0x305u},
	// --- Puente (agua N/S + camino E/W) sobre el cauce del río.
	{.edges = {Edge::Water, Edge::Path, Edge::Water, Edge::Path},
	 .features = Feature::River | Feature::Bridge,
	 .seed = 0x306u},
	// --- Muralla del castillo: rectas, esquinas, torre, puerta y patio.
	{.edges = {Edge::Wall, Edge::Grass, Edge::Wall, Edge::Grass}, .features = Feature::WallSeg, .seed = 0x401u},
	{.edges = {Edge::Grass, Edge::Wall, Edge::Grass, Edge::Wall}, .features = Feature::WallSeg, .seed = 0x402u},
	{.edges = {Edge::Grass, Edge::Wall, Edge::Wall, Edge::Grass}, .features = Feature::WallSeg, .seed = 0x403u},
	{.edges = {Edge::Grass, Edge::Grass, Edge::Wall, Edge::Wall}, .features = Feature::WallSeg, .seed = 0x404u},
	{.edges = {Edge::Wall, Edge::Wall, Edge::Grass, Edge::Grass}, .features = Feature::WallSeg, .seed = 0x405u},
	{.edges = {Edge::Wall, Edge::Grass, Edge::Grass, Edge::Wall}, .features = Feature::WallSeg, .seed = 0x406u},
	{.edges = {Edge::Wall, Edge::Grass, Edge::Wall, Edge::Grass}, .features = Feature::Tower, .seed = 0x407u},
	{.edges = {Edge::Grass, Edge::Wall, Edge::Grass, Edge::Wall},
	 .features = Feature::Gate | Feature::WallSeg,
	 .seed = 0x408u},
	{.edges = {Edge::Wall, Edge::Wall, Edge::Wall, Edge::Wall}, .features = Feature::CastleYard, .seed = 0x409u},
	// --- Casa grande 2×2 (variante A: puerta en SW; B: espejada a SE).
	{.features = Feature::HouseA_NW, .seed = kHouseASeed},
	{.features = Feature::HouseA_NE, .seed = kHouseASeed},
	{.features = Feature::HouseA_SW, .seed = kHouseASeed},
	{.features = Feature::HouseA_SE, .seed = kHouseASeed},
	{.features = Feature::HouseB_NW, .seed = kHouseBSeed},
	{.features = Feature::HouseB_NE, .seed = kHouseBSeed},
	{.features = Feature::HouseB_SW, .seed = kHouseBSeed},
	{.features = Feature::HouseB_SE, .seed = kHouseBSeed},
	// --- Árbol grande 2×2 (misma seed por estructura).
	{.features = Feature::TreeBig_NW, .seed = kTreeSeed0},
	{.features = Feature::TreeBig_NE, .seed = kTreeSeed0},
	{.features = Feature::TreeBig_SW, .seed = kTreeSeed0},
	{.features = Feature::TreeBig_SE, .seed = kTreeSeed0},
	// --- Arbolitos sueltos (una pieza, variantes de seed).
	{.features = Feature::Trees, .seed = 0x501u},
	{.features = Feature::Trees | Feature::Rocks, .seed = 0x502u},
};

/// **Estado del mundo** (todo constexpr): piezas pintadas, mapa por capas, máscara de
/// copas y las **bandas planares del actor** (16 subposiciones) + su máscara.
struct WorldData {
	proc::BitmapBuffer<kTile, kTile> corner[16] {};     // transiciones de esquinas
	proc::BitmapBuffer<kTile, kTile> pieces[wp::Count] {}; // piezas de estructura
	proc::LayeredTileMap<kMapW, kMapH, kTile> map {};
	proc::BitmapBuffer<kTile, kTile> flower {};         // decal
	proc::BitmapBuffer<kTile, kTile> canopy[2] {};      // overhead (fondo transparente)
	u16 canopy_mask[kMapH * kTile][kMapW] {};           // 1 palabra por fila/tile
	u16 actor_band[kTile][kActorBandWords] {};
	u16 actor_mask[kTile][kActorWords * kActorRows] {};
};

/// Señala la celda `(x,y)` con una pieza del catálogo (índice 0..15 = esquinas de
/// terreno; 16+n = pieza de estructura `n`). Marca también una estructura 2×2 completa.
constexpr void place_piece(proc::LayeredTileMap<kMapW, kMapH, kTile>& map, u16 x, u16 y, u16 n) {
	map.set_cell(proc::MapLayer::Ground, x, y, u16(n));
}
constexpr void place_2x2(proc::LayeredTileMap<kMapW, kMapH, kTile>& map, u16 x, u16 y, u16 nw,
			 u16 ne, u16 sw, u16 se) {
	place_piece(map, x, y, nw);
	place_piece(map, u16(x + 1u), y, ne);
	place_piece(map, x, u16(y + 1u), sw);
	place_piece(map, u16(x + 1u), u16(y + 1u), se);
}

/// **Patrón del actor** (bicho de 16×16): 31 = cuerpo (blanco, color ÚNICO de la
/// escena; el HUD imprime en 1 del bitplane de overlay), 2 = ojos, 0 = transparente.
inline constexpr eng::u8 kActorPixels[kTile * kTile] = {
	0,  0,  0,  0,  0,  31, 31, 31, 31, 31, 31, 0,  0,  0,  0,  0,
	0,  0,  0,  0,  31, 31, 31, 31, 31, 31, 31, 31, 0,  0,  0,  0,
	0,  0,  0,  31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 0,  0,  0,
	0,  0,  31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 0,  0,
	0,  0,  31, 31, 2,  2,  31, 31, 31, 31, 2,  2,  31, 31, 0,  0,
	0,  0,  31, 31, 2,  2,  31, 31, 31, 31, 2,  2,  31, 31, 0,  0,
	0,  31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 0,
	0,  31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 0,
	0,  31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 0,
	0,  0,  31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 0,  0,
	0,  0,  0,  31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 0,  0,  0,
	0,  0,  0,  0,  31, 31, 31, 31, 31, 31, 31, 31, 0,  0,  0,  0,
	0,  0,  0,  0,  0,  31, 31, 31, 31, 31, 31, 0,  0,  0,  0,  0,
	0,  0,  0,  0,  31, 31, 0,  0,  0,  0,  31, 31, 0,  0,  0,  0,
	0,  0,  0,  0,  31, 31, 0,  0,  0,  0,  31, 31, 0,  0,  0,  0,
	0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

[[nodiscard]] constexpr WorldData make_world() {
	WorldData w {};
	// 1) Las 16 transiciones de esquinas con materiales (agua/hierba/espuma).
	const proc::CornerMaterials mats {proc::MaterialId::Grass, proc::MaterialId::Water,
					  proc::MaterialId::Foam};
	const proc::CornerNoise noise {0x51D3u, 88u, 1u};
	for (u8 m = 0u; m < 16u; ++m) {
		proc::paint_corner_materials(w.corner[m], m, kWorldAtlas, mats, noise);
	}
	// 2) Piezas de estructura (una vez, con seeds fijos).
	for (u16 i = 0u; i < wp::Count; ++i) {
		terr::paint_terrain_tile(w.pieces[i], kPieces[i], kWorldAtlas, i, 0u, 4u);
	}

	// 3) Terreno: río N→S (semilla elegida para que el cauce quede a la izquierda del
	//    carril) + lago con isla; el terreno se resuelve por máscaras de esquinas.
	proc::OccupancyMap<kMapW + 1u, kMapH + 1u> occ {};
	occ.fill(0u);
	proc::stamp_river(occ, 3u, 2u, 0x5007u, true);
	proc::stamp_disc(occ, 14, 10, 3u, 2u, 1u, 0xD15Cu);
	proc::stamp_disc(occ, 14, 10, 1u, 1u, 0u, 0x15A0u);

	// 4) Base del mapa: máscara de esquinas por celda.
	for (u16 y = 0u; y < kMapH; ++y) {
		for (u16 x = 0u; x < kMapW; ++x) {
			place_piece(w.map, x, y, proc::corner_mask(occ, x, y));
		}
	}

	// 5) **Caminos** (piezas por lado conectado): de oeste a este por la fila 6 hasta la
	//    columna 14, sube (columna 14) y entra al castillo por la puerta sur. Donde el
	//    camino cruza el cauce va el **puente**.
	for (u16 x = 0u; x <= 14u; ++x) {
		const bool water = occ.at(s16(x), 6) != 0u || occ.at(s16(x) + 1, 6) != 0u ||
				   occ.at(s16(x), 7) != 0u;
		place_piece(w.map, x, 6u, water ? u16(16u + wp::Bridge) : u16(16u + wp::PathH));
	}
	place_piece(w.map, 14u, 5u, u16(16u + wp::PathV));
	place_piece(w.map, 14u, 4u, u16(16u + wp::PathCornerSE));
	place_piece(w.map, 15u, 4u, u16(16u + wp::PathH));
	place_piece(w.map, 16u, 4u, u16(16u + wp::PathCornerWN)); // sube a la puerta
	place_piece(w.map, 14u, 6u, u16(16u + wp::PathCornerWN)); // codo del camino

	// 6) **Castillo 3×3** (muralla por casación): esquinas, rectas, torre y puerta sur
	//    alineada con el camino; patio en el centro.
	place_piece(w.map, 15u, 1u, u16(16u + wp::CornerSE)); // anillo: pared sur+este
	place_piece(w.map, 17u, 1u, u16(16u + wp::CornerSW));
	place_piece(w.map, 15u, 3u, u16(16u + wp::CornerNE));
	place_piece(w.map, 17u, 3u, u16(16u + wp::CornerNW));
	place_piece(w.map, 16u, 1u, u16(16u + wp::WallH));  // muro superior
	place_piece(w.map, 17u, 2u, u16(16u + wp::WallV));  // muro este
	place_piece(w.map, 15u, 2u, u16(16u + wp::Tower));  // torre en el muro oeste
	place_piece(w.map, 16u, 3u, u16(16u + wp::Gate));   // puerta al sur (llega el camino)
	place_piece(w.map, 16u, 2u, u16(16u + wp::Yard));   // patio

	// 7) **Casas grandes 2×2**: variante A (puerta a la izquierda) y B (espejada) sobre
	//    el prado, con las cuatro partes casando (misma seed por estructura).
	place_2x2(w.map, 1u, 10u, u16(16u + wp::HouseA_NW), u16(16u + wp::HouseA_NE),
		  u16(16u + wp::HouseA_SW), u16(16u + wp::HouseA_SE));
	place_2x2(w.map, 7u, 10u, u16(16u + wp::HouseB_NW), u16(16u + wp::HouseB_NE),
		  u16(16u + wp::HouseB_SW), u16(16u + wp::HouseB_SE));

	// 8) **Árboles grandes 2×2** (copa continua) + arbolitos sueltos con variantes.
	place_2x2(w.map, 6u, 0u, u16(16u + wp::TreeBig_NW), u16(16u + wp::TreeBig_NE),
		  u16(16u + wp::TreeBig_SW), u16(16u + wp::TreeBig_SE));
	place_2x2(w.map, 9u, 0u, u16(16u + wp::TreeBig_NW), u16(16u + wp::TreeBig_NE),
		  u16(16u + wp::TreeBig_SW), u16(16u + wp::TreeBig_SE));
	place_piece(w.map, 12u, 1u, u16(16u + wp::Trees));
	place_piece(w.map, 18u, 7u, u16(16u + wp::TreesRocks));
	place_piece(w.map, 3u, 12u, u16(16u + wp::Trees));
	place_piece(w.map, 9u, 12u, u16(16u + wp::TreesRocks));
	place_piece(w.map, 18u, 12u, u16(16u + wp::Trees));

	// 9) Decal: flores (3 px de color 16/17) sobre hierba llana.
	w.flower.at(4u, 4u) = 16u;
	w.flower.at(5u, 4u) = 17u;
	w.flower.at(4u, 5u) = 17u;
	w.flower.at(11u, 9u) = 16u;
	w.flower.at(10u, 10u) = 16u;
	w.flower.at(11u, 10u) = 17u;
	const u16 flower_cells[][2] = {{2u, 2u}, {4u, 8u}, {8u, 8u}, {12u, 9u}, {9u, 6u},
				       {5u, 11u}, {18u, 5u}, {2u, 7u}};
	for (const auto& fc : flower_cells) {
		if (w.map.cell(proc::MapLayer::Ground, fc[0], fc[1]) == 0u) {
			w.map.set_cell(proc::MapLayer::Decal, fc[0], fc[1], 0u);
		}
	}

	// 10) Overhead: **copas** (disco irregular de follaje, fondo transparente) sobre el
	//     carril del actor y decorativas. La máscara por fila/tile marca los píxeles de
	//     copa (para que el actor NO se escriba debajo).
	const proc::BitmapBuffer<kTile, kTile>& foliage =
		kWorldAtlas.get(proc::MaterialId::Foliage, 0u);
	for (u8 v = 0u; v < 2u; ++v) {
		proc::SoftCircleMask<MF> blob {};
		blob.cx = 8u;
		blob.cy = u16(v == 0u ? 8u : 9u);
		blob.r2_min = 16u;
		blob.r2_max = 49u;
		blob.seed = u32(0x71u + v);
		proc::fill_masked(
			w.canopy[v], [&](u16 x, u16 y) { return foliage.at(x, y); },
			[&](u16 x, u16 y) { return blob(x, y) != 0u; });
	}
	const u16 canopy_cells[][3] = {
		{5u, 9u, 0u}, {6u, 9u, 1u}, {10u, 9u, 0u}, // sobre el carril (ocultan al actor)
		{13u, 6u, 1u}, {12u, 6u, 0u},               // copas junto al camino
		{3u, 11u, 1u}, {13u, 12u, 0u},              // sueltas
	};
	for (const auto& cc : canopy_cells) {
		w.map.set_cell(proc::MapLayer::Overhead, cc[0], cc[1], cc[2]);
		for (u16 r = 0u; r < kTile; ++r) {
			u16 word = 0u;
			for (u16 x = 0u; x < kTile; ++x) {
				if (w.canopy[cc[2]].at(x, r) != 0u) word = u16(word | u16(0x8000u >> x));
			}
			w.canopy_mask[cc[1] * kTile + r][cc[0]] = word;
		}
	}

	// 11) Bandas planares del actor: los 16 subpíxeles precomputados (el frame solo
	//     indexa; el Blitter no hace shifts).
	for (u16 sh = 0u; sh < 16u; ++sh) {
		eng::graphics::make_band_from_pixels<kTile, kTile, 5>(
			kActorPixels, sh, w.actor_band[sh], w.actor_mask[sh]);
	}
	return w;
}

inline constexpr WorldData kWorld = make_world();

/// **Cocido** del mundo a tileset indexado (celdas idénticas → una pieza): el formato
/// que consume el motor de tiles (banco + rejilla de índices).
inline constexpr auto kBaked = []() constexpr {
	struct GroundOf {
		[[nodiscard]] constexpr const proc::BitmapBuffer<kTile, kTile>& operator()(u16 i) const {
			return (i < 16u) ? kWorld.corner[i] : kWorld.pieces[u16(i - 16u)];
		}
	};
	struct DecalOf {
		[[nodiscard]] constexpr const proc::BitmapBuffer<kTile, kTile>& operator()(u16) const {
			return kWorld.flower;
		}
	};
	struct OverOf {
		[[nodiscard]] constexpr const proc::BitmapBuffer<kTile, kTile>& operator()(u16 i) const {
			return kWorld.canopy[i & 1u];
		}
	};
	return proc::bake_layered_map<kMapW, kMapH, kTile, 64>(kWorld.map, GroundOf {}, DecalOf {},
							       OverOf {});
}();
static_assert(kBaked.overflow == 0u, "el mundo cabe en el tileset cocido");

/// Formato de la demo (major.minor; el build inyecta `-DENG_BUILD_NUMBER`).
constexpr eng::debug::Version kVersion {1u, 0u};

// Paleta del HUD (1 bitplane, 2 colores) y del campo (32 colores del atlas).
eng::u16 g_hud_palette[2] {0x000u, 0xFFFu};
eng::u16 g_world_palette[32] {};

/// Número decimal a cadena (telemetría del HUD).
void u16_to_str(u16 v, char* out) {
	char tmp[6] {};
	u8 i = 0u;
	do {
		tmp[i++] = char('0' + (v % 10u));
		v = u16(v / 10u);
	} while (v != 0u && i < 5u);
	u8 n = 0u;
	while (i > 0u) out[n++] = tmp[--i];
	out[n] = '\0';
}

struct ProcStructuresDemo {
	playfield::XlimitedScene<kScrollConsts> scene {};
	playfield::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	// Scratch en Chip para el Blitter: 16 bandas del actor + máscara combinada +
	// banda del carril (fondo que se restaura cada frame).
	eng::Block<eng::PlaneTag> m_scratch {};
	u16* m_bands = nullptr;   // kTile bandas de kActorBandWords
	u16* m_mask = nullptr;    // kActorWords*kActorRows palabras
	u16* m_lane_bg = nullptr; // kLaneBandWords
	u16 m_actor_x = 128u; // TEMP DEBUG
	s16 m_dir = 1;
	u32 m_frames = 0u;
	char m_frame_text[12] = "F 0";
	bool m_hud_dirty = true;
	bool m_hud_cleared = false;
	bool m_ready = false;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Chip: anillo del corkscrew + banco + copper + HUD + scratch del actor.
		if (!backend.configure_memory({200u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012101u);
			return;
		}
		m_scratch = backend.memory_manager().chip().reserve<eng::PlaneTag>(
			eng::u32(kTile) * kActorBandWords * 2u +
				eng::u32(kActorWords * kActorRows) * 2u + kLaneBandWords * 2u + 8u,
			16u);
		if (!m_scratch.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012102u);
			return;
		}
		u16* const base = reinterpret_cast<u16*>(m_scratch.view.data());
		m_bands = base;
		m_mask = base + eng::u32(kTile) * kActorBandWords;
		m_lane_bg = m_mask + kActorWords * kActorRows;

		// Bandas del actor + fondo del carril, a Chip (el Blitter no lee rodata).
		for (u16 sh = 0u; sh < kTile; ++sh) {
			eng::graphics::make_band_from_pixels<kTile, kTile, 5>(
				kActorPixels, sh, m_bands + eng::u32(sh) * kActorBandWords, m_mask);
		}
		build_lane_background();

		// Config del motor de tiles: mapa cocido, banco indexado, cámara fija (no hay
		// llamadas a `update`), campo 320×208 + HUD de 1 bitplane.
		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.hud.height = kHudH;
		scene_cfg.hud.planes = 1u;
		scene_cfg.hud.palette = g_hud_palette;
		scene_cfg.tile_width = kTile;
		scene_cfg.tile_height = kTile;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0u;
		scene_cfg.y_mode = playfield::AxisPolicy::Ring;
		scene_cfg.direction = playfield::DirectionPolicy::Bidirectional;
		scene_cfg.linear_display = false;
		scene_cfg.max_step = 2u;
		scene_cfg.map.cells = kBaked.cells_view();
		scene_cfg.map.width = kMapW;
		scene_cfg.map.height = kMapH;
		scene_cfg.map.wrap_x = 0u; // cámara fija: sin envoltura
		scene_cfg.map.wrap_y = 0u;
		scene_cfg.map.edge_tile = 0u;
		scene_cfg.map.empty_tile = 0xFFFFu;
		scene_cfg.visible_tile_bias_x = 1u;
		scene_cfg.visible_tile_bias_y = 1u;
		scene_cfg.tileset_count = kBaked.count;
		scene_cfg.indexed_tiles = eng::TileBankBytes {kBaked.bank_view().data(),
							      kBaked.bank_view().size()};
		scene_cfg.indexed_stride = kTile * kTile; // 1 B/píxel
		for (u8 i = 0u; i < 32u; ++i) g_world_palette[i] = kWorldAtlas.palette.color[i];
		scene_cfg.palette = eng::PaletteWords {g_world_palette, 32u};

		if (!scene.begin(backend.memory_manager(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012103u);
			return;
		}
		// **Relleno por el motor de tiles**: todos los tiles del mundo al bitmap con el
		// Blitter (banco interleaved). Sin compose por píxel ni C2P.
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012104u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012105u);
			return;
		}
		scene.takeover(backend);
		draw_hud();
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x00000121u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!m_ready) {
			return;
		}
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});
#if !defined(K121_SKIP_ACTOR)
		// **Actor por Blitter**: (1) restaura la banda del carril desde la copia de
		// fondo; (2) dibuja el bicho desplazado con la máscara combinada
		// (`actor & ~copas`): bajo la copa no se escribe y el bicho queda oculto.
		if (!scene.bg().add_world_bitmap(
			    plan,
			    eng::Span<const eng::u16> {m_lane_bg, kLaneBandWords}, 0, kActorY,
			    u16(kLaneWordCells * 16u), kActorRows,
			    u16(kLaneWordCells * 2u), kLaneBandWords * 2u / 5u, kPlanes)) {
			m_ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00012110u);
			return;
		}
		// Avance del bicho (1 px/frame, rebote en los extremos). `K121_STATIC_ACTOR`
		// congela la posición (sonda de alineación del parcheo planar vs. el fondo).
#if !defined(K121_STATIC_ACTOR)
		if (m_dir > 0 && m_actor_x >= kActorMaxX) {
			m_dir = -1;
		} else if (m_dir < 0 && m_actor_x <= kActorMinX) {
			m_dir = 1;
		}
		m_actor_x = u16(s32(m_actor_x) + m_dir);
#else
		m_actor_x = 96u;
#endif
		patch_actor();
		if (!scene.bg().add_world_bitmap_masked(
			    plan, eng::Span<const eng::u16> {m_bands + eng::u32(m_actor_x & 15u) * kActorBandWords,
							     kActorBandWords},
			    eng::Span<const eng::u16> {m_mask, kActorWords * kActorRows},
			    s32((m_actor_x >> 4u) << 4u), kActorY, 32u, kActorRows, 4u,
			    kActorRows * 4u, kPlanes)) {
			m_ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00012111u);
			return;
		}
#endif
		if (!backend.execute_frame_plan(plan)) {
			m_ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00012112u);
			return;
		}
		// Telemetría: posición y sentido del actor.
		g_eng_run_status.detail = u32(m_actor_x) | (u32(u16(m_dir)) << 16u);
		++m_frames;
#if !defined(K121_SKIP_HUD)
		if ((m_frames & 63u) == 0u) {
			char num[8];
			u16_to_str(u16(m_frames & 0xFFFFu), num);
			u8 n = 0u;
			m_frame_text[n++] = 'F';
			m_frame_text[n++] = ' ';
			for (u8 i = 0u; num[i] != '\0' && n < 11u; ++i) m_frame_text[n++] = num[i];
			m_frame_text[n] = '\0';
			m_hud_dirty = true;
		}
#endif
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (m_ready) {
#if !defined(K121_SKIP_HUD)
			if (m_hud_dirty) {
				draw_hud();
				m_hud_dirty = false;
			}
#endif
			scene.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// **Fondo del carril** (banda planar del bitmap del campo, x 0..287): se construye
	/// desde las MISMAS piezas cocidas que usa el banco del motor de tiles (mismo mapeo
	/// `bit p del índice = plano p`, MSB izquierda), para que la restauración sea
	/// idéntica al fondo pintado por el Blitter.
	void build_lane_background() {
		const u16 lane_row = u16(kActorY / kTile); // fila 9 del mapa
		const u16 row_bytes = u16(kLaneWordCells * 2u);
		const u32 plane_stride = u32(row_bytes) * kActorRows;
		for (u32 p = 0; p < kPlanes; ++p) {
			for (u16 r = 0; r < kActorRows; ++r) {
				for (u16 c = 0; c < kLaneWordCells; ++c) {
					const u16 cell = kBaked.cells[u32(lane_row) * kMapW + c];
					const u8* const tile = kBaked.tiles[cell & 63u];
					u16 word = 0u;
					for (u16 x = 0; x < kTile; ++x) {
						const u8 px = tile[u32(r) * kTile + x];
						if (((px >> p) & 1u) != 0u) word = u16(word | u16(0x8000u >> x));
					}
					reinterpret_cast<u16*>(reinterpret_cast<u8*>(m_lane_bg) +
							       p * plane_stride + u32(r) * row_bytes)[c] = word;
				}
			}
		}
	}

	/// Combina la máscara del subpíxel tocado con la negación de la máscara de copas
	/// (solo 32 palabras por frame: el trabajo por píxel lo hace el Blitter).
	void patch_actor() {
		const u16 sub = u16(m_actor_x & 15u);
		const u16 wx = u16(m_actor_x >> 4u);
		const u16* const am = kWorld.actor_mask[sub];
		for (u16 r = 0u; r < kActorRows; ++r) {
			const u16* const cm = &kWorld.canopy_mask[kActorY + r][wx];
			m_mask[u32(r) * 2u + 0u] = am[u32(r) * 2u + 0u]; (void)cm; // TEMP DEBUG: sin mascara de copas
			m_mask[u32(r) * 2u + 1u] = am[u32(r) * 2u + 1u];
		}
	}

	/// HUD de 1 bitplane (norma del repo): nombre + versión + contador + nº de piezas
	/// cocidas. Texto **por palabras** (`draw_text_words`), nunca píxel a píxel.
	void draw_hud() {
		auto hud = scene.hud_surface();
		if (!m_hud_cleared) {
			hud.fill_rect(0, 0, 512, kHudH, 0u);
			m_hud_cleared = true;
		}
		char label[24];
		const u8 vlen = eng::debug::version_label(kVersion, label, sizeof(label));
		char line1[48] = "PROC STRUCTURES ";
		u8 n = 16u;
		for (u8 i = 0u; i < vlen && n + 1u < sizeof(line1); ++i) line1[n++] = label[i];
		const char* sep = "  ";
		for (const char* c = sep; *c != '\0' && n + 1u < sizeof(line1); ++c) line1[n++] = *c;
		for (const char* c = m_frame_text; *c != '\0' && n + 1u < sizeof(line1); ++c) {
			line1[n++] = *c;
		}
		line1[n] = '\0';
		(void)hud.draw_text_words(16, 4, line1, 1u);

		char line2[48] = "X-LIMITED 20x13  PIECES ";
		u8 m = 25u;
		char num[8];
		u16_to_str(kBaked.count, num);
		for (const char* c = num; *c != '\0' && m + 1u < sizeof(line2); ++c) line2[m++] = *c;
		line2[m] = '\0';
		(void)hud.draw_text_words(16, 20, line2, 1u);
	}
};

static_assert(eng::DisplayDriver<playfield::XlimitedScene<kScrollConsts>, eng::amiga::AmigaBackend>);

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	ProcStructuresDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
