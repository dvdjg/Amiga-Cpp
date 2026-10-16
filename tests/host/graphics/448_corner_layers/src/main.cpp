// ============================================================================
// Test HOST-448: autotiling por esquinas (dual-grid) y mapas por capas
// ============================================================================
//
// Cubre las dos piezas nuevas del pipeline de tiles de `eng::gfx::proc`:
//
//   - `corner_tiles.hpp`: ocupación a resolución de celda, máscara de 4 esquinas,
//     pintores orgánico (campo bilineal Q8 + ruido entero tileable + orilla con
//     dither Bayer) y duro (marching squares), las 16 variantes del set, stamps
//     (rect / disco ruidoso / río meandriforme) y composición del mapa por
//     máscaras. La CONTINUIDAD DE LA JUNTA se verifica para todas las parejas de
//     máscaras compatibles (la propiedad que el diseño garantiza por construcción).
//   - `layered_map.hpp`: mapa ground/decal/overhead (SoA), transparencia por
//     índice 0, callback de actores entre capas (el actor «pasa bajo» las copas),
//     banderas de colisión horneadas desde el suelo y movimiento AABB eje a eje.
//
//   bash tools/run-host-tests.sh tests/host/graphics/448_corner_layers
//   (opcional) <binario> --dump <dir>  → PPM de evidencia visual (Ollama)

#include <cstdio>
#include <cstring>

// `proc/*.hpp` es genérico y NO incluye escalares concretos (§1.11): el test trae el suyo.
#include <eng/core/math/minifloat.hpp>
#include <eng/field/tile_map.hpp> // solo el test: el mapa por capas encaja con el tipo del field
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/corner_tiles.hpp>
#include <eng/graphics/proc/evaluators.hpp>
#include <eng/graphics/proc/layered_map.hpp>
#include <eng/graphics/proc/terrain_paint.hpp>
#include <eng/graphics/proc/tile_bake.hpp>

namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
using eng::s16;
using eng::u16;
using eng::u8;
using MF = eng::math::MiniFloat16;

