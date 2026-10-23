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
#include <eng/core/util/text_line.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/actor_target.hpp>
#include <eng/field/playfield_probe.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/corner_tiles.hpp>
#include <eng/graphics/proc/layered_map.hpp>
#include <eng/graphics/proc/shapes.hpp>
#include <eng/graphics/proc/terrain_paint.hpp>
#include <eng/graphics/proc/tile_bake.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/scene/actor.hpp>
#include <eng/setup/world_blit.hpp>

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
// **Línea del anillo de los blits de mundo**: la primera línea del anillo es la
// guarda superior (visible_tile_bias_y=1): el campo pinta la fila r del mapa en la
// línea (r+1)·tile del anillo, y el y VISIBLE (0..207) se corresponde con la línea
// del anillo y + kRingGuardY. `add_world_bitmap*` recibe (y envuelve con `dmod2`)
// la LÍNEA DEL ANILLO, no el y visible.
constexpr u16 kRingGuardY = kTile;
// **Guarda HORIZONTAL del display** (espejo de la vertical): la ventana del corkscrew
// muestra la columna c del ANILLO en la pantalla c − 1 (la columna 0 es la guarda del
// x-limited); el mundo pinta la columna del mapa c en la columna c + 1 del anillo. Un
// objeto en x VISIBLE se dibuja en la COLUMNA DEL ANILLO x + kRingGuardX (y ahí se lee);
// las consultas al MAPA (copas) van en coordenadas de pantalla. Sin este sesgo el objeto
// sale una columna a la izquierda y su oclusión queda desfasada (medido con landmarks).
constexpr u16 kRingGuardX = kTile;
static_assert(kMainH == kMapH * kTile, "el campo visible es kMapH filas");
constexpr u16 kActorMinX = 32u;
constexpr u16 kActorMaxX = 256u; // EXPERIMENTO TEMPORAL (out/tmp): 272 excede el carril (288 px)
constexpr u16 kActorWords = 2u;                 // ventana planar (16 px + subpíxel)
constexpr u16 kActorRows = 16u;
constexpr u32 kActorBandWords = kActorWords * kActorRows * 5u; // 160
// Carril x 0..303: cubre la ventana de 32 px del actor en su x extrema (kActorMaxX=272
// + 32 px = 304 px = 19 celdas). Con 18 celdas, la ventana de restauración en el extremo
// derecho leía una palabra fuera de la banda (la detección está en el validador de setup
// `check_world_window`, que se ejecuta en init).
constexpr u16 kLaneWordCells = 20u;
constexpr u32 kLaneBandWords = kLaneWordCells * kActorRows * 5u; // 1520

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

#if !defined(K121_ACTOR_LEGACY) && !defined(K121_SKIP_ACTOR)
// Frame y animación del actor para el camino con el SISTEMA del engine (DT-024.3): la
// ventana mide 32 px (16 del arte + 16 de guarda del pack) por 16 de alto.
constexpr eng::scene::Frame kActorFrames[1] = {{0, 0, 32u, 16u}};
constexpr eng::scene::Animation kActorAnim {eng::Span<const eng::scene::Frame> {kActorFrames, 1u},
					    true};
#endif

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
#if defined(K121_GOLDEN_ACTORS)
	u16* m_gmask = nullptr; // 16 máscaras (golden): kTile slots de kActorWords*kActorRows
#endif
#if !defined(K121_ACTOR_LEGACY) && !defined(K121_SKIP_ACTOR)
	// Camino del SISTEMA de actores (DT-024.3): hoja pack con guarda + buffer de
	// save-under + el actor retenido del engine.
	eng::Block<eng::PlaneTag> m_aux {};
	u16* m_pack = nullptr;     // 16 filas × 5 planos × (img+guarda+máscara+guarda) = 320
	u16* m_save_buf = nullptr; // save-under: 3 palabras/fila × 16 filas × 5 planos
	eng::graphics::BobTarget m_ring_target {};
#if defined(K121_GOLDEN_ACTORS)
	eng::scene::ActorStore<17> m_actor_store {}; // 16 del golden + el del auto-chequeo
#else
	eng::scene::ActorStore<1> m_actor_store {};
#endif
	eng::scene::RepresentationAllocator m_actor_alloc {};
	eng::scene::ActorId m_actor_id {};
	u16 m_sil_mask[kTile] {}; // silueta del actor por fila (precomputada: art != 0)
