// ============================================================================
// Test HOST-442: generación procedural de bitmaps (`eng/graphics/proc/`).
// ============================================================================
//
// Cubre la librería `eng::gfx::proc`: buffer de índices, dither Bayer, perfiles 1D,
// evaluadores, transformaciones Wang, texturas orgánicas tileables, atlas de materiales
// con variantes, capas de material por altura y el generador de terreno (piezas +
// pintor + catálogos + set).
//
// El MISMO código se instancia con DOS escalares (negativo de §1.11): `MiniFloat16` (el
// válido en Amiga para ruido; ver `noise.hpp`) y `float` (host). Los chequeos de
// tileabilidad comparan texturas en px=0 y px=W (deben ser idénticas bit a bit).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/442_proc_bitmaps
//   (opcional) <binario> --dump <dir>   → escribe PPM de atlas/piezas (evidencia visual)

#include <cmath>
#include <cstdio>
#include <cstring>

// `proc/*.hpp` es genérico y NO incluye escalares concretos (§1.11): el test trae los suyos.
#include <eng/core/math/minifloat.hpp>
#include <eng/field/tile_map.hpp> // solo el test: verifica que el mapa colocado encaja con el tipo del field
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/dither.hpp>
#include <eng/graphics/proc/evaluators.hpp>
#include <eng/graphics/proc/profile.hpp>
#include <eng/graphics/proc/shapes.hpp>
#include <eng/graphics/proc/terrain.hpp>
#include <eng/graphics/proc/terrain_place.hpp>
#include <eng/graphics/proc/textures.hpp>
#include <eng/graphics/proc/tiles.hpp>

namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
namespace tx = eng::gfx::proc::tex;
using eng::math::MiniFloat16;
using eng::u16;
using eng::u32;
using eng::u8;