namespace {

int g_fail = 0;
const char* g_dump_dir = nullptr; // `--dump <dir>`: escribe PPM de evidencia visual

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// ¿Las esquinas del borde derecho de `ma` coinciden con las del izquierdo de
/// `mb`? (NE de A == NW de B y SE de A == SW de B). Es la compatibilidad que el
/// generador de mundo respeta por construcción (las esquinas son compartidas).
bool corners_compatible(u8 ma, u8 mb) {
	return ((ma >> 1u) & 1u) == ((mb >> 0u) & 1u) && ((ma >> 3u) & 1u) == ((mb >> 2u) & 1u);
}

// ---------------------------------------------------------------------------
//  1. Occupancy + máscara de 4 esquinas (dual-grid)
// ---------------------------------------------------------------------------

void test_masks() {
	proc::OccupancyMap<6, 6> occ {};
	occ.fill(0u);
	check(occ.at(-1, 0) == 0u && occ.at(6, 6) == 0u, "occupancy: fuera del mapa = outside (0)");
	occ.outside = 2u;
	check(occ.at(0, -1) == 2u, "occupancy: outside configurable");
	occ.outside = 0u;

	proc::stamp_rect(occ, 2u, 2u, 2u, 2u, 1u);
	check(occ.at(2, 2) == 1u && occ.at(3, 3) == 1u && occ.at(1, 2) == 0u && occ.at(4, 2) == 0u,
	      "stamp_rect sella el rectangulo y solo el");
	// Tabla exacta de máscaras alrededor de un bloque 2×2 en (2,2): la esquina
	// superior izquierda del tile (tx,ty) es la celda (tx,ty).
	check(proc::corner_mask(occ, 0u, 0u) == 0u, "mascara 0: sin esquinas activas");
	check(proc::corner_mask(occ, 1u, 1u) == 8u, "mascara 8: solo SE");
	check(proc::corner_mask(occ, 2u, 2u) == 15u, "mascara 15: las cuatro esquinas");
	check(proc::corner_mask(occ, 2u, 1u) == 12u, "mascara 12: mitad inferior (SW+SE)");
	check(proc::corner_mask(occ, 3u, 1u) == 4u, "mascara 4: solo SW");
	check(proc::corner_mask(occ, 1u, 2u) == 10u, "mascara 10: mitad derecha (NE+SE)");
	check(proc::corner_mask(occ, 1u, 3u) == 2u, "mascara 2: solo NE");

	u8 masks[5][5] {};
	proc::fill_corner_layout(occ, masks);
	check(masks[1][1] == 8u && masks[2][2] == 15u && masks[0][0] == 0u,
	      "fill_corner_layout rellena las mascaras por celda");
	// Contrato estructural: la esquina compartida entre vecinos es la misma.
	bool shared = true;
	for (u16 y = 0; y < 5u; ++y) {
		for (u16 x = 0; x < 5u; ++x) {
			if (x + 1u < 5u) {
				shared = shared && (((masks[y][x] >> 3u) & 1u) == ((masks[y][x + 1u] >> 2u) & 1u));
			}
			if (y + 1u < 5u) {
				shared = shared && (((masks[y][x] >> 2u) & 1u) == ((masks[y + 1u][x] >> 0u) & 1u));
			}
		}
	}
	check(shared, "las esquinas compartidas coinciden entre tiles vecinos");

	check(proc::corner_set(3u, proc::Corner::NorthWest) &&
		      proc::corner_set(3u, proc::Corner::NorthEast) &&
		      !proc::corner_set(3u, proc::Corner::SouthWest),
	      "corner_set lee los bits de la mascara");
}

// ---------------------------------------------------------------------------
//  2. Pintor duro (marching squares) + continuidad de la junta
// ---------------------------------------------------------------------------

void test_hard() {
	check(!proc::corner_solid<16>(7u, 7u, 0u) && proc::corner_solid<16>(7u, 7u, 15u),
	      "hard: mascaras 0/15 planas");
	check(proc::corner_solid<16>(4u, 4u, 1u) && !proc::corner_solid<16>(8u, 4u, 1u) &&
		      !proc::corner_solid<16>(4u, 8u, 1u),
	      "hard: esquina NW = cuarto noroeste");
	check(proc::corner_solid<16>(4u, 4u, 3u) && !proc::corner_solid<16>(4u, 8u, 3u),
	      "hard: mitad superior");
	check(proc::corner_solid<16>(12u, 4u, 6u) && proc::corner_solid<16>(4u, 12u, 6u) &&
		      !proc::corner_solid<16>(4u, 4u, 6u) && !proc::corner_solid<16>(12u, 12u, 6u),
	      "hard: silla NE+SW por cuadrantes (sin banda diagonal)");
	check(proc::corner_solid<16>(4u, 4u, 9u) && proc::corner_solid<16>(12u, 12u, 9u) &&
		      !proc::corner_solid<16>(12u, 4u, 9u),
	      "hard: silla NW+SE por cuadrantes");
	check(!proc::corner_solid<16>(12u, 12u, 7u) && proc::corner_solid<16>(12u, 4u, 7u),
	      "hard: tres esquinas (falta SE)");

	// La junta casa para TODAS las parejas compatibles: la última columna del
	// tile izquierdo clasifica igual que la primera del derecho.
	bool seam = true;
	for (u8 ma = 0u; ma < 16u; ++ma) {
		for (u8 mb = 0u; mb < 16u; ++mb) {
			if (!corners_compatible(ma, mb)) continue;
			for (u16 y = 0u; y < 16u; ++y) {
				seam = seam && (proc::corner_solid<16>(15u, y, ma) ==
						proc::corner_solid<16>(0u, y, mb));
			}
		}
	}
	check(seam, "hard: la junta casa en todas las mascaras compatibles");

	proc::BitmapBuffer<16, 16> tile {};
	proc::paint_corner_hard(tile, 12u, proc::CornerStyle {8u, 4u, 6u});
	check(tile.at(0, 8) == 4u && tile.at(0, 7) == 8u, "paint_corner_hard: mitad inferior en B");
}

// ---------------------------------------------------------------------------
//  3. Pintor orgánico (campo + ruido) + continuidad de la junta
// ---------------------------------------------------------------------------

void test_organic() {
	const proc::CornerStyle style {8u, 4u, 6u};
	const proc::CornerNoise noise {0x1234u, 88u, 1u};

	proc::CornerTileset<16> set_a {};
	proc::CornerTileset<16> set_b {};
	set_a.generate_organic(style, noise);
	set_b.generate_organic(style, noise);
	bool same = true;
	for (u8 m = 0u; m < 16u; ++m) {
		same = same && std::memcmp(set_a.get(m).pixels, set_b.get(m).pixels, 16u * 16u) == 0;
	}
	check(same, "organico: determinista (misma semilla = mismo set)");
	// Los extremos quedan planos (el ruido no alcanza a cruzar el umbral: ±44 < 96).
	bool flat0 = true, flat15 = true, in_range = true;
	for (u16 i = 0u; i < 16u * 16u; ++i) {
		flat0 = flat0 && set_a.get(0u).pixels[i] == 8u;
		flat15 = flat15 && set_a.get(15u).pixels[i] == 4u;
		const u8 v = set_a.get(3u).pixels[i];
		in_range = in_range && (v == 8u || v == 4u || v == 6u);
	}
	check(flat0 && flat15, "organico: mascaras 0/15 planas (A y B)");
	check(in_range, "organico: solo los colores A/B/orilla");
	bool foam = false;
	for (u16 x = 0u; x < 16u; ++x) {
		if (set_a.get(3u).at(x, 8u) == 6u) foam = true; // borde horizontal por el centro
	}
	check(foam, "organico: la orilla aparece en la transicion");

	// Continuidad en la junta (muestra virtual x=N del tile izquierdo == x=0 del
	// derecho): el campo depende solo de las esquinas y el ruido es periódico.
	bool seam = true;
	for (u8 ma = 0u; ma < 16u; ++ma) {
		for (u8 mb = 0u; mb < 16u; ++mb) {
			if (!corners_compatible(ma, mb)) continue;
			for (u16 y = 0u; y <= 16u; ++y) {
				seam = seam && (proc::corner_classify<16>(16u, y, ma, noise) ==
						proc::corner_classify<16>(0u, y, mb, noise));
			}
		}
	}
	check(seam, "organico: la junta casa en todas las mascaras compatibles");
}

// ---------------------------------------------------------------------------
//  4. Materiales del atlas sobre el campo de esquinas
// ---------------------------------------------------------------------------

void test_materials() {
	proc::TextureAtlas<16, 16, MF> atlas {};
	atlas.generate(0x7E5A1u);
	const proc::CornerMaterials mats {proc::MaterialId::Grass, proc::MaterialId::Water,
					  proc::MaterialId::Foam};
	const proc::CornerNoise noise {0x51D3u, 88u, 1u};

	proc::BitmapBuffer<16, 16> tile {};
	proc::paint_corner_materials(tile, 15u, atlas, mats, noise);
	bool all_water = true;
	const proc::BitmapBuffer<16, 16>& water = atlas.get(proc::MaterialId::Water, 0u);
	for (u16 i = 0u; i < 16u * 16u; ++i) all_water = all_water && tile.pixels[i] == water.pixels[i];
	check(all_water, "materiales: mascara 15 = agua del atlas (variante 0, tileable)");

	proc::paint_corner_materials(tile, 0u, atlas, mats, noise);
	bool all_grass = true;
	const proc::BitmapBuffer<16, 16>& grass = atlas.get(proc::MaterialId::Grass, 0u);
	for (u16 i = 0u; i < 16u * 16u; ++i) all_grass = all_grass && tile.pixels[i] == grass.pixels[i];
	check(all_grass, "materiales: mascara 0 = hierba del atlas");
}

// ---------------------------------------------------------------------------
//  5. Stamps (disco ruidoso, río) y composición del mapa
// ---------------------------------------------------------------------------

void test_stamps() {
	// Lago: contorno ruidoso pero CONTIGUO y sin motas aisladas.
	proc::OccupancyMap<12, 9> occ {};
	occ.fill(0u);
	proc::stamp_disc(occ, 5, 4, 4u, 3u, 1u, 0xD15Cu);
	check(occ.at(5, 4) == 1u && occ.at(4, 4) == 1u && occ.at(5, 3) == 1u,
	      "stamp_disc: nucleo dentro");
	check(occ.at(0, 0) == 0u && occ.at(11, 8) == 0u && occ.at(11, 0) == 0u,
	      "stamp_disc: esquinas fuera");
	u16 water = 0u;
	for (u16 i = 0u; i < 12u * 9u; ++i) {
		if (occ.cells[i] != 0u) ++water;
	}
	bool no_speck = true;
	for (u16 y = 0u; y < 9u; ++y) {
		for (u16 x = 0u; x < 12u; ++x) {
			if (occ.at(x, y) == 0u) continue;
			const bool near = (x > 0u && occ.at(u16(x - 1u), y) != 0u) ||
					  (x + 1u < 12u && occ.at(u16(x + 1u), y) != 0u) ||
					  (y > 0u && occ.at(x, u16(y - 1u)) != 0u) ||
					  (y + 1u < 9u && occ.at(x, u16(y + 1u)) != 0u);
			no_speck = no_speck && near;
		}
	}
	check(water >= 25u, "stamp_disc: el lago tiene area");
	check(no_speck, "stamp_disc: sin celdas de agua aisladas (contiguo)");

	// Río N→S: 1..3 columnas de agua por fila, contiguas, y cruza el mapa entero.
	proc::OccupancyMap<12, 9> riv {};
	proc::stamp_river(riv, 4u, 2u, 0x2117u, true);
	bool rows_ok = true, full_span = true;
	for (u16 y = 0u; y < 9u; ++y) {
		u16 first = 12u, last = 0u, cnt = 0u;
		for (u16 x = 0u; x < 12u; ++x) {
			if (riv.at(x, y) == 0u) continue;
			++cnt;
			if (first == 12u) first = x;
			last = x;
		}
		rows_ok = rows_ok && cnt >= 2u && cnt <= 3u;
		full_span = full_span && (u16(last - first + 1u) == cnt); // banda contigua
	}
	check(rows_ok, "stamp_river: 2-3 celdas de agua por fila");
	check(full_span, "stamp_river: la banda es contigua en cada fila");
	bool exits = false;
	for (u16 x = 0u; x < 12u; ++x) {
		if (riv.at(x, 0u) != 0u && riv.at(x, 8u) != 0u) exits = true;
	}
	check(exits, "stamp_river: entra y sale del mapa (N y S)");

	// Composición del mapa por máscaras (occupancy 8×6 → 7×5 tiles de 16 → 112×80).
	proc::OccupancyMap<8, 6> o2 {};
	o2.fill(0u);
	proc::stamp_disc(o2, 4, 2, 2u, 2u, 1u, 0x1A4Eu);
	proc::CornerTileset<16> set {};
	set.generate_organic(proc::CornerStyle {8u, 4u, 6u}, proc::CornerNoise {0x51D3u, 88u, 1u});
	proc::BitmapBuffer<112, 80> map {};
	set.compose(o2, map);
	bool pal = true, foam_map = false;
	for (u16 i = 0u; i < 112u * 80u; ++i) {
		const u8 v = map.pixels[i];
		pal = pal && (v == 8u || v == 4u || v == 6u);
		if (v == 6u) foam_map = true;
	}
	check(pal, "compose: todo el mapa usa los colores del estilo");
	check(foam_map, "compose: el mapa lleva orilla (transiciones cerradas)");
	// El centro del disco (celda (4,2) → tile (3,1)) es agua con las 4 esquinas
	// dentro del núcleo: la máscara 15 pinta B en todo el tile.
	check(map.at(3u * 16u + 8u, 1u * 16u + 8u) == 4u, "compose: el centro del lago es agua");
	check(map.at(0u, 0u) == 8u, "compose: la esquina lejana es hierba");
}

// ---------------------------------------------------------------------------
//  6. Mapa por capas: transparencia, actor entre capas y colisión
// ---------------------------------------------------------------------------

/// El mapa de prueba de las capas: 4×3 tiles de 16 px (64×48).
using LayerMap = proc::LayeredTileMap<4, 3, 16>;

void test_layers() {
	LayerMap map {};
	proc::BitmapBuffer<16, 16> ground_tiles[2] {};
	proc::fill(ground_tiles[0], [](u16, u16) { return 9u; });  // hierba
	proc::fill(ground_tiles[1], [](u16, u16) { return 4u; });  // agua
	proc::BitmapBuffer<16, 16> decal_tiles[1] {};              // flor: 1 px central
	decal_tiles[0].at(8u, 8u) = 2u;
	proc::BitmapBuffer<16, 16> over_tiles[1] {}; // copa: disco opaco con fondo 0
	for (u16 y = 0u; y < 16u; ++y) {
		for (u16 x = 0u; x < 16u; ++x) {
			const s16 dx = s16(x) - 8;
			const s16 dy = s16(y) - 8;
			if (dx * dx + dy * dy <= 36) over_tiles[0].at(x, y) = 28u;
		}
	}
	map.set_cell(proc::MapLayer::Ground, 1u, 1u, 1u); // charca de un tile
	map.set_cell(proc::MapLayer::Decal, 1u, 0u, 0u);  // flor sobre la hierba
	map.set_cell(proc::MapLayer::Overhead, 2u, 1u, 0u); // copa

	auto ground_of = [&](u16 i) -> const proc::BitmapBuffer<16, 16>& { return ground_tiles[i]; };
	auto decal_of = [&](u16) -> const proc::BitmapBuffer<16, 16>& { return decal_tiles[0]; };
	auto over_of = [&](u16) -> const proc::BitmapBuffer<16, 16>& { return over_tiles[0]; };

	proc::BitmapBuffer<64, 48> screen {};
	// Actor (BLANCO 31, 12×10 px) que solapa con la copa: x 32..43 en la celda de
	// la copa (x 32..47); el disco opaco ocupa x 34..45 en su banda central.
	auto actor = [](proc::BitmapBuffer<64, 48>& buf) {
		for (u16 y = 22u; y < 32u; ++y) {
			for (u16 x = 32u; x < 44u; ++x) buf.at(x, y) = 31u;
		}
	};
	proc::render_layered_map(map, screen, ground_of, decal_of, over_of, actor);

	// La variante `Span`+stride (framebuffer de runtime, la que usa la demo 121) compone
	// EXACTAMENTE igual que la de `BitmapBuffer` (mismo orden y transparencia).
	proc::BitmapBuffer<64, 48> screen_span {};
	auto actor_span = [](eng::Span<u8> buf) {
		for (u16 y = 22u; y < 32u; ++y) {
			for (u16 x = 32u; x < 44u; ++x) buf[eng::u32(y) * 64u + x] = 31u;
		}
	};
	proc::render_layered_map(map, eng::Span<u8> {screen_span.pixels, sizeof(screen_span.pixels)},
				 64u, ground_of, decal_of, over_of, actor_span);
	check(std::memcmp(screen.pixels, screen_span.pixels, sizeof(screen.pixels)) == 0,
	      "capas: la variante Span+stride compone igual que BitmapBuffer");

	check(screen.at(8u, 40u) == 9u, "capas: el suelo se pinta (hierba)");
	check(screen.at(16u, 40u) == 9u, "capas: sin decal/copa el suelo manda");
	check(screen.at(24u, 8u) == 2u, "capas: el decal no cero se pinta (flor)");
	check(screen.at(24u, 9u) == 9u, "capas: el pixel 0 del decal es transparente");
	check(screen.at(20u, 20u) == 4u, "capas: la charca (ground) se pinta");
	// El actor está ENTRE decal y overhead: visible al descubierto…
	check(screen.at(33u, 22u) == 31u, "capas: el actor se ve fuera de la copa");
	// …pero bajo el disco opaco de la copa manda la copa.
	check(screen.at(40u, 27u) == 28u, "capas: la copa tapa al actor (pasa por debajo)");
	check(screen.at(2u * 16u + 8u, 1u * 16u + 1u) == 9u, "capas: el fondo de la copa es transparente");

	// El mapa por capas se envuelve como capa del field sin conversión.
	eng::playfield::TileLayerMap tl {};
	tl.cells = map.span(proc::MapLayer::Ground);
	tl.width = 4u;
	tl.height = 3u;
	tl.wrap_x = 0u;
	tl.edge_tile = proc::kEmptyTile;
	check(tl.tile_at(1, 1) == 1u && tl.tile_at(0, 0) == 0u,
	      "capas: span() encaja con TileLayerMap del field");
}

void test_collision() {
	using CellFlags = proc::CellFlags;
	check(proc::has(CellFlags::Solid | CellFlags::Water, CellFlags::Water) &&
		      !proc::has(CellFlags::Solid | CellFlags::Water, CellFlags::Slow),
	      "CellFlags: union y consulta de bits");
	check(((CellFlags::Solid | CellFlags::Trigger) & CellFlags::Solid) == CellFlags::Solid,
	      "CellFlags: interseccion");

	LayerMap m {}; // 4×3 tiles de 16 → 64×48 px
	m.set_cell(proc::MapLayer::Ground, 2u, 0u, 1u); // muro
	m.set_cell(proc::MapLayer::Ground, 2u, 1u, 1u);
	m.set_cell(proc::MapLayer::Ground, 0u, 0u, 2u); // agua
	m.bake_flags([](u16 g) {
		if (g == 1u) return CellFlags::Solid;
		if (g == 2u) return CellFlags::Water;
		return CellFlags::None;
	});
	check(proc::has(m.flags_at(2u, 0u), CellFlags::Solid), "bake_flags: la muralla es solida");
	check(!proc::has(m.flags_at(1u, 0u), CellFlags::Solid), "bake_flags: la hierba no bloquea");
	check(proc::has(m.flags_at(4u, 0u), CellFlags::Solid), "flags_at: fuera del mapa = outside");
	check(proc::has(m.flags_at_pixel(40, 8), CellFlags::Solid) &&
		      proc::has(m.flags_at_pixel(5, 5), CellFlags::Water) &&
		      !proc::has(m.flags_at_pixel(20, 5), CellFlags::Solid),
	      "flags_at_pixel: celda que contiene el punto");
	check(proc::has(m.flags_at_pixel(-1, 5), CellFlags::Solid) &&
		      proc::has(m.flags_at_pixel(5, 99), CellFlags::Solid),
	      "flags_at_pixel: fuera del mapa = outside");

	// AABB: la hitbox cubre varias celdas (OR de banderas).
	check(!m.blocked(20, 0, 10, 6), "blocked: caja libre");
	check(m.blocked(30, 0, 10, 6), "blocked: caja que toca la muralla");
	check(m.blocked(20, 0, 20, 6), "blocked: caja ancha que alcanza la muralla");

	// Movimiento eje a eje: choca en X y sigue en Y (deslizamiento).
	proc::MapBody body {20, 10, 10, 6};
	proc::move_and_slide(m, body, 8, 0);
	check(body.x == 20, "move: X bloqueada revierte");
	proc::move_and_slide(m, body, 0, 8);
	check(body.y == 18, "move: Y libre avanza");
	proc::MapBody diag {20, 20, 10, 6};
	proc::move_and_slide(m, diag, 8, -8);
	check(diag.x == 20 && diag.y == 12, "move: bloqueo en X no impide el avance en Y");
	proc::MapBody edge {0, 0, 10, 6};
	proc::move_and_slide(m, edge, -2, 0);
	check(edge.x == 0, "move: el borde del mundo bloquea (outside = Solid)");

	check(m.cell(proc::MapLayer::Ground, 2u, 0u) == 1u, "cell() lee la capa pedida");
}

// ---------------------------------------------------------------------------
//  7. Cocido del mapa por capas a tileset indexado (dedup)
// ---------------------------------------------------------------------------

void test_bake() {
	proc::LayeredTileMap<3, 2, 16> bm {};
	proc::BitmapBuffer<16, 16> g[2] {};
	proc::fill(g[0], [](u16, u16) { return 9u; });  // hierba
	proc::fill(g[1], [](u16, u16) { return 4u; });  // agua
	proc::BitmapBuffer<16, 16> d0 {};               // decal: 1 px de color 2
	d0.at(8u, 8u) = 2u;
	proc::BitmapBuffer<16, 16> c0 {};               // overhead: 1 px de color 28
	c0.at(3u, 3u) = 28u;
	bm.set_cell(proc::MapLayer::Ground, 1u, 1u, 1u); // charca en (1,1)
	bm.set_cell(proc::MapLayer::Decal, 0u, 0u, 0u);  // flor en (0,0)
	bm.set_cell(proc::MapLayer::Overhead, 2u, 1u, 0u); // copa en (2,1)

	auto gout = [&](u16 i) -> const proc::BitmapBuffer<16, 16>& { return g[i]; };
	auto dout = [&](u16) -> const proc::BitmapBuffer<16, 16>& { return d0; };
	auto oout = [&](u16) -> const proc::BitmapBuffer<16, 16>& { return c0; };
	const auto baked = proc::bake_layered_map<3, 2, 16, 16>(bm, gout, dout, oout);
	check(baked.count == 4u && baked.overflow == 0u,
	      "bake: 4 celdas distintas -> 4 piezas unicas");
	check(baked.cells[0u] == 0u && baked.cells[1u] == 1u && baked.cells[2u] == 1u &&
		      baked.cells[3u] == 1u && baked.cells[4u] == 2u && baked.cells[5u] == 3u,
	      "bake: dedup reasigna celdas (primera aparicion = pieza nueva)");
	check(baked.tiles[0u][8u * 16u + 8u] == 2u && baked.tiles[1u][8u * 16u + 8u] == 9u,
	      "bake: la pieza con decal conserva el pixel de flor");
	check(baked.tiles[2u][0u] == 4u && baked.tiles[3u][3u * 16u + 3u] == 28u,
	      "bake: charca y copa en sus piezas");
	check(baked.bank_view().size() == eng::usize(4u) * 256u, "bake: banco contiguo 4x256");
	// Equivalencia: el cocido es exactamente lo que compone `render_layered_map`.
	proc::BitmapBuffer<48, 32> full {};
	proc::render_layered_map(bm, full, gout, dout, oout);
	bool same = true;
	for (u16 cy = 0u; cy < 2u; ++cy) {
		for (u16 cx = 0u; cx < 3u; ++cx) {
			const eng::u8* t = baked.tiles[baked.cells[cy * 3u + cx]];
			for (u16 ly = 0u; ly < 16u; ++ly) {
				for (u16 lx = 0u; lx < 16u; ++lx) {
					same = same && full.at(u16(cx * 16u + lx), u16(cy * 16u + ly)) ==
							    t[ly * 16u + lx];
				}
			}
		}
	}
	check(same, "bake: pixel a pixel igual al render por capas");
}

// ---------------------------------------------------------------------------
//  8. Estructuras multi-tile: casa grande 2×2 y árbol grande 2×2
// ---------------------------------------------------------------------------

/// ¿El índice está en el tramo de paleta de un material?
bool in_tone(u8 v, proc::MaterialId id) {
	const proc::MaterialDesc d = proc::material_desc(id);
	return v >= d.base && v <= u8(d.base + d.levels - 1u);
}

/// Ensambla las 4 piezas de una estructura 2×2 en un buffer de 32×32.
template <class S>
void assemble_struct(proc::BitmapBuffer<32, 32>& out, const terr::TileSpec (&parts)[4],
		     const proc::TextureAtlas<16, 16, S>& atlas) {
	for (u8 p = 0u; p < 4u; ++p) {
		proc::BitmapBuffer<16, 16> t {};
		terr::paint_terrain_tile(t, parts[p], atlas, u16(p), 0u, 6u);
		proc::blit_tile(out, t, u16((p & 1u) * 16u), u16((p >> 1u) * 16u), false);
	}
}

void test_structs() {
	proc::TextureAtlas<16, 16, MF> atlas {};
	atlas.generate(0x7E5A1u);
	const eng::u32 kSeed = 0x5717u;
	// Casa grande A (puerta en SW) y B (puerta en SE): mismo diseño, puerta espejada.
	terr::TileSpec house_a[4] = {
		{.features = terr::Feature::HouseA_NW, .seed = kSeed},
		{.features = terr::Feature::HouseA_NE, .seed = kSeed},
		{.features = terr::Feature::HouseA_SW, .seed = kSeed},
		{.features = terr::Feature::HouseA_SE, .seed = kSeed},
	};
	terr::TileSpec house_b[4] = {
		{.features = terr::Feature::HouseB_NW, .seed = kSeed},
		{.features = terr::Feature::HouseB_NE, .seed = kSeed},
		{.features = terr::Feature::HouseB_SW, .seed = kSeed},
		{.features = terr::Feature::HouseB_SE, .seed = kSeed},
	};
	proc::BitmapBuffer<32, 32> ha {};
	proc::BitmapBuffer<32, 32> hb {};
	assemble_struct(ha, house_a, atlas);
	assemble_struct(hb, house_b, atlas);
	// Tejado arriba (tramo de `Roof`) y fuera del contorno queda la base (hierba).
	check(in_tone(ha.at(8u, 3u), proc::MaterialId::Roof) &&
		      in_tone(ha.at(24u, 3u), proc::MaterialId::Roof),
	      "casa 2x2: el tejado cubre el ancho (ambos cuadrantes de arriba)");
	check(in_tone(ha.at(0u, 3u), proc::MaterialId::Grass) &&
		      in_tone(ha.at(31u, 8u), proc::MaterialId::Grass),
	      "casa 2x2: fuera del contorno queda el material base");
	// Puertas: A abajo a la izquierda, B a la derecha; la otra mitad es pared.
	check(in_tone(ha.at(7u, 28u), proc::MaterialId::Path) &&
		      in_tone(ha.at(24u, 28u), proc::MaterialId::Wood),
	      "casa A: puerta a la izquierda y pared a la derecha");
	check(in_tone(hb.at(7u, 28u), proc::MaterialId::Wood) &&
		      in_tone(hb.at(24u, 28u), proc::MaterialId::Path),
	      "casa B: puerta espejada a la derecha");
	// Ventanas de mármol en ambos lados de la planta baja.
	check(in_tone(ha.at(6u, 19u), proc::MaterialId::Marble) &&
		      in_tone(ha.at(25u, 19u), proc::MaterialId::Marble),
	      "casa 2x2: ventanas en los dos cuadrantes inferiores");
	// El diseño casa entre cuadrantes: la fila del tejado es continua.
	bool roof_cont = true;
	for (u16 gx = 2u; gx <= 29u; ++gx) {
		roof_cont = roof_cont && in_tone(ha.at(gx, 10u), proc::MaterialId::Roof);
	}
	check(roof_cont, "casa 2x2: el tejado es continuo en la costura de cuadrantes");

	// Árbol grande: copa continua entre cuadrantes y tronco centrado abajo.
	terr::TileSpec tree[4] = {
		{.features = terr::Feature::TreeBig_NW, .seed = kSeed},
		{.features = terr::Feature::TreeBig_NE, .seed = kSeed},
		{.features = terr::Feature::TreeBig_SW, .seed = kSeed},
		{.features = terr::Feature::TreeBig_SE, .seed = kSeed},
	};
	proc::BitmapBuffer<32, 32> tr {};
	assemble_struct(tr, tree, atlas);
	check(in_tone(tr.at(16u, 13u), proc::MaterialId::Foliage) &&
		      in_tone(tr.at(15u, 13u), proc::MaterialId::Foliage),
	      "arbol 2x2: copa continua en la costura vertical");
	check(in_tone(tr.at(15u, 30u), proc::MaterialId::Trunk) &&
		      in_tone(tr.at(16u, 30u), proc::MaterialId::Trunk),
	      "arbol 2x2: tronco en las columnas centrales (SW+SE)");
	check(in_tone(tr.at(1u, 1u), proc::MaterialId::Grass),
	      "arbol 2x2: la esquina lejana queda en hierba (copa irregular)");
}

// ---------------------------------------------------------------------------
//  Evidencia visual opcional: PPM de sets y mundos
// ---------------------------------------------------------------------------

void write_ppm(const char* path, const u8* pixels, u16 w, u16 h, const eng::Palette32& pal) {
	std::FILE* f = std::fopen(path, "wb");
	if (f == nullptr) return;
	std::fprintf(f, "P6\n%u %u\n255\n", unsigned(w), unsigned(h));
	// Índice u32: con u16 el bucle no termina cuando la imagen supera 65535 píxeles.
	const eng::u32 count = eng::u32(w) * h;
	for (eng::u32 i = 0u; i < count; ++i) {
		const eng::u16 c = pal.color[pixels[i] & 31u];
		const u8 rgb[3] = {u8(((c >> 8u) & 0xFu) * 17u), u8(((c >> 4u) & 0xFu) * 17u),
				   u8((c & 0xFu) * 17u)};
		std::fwrite(rgb, 1u, 3u, f);
	}
	std::fclose(f);
}

void dump_visual_evidence(const char* dir) {
	char path[512];
	proc::TextureAtlas<32, 32, MF> atlas {};
	atlas.generate(0x7E5A1u);
	const proc::CornerMaterials mats {proc::MaterialId::Grass, proc::MaterialId::Water,
					  proc::MaterialId::Foam};
	const proc::CornerNoise noise {0x51D3u, 88u, 1u};

	// 1) Las 16 transiciones con materiales (4×4 → 128×128): orillas cerradas.
	proc::BitmapBuffer<32, 32> tiles[16] {};
	for (u8 m = 0u; m < 16u; ++m) {
		proc::paint_corner_materials(tiles[m], m, atlas, mats, noise);
	}
	proc::BitmapBuffer<128, 128> sheet {};
	for (u8 m = 0u; m < 16u; ++m) {
		const u16 tx = u16((m & 3u) * 32u);
		const u16 ty = u16((m >> 2u) * 32u);
		proc::blit_tile(sheet, tiles[m], tx, ty, false);
	}
	std::snprintf(path, sizeof(path), "%s/corner_set.ppm", dir);
	write_ppm(path, sheet.pixels, 128u, 128u, atlas.palette);

	// 2) Mundo: río N→S + lago con isla (15×9 tiles de 32 → 480×288).
	proc::OccupancyMap<16, 10> occ {};
	occ.fill(0u);
	proc::stamp_river(occ, 3u, 2u, 0x2117u, true);
	proc::stamp_disc(occ, 10, 5, 3u, 2u, 1u, 0xD15Cu);
	proc::stamp_disc(occ, 10, 5, 1u, 1u, 0u, 0x15A0u);
	proc::BitmapBuffer<15u * 32u, 9u * 32u> world {};
	for (u16 ty = 0u; ty < 9u; ++ty) {
		for (u16 tx = 0u; tx < 15u; ++tx) {
			const u8 m = proc::corner_mask(occ, tx, ty);
			proc::blit_tile(world, tiles[m], u16(tx * 32u), u16(ty * 32u), false);
		}
	}
	std::snprintf(path, sizeof(path), "%s/corner_world.ppm", dir);
	write_ppm(path, world.pixels, 15u * 32u, 9u * 32u, atlas.palette);

	// 3) Mapa por capas con actor bajo la copa (6×4 tiles de 32 → 192×128).
	proc::LayeredTileMap<6, 4, 32> lmap {};
	proc::BitmapBuffer<32, 32> g {};
	proc::blit_tile(g, atlas.get(proc::MaterialId::Grass, 0u), 0u, 0u, false);
	proc::BitmapBuffer<32, 32> d {};
	proc::fill_masked(d, [&](u16 x, u16 y) { return atlas.get(proc::MaterialId::Foam, 0u).at(x, y); },
			  [](u16 x, u16 y) {
				  const s16 dx = s16(x) - 8;
				  const s16 dy = s16(y) - 24;
				  return dx * dx + dy * dy <= 16;
			  });
	proc::BitmapBuffer<32, 32> o {};
	proc::fill_masked(o,
			  [&](u16 x, u16 y) { return atlas.get(proc::MaterialId::Foliage, 0u).at(x, y); },
			  [](u16 x, u16 y) {
				  const s16 dx = s16(x) - 16;
				  const s16 dy = s16(y) - 14;
				  return dx * dx + dy * dy <= 121;
			  });
	// Relleno de capas: suelo de hierba en todas las celdas, y copas/flores en un
	// patrón disperso (el actor se dibuja entre decal y overhead).
	for (u16 ty = 0u; ty < 4u; ++ty) {
		for (u16 tx = 0u; tx < 6u; ++tx) {
			lmap.set_cell(proc::MapLayer::Ground, tx, ty, 0u);
			if ((tx + ty) % 3u == 0u) lmap.set_cell(proc::MapLayer::Decal, tx, ty, 0u);
			if ((tx * 2u + ty) % 5u == 0u) lmap.set_cell(proc::MapLayer::Overhead, tx, ty, 0u);
		}
	}
	proc::BitmapBuffer<192, 128> view {};
	auto g_of = [&](u16) -> const proc::BitmapBuffer<32, 32>& { return g; };
	auto d_of = [&](u16) -> const proc::BitmapBuffer<32, 32>& { return d; };
	auto o_of = [&](u16) -> const proc::BitmapBuffer<32, 32>& { return o; };
	auto actor = [&](proc::BitmapBuffer<192, 128>& buf) {
		for (u16 y = 40u; y < 56u; ++y) {
			for (u16 x = 40u; x < 60u; ++x) buf.at(x, y) = 31u;
		}
	};
	proc::render_layered_map(lmap, view, g_of, d_of, o_of, actor);
	std::snprintf(path, sizeof(path), "%s/layered.ppm", dir);
	write_ppm(path, view.pixels, 192u, 128u, atlas.palette);

	std::printf("[dump] corner_set/corner_world/layered en %s\n", dir);
}

} // namespace

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) g_dump_dir = argv[i + 1];
	}
	std::printf("== HOST-448 corner tiles (dual-grid) y mapas por capas ==\n");
	test_masks();
	test_hard();
	test_organic();
	test_materials();
	test_stamps();
	test_layers();
	test_collision();
	test_bake();
	test_structs();
	if (g_fail == 0 && g_dump_dir != nullptr) dump_visual_evidence(g_dump_dir);
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: autotiling por esquinas y capas validados (junta continua, colision por ejes).\n");
	return 0;
}