#endif
	u16 m_actor_x = kActorMinX;
	u16 m_restore_x = kActorMinX; // ventana alineada del actor del frame anterior
	s16 m_dir = 1;
	u32 m_frames = 0u;
	eng::util::TextLine<16> m_frame_text;
	bool m_hud_dirty = true;
	bool m_hud_cleared = false;
	bool m_ready = false;
	eng::debug::HostHud m_host_hud {}; ///< HUD del host (nombre/número/versión/frame/fps)
	u8 m_body_faults = 0u; // fallos consecutivos de la guarda de render

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_frame_text << 'F' << ' ' << 0u;
		// Chip: anillo del corkscrew + banco + copper + HUD + scratch del actor.
		if (!backend.configure_memory({200u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012101u);
			return;
		}
		m_scratch = backend.memory_manager().chip().reserve<eng::PlaneTag>(
			eng::u32(kTile) * kActorBandWords * 2u +
				eng::u32(kActorWords * kActorRows) * 2u + kLaneBandWords * 2u + 8u
#if defined(K121_GOLDEN_ACTORS)
				// Golden: una máscara por subposición (los 16 dibujos del plan se
				// ejecutan tras el bucle; compartir `m_mask` usaría la de la última).
				+ eng::u32(kTile * kActorWords * kActorRows) * 2u
#endif
			,
			16u);	if (!m_scratch.valid()) {
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
#if defined(K121_GOLDEN_ACTORS)
		m_gmask = m_lane_bg + kLaneBandWords; // 16 slots de máscara (ver reserve)
#endif

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
#if defined(K121_GOLDEN_ACTORS)
		// **Modo golden**: paleta identidad (índice → color inyectivo `(i<<6)|0x3F`) para
		// que el comparador externo (`tools/analyze/verify-121-actor-golden.mjs`) decodifique
		// el índice de cada píxel del PNG sin duplicar la paleta real; el mundo se pinta
		// igual (los índices no cambian, solo sus colores).
		for (u8 i = 0u; i < 32u; ++i) {
			g_world_palette[i] = u16((u16(i) << 6u) | 0x003Fu);
		}
#else
		for (u8 i = 0u; i < 32u; ++i) g_world_palette[i] = kWorldAtlas.palette.color[i];
#endif
		scene_cfg.palette = eng::PaletteWords {g_world_palette, 32u};

		if (!scene.begin(backend.memory_manager(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012103u);
			return;
		}
		// **Invariante de geometría del anillo**: la guarda que suman los blits del actor
		// (`kRingGuardY`) debe coincidir con el `display_offset` real de la vista — es el
		// origen de la ventana visible que usan a la vez el Copper, los blits y la sonda
		// (`read_visible_pixel`); si divergen, el actor se dibuja fuera de la fila que se
		// muestra y la verificación leería otro píxel.
		if (scene.bg().hardware_view().display_offset != kRingGuardY) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012115u);
			return;
		}
		// **Chequeo de solape scratch ↔ bitmap del campo**: el scratch del actor
		// (bandas/carril) y el anillo del campo viven en el mismo banco Chip; un solape
		// haría que el restaurador escribiera datos de una región en la otra (se ha visto
		// un valor de canopia en la verificación del carril en ~mitad de arranques). El
		// `detail` lleva el desplazamiento en KB entre ambas bases para el diagnóstico.
		{
			const eng::playfield::PlayfieldHardwareView hv = scene.bg().hardware_view();
			const eng::u32 sb = eng::Address<eng::MemoryKind::Chip>::from_storage(
						    m_scratch.view.data())
						    .value;
			const eng::u32 se = sb + eng::u32(kTile) * kActorBandWords * 2u +
					    eng::u32(kActorWords * kActorRows) * 2u +
					    kLaneBandWords * 2u + 8u;
			const eng::u32 fb = hv.bitplanes.value;
			const eng::u32 fe = fb + eng::u32(hv.bitmap_bytes_per_row) * hv.planes *
						     hv.bitmap_height;
			if (sb < fe && fb < se) {
				const eng::u32 delta = (fb > sb) ? (fb - sb) : (sb - fb);
				eng::debug::mark_failed(
					g_eng_run_status,
					0x00012119u | ((delta >> 10u) & 0xFFFFu) << 8u);
				return;
			}
		}
		// **Validación de setup de la geometría de blits** (`eng::setup::check_world_blit`):
		// alineación a palabra, fila destino y cobertura del span de las tres rutas de blit
		// del frame (carril completo, ventana de restauración y actor en su x extrema),
		// ANTES de dibujar nada. Un desajuste se convierte en `detail` con el código del
		// validador y el valor culpable en vez de un blit torcido silencioso.
		{
			const eng::playfield::PlayfieldHardwareView hv = scene.bg().hardware_view();
			const u16 row_pixels = u16(hv.bitmap_bytes_per_row * 8u);
			const eng::setup::WorldBlitGeometry geometries[3] = {
				// (1) banda completa del carril (limpieza tras el auto-chequeo).
				{0u, u16(kLaneWordCells * 16u), kActorRows, u8(kPlanes),
				 u16(kLaneWordCells * 2u), kLaneBandWords * 2u / kPlanes, kLaneBandWords},
				// (2) ventana de restauración (2 palabras).
				{kActorMaxX + kRingGuardX, 32u, kActorRows, u8(kPlanes),
				 u16(kLaneWordCells * 2u),
				 kLaneBandWords * 2u / kPlanes, kLaneBandWords},
				// (3) actor (x extrema derecha + ventana de banda de 32 px).
				{kActorMaxX + kRingGuardX, 32u, kActorRows, u8(kPlanes), 4u,
				 u32(kActorRows) * 4u,
				 kActorBandWords}};
			for (u8 i = 0u; i < 3u; ++i) {
				const eng::setup::CheckResult r =
					eng::setup::check_world_blit(geometries[i], row_pixels, kDisplayH);
				if (!r.ok()) {
					eng::debug::mark_failed(
						g_eng_run_status,
						0x00012116u | (u32(r.code) << 8u) |
							(u32(r.actual & 0xFFFFu) << 16u));
					return;
				}
			}
			const eng::setup::CheckResult w =
				eng::setup::check_world_window(u16((kActorMaxX + kRingGuardX) / 16u), 2u,
							       u16(kLaneWordCells));
			if (!w.ok()) {
				eng::debug::mark_failed(g_eng_run_status,
							0x00012117u | (u32(w.actual) << 16u));
				return;
			}
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
#if !defined(K121_ACTOR_LEGACY) && !defined(K121_SKIP_ACTOR)
		// **Camino del SISTEMA de actores** (DT-024: camino por DEFECTO; el camino propio
		// queda tras `K121_ACTOR_LEGACY` para el A/B):
		// hoja pack con guarda (`pack_interleaved_pair`) + `ActorStore`/`Animation` +
		// `SaveUnder`, emitidos sobre el anillo con el puente `actor_target`. Sin bandas
		// por subpíxel ni restaurador de ventana propios: el sistema lo cubre.
		m_aux = backend.memory_manager().chip().reserve<eng::PlaneTag>(2048u, 16u);
		if (!m_aux.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012130u);
			return;
		}
		m_pack = reinterpret_cast<u16*>(m_aux.view.data());
		m_save_buf = m_pack + 320u;
		eng::graphics::pack_interleaved_pair<kTile, kTile, kPlanes>(kActorPixels, nullptr,
									    m_pack);
		for (u16 r = 0u; r < kTile; ++r) {
			u16 sil = 0u;
			for (u16 b = 0u; b < kTile; ++b) {
				if (kActorPixels[u32(r) * kTile + b] != 0u) {
					sil = u16(sil | u16(0x8000u >> b));
				}
			}
			m_sil_mask[r] = sil;
		}
		cook_pack_mask(u16(kActorMinX));
		m_ring_target = eng::playfield::make_actor_target(scene.bg().hardware_view());
		{
			eng::scene::ActorDesc d {};
			d.visual.pixels = eng::Span<const eng::u16> {m_pack, 320u};
			d.visual.w = kTile;
			d.visual.h = kTile;
			d.visual.bitplanes = u8(kPlanes);
			d.animation = &kActorAnim;
			d.x = s16(kActorMinX + kRingGuardX); // anillo (el sistema no aplica sesgo de X)
			d.y = s16(kActorY);
#if defined(K121_ACTOR_SEAM)
			// **Modo costura** (verificación de hardware del reparto de `bob_ring.hpp`):
			// el actor queda a 4 filas del FINAL del anillo (`kDisplayH`), de modo que el
			// dibujo CRUZA la costura — 4 filas del arte caen en las últimas líneas
			// VISIBLES de la ventana y las 12 restantes envuelven a las líneas 0.. de la
			// guarda (fuera de la ventana; su corrección se comprueba leyendo el bitmap
			// del anillo, `seam_check`). Columna limpia de copas (64): la máscara del
			// pack no depende de la fila.
			d.y = s16(kDisplayH - 4u - kRingGuardY);
#endif
			d.offset.y = s16(kRingGuardY); // visible -> anillo (contrato del puente)
			d.transparency = eng::scene::TransparencyMode::Mask1Bit;
			d.background = eng::scene::BackgroundPolicy::SaveUnder;
			d.save[0] = eng::ChipView<eng::BobTag> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(m_save_buf), 480u};
			d.save_words_per_row = 3u; // 2 palabras (32 px) + guarda del shift
			d.save_height = kTile;
			m_actor_store.reset();
#if defined(K121_GOLDEN_ACTORS)
			m_actor_alloc.reset(eng::scene::RepresentationBudget {24u, 20000u, 0u});
#else
			m_actor_alloc.reset(eng::scene::RepresentationBudget {8u, 10000u, 0u});
#endif
			m_actor_id = m_actor_store.add(d, m_actor_alloc);
			auto actor = m_actor_store.get(m_actor_id);
			if (!actor.valid()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00012131u);
				return;
			}
			// Contrato del destino interleaved: pack con guarda (ver HOST-453).
			actor->bob.sheet = eng::ChipView<eng::BobTag> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(m_pack), 640u};
			actor->bob.mask = {};
			actor->bob.width = 32u; // ventana = arte (kTile) + guarda (kTile)
			actor->bob.height = kTile;
			actor->bob.planes = u8(kPlanes);
			actor->bob.layout = eng::graphics::BobLayout::Interleaved;
			actor->bob.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
			actor->bob.draw = eng::graphics::BobDraw::CookieCut;
			actor->bob.frame_count = 1u;
			actor->bob.frame_stride = 0u;
		}
		// **Auto-chequeo del camino nuevo**: emite una vez en la posición inicial y relee
		// el cuerpo (el primer `update` restaurará esa caja con el save-under).
		if (!emit_actor_system(backend)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012132u);
			return;
		}