namespace {

int g_fail = 0;
const char* g_dump_dir = nullptr; // `--dump <dir>`: escribe PPM de evidencia visual

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// ---------------------------------------------------------------------------
//  1. Buffer de índices + dither Bayer + cuantización
// ---------------------------------------------------------------------------

void test_buffer_and_dither() {
	proc::BitmapBuffer<8, 4> buf {};
	bool zeroed = true;
	for (u16 i = 0; i < 8u * 4u; ++i) zeroed = zeroed && buf.pixels[i] == 0u;
	check(zeroed, "BitmapBuffer nace a cero");
	buf.at(3, 2) = 7u;
	check(buf.pixels[2u * 8u + 3u] == 7u, "BitmapBuffer::at indexa y*W+x");
	check(buf.at(3, 2) == 7u, "BitmapBuffer::at const lee el mismo píxel");
	check(proc::BitmapBuffer<8, 4>::width == 8u && proc::BitmapBuffer<8, 4>::height == 4u,
	      "BitmapBuffer expone width/height");

	// Las matrices de Bayer son permutaciones de 0..n²-1 (cada umbral, una vez).
	bool perm2 = true, perm4 = true, perm8 = true;
	bool seen2[4] = {}, seen4[16] = {}, seen8[64] = {};
	for (u16 y = 0; y < 2; ++y)
		for (u16 x = 0; x < 2; ++x) seen2[proc::Bayer::b2[y][x]] = true;
	for (u16 y = 0; y < 4; ++y)
		for (u16 x = 0; x < 4; ++x) seen4[proc::Bayer::b4[y][x]] = true;
	for (u16 y = 0; y < 8; ++y)
		for (u16 x = 0; x < 8; ++x) seen8[proc::Bayer::b8[y][x]] = true;
	for (bool s : seen2) perm2 = perm2 && s;
	for (bool s : seen4) perm4 = perm4 && s;
	for (bool s : seen8) perm8 = perm8 && s;
	check(perm2 && perm4 && perm8, "Bayer 2/4/8 son permutaciones de 0..n^2-1");

	// Umbral normalizado: 0 en la celda mínima, <=251 siempre (extremo «opaco» intacto).
	check(proc::Bayer::threshold8(0, 0) == 0u, "threshold8(0,0) == 0");
	bool thr_ok = true;
	for (u16 y = 0; y < 8; ++y)
		for (u16 x = 0; x < 8; ++x) {
			const u8 t = proc::Bayer::threshold8(x, y);
			thr_ok = thr_ok && t <= 251u;
		}
	check(thr_ok, "threshold8 <= 251 (rango de dither)");

	// Cuantización plana: extremos exactos y rango.
	check(proc::quantize_plain(0u, 16u) == 0u, "quantize_plain(0) == 0");
	check(proc::quantize_plain(255u, 16u) == 15u, "quantize_plain(255) == levels-1");
	bool q_range = true;
	for (u16 i = 0; i <= 255u; ++i) q_range = q_range && proc::quantize_plain(u8(i), 12u) < 12u;
	check(q_range, "quantize_plain en rango 0..levels-1");

	// Dither: extremos estables y rango con dither (el fallo clásico: I=0 subía a >0).
	check(proc::quantize_dither(0u, 3u, 5u, 16u) == 0u, "quantize_dither(0) == 0");
	check(proc::quantize_dither(255u, 3u, 5u, 16u) == 15u, "quantize_dither(255) == 15");
	bool d_range = true, d_varies = false;
	for (u16 y = 0; y < 8; ++y)
		for (u16 x = 0; x < 8; ++x) {
			const u8 q = proc::quantize_dither(128u, x, y, 16u);
			d_range = d_range && q < 16u;
			if (q != 8u) d_varies = true;
		}
	check(d_range, "quantize_dither en rango 0..levels-1");
	check(d_varies, "quantize_dither dithera alrededor del nivel medio (+-1)");

	// Halftone: el tablero alterna a/b en bloques de 2×2 (mezcla 50 % aparente) y la
	// escala de 2·levels−1 pasos intercala los intermedios entre los sólidos.
	check(proc::halftone2x2(1u, 2u, 0u, 0u) == 1u && proc::halftone2x2(1u, 2u, 1u, 1u) == 1u &&
		      proc::halftone2x2(1u, 2u, 2u, 0u) == 2u && proc::halftone2x2(1u, 2u, 2u, 2u) == 1u,
	      "halftone2x2 alterna en bloques 2x2");
	check(proc::halftone_index(0u, 8u, 4u, 0u, 0u) == 8u, "halftone_index: paso 0 = base solida");
	check(proc::halftone_index(6u, 8u, 4u, 0u, 0u) == 11u, "halftone_index: paso maximo = top");
	const u8 hs = proc::halftone_index(1u, 8u, 4u, 0u, 0u);
	check(hs == 8u || hs == 9u, "halftone_index: paso impar = tablero del par (8|9)");
}

// ---------------------------------------------------------------------------
//  2. Perfiles 1D
// ---------------------------------------------------------------------------

void test_profile() {
	constexpr auto hill = proc::Profile1D<64>::hill(20u, 40u);
	check(hill.y[0] == 40u && hill.y[64] == 40u, "hill: base en ambos extremos");
	check(hill.y[32] == 60u, "hill: pico = base+amp en el centro");
	bool sym = true, mono = true;
	for (u16 d = 0; d <= 32u; ++d) {
		sym = sym && hill.y[d] == hill.y[64u - d];
		if (d > 0u) mono = mono && hill.y[d] >= hill.y[d - 1u];
	}
	check(sym, "hill: simetria izquierda/derecha");
	check(mono, "hill: subida monotona hasta el centro");

	constexpr auto valley = proc::Profile1D<48>::valley(16u, 32u);
	check(valley.y[24] == 32u, "valley: minimo = base en el centro");
	check(valley.y[0] == 48u && valley.y[48] == 48u, "valley: extremos altos");
}

// ---------------------------------------------------------------------------
//  3. Evaluadores + fill
// ---------------------------------------------------------------------------

void test_evaluators() {
	proc::CircleFill disc {};
	disc.cx = 16;
	disc.cy = 16;
	disc.r2 = 16u; // radio 4
	disc.color_in = 5u;
	disc.color_out = 2u;
	check(disc(16, 16) == 5u, "CircleFill: centro dentro");
	check(disc(21, 16) == 2u, "CircleFill: fuera del radio");
	check(disc(18, 18) == 5u, "CircleFill: distancia^2 dentro (18,18)");

	proc::BitmapBuffer<8, 8> buf {};
	proc::fill(buf, [](u16 x, u16 y) { return u8(x + y); });
	bool indexed = true;
	for (u16 y = 0; y < 8; ++y)
		for (u16 x = 0; x < 8; ++x) indexed = indexed && buf.at(x, y) == u8(x + y);
	check(indexed, "fill evalua fn(x,y) por pixel");

	proc::BitmapBuffer<8, 8> masked {};
	proc::fill_masked(masked, [](u16, u16) { return 9u; },
			  [](u16 x, u16 y) { return ((x + y) & 1u) != 0u; });
	bool mask_ok = true;
	for (u16 y = 0; y < 8; ++y)
		for (u16 x = 0; x < 8; ++x)
			mask_ok = mask_ok && masked.at(x, y) == (((x + y) & 1u) != 0u ? 9u : 0u);
	check(mask_ok, "fill_masked deja 0 (transparente) donde la mascara es 0");

	// Hill2D: perfil constante alto -> nivel maximo; Layer elige A o B por mascara.
	proc::Hill2D<4> hill2d {};
	hill2d.px = proc::Profile1D<4>::hill(255u, 0u);
	hill2d.py = proc::Profile1D<4>::hill(255u, 0u);
	hill2d.levels = 16u;
	check(hill2d(2, 2) == 15u, "Hill2D: pico (2,2) -> levels-1");
	check(hill2d(0, 0) == 0u, "Hill2D: valle (0,0) -> 0");

	proc::Layer<proc::CircleFill, proc::CircleFill, proc::CircleFill> layer {};
	layer.a.color_in = 7u;
	layer.b.color_in = 3u;
	layer.mask.r2 = 4u; // mascara: disco pequeño en (0,0)
	layer.a.cx = layer.a.cy = 0u;
	layer.b.cx = layer.b.cy = 0u;
	layer.mask.cx = layer.mask.cy = 0u;
	check(layer(0, 0) == 7u, "Layer: mascara activa elige la capa A");
}

// ---------------------------------------------------------------------------
//  4. FbmFill: determinismo y tileabilidad con dos escalares
// ---------------------------------------------------------------------------

template <class S>
void test_fbm_fill_scalar(const char* name) {
	const auto scale = proc::tile_scale<S>(32u, 2u); // 2 ciclos por tile de 32 px
	proc::FbmFill<S> fill {};
	fill.seed = 0x1234u;
	fill.levels = 16u;
	fill.octaves = 3u;
	fill.dither = true;
	fill.period = 2;
	fill.inv_size = scale;
	const u8 a = fill(3u, 7u);
	const u8 b = fill(3u, 7u);
	check(a == b, name);
	check(a < 16u, name);
	// Tileable: px=0 y px=32 (un periodo exacto) dan el mismo indice.
	bool tile_ok = true;
	for (u16 y = 0; y < 8u; ++y) tile_ok = tile_ok && fill(0u, y) == fill(32u, y);
	check(tile_ok, name);
}

void test_fbm_fill() {
	test_fbm_fill_scalar<MiniFloat16>("FbmFill determinista (MiniFloat16)");
	test_fbm_fill_scalar<float>("FbmFill determinista (float)");
	// El test negativo de §1.11: la cabecera se instancia con dos escalares distintos.
	check(proc::FbmFill<MiniFloat16> {}.levels == 16u && proc::FbmFill<float> {}.levels == 16u,
	      "FbmFill instanciado con MiniFloat16 y float (sin fijar escalar)");
}

// ---------------------------------------------------------------------------
//  5. Tiles Wang: emparejamiento, transformaciones y rampas
// ---------------------------------------------------------------------------

void test_tiles() {
	proc::EdgeSides<u8> a {1u, 2u, 3u, 4u};
	proc::EdgeSides<u8> b {3u, 4u, 1u, 2u}; // espejo: casa por los cuatro lados
	proc::EdgeSides<u8> c {9u, 2u, 1u, 2u}; // su borde S (1) no casa con a.n (1): si, casa; su borde N (9) no casa con a.s (3)
	check(proc::edges_match(a, b, proc::Side::North), "edges_match: a.n == b.s");
	check(!proc::edges_match(a, c, proc::Side::South), "edges_match: borde distinto no casa");
	check(proc::edges_match(a, c, proc::Side::North), "edges_match: borde igual si casa");

	// Rot90 x4 vuelve al origen (pixeles y bordes).
	proc::BitmapBuffer<4, 4> src {};
	for (u16 i = 0; i < 16u; ++i) src.pixels[i] = u8(i);
	proc::BitmapBuffer<4, 4> t1 {}, t2 {}, t3 {}, t4 {};
	proc::apply_transform(src, t1, proc::Transform::Rot90);
	proc::apply_transform(t1, t2, proc::Transform::Rot90);
	proc::apply_transform(t2, t3, proc::Transform::Rot90);
	proc::apply_transform(t3, t4, proc::Transform::Rot90);
	check(std::memcmp(src.pixels, t4.pixels, 16u) == 0, "apply_transform: Rot90 x4 = identidad");
	proc::BitmapBuffer<4, 4> f1 {}, f2 {};
	proc::apply_transform(src, f1, proc::Transform::FlipX);
	proc::apply_transform(f1, f2, proc::Transform::FlipX);
	check(std::memcmp(src.pixels, f2.pixels, 16u) == 0, "apply_transform: FlipX x2 = identidad");
	check(f1.at(0, 0) == src.at(3, 0), "apply_transform: FlipX espeja x");

	proc::EdgeSides<u8> r = a;
	for (int i = 0; i < 4; ++i) r = proc::transform_edges(r, proc::Transform::Rot90);
	check(r.n == a.n && r.e == a.e && r.s == a.s && r.w == a.w,
	      "transform_edges: Rot90 x4 = identidad");
	const proc::EdgeSides<u8> r90 = proc::transform_edges(a, proc::Transform::Rot90);
	check(r90.e == a.n && r90.s == a.e && r90.w == a.s && r90.n == a.w,
	      "transform_edges: Rot90 gira los ids (n->e)");

	// Rampa de borde: creciente, 0..255 y extremo exterior saturado.
	check(proc::kRamp4.t[0] == 0u, "Ramp4: interior = 0");
	check(proc::kRamp8.t[7] == 255u, "Ramp8: exterior = 255");
	bool ramp_mono = true;
	for (u8 i = 1; i < 8u; ++i) ramp_mono = ramp_mono && proc::kRamp8.t[i] >= proc::kRamp8.t[i - 1u];
	check(ramp_mono, "Ramp8: monotona creciente");

	check(proc::blend_index(1u, 2u, 0u, 0u, 0u) == 1u, "blend_index: alpha 0 = interior");
	check(proc::blend_index(1u, 2u, 255u, 0u, 0u) == 2u, "blend_index: alpha 255 = borde");
}

// ---------------------------------------------------------------------------
//  6. Texturas tileables (dos escalares)
// ---------------------------------------------------------------------------

/// Comprueba `[0,1)`, determinismo y tileabilidad (px=0 == px+W) de una textura.
/// \param name etiqueta del chequeo. \param tex textura (con scale/period ya puestos).
/// \param tile_px ancho del tile en píxeles (periodo exacto).
template <class S, class Tex>
void check_texture(const char* name, const Tex& tex, u16 tile_px, double tol) {
	using eng::math::to_double;
	bool range_ok = true;
	for (u16 y = 0; y < tile_px; y += 3u) {
		for (u16 x = 0; x < tile_px; x += 3u) {
			const double v = to_double(tex(x, y));
			range_ok = range_ok && v >= -tol && v <= 1.0 + tol;
		}
	}
	check(range_ok, name);
	bool tile_ok = true;
	for (u16 y = 0; y < tile_px; y += 5u) {
		const double v0 = to_double(tex(0u, y));
		const double vw = to_double(tex(tile_px, y));
		tile_ok = tile_ok && std::fabs(v0 - vw) <= tol;
	}
	check(tile_ok, name);
}

template <class S>
void test_textures_scalar(const char* tag) {
	(void)tag;
	// Todas las texturas del atlas con la regla scale = k/32, period = k (tile 32).
	const auto s1 = proc::tile_scale<S>(32u, 1u);
	const auto s2 = proc::tile_scale<S>(32u, 2u);
	const auto s4 = proc::tile_scale<S>(32u, 4u);
	const auto one = eng::math::scalar_traits<S>::one();
	const auto half = eng::math::div_norm(one, eng::math::scalar_traits<S>::from_int(2));
	const auto tol = 1.5e-2; // tolerancia de MiniFloat16 (10 bits); float va sobrado

	tx::Clouds<S> clouds {0xC10u, 3u, s1, 1};
	check_texture<S>("Clouds tileable y en rango", clouds, 32u, tol);
	tx::Marble<S> marble {0xC20u, s1, eng::math::scalar_traits<S>::from_int(6), 5u, 1};
	check_texture<S>("Marble tileable y en rango", marble, 32u, tol);
	tx::Wood<S> wood {0xC30u, s2, half, 10u, 2};
	check_texture<S>("Wood tileable y en rango", wood, 32u, tol);
	tx::Stone<S> stone {0xC40u, s2, 4u, 2u, 2};
	check_texture<S>("Stone tileable y en rango", stone, 32u, tol);
	tx::Granite<S> granite {0xC50u, s2, 2u, 4u, 2};
	check_texture<S>("Granite tileable y en rango", granite, 32u, tol);
	tx::Foam<S> foam {0xC60u, s4, half, 4};
	check_texture<S>("Foam tileable y en rango", foam, 32u, tol);
	tx::Rust<S> rust {0xC70u, s2, 3u, 2};
	check_texture<S>("Rust tileable y en rango", rust, 32u, tol);
	tx::Moss<S> moss {0xC80u, s4, 2u, 4};
	check_texture<S>("Moss tileable y en rango", moss, 32u, tol);
	tx::WeatheredStone<S> weathered {0xC90u, s2, 2};
	check_texture<S>("WeatheredStone tileable y en rango", weathered, 32u, tol);
}

void test_textures() {
	test_textures_scalar<MiniFloat16>("MF");
	test_textures_scalar<float>("float");
	check(true, "texturas instanciadas con MiniFloat16 y float (sin fijar escalar)");
}

// ---------------------------------------------------------------------------
//  7. Primitivas de shapes (siluetas y mascaras)
// ---------------------------------------------------------------------------

void test_shapes() {
	proc::LakeBlob<MiniFloat16> lake {};
	lake.cx = 16;
	lake.cy = 16;
	lake.rx = 10;
	lake.ry = 8;
	lake.color = 1u;
	lake.seed = 7u;
	check(lake(16, 16) == 1u, "LakeBlob: centro es agua");
	check(lake(0, 0) == 0u, "LakeBlob: esquina fuera del lago");

	proc::RiverStroke<MiniFloat16> river {16u, 0u, 16u, 31u, 4u, 1u, 3u, 3u};
	check(river(16, 16) == 1u, "RiverStroke: cauce vertical en el centro");
	check(river(0, 16) == 0u, "RiverStroke: lejos del cauce");

	proc::SoftCircleMask<MiniFloat16> mask {};
	mask.cx = 16;
	mask.cy = 16;
	mask.r2_min = 25u;  // r=5
	mask.r2_max = 100u; // r=10
	mask.seed = 3u;
	check(mask(16, 16) == 1u, "SoftCircleMask: interior opaco");
	check(mask(0, 0) == 0u, "SoftCircleMask: fuera transparente");

	// Primitivas geometricas (sin ruido): muralla, torre, casa y cueva.
	proc::WallSegment wall {};
	wall.dir = proc::WallSegment::Dir::North;
	wall.center_x = 16;
	wall.tile_w = 32;
	wall.tile_h = 32;
	check(wall(16, 10) != 0u, "WallSegment N: franja vertical en el centro");
	check(wall(0, 10) == 0u, "WallSegment N: lejos de la franja");
	proc::TowerTop tower {};
	tower.cx = 16;
	tower.cy = 16;
	check(tower(16, 16) != 0u, "TowerTop: centro dentro del disco");
	check(tower(0, 0) == 0u, "TowerTop: esquina fuera");
	proc::HouseFootprint house {};
	check(house(house.x0 + 1u, house.y0 + house.h - 1u) != 0u, "HouseFootprint: cuerpo");
	check(house(0, 0) == 0u, "HouseFootprint: fuera");
	proc::CaveMouth cave {};
	cave.cx = 16;
	cave.cy = 20;
	cave.dark = 1u;
	cave.rock = 2u;
	check(cave(16, 20) == cave.dark, "CaveMouth: centro oscuro");
	check(cave(16, 26) == cave.rock, "CaveMouth: anillo de roca");
	check(cave(0, 0) == 0u, "CaveMouth: esquina transparente");
}

// ---------------------------------------------------------------------------
//  8. Atlas de materiales
// ---------------------------------------------------------------------------

template <class S>
void test_atlas_scalar(const char* tag) {
	proc::TextureAtlas<32, 32, S> atlas {};
	atlas.generate(0x7E44A1u);
	check(atlas.total_colors == 32u, tag);
	// La cota de la tabla: base + levels-1 <= 31 para TODOS los materiales.
	bool bounds = true;
	for (u8 m = 0; m < proc::kMaterialCount; ++m) {
		const proc::MaterialDesc d =
			proc::TextureAtlas<32, 32, S>::desc(proc::MaterialId(m));
		bounds = bounds && eng::u16(d.base) + d.levels - 1u <= 31u;
	}
	check(bounds, tag);
	// Los tramos no cruzan de tono (un material no mezcla colores de otra familia:
	// mar/madera verdes, piedra con madera, etc.). Familias de 4 niveles del doc del atlas.
	const auto within = [&](proc::MaterialId id, u8 lo, u8 hi) {
		const proc::MaterialDesc d = proc::TextureAtlas<32, 32, S>::desc(id);
		return d.base >= lo && eng::u16(d.base) + d.levels - 1u <= hi;
	};
	bool tones = true;
	const proc::MaterialId stone_family[] = {proc::MaterialId::Stone, proc::MaterialId::Granite,
						 proc::MaterialId::Wall, proc::MaterialId::Rock,
						 proc::MaterialId::Marble, proc::MaterialId::Cobble};
	for (const proc::MaterialId m : stone_family) tones = tones && within(m, 20u, 23u);
	const proc::MaterialId wood_family[] = {proc::MaterialId::Wood, proc::MaterialId::Roof,
						proc::MaterialId::Rust, proc::MaterialId::Trunk};
	for (const proc::MaterialId m : wood_family) tones = tones && within(m, 24u, 27u);
	const proc::MaterialId green_family[] = {proc::MaterialId::Grass, proc::MaterialId::Moss};
	for (const proc::MaterialId m : green_family) tones = tones && within(m, 8u, 11u);
	// El follaje tiene su propio tono (verde profundo plano) para no confundirse con la hierba.
	tones = tones && within(proc::MaterialId::Foliage, 28u, 29u);
	const proc::MaterialId water_family[] = {proc::MaterialId::Water, proc::MaterialId::Foam};
	for (const proc::MaterialId m : water_family) tones = tones && within(m, 4u, 7u);
	const proc::MaterialId dirt_family[] = {proc::MaterialId::Dirt, proc::MaterialId::Sand,
						proc::MaterialId::Path};
	for (const proc::MaterialId m : dirt_family) tones = tones && within(m, 12u, 15u);
	const proc::MaterialId crop_family[] = {proc::MaterialId::CropA, proc::MaterialId::CropB};
	for (const proc::MaterialId m : crop_family) tones = tones && within(m, 16u, 19u);
	tones = tones && within(proc::MaterialId::Cave, 0u, 3u);
	tones = tones && within(proc::MaterialId::Snow, 30u, 31u);
	check(tones, tag);
	// Todos los pixeles remapeados caen en la paleta (32 colores: 5 planos).
	bool in_palette = true;
	for (u8 m = 0; m < proc::kMaterialCount; ++m) {
		for (u8 v = 0; v < proc::kVariantsPerMaterial; ++v) {
			const auto& t = atlas.get(proc::MaterialId(m), v);
			for (u16 i = 0; i < 32u * 32u; ++i) in_palette = in_palette && t.pixels[i] < 32u;
		}
	}
	check(in_palette, tag);
	// Variantes deterministas y en rango.
	const u8 v0 = atlas.variant_at(3u, 5u, proc::MaterialId::Stone);
	check(v0 == atlas.variant_at(3u, 5u, proc::MaterialId::Stone), tag);
	check(v0 < proc::kVariantsPerMaterial, tag);
	// Paleta: 0 = negro (transparente), 31 = luz (nieve).
	check(atlas.palette.color[0] == 0u, tag);
	check(atlas.palette.color[31] != 0u, tag);
	// Materiales distintos tienen texturas distintas (no todo es el mismo ruido).
	bool differ = false;
	for (u16 i = 0; i < 32u * 32u; ++i) {
		if (atlas.get(proc::MaterialId::Grass).pixels[i] !=
		    atlas.get(proc::MaterialId::Granite).pixels[i]) {
			differ = true;
			break;
		}
	}
	check(differ, tag);
}

void test_atlas() {
	test_atlas_scalar<MiniFloat16>("atlas MF");
	test_atlas_scalar<float>("atlas float");
	// Determinismo entre dos atlas con el mismo seed.
	proc::TextureAtlas<32, 32, MiniFloat16> a {}, b {};
	a.generate(0x1234u);
	b.generate(0x1234u);
	check(std::memcmp(a.get(proc::MaterialId::Water).pixels,
			  b.get(proc::MaterialId::Water).pixels, 32u * 32u) == 0,
	      "atlas determinista por seed");
}

// ---------------------------------------------------------------------------
//  9. MaterialLayer (bandas por altura)
// ---------------------------------------------------------------------------

template <class S>
void test_material_layer_scalar(const char* tag) {
	proc::TextureAtlas<32, 32, S> atlas {};
	atlas.generate(0x51EEDu);
	proc::MaterialLayer<32, 32, S> layer {};
	using eng::math::scalar_traits;
	const S zero = scalar_traits<S>::zero();
	const S one = scalar_traits<S>::one();
	const S c35 = eng::math::div_norm(scalar_traits<S>::from_int(35), scalar_traits<S>::from_int(100));
	const S c30 = eng::math::div_norm(scalar_traits<S>::from_int(30), scalar_traits<S>::from_int(100));
	const S c70 = eng::math::div_norm(scalar_traits<S>::from_int(70), scalar_traits<S>::from_int(100));
	const S c65 = eng::math::div_norm(scalar_traits<S>::from_int(65), scalar_traits<S>::from_int(100));
	const S soft = eng::math::div_norm(one, scalar_traits<S>::from_int(10));
	layer.add(proc::MaterialId::Grass, 0u, zero, c35, soft);
	layer.add(proc::MaterialId::Stone, 1u, c30, c70, soft);
	layer.add(proc::MaterialId::Snow, 2u, c65, one, soft);
	proc::BitmapBuffer<32, 32> buf {};
	// Altura baja -> hierba (tramo 8..11); altura alta -> nieve (tramo 30..31, blanco).
	layer.render_with_height(buf, atlas, [](u16, u16) { return scalar_traits<S>::zero(); });
	bool low_ok = true;
	for (u16 i = 0; i < 32u * 32u; ++i) {
		low_ok = low_ok && buf.pixels[i] >= 8u && buf.pixels[i] <= 11u;
	}
	check(low_ok, tag);
	layer.render_with_height(buf, atlas,
				 [](u16, u16) { return scalar_traits<S>::one(); });
	bool high_ok = true;
	for (u16 i = 0; i < 32u * 32u; ++i) {
		high_ok = high_ok && buf.pixels[i] >= 30u && buf.pixels[i] <= 31u;
	}
	check(high_ok, tag);
	// El campo de altura interno (fBm) tambien rellena todo en rango.
	layer.height_scale = proc::tile_scale<S>(32u, 2u);
	layer.height_period = 2;
	layer.render(buf, atlas);
	bool in_palette = true;
	for (u16 i = 0; i < 32u * 32u; ++i) in_palette = in_palette && buf.pixels[i] < 32u;
	check(in_palette, tag);
}

void test_material_layer() {
	test_material_layer_scalar<MiniFloat16>("MaterialLayer MF");
	test_material_layer_scalar<float>("MaterialLayer float");
}

// ---------------------------------------------------------------------------
// 10. Terreno: catalogo, pintor y set de piezas
// ---------------------------------------------------------------------------

template <class S>
void test_terrain_scalar(const char* tag) {
	proc::TextureAtlas<32, 32, S> atlas {};
	atlas.generate(0x7E5A1u);
	terr::TerrainTileSet<32, 32, 64> set {};
	set.generate(0xBEE5u, atlas, 6u);
	check(set.count == 45u, tag); // 11 castillo + 7 rio + 15 naturaleza + 12 extra
	bool in_palette = true;
	for (u16 t = 0; t < set.count; ++t)
		for (u16 i = 0; i < 32u * 32u; ++i) in_palette = in_palette && set.tiles[t].pixels[i] < 32u;
	check(in_palette, tag);

	// Piezas del catalogo: todas presentes y con bordes validos.
	check(set.find([](const terr::TileSpec& s) {
		return terr::has(s.features, terr::Feature::Gate);
	}) < set.count, tag);
	check(set.find([](const terr::TileSpec& s) {
		return terr::has(s.features, terr::Feature::CastleYard);
	}) < set.count, tag);

	// La muralla vertical casa consigo misma por N (n == s en el catalogo).
	const u16 vert = set.find([](const terr::TileSpec& s) {
		return terr::has(s.features, terr::Feature::WallSeg) &&
		       s.edges.n == terr::Edge::Wall && s.edges.s == terr::Edge::Wall &&
		       s.edges.e == terr::Edge::Grass;
	});
	check(vert < set.count && set.can_place(vert, vert, proc::Side::North),
	      tag);
	// La puerta (muro E/W) casa con la muralla horizontal por el oeste.
	const u16 gate = set.find([](const terr::TileSpec& s) {
		return terr::has(s.features, terr::Feature::Gate);
	});
	const u16 horiz = set.find([](const terr::TileSpec& s) {
		return terr::has(s.features, terr::Feature::WallSeg) &&
		       s.edges.e == terr::Edge::Wall && s.edges.w == terr::Edge::Wall &&
		       s.edges.n == terr::Edge::Grass;
	});
	check(horiz < set.count && set.can_place(gate, horiz, proc::Side::West), tag);

	// El pintor del rio vertical: el agua entra por el centro exacto del borde N.
	const terr::TileSpec river_spec {.edges = {terr::Edge::Water, terr::Edge::Grass,
						   terr::Edge::Water, terr::Edge::Grass},
					 .features = terr::Feature::River,
					 .seed = 0x777u};
	proc::BitmapBuffer<32, 32> r1 {};
	terr::paint_terrain_tile(r1, river_spec, atlas, 0u, 0u, 6u);
	proc::BitmapBuffer<32, 32> r2 {};
	terr::paint_terrain_tile(r2, river_spec, atlas, 0u, 3u, 6u);
	// El cauce muestrea la variante FIJA 0 (textura tileable: continuidad entre tiles).
	const auto& wtex = atlas.get(proc::MaterialId::Water, 0u);
	// El agua entra por el centro exacto de sus bordes: union de dos tramos consecutivos.
	check(r1.at(16, 0) == wtex.at(16, 0), tag);   // borde N del tramo 1
	check(r1.at(16, 31) == wtex.at(16, 31), tag); // borde S del tramo 1
	check(r2.at(16, 0) == wtex.at(16, 0), tag);   // borde N del tramo 2 (casa con el 1)

	// La puerta abre el hueco transparente (indice 0) en el centro.
	const u16 gate_idx = set.find([](const terr::TileSpec& s) {
		return terr::has(s.features, terr::Feature::Gate);
	});
	check(gate_idx < set.count && set.tiles[gate_idx].at(16, 16) == 0u, tag);
}

void test_terrain() {
	test_terrain_scalar<MiniFloat16>("terreno MF");
}

// ---------------------------------------------------------------------------
// 11. Variantes transformadas, colocador de mundos y encaje con TileLayerMap
// ---------------------------------------------------------------------------

void test_variants_and_placer() {
	proc::TextureAtlas<32, 32, MiniFloat16> atlas {};
	atlas.generate(0x7E5A1u);
	terr::TerrainTileSet<32, 32, 56> set {};
	set.generate(0xBEE5u, atlas, 6u);
	check(set.count == 45u, "catalogo con hierba llana y camino: 45 piezas");

	// add_variant: los pixeles y los bordes son los transformados de la base.
	const eng::u16 base = set.find([](const terr::TileSpec& s) {
		return s.edges.n == terr::Edge::Water && s.edges.e == terr::Edge::Water &&
		       s.edges.s == terr::Edge::Grass && s.edges.w == terr::Edge::Grass;
	});
	check(base < set.count, "pieza de curva de rio localizada");
	const eng::u16 count_before = set.count;
	const eng::u16 var = set.add_variant(base, proc::Transform::Rot90);
	check(var == count_before && set.count == count_before + 1u, "add_variant anade una pieza");
	proc::BitmapBuffer<32, 32> expected {};
	proc::apply_transform(set.tiles[base], expected, proc::Transform::Rot90);
	check(std::memcmp(set.tiles[var].pixels, expected.pixels, 32u * 32u) == 0,
	      "add_variant: pixeles transformados");
	const proc::EdgeSides<terr::Edge> ebase = set.specs[base].edges;
	const proc::EdgeSides<terr::Edge> evar = set.specs[var].edges;
	check(evar.n == ebase.w && evar.e == ebase.n && evar.s == ebase.e && evar.w == ebase.s,
	      "add_variant: bordes transformados (Rot90)");
	check(set.add_variant(base, proc::Transform::Identity) == base,
	      "add_variant(Identity) devuelve la base");
	// Capacidad justa: sin hueco, no se crea variante (devuelve la base, no corrompe).
	{
		terr::TerrainTileSet<32, 32, 45> tight {};
		tight.generate(0xBEE5u, atlas, 6u);
		check(tight.count == 45u && tight.add_variant(0u, proc::Transform::Rot90) == 0u,
		      "add_variant sin capacidad devuelve la base");
	}

	// Colocador: mapa 10x7 conectado, determinista y con las estructuras ancladas. Se usa
	// un set gemelo fresco (el `set` de arriba ya lleva una variante de prueba).
	terr::TerrainTileSet<32, 32, 56> pset {};
	pset.generate(0xBEE5u, atlas, 6u);
	terr::TerrainLayout<10, 7> lay {};
	check(terr::place_world(lay, pset, 0xBEE5u), "place_world: mapa conectado");
	check(terr::layout_is_connected(lay, pset), "layout_is_connected: todas las uniones casan");
	const auto spec_at = [&](eng::u16 x, eng::u16 y) -> const terr::TileSpec& {
		return pset.specs[lay.at(x, y)];
	};
	check(terr::has(spec_at(0u, 0u).features, terr::Feature::CaveMouth), "cueva en la esquina (0,0)");
	check(terr::has(spec_at(7u, 3u).features, terr::Feature::Gate), "puerta del castillo en (7,3)");
	check(terr::has(spec_at(6u, 2u).features, terr::Feature::Tower), "torre en (6,2)");
	// Desembocadura: la entrada del cauce (columna 2) se une al afluente de la fila 0 con
	// la pieza T del catalogo (agua N/E/S; abre al oeste). Y el sendero sube a la puerta.
	check(spec_at(2u, 0u).edges.n == terr::Edge::Water &&
		      spec_at(2u, 0u).edges.e == terr::Edge::Water &&
		      spec_at(2u, 0u).edges.s == terr::Edge::Water &&
		      spec_at(2u, 0u).edges.w == terr::Edge::Grass,
	      "desembocadura en T del afluente en (2,0)");
	check(spec_at(5u, 0u).edges.e == terr::Edge::Water &&
		      spec_at(5u, 0u).edges.w == terr::Edge::Water,
	      "afluente horizontal por la fila 0 (recta E-W)");
	check(spec_at(7u, 5u).edges.n == terr::Edge::Path && spec_at(7u, 5u).edges.e == terr::Edge::Path &&
		      spec_at(7u, 5u).edges.s == terr::Edge::Grass,
	      "cruce en T del sendero a la puerta (7,5)");
	check(spec_at(7u, 4u).edges.s == terr::Edge::Path && spec_at(7u, 4u).edges.n == terr::Edge::Grass,
	      "celda del sendero bajo la puerta (fondo de saco)");
	bool river_exit = false, bridge_on_road = false;
	for (eng::u16 x = 0; x < 10u; ++x) {
		if (spec_at(x, 6u).edges.s == terr::Edge::Water) river_exit = true;
		if (terr::has(spec_at(x, 5u).features, terr::Feature::Bridge)) bridge_on_road = true;
	}
	check(river_exit, "el rio sale por el borde inferior");
	check(bridge_on_road, "el camino cruza el rio con un puente (fila 5)");
	// Lago junto al cauce: en la fila 4 (encima del camino, donde no hay meandro) y con el
	// río como vecino oeste.
	bool lake_beside = false;
	for (eng::u16 x = 1u; x < 10u; ++x) {
		const terr::TileSpec& ls = spec_at(x, 4u);
		if (terr::has(ls.features, terr::Feature::Lake) && ls.edges.n == terr::Edge::Grass) {
			lake_beside = terr::has(spec_at(eng::u16(x - 1u), 4u).features, terr::Feature::River);
		}
	}
	check(lake_beside, "lago junto al cauce (fila 4, vecino oeste del rio)");
	// Todo el mapa usa la paleta del atlas.
	bool in_palette = true;
	for (eng::u16 y = 0; y < 7u; ++y) {
		for (eng::u16 x = 0; x < 10u; ++x) {
			for (eng::u16 j = 0; j < 32u * 32u; ++j) {
				in_palette = in_palette && pset.tiles[lay.at(x, y)].pixels[j] < 32u;
			}
		}
	}
	check(in_palette, "todas las piezas del mapa estan en la paleta");

	// Determinismo del colocador (set y semilla nuevos -> mismos indices).
	terr::TerrainTileSet<32, 32, 56> pset2 {};
	pset2.generate(0xBEE5u, atlas, 6u);
	terr::TerrainLayout<10, 7> lay2 {};
	check(terr::place_world(lay2, pset2, 0xBEE5u) &&
		      std::memcmp(lay.cells, lay2.cells, sizeof(lay.cells)) == 0,
	      "place_world determinista con la misma semilla");

	// Encaje con el tipo del field: TerrainLayout -> TileLayerMap sin conversion de datos.
	eng::playfield::TileLayerMap map {};
	map.cells = lay.span();
	map.width = 10u;
	map.height = 7u;
	map.wrap_x = 0u; // acotado: fuera del mapa devuelve `edge_tile`
	map.edge_tile = 0u;
	check(map.tile_at(3, 4) == lay.at(3u, 4u), "TileLayerMap consulta el mapa colocado");
	check(map.tile_at(-1, 0) == 0u, "TileLayerMap devuelve edge_tile fuera del mapa");
}

// ---------------------------------------------------------------------------
//  Evidencia visual opcional: PPM de atlas y piezas
// ---------------------------------------------------------------------------

void write_ppm(const char* path, const u8* pixels, u16 w, u16 h, const eng::Palette32& pal) {
	std::FILE* f = std::fopen(path, "wb");
	if (f == nullptr) return;
	std::fprintf(f, "P6\n%u %u\n255\n", unsigned(w), unsigned(h));
	// Índice u32: con u16 el bucle no termina cuando la imagen supera 65535 píxeles
	// (DT-019; el dump de mapas grandes se quedaba colgado).
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
	proc::TextureAtlas<32, 32, MiniFloat16> atlas {};
	atlas.generate(0x7E44A1u);
	char path[512];
	for (u8 m = 0; m < proc::kMaterialCount; ++m) {
		const proc::MaterialDesc d = decltype(atlas)::desc(proc::MaterialId(m));
		std::snprintf(path, sizeof(path), "%s/atlas_%02u_%s.ppm", dir, unsigned(m), d.name);
		write_ppm(path, atlas.get(proc::MaterialId(m), 0u).pixels, 32u, 32u, atlas.palette);
	}
	terr::TerrainTileSet<32, 32, 64> set {};
	set.generate(0xBEE5u, atlas, 6u);
	for (u16 t = 0; t < set.count; ++t) {
		std::snprintf(path, sizeof(path), "%s/tile_%02u.ppm", dir, unsigned(t));
		write_ppm(path, set.tiles[t].pixels, 32u, 32u, atlas.palette);
	}
	// `map.ppm`: el mundo completo que produce el COLOCADOR (10x7 piezas = 320x224),
	// con cueva, rio meandriforme continuo, camino con puente, castillo con torre y
	// puerta, aldea y naturaleza. Es la evidencia visual de `place_world`.
	terr::TerrainLayout<10, 7> lay {};
	if (!terr::place_world(lay, set, 0xBEE5u)) {
		std::printf("[dump] ERROR: place_world no produjo un mapa conectado\n");
	}
	char mappath[512];
	std::snprintf(mappath, sizeof(mappath), "%s/map.ppm", dir);
	std::FILE* f = std::fopen(mappath, "wb");
	if (f == nullptr) return;
	std::fprintf(f, "P6\n%u %u\n255\n", unsigned(10u * 32u), unsigned(7u * 32u));
	for (u16 cy = 0; cy < 7u; ++cy) {
		for (u16 row = 0; row < 32u; ++row) {
			for (u16 cx = 0; cx < 10u; ++cx) {
				for (u16 px = 0; px < 32u; ++px) {
					const u8 v = set.tiles[lay.at(cx, cy)].at(px, row);
					const eng::u16 c = atlas.palette.color[v & 15u];
					const u8 rgb[3] = {u8(((c >> 8u) & 0xFu) * 17u),
							   u8(((c >> 4u) & 0xFu) * 17u),
							   u8((c & 0xFu) * 17u)};
					std::fwrite(rgb, 1u, 3u, f);
				}
			}
		}
	}
	std::fclose(f);
	std::printf("[dump] %u materiales + %u tiles + mapa del colocador en %s\n",
		    unsigned(proc::kMaterialCount), unsigned(set.count), dir);
}

} // namespace

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) g_dump_dir = argv[i + 1];
	}
	std::printf("== HOST-442 proc bitmaps (generacion procedural) ==\n");
	test_buffer_and_dither();
	test_profile();
	test_evaluators();
	test_fbm_fill();
	test_tiles();
	test_textures();
	test_shapes();
	test_atlas();
	test_material_layer();
	test_terrain();
	test_variants_and_placer();
	if (g_dump_dir != nullptr) dump_visual_evidence(g_dump_dir);
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: proc bitmaps validados (dos escalares, tileable, atlas y terreno).\n");
	return 0;
}