#if defined(K121_ACTOR_SEAM)
		// Modo costura: el cuerpo ya no está en el carril — se comprueba el reparto
		// (parte visible + parte envuelta a la guarda) leyendo el bitmap del anillo.
		if (!seam_check()) {
			return;
		}
#else
		{
			const u8 body = read_body_stable(u16(kActorMinX + kRingGuardX + 8u), u16(kActorY + 8u));
			if (body != 31u) {
				eng::debug::mark_failed(g_eng_run_status,
							0x00012133u | (u32(body) << 16u));
				return;
			}
		}
#endif
#if defined(K121_GOLDEN_ACTORS) && !defined(K121_SKIP_ACTOR)
		// **Golden de subposiciones por el SISTEMA** (gate visual de §14.8): 16 actores
		// del `ActorStore` a la vez — cada uno en su columna limpia del carril con su
		// subposición (mismos pares col/sub que el oráculo JS). Los 16 COMPARTEN el pack:
		// en columna limpia la máscara es la silueta (no hay copa que recortar) y el
		// subpíxel lo aplica el shifter del par. Sin save-under (política `None`: los 16
		// conviven) y SIN clip: las últimas columnas (17/18) desbordan la fila del anillo
		// por la guarda del par (máscara 0 -> D=C, no-op: sin daño colateral — el camino
		// propio sí escribía el desbordamiento). Captura vs
		// `tools/analyze/verify-121-actor-golden.mjs` (paleta identidad).
		{
			// Limpieza del actor del auto-chequeo: se restaura su caja (save-under del
			// emit de arriba) para no dejar píxeles fuera de las 16 ventanas.
			{
				auto a0 = m_actor_store.get(m_actor_id);
				const eng::scene::DirtyRect& prev = a0->prev[0];
				if (prev.valid()) {
					plan.clear();
					plan.set_blit_budget_limits({8192, 16384, 6, 120});
					if (!eng::graphics::bob_restore_box_ring(
						    plan, a0->bob, prev.width(), prev.height(), prev.left,
						    prev.top, scene.bg().hardware_view().display_height, m_ring_target, a0->desc.save[0],
						    a0->desc.save_words_per_row, a0->desc.save_height) ||
					    !backend.execute_frame_plan(plan)) {
						eng::debug::mark_failed(g_eng_run_status, 0x00012122u);
						return;
					}
				}
			}
			const u16 cols[16] = {0u, 1u, 2u, 3u, 4u, 7u, 8u, 9u,
					      11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u};
			eng::scene::ActorEmitContext gctx {};
			gctx.targets = eng::Span<const eng::graphics::BobTarget> {&m_ring_target, 1u};
			gctx.buffer = 0;
			gctx.ring_height = scene.bg().hardware_view().display_height;
			// `gctx.clip` queda inválido (sin recorte): el par hace no-op el desborde.
			plan.clear();
			plan.set_blit_budget_limits({8192, 32768, 32, 400});
			for (u8 k = 0u; k < 16u; ++k) {
				eng::scene::ActorDesc gd {};
				gd.visual.pixels = eng::Span<const eng::u16> {m_pack, 320u};
				gd.visual.w = kTile;
				gd.visual.h = kTile;
				gd.visual.bitplanes = u8(kPlanes);
				gd.animation = &kActorAnim;
				gd.x = s16(cols[k] * 16u + k + kRingGuardX); // anillo (como el golden propio)
				gd.y = s16(kActorY);
				gd.offset.y = s16(kRingGuardY);
				gd.transparency = eng::scene::TransparencyMode::Mask1Bit;
				gd.background = eng::scene::BackgroundPolicy::None;
				const eng::scene::ActorId gid = m_actor_store.add(gd, m_actor_alloc);
				auto ga = m_actor_store.get(gid);
				if (!ga.valid()) {
					eng::debug::mark_failed(g_eng_run_status, 0x00012123u);
					return;
				}
				ga->bob.sheet = eng::ChipView<eng::BobTag> {
					eng::Address<eng::MemoryKind::Chip>::from_storage(m_pack), 640u};
				ga->bob.mask = {};
				ga->bob.width = 32u;
				ga->bob.height = kTile;
				ga->bob.planes = u8(kPlanes);
				ga->bob.layout = eng::graphics::BobLayout::Interleaved;
				ga->bob.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
				ga->bob.draw = eng::graphics::BobDraw::CookieCut;
				ga->bob.frame_count = 1u;
				ga->bob.frame_stride = 0u;
				if (eng::scene::actor_emit(plan, *ga, gctx) !=
				    eng::scene::ActorEmitStatus::Ok) {
					eng::debug::mark_failed(g_eng_run_status, 0x00012124u);
					return;
				}
#if defined(K121_GOLDEN_ONE_PLAN_PER_ACTOR)
				if (!backend.execute_frame_plan(plan)) {
					eng::debug::mark_failed(g_eng_run_status, 0x00012125u);
					return;
				}
				plan.clear();
				plan.set_blit_budget_limits({8192, 32768, 32, 400});
#endif
			}
			if (!backend.execute_frame_plan(plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00012125u);
				return;
			}
			m_actor_x = kActorMinX;
			m_restore_x = kActorMinX;
		}
#endif
#else
		// **Auto-chequeo de hardware** (patrón de `COHERENCIA_DE_SETUP.md` §22): dibuja
		// el actor con las MISMAS rutinas del frame en una posición fija, RELEE el
		// bitmap del campo y valida color del cuerpo (31), transparencia del fondo y
		// geometría; si algo no cuadra, `mark_failed` con los valores observados en
		// `detail` en vez de enseñar basura silenciosa.
		{
			const u32 check = self_check_actor(backend);
			if (check != 0u) {
				eng::debug::mark_failed(g_eng_run_status, check);
				return;
			}
		}
		// Limpieza del auto-chequeo: el chequeo dibuja al actor en varias posiciones y
		// deja sus píxeles en el carril; se restaura la banda completa una vez para que
		// el primer frame del bucle parta del fondo limpio. La ventana a restaurar cada
		// frame es la del actor (aún sin dibujar): el carril queda coherente igual.
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});
		if (!scene.bg().add_world_bitmap(
			    plan, eng::Span<const eng::u16> {m_lane_bg, kLaneBandWords}, 0,
			    s32(kActorY) + s32(kRingGuardY), u16(kLaneWordCells * 16u),
			    kActorRows, u16(kLaneWordCells * 2u), kLaneBandWords * 2u / 5u,
			    kPlanes) ||
		    !backend.execute_frame_plan(plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012114u);
			return;
		}
		m_restore_x = u16((m_actor_x >> 4u) << 4u);
#if defined(K121_GOLDEN_ACTORS) && !defined(K121_SKIP_ACTOR)
		// **Golden de subposiciones**: pinta las 16 bandas del actor a la vez, cada una en
		// su columna limpia del carril (las copas están en 5, 6 y 10), con la MISMA ruta
		// de blits del frame. La captura se compara píxel a píxel contra un oráculo
		// independiente (banda reconstruida en JS desde el patrón del actor) y contra una
		// captura sin actores (para exigir que nada fuera del actor cambie).
		{
			const u16 cols[16] = {0u, 1u, 2u, 3u, 4u, 7u, 8u, 9u,
					      11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u};
			plan.clear();
			plan.set_blit_budget_limits({8192, 32768, 32, 400});
			for (u8 k = 0u; k < 16u; ++k) {
				m_actor_x = u16(cols[k] * 16u + k);
				// Máscara propia del slot k (los 16 dibujos se ejecutan juntos: cada uno
				// necesita la suya; `patch_actor` comparte `m_mask` y no sirve aquí).
				u16* const gm = m_gmask + eng::u32(k) * (kActorWords * kActorRows);
				{
					const u16 wx = u16(m_actor_x >> 4u);
					const u16* const am = kWorld.actor_mask[k];
					for (u16 r = 0u; r < kActorRows; ++r) {
						const u16* const cm = &kWorld.canopy_mask[kActorY + r][wx];
						gm[eng::u32(r) * 2u + 0u] =
							u16(am[eng::u32(r) * 2u + 0u] & u16(~cm[0u]));
						gm[eng::u32(r) * 2u + 1u] =
							u16(am[eng::u32(r) * 2u + 1u] & u16(~cm[1u]));
					}
				}
				if (!scene.bg().add_world_bitmap_masked(
					    plan,
					    eng::Span<const eng::u16> {
						    m_bands + eng::u32(k) * kActorBandWords,
						    kActorBandWords},
					    eng::Span<const eng::u16> {
						    gm, eng::u32(kActorWords) * kActorRows},
					    s32(cols[k] * 16u + kRingGuardX), s32(kActorY) + s32(kRingGuardY), 32u,
					    kActorRows, 4u, kActorRows * 4u, kPlanes)) {
					eng::debug::mark_failed(g_eng_run_status, 0x00012120u);
					return;
				}
#if defined(K121_GOLDEN_ONE_PLAN_PER_ACTOR)
				// Bisección: cada actor en su PROPIO plan/execute (para aislar si el
				// corrimiento de subposiciones viene del plan multi-job/lote).
				if (!backend.execute_frame_plan(plan)) {
					eng::debug::mark_failed(g_eng_run_status, 0x00012121u);
					return;
				}
				plan.clear();
				plan.set_blit_budget_limits({8192, 32768, 32, 400});
#endif
			}
			if (!backend.execute_frame_plan(plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00012121u);
				return;
			}
			m_actor_x = kActorMinX;
			m_restore_x = kActorMinX;
		}
#endif
#endif // !K121_ACTOR_SYSTEM
		draw_hud();
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x00000121u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!m_ready) {
			return;
		}
		// **Sonda del buffer de display** (una vez, ya con el Copper ejecutando la
		// copperlist de la escena): BPL1PT debe apuntar al MISMO bloque que la base de
		// los blits (la diferencia esperada es solo el offset de ventana, ≪ 4 KB). Si
		// divergen, la pantalla nunca mostrará lo que se dibuja — el fallo que la guarda
		// (que lee la base de los blits) no puede ver.
		if (m_frames == 3u) { // tras el swap de copperlist (instalacion con margen)
			const volatile eng::u16* const dff =
				reinterpret_cast<const volatile eng::u16*>(0xDFF0E0u);
			const eng::u32 bpl =
				(eng::u32(dff[0] & 0x00FFu) << 16u) | eng::u32(dff[1] & 0xFFFEu);
			const eng::u32 base = scene.bg().hardware_view().bitplanes.value;
			const eng::u32 delta = (bpl > base) ? (bpl - base) : (base - bpl);
			if (delta > 4096u) {
				m_ready = false;
				eng::debug::mark_failed(
					g_eng_run_status,
					0x00012141u | (((delta >> 8u) & 0xFFFFu) << 8u));
				return;
			}
		}
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});
#if defined(K121_FREEZE_ACTOR_RENDER) || defined(K121_GOLDEN_ACTORS)
		// Sonda diagnóstica: sin blits por frame (el actor queda donde lo dejó el
		// auto-chequeo de init). Elimina la carrera read-back vs Blitter y permite
		// capturar el render estático del mismo camino de blits.
		(void)backend;
		if (!backend.execute_frame_plan(plan)) {
			m_ready = false;
		}
#elif !defined(K121_ACTOR_LEGACY)
		// **Actor por el SISTEMA del engine** (A/B de DT-024.3): avance con el mismo
		// contrato (1 px/frame con rebote), re-cocido de la máscara si cambia la columna
		// (oclusión por copas) y emisión: SaveUnder restaura/guarda el fondo y el pack
		// con guarda dibuja el subpíxel que toque (sin bandas ni ventana propias).
		(void)eng::scene::actor_tick(*m_actor_store.get(m_actor_id), 1u);
#if !defined(K121_STATIC_ACTOR)
		if (m_dir > 0 && m_actor_x >= kActorMaxX) {
			m_dir = -1;
		} else if (m_dir < 0 && m_actor_x <= kActorMinX) {
			m_dir = 1;
		}
		m_actor_x = u16(s32(m_actor_x) + m_dir);
#else
		// Estático: 64 = centro en la columna 4 (limpia de copas 5/6/10); medible con
		// landmarks en ambos caminos.
		m_actor_x = 64u;
#endif
		{
			auto actor = m_actor_store.get(m_actor_id);
			actor->desc.x = s16(m_actor_x + kRingGuardX);
			cook_pack_mask(m_actor_x);
			if (!emit_actor_system(backend)) {
				m_ready = false;
				eng::debug::mark_failed(g_eng_run_status, 0x00012134u);
				return;
			}
#if defined(K121_ACTOR_SEAM)
			// Evidencia por frame del reparto en la costura (parte visible + envuelta).
			if (!seam_check()) {
				return;
			}
#endif
		}
#elif defined(K121_ACTOR_LEGACY) && !defined(K121_SKIP_ACTOR)
		// **Actor por Blitter**: (1) restaura SOLO la ventana de 2 palabras que ocupaba
		// el actor en el frame anterior con el emisor de ventana del playfield
		// (`add_world_window`: banda completa + columna, validado en el propio emisor);
		// (2) dibuja el bicho desplazado con la máscara combinada (`actor & ~copas`):
		// bajo la copa no se escribe y el bicho queda oculto.
		if (!scene.bg().add_world_window(
			    plan, eng::Span<const eng::u16> {m_lane_bg, kLaneBandWords},
			    u16((m_restore_x + kRingGuardX) / 16u), s32(m_restore_x + kRingGuardX),
			    s32(kActorY) + s32(kRingGuardY),
			    2u, kActorRows, u16(kLaneWordCells), kPlanes)) {
			m_ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00012110u);
			return;
		}
		// **Plan único por frame** (restaurador + actor en el MISMO `FramePlan`): la
		// pérdida intermitente del plano 0 que obligaba a separarlos quedó resuelta en el
		// motor por el contrato de espera por job (`blitter-busy-writes.md`; escribir
		// BLTSIZE con un blit activo lo pierde). Verificado 5/5 arranques con telemetría
		// viva y medidas estables (47,67 fps / 148,8k ciclos, igual que con dos planes).
		// `K121_TWO_PLAN_SPLIT` conserva el camino separado para el A/B.
#if defined(K121_TWO_PLAN_SPLIT)
		if (!backend.execute_frame_plan(plan)) {
			m_ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00012113u);
			return;
		}
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});
#endif
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
		m_actor_x = 64u;
#endif
		patch_actor();
		if (!scene.bg().add_world_bitmap_masked(
			    plan, eng::Span<const eng::u16> {m_bands + eng::u32(m_actor_x & 15u) * kActorBandWords,
							     kActorBandWords},
			    eng::Span<const eng::u16> {m_mask, kActorWords * kActorRows},
			    s32(((m_actor_x + kRingGuardX) >> 4u) << 4u), s32(kActorY) + s32(kRingGuardY),
			    32u, kActorRows,
4u,
			    kActorRows * 4u, kPlanes)) {
			m_ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00012111u);
			return;
		}
		// La ventana a restaurar el frame siguiente es la que ocupa el actor ahora.
		m_restore_x = u16((m_actor_x >> 4u) << 4u);
#endif
		if (!backend.execute_frame_plan(plan)) {
			m_ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00012112u);
			return;
		}
		// **Guarda de render por frame** (COHERENCIA_DE_SETUP §22): relee el píxel del
		// cuerpo del actor en el bitmap y exige color 31 salvo que la celda esté bajo
		// una copa (entonces el pixel correcto es el de la copa). Convierte cualquier
		// desajuste de planos/posición en un fallo visible con la x exacta en `detail`.
#if !defined(K121_FREEZE_ACTOR_RENDER) && !defined(K121_GOLDEN_ACTORS)
		{
			const u16 bx = u16(m_actor_x + kRingGuardX + 8u); // anillo
			const u16 sx = u16(m_actor_x + 8u);                // pantalla/mapa
			const bool covered =
				(kWorld.canopy_mask[kActorY + 8u][u16(sx >> 4u)] &
				 u16(0x8000u >> (sx & 15u))) != 0u;
			const u8 body = read_body_stable(bx, u16(kActorY + 8u));
			// Tolerancia a fallos aislados: un desajuste persistente en el render
			// (el caso real) falla TODOS los frames; un pico transitorio de un solo
			// plano visto una vez (carrera conocida de un plano al encadenar blits)
			// se descarta si el frame siguiente vuelve a estar bien.
			if (!covered && body != 31u) {
				++m_body_faults;
				if (m_body_faults >= 3u) {
					m_ready = false;
					eng::debug::mark_failed(g_eng_run_status,
								0x0001212Fu |
									(u32(m_actor_x) << 8u) |
									(u32(body) << 24u));
					return;
				}
			} else {
				m_body_faults = 0u;
			}
		}
#endif
		// Telemetría: posición y sentido del actor.
		g_eng_run_status.detail = u32(m_actor_x) | (u32(u16(m_dir)) << 16u);
		++m_frames;
		m_host_hud.draw(backend.debug(), "121 PROC CORNER LAYERS", kVersion, m_frames,
				eng::debug::prof_clock());
#if !defined(K121_SKIP_HUD)
		if ((m_frames & 63u) == 0u) {
			m_frame_text.clear();
			m_frame_text << 'F' << ' ' << u16(m_frames & 0xFFFFu);
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
	/// Lee el **color** (índice de paleta) del píxel visible `(sx, sy)` del campo con la
	/// **sonda del engine** (`eng::playfield::read_visible_pixel`): resuelve la línea del
	/// anillo con el `display_offset` de la vista (la guarda del corkscrew) y el layout
	/// intercalado, sin constantes mágicas en la demo. El init comprueba la invariante
	/// `display_offset == kRingGuardY` (los blits usan esa misma guarda).
	/// \param sx,sy píxel visible (0..319, 0..207). \return índice de color 0..31.
	[[nodiscard]] u8 read_field_pixel(u16 sx, u16 sy) const {
		return eng::playfield::read_visible_pixel(scene.bg().hardware_view(), sx, sy);
	}

	/// Lee el color del cuerpo del actor con **re-lectura de confirmación**
	/// (`eng::playfield::read_confirm`): se han observado lecturas aisladas con un plano
	/// obsoleto (cuerpo=30/26) aunque `execute_frame_plan` espera al último blit; una
	/// repetición inmediata confirma el valor estable y evita fallos espurios del
	/// auto-chequeo.
	[[nodiscard]] u8 read_body_stable(u16 sx, u16 sy) const {
		return eng::playfield::read_confirm(
			[this, sx, sy] { return read_field_pixel(sx, sy); }, 31u);
	}

#if defined(K121_ACTOR_SEAM)
	/// **Auto-chequeo del modo costura** (`bob_ring.hpp`): el actor está a 4 filas del
	/// final del anillo, así que su dibujo se emite como DOS trabajos (parte visible +
	/// envuelta). Lee el bitmap del anillo con la sonda de línea directa
	/// (`read_ring_pixel`; la visible no llega a la guarda) y exige:
	///   (a) fila del arte 2 → anillo `kDisplayH − 2` (última zona visible) = 31;
	///   (b) fila del arte 12 → anillo 8 (guarda, parte ENVUELTA) = 31 — prueba de que
	///       el segundo trabajo del reparto escribió en la zona correcta;
	///   (c) fila del arte 15 → anillo 11 conserva el FONDO (no 31: la esquina del arte
	///       es transparente y la máscara no debe pintarla; la guarda ya traía mundo).
	/// Fallo en `detail` = 0x12135/0x12136/0x12137 | got<<16. La columna es la limpia
	/// de copas (`kActorMinX + kRingGuardX + 8`): la máscara del pack es la silueta.
	[[nodiscard]] bool seam_check() const {
		const eng::playfield::PlayfieldHardwareView hv = scene.bg().hardware_view();
		const u16 sx = u16(kActorMinX + kRingGuardX + 8u); // centro del arte (columna de bitmap)
		const u8 top = eng::playfield::read_ring_pixel(hv, u16(kDisplayH - 2u), sx);
		if (top != 31u) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012135u | (u32(top) << 16u));
			return false;
		}
		const u8 wrapped = eng::playfield::read_ring_pixel(hv, 8u, sx);
		if (wrapped != 31u) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012136u | (u32(wrapped) << 16u));
			return false;
		}
		const u8 corner = eng::playfield::read_ring_pixel(hv, 11u, sx);
		if (corner == 31u) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012137u | (u32(corner) << 16u));
			return false;
		}
		return true;
	}
#endif

	/// **Auto-chequeo**: (1) tras ejecutar SOLO el restaurador del carril, un píxel del
	/// carril debe ser el de la pieza cocida de la fila 9 (misma fuente que el banco);
	/// (2) el actor se dibuja con las MISMAS rutinas del frame en varias columnas y
	/// subposiciones, y el cuerpo (local 8,8) debe ser color 31. Devuelve 0 si OK; si
	/// no, `detail` con lo observado (`0x12106 | idx<<8 | body<<16`). El primer
	/// `update` restaura el carril.
	[[nodiscard]] u32 self_check_actor(eng::amiga::AmigaBackend& backend) {
		const u16 saved_x = m_actor_x;
		u32 bad = 0u;
		// (1) Restaurador: píxel del carril vs pieza cocida (fila 9, col 8, local 9,6).
		//     Con reintento: el grupo de copias del restaurador se ha visto con un plano
		//     desviado en la primera pasada (clase de fallo de DT-022); un re-dibujo
		//     inmediato lo resuelve y el fallo solo se reporta si persiste.
		const u16 lane_cell = kBaked.cells[eng::u32(kActorY / kTile) * kMapW + 7u];
		const u8 exp = kBaked.tiles[lane_cell][eng::u32(6u) * kTile + 9u];
		u8 got = 0u;
		for (u8 attempt = 0u; attempt < 2u; ++attempt) {
			plan.clear();
			plan.set_blit_budget_limits({8192, 16384, 6, 120});
			const bool qr = scene.bg().add_world_bitmap(
				plan, eng::Span<const eng::u16> {m_lane_bg, kLaneBandWords}, 0,
				s32(kActorY) + s32(kRingGuardY), u16(kLaneWordCells * 16u),
				kActorRows, u16(kLaneWordCells * 2u), kLaneBandWords * 2u / 5u,
				kPlanes);
			if (!qr || !backend.execute_frame_plan(plan)) {
				m_actor_x = saved_x;
				return 0x00012107u;
			}
			// Re-lectura de confirmación contra lecturas transitorias.
			got = eng::playfield::read_confirm(
				[this] { return read_field_pixel(137u, u16(kActorY + 6u)); }, exp);
			if (got == exp) {
				break;
			}
		}
		if (got != exp) {
				// Traza de diagnóstico por la consola del periférico (el runner la lee
				// con `--read-debugperiph console`): el valor de la banda y lecturas
				// vecinas distinguen corrimiento (±16/±32 px) de corrupcion de memoria.
				{
					// Píxel equivalente leído de la banda en Chip (mismo layout que el
					// restore): si coincide con `got`, la banda es la fuente del valor.
					u8 band_px = 0u;
					for (u8 p = 0u; p < kPlanes; ++p) {
						const u16 w = m_lane_bg[eng::u32(p) * (kLaneBandWords / kPlanes) +
									 6u * kLaneWordCells + 8u];
						if ((w & 0x0040u) != 0u) band_px = u8(band_px | u8(1u << p));
					}
					eng::util::TextLine<96> line;
					line << "s1 got" << eng::util::hex(got, 2u) << ' '
					     << "exp" << eng::util::hex(exp, 2u) << ' '
					     << "bnd" << eng::util::hex(band_px, 2u) << ' '
					     << "xm32" << eng::util::hex(read_field_pixel(u16(137u - 32u), u16(kActorY + 6u)), 2u) << ' '
					     << "xm16" << eng::util::hex(read_field_pixel(u16(137u - 16u), u16(kActorY + 6u)), 2u) << ' '
					     << "xp16" << eng::util::hex(read_field_pixel(u16(137u + 16u), u16(kActorY + 6u)), 2u) << ' '
					     << "xp32" << eng::util::hex(read_field_pixel(u16(137u + 32u), u16(kActorY + 6u)), 2u);
					eng::debug::DebugPeripheral::console_line(line.c_str());
				}
				bad = 0x00012108u | (u32(got) << 16u) | (u32(exp) << 24u); // 0x12108 | got | exp
		}
		// (2) Integridad de las bandas en Chip: el contenido debe ser exactamente lo que
		//     produce `make_band_from_pixels` (detección de corrupción de memoria).
		{
			u16 ref[kActorBandWords] {};
			u16 refmask[kActorWords * kActorRows] {};
			for (u16 sh = 0u; sh < 16u && bad == 0u; ++sh) {
				eng::graphics::make_band_from_pixels<kTile, kTile, 5>(
					kActorPixels, sh, ref, refmask);
				for (u32 i = 0u; i < kActorBandWords; ++i) {
					if (ref[i] != m_bands[eng::u32(sh) * kActorBandWords + i]) {
						bad = 0x00012109u | (u32(sh) << 8u) | (i << 16u);
						break;
					}
				}
			}
		}
		// (3) Actor: las 16 subposiciones en una columna sin copa (cuerpo en local 8;
		//     x 112..127 → cuerpo en 120..135, columnas 7-8 sin copa).
		for (u16 sub = 0u; sub < 16u && bad == 0u; ++sub) {
			const u16 px = u16(112u + sub);
			m_actor_x = px;
			plan.clear();
			plan.set_blit_budget_limits({8192, 16384, 6, 120});
			patch_actor();
			const bool queued = scene.bg().add_world_bitmap_masked(
				plan,
				eng::Span<const eng::u16> {m_bands + eng::u32(sub) * kActorBandWords,
							   kActorBandWords},
				eng::Span<const eng::u16> {m_mask, kActorWords * kActorRows},
				s32(((px + kRingGuardX) >> 4u) << 4u), s32(kActorY) + s32(kRingGuardY), 32u, kActorRows,
				4u, kActorRows * 4u, kPlanes);
			const bool ran = queued && backend.execute_frame_plan(plan);
			const u8 body = ran ? read_body_stable(u16(px + kRingGuardX + 8u), u16(kActorY + 8u)) : 0xFFu;
			if (!ran) {
				bad = 0x00012107u;
			} else if (body != 31u) {
				bad = 0x00012100u | (u32(sub) << 8u) | (u32(body) << 16u);
			}
		}
		// (4) Columnas de la pista con subposiciones representativas.
		const u16 probe_x[5] = {128u, 192u, 224u, 240u, 256u};
		const u16 probe_sub[3] = {0u, 7u, 15u};
		for (u8 i = 0u; i < 5u && bad == 0u; ++i) {
			for (u8 k = 0u; k < 3u && bad == 0u; ++k) {
				const u16 px = u16(probe_x[i] + probe_sub[k]);
				m_actor_x = px;
				plan.clear();
				plan.set_blit_budget_limits({8192, 16384, 6, 120});
				patch_actor();
				const bool queued = scene.bg().add_world_bitmap_masked(
					plan,
					eng::Span<const eng::u16> {
						m_bands + eng::u32(px & 15u) * kActorBandWords,
						kActorBandWords},
					eng::Span<const eng::u16> {m_mask, kActorWords * kActorRows},
					s32(((px + kRingGuardX) >> 4u) << 4u), s32(kActorY) + s32(kRingGuardY), 32u,
					kActorRows, 4u, kActorRows * 4u, kPlanes);
				const bool ran = queued && backend.execute_frame_plan(plan);
				const u8 body =
					ran ? read_body_stable(u16(px + kRingGuardX + 8u), u16(kActorY + 8u)) : 0xFFu;
				if (!ran) {
					bad = 0x00012107u;
				} else if (body != 31u) {
					bad = 0x00012106u | (u32(i * 3u + k) << 8u) | (u32(body) << 16u);
				}
			}
		}
		// (5) Ruta EXACTA del bucle (`update`): restaurador del carril + actor en el
		//     MISMO plan, para las 16 subposiciones (el plan real lleva los dos jobs).
		//     Reintento único: se ha observado (2 de ~8 arranques) un fallo transitorio
		//     de un solo plano al encadenar restaurador + actor en el mismo plan
		//     (`body=30`, plano 0 sin escribir, persistente entre lecturas); un
		//     re-dibujo inmediato lo resuelve. El caso es determinista por ejecución:
		//     si el reintento también falla, es un fallo real y se reporta.
		for (u16 sub = 0u; sub < 16u && bad == 0u; ++sub) {
			const u16 px = u16(112u + sub);
			m_actor_x = px;
			u8 body = 0u;
			for (u8 attempt = 0u; attempt < 2u; ++attempt) {
				plan.clear();
				plan.set_blit_budget_limits({8192, 16384, 6, 120});
				const bool q0 = scene.bg().add_world_bitmap(
					plan, eng::Span<const eng::u16> {m_lane_bg, kLaneBandWords},
					0, s32(kActorY) + s32(kRingGuardY),
					u16(kLaneWordCells * 16u), kActorRows,
					u16(kLaneWordCells * 2u), kLaneBandWords * 2u / 5u,
					kPlanes);
				if (!q0 || !backend.execute_frame_plan(plan)) {
					bad = 0x00012107u;
					break;
				}
				plan.clear();
				plan.set_blit_budget_limits({8192, 16384, 6, 120});
				patch_actor();
				const bool q1 = scene.bg().add_world_bitmap_masked(
							      plan,
							      eng::Span<const eng::u16> {
								      m_bands +
									      eng::u32(sub) * kActorBandWords,
								      kActorBandWords},
							      eng::Span<const eng::u16> {
								      m_mask, kActorWords * kActorRows},
							      s32(((px + kRingGuardX) >> 4u) << 4u),
							      s32(kActorY) + s32(kRingGuardY), 32u,
							      kActorRows, 4u, kActorRows * 4u, kPlanes);
				const bool ran = q1 && backend.execute_frame_plan(plan);
				body = ran ? read_body_stable(u16(px + kRingGuardX + 8u), u16(kActorY + 8u))
					   : 0xFFu;
				if (!ran) {
					bad = 0x00012107u;
					break;
				}
				if (body == 31u) break;
			}
			if (bad == 0u && body != 31u) {
				bad = 0x00012102u | (u32(sub) << 8u) | (u32(body) << 16u);
			}
		}
		m_actor_x = saved_x;
		return bad;
	}

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
					const u16 cell = kBaked.cells[u32(lane_row) * kMapW + (c == 0u ? 0u : u32(c) - 1u)];
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

#if !defined(K121_ACTOR_LEGACY) && !defined(K121_SKIP_ACTOR)
	/// **Cocina la máscara del pack** (por fila y plano) para la posición `x`: cada bit
	/// local `b` de la hoja cae en pantalla en `x + b` (el blit desplaza imagen y máscara
	/// juntas con `shift`), así que la copa se muestrea en la columna REAL de cada píxel
	/// — no en la columna alineada (error de ±subpíxel que la guarda cazó en x=86).
	/// Las imágenes del pack son estáticas: solo cambia la máscara.
	void cook_pack_mask(u16 x) {
		const u16 s = u16(x & 15u);
		const u16 wx = u16(x >> 4u);
		for (u16 r = 0u; r < kTile; ++r) {
			const u16* const crow = &kWorld.canopy_mask[u32(kActorY) + r][0u];
			// Copas de las dos columnas que puede tocar el actor (composición por shift:
			// el bit b de la máscara mira la copa en la pantalla x+b = columna wx, bit s+b
			// de la secuencia [col wx | col wx+1]).
			const u32 pair = (u32(crow[wx]) << 16u) | u32(crow[wx + 1u]);
			const u16 canopy = u16(pair >> (16u - s));
			const u16 m0 = u16(m_sil_mask[r] & u16(~canopy));
			for (u8 p = 0u; p < kPlanes; ++p) {
				m_pack[(u32(r) * kPlanes + p) * 4u + 2u] = m0; // máscara del par
			}
		}
	}

	/// Emite el actor con el SISTEMA del engine (un plan/execute por frame): SaveUnder
	/// restaura la caja previa y guarda la actual; el pack dibuja con la guarda invisible.
	[[nodiscard]] bool emit_actor_system(eng::amiga::AmigaBackend& backend) {
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 16u, 240u});
		eng::scene::ActorEmitContext ctx {};
		const eng::playfield::PlayfieldHardwareView hv = scene.bg().hardware_view();
		ctx.targets = eng::Span<const eng::graphics::BobTarget> {&m_ring_target, 1u};
		ctx.clip = eng::scene::DirtyRect {0, 0, s16(hv.viewport_w), s16(hv.display_height)};
		ctx.buffer = 0;
		// **Anillo**: el rect del actor viaja en líneas del anillo y CRUZA la costura si
		// toca — el emisor lo reparte en dos trabajos (`bob_ring.hpp`); sin cruce, vía
		// lineal exacta con una única comparación.
		ctx.ring_height = hv.display_height;
		ctx.cam_x = 0;
		ctx.cam_y = 0;
		auto actor = m_actor_store.get(m_actor_id);
		if (eng::scene::actor_emit(plan, *actor, ctx) != eng::scene::ActorEmitStatus::Ok) {
			return false;
		}
		return backend.execute_frame_plan(plan);
	}
#endif

	/// Combina la máscara del subpíxel tocado con la negación de la máscara de copas
	/// (solo 32 palabras por frame: el trabajo por píxel lo hace el Blitter).
	void patch_actor() {
		const u16 sub = u16(m_actor_x & 15u);
		const u16 wx = u16(m_actor_x >> 4u);
		const u16* const am = kWorld.actor_mask[sub];
		for (u16 r = 0u; r < kActorRows; ++r) {
			const u16* const cm = &kWorld.canopy_mask[kActorY + r][wx];
			m_mask[u32(r) * 2u + 0u] = u16(am[u32(r) * 2u + 0u] & u16(~cm[0u]));
			m_mask[u32(r) * 2u + 1u] = u16(am[u32(r) * 2u + 1u] & u16(~cm[1u]));
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
		eng::util::TextLine<64> line1;
		line1 << "PROC STRUCTURES " << eng::util::StringView(label, vlen)
		      << "  " << m_frame_text.view();
		(void)hud.draw_text_words(16, 4, line1.c_str(), 1u);

		eng::util::TextLine<64> line2;
		line2 << "X-LIMITED 20x13  PIECES " << kBaked.count;
		(void)hud.draw_text_words(16, 20, line2.c_str(), 1u);
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
