// ============================================================================
// Test HOST-445: mundo de biomas (`eng/graphics/proc/terrain_biome.hpp`).
// ============================================================================
//
// Cubre el generador con **conocimiento de compatibilidad**: alfabeto de materiales,
// gramática de adyacencia, set que pinta/memoiza la pieza de CUALQUIER máscara (canónica
// por rotación), generador de mundo (río, playa, tierra, sierra, camino en L con puente,
// castillo, aldea, bosques) y el contrato `biome_world_connected` sobre las máscaras
// instanciadas. Se instancia con DOS escalares (negativo de §1.11): MiniFloat16 y float.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/445_biome_world
//   (opcional) <binario> --dump <dir>   → escribe PPM del mundo (evidencia visual)

#include <cstdio>
#include <cstring>

#include <eng/core/math/minifloat.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/noise_tile.hpp>
#include <eng/graphics/proc/terrain_biome.hpp>

namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
using eng::u16;
using eng::u32;
using eng::u8;
using eng::math::MiniFloat16;

namespace {

int g_fail = 0;
const char* g_dump_dir = nullptr;

// ---------------------------------------------------------------------------
//  0. Sonda `constexpr`: el generador completo se evalúa EN COMPILACIÓN
// ---------------------------------------------------------------------------
//
// Se usa un atlas plano de juguete (texturas con un patrón simple) para probar que la
// ruta entera (set por máscaras + generador + pintado por celda) es evaluable en
// compilación sin pagar el ruido del atlas real (la demo 119 sí lo evalúa: su build
// tarda ~1 min con `-fconstexpr-ops-limit`, ver su `build.args`).
constexpr proc::TextureAtlas<16, 16, eng::math::MiniFloat16> make_flat_atlas() {
	proc::TextureAtlas<16, 16, eng::math::MiniFloat16> a {};
	for (eng::u8 m = 0u; m < proc::kMaterialCount; ++m) {
		for (eng::u8 v = 0u; v < proc::kVariantsPerMaterial; ++v) {
			for (eng::u16 i = 0u; i < 256u; ++i) {
				a.textures[m][v].pixels[i] = eng::u8(((i >> 1u) + m) % 6u + 4u);
			}
		}
	}
	a.total_colors = 32u;
	a.base_seed = 1u;
	return a;
}
constexpr auto kFlatAtlas = make_flat_atlas();

constexpr bool make_constexpr_world() {
	// Mundo mínimo (10×8) para caber en el límite de operaciones por defecto del build de
	// tests; la demo 119 evalúa el mundo completo (20×14) con `-fconstexpr-ops-limit`.
	terr::TerrainLayout<10, 8> lay {};
	terr::BiomeTileSet<16, 16, 96> set {};
	return terr::generate_biome_world<10, 8>(lay, set, kFlatAtlas, 0xBEE5u);
}
static_assert(make_constexpr_world(), "el mundo de biomas se genera en compilacion");

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// ---------------------------------------------------------------------------
//  1. Ruido entero (mulu.w): hilado, tileable y determinista
// ---------------------------------------------------------------------------

void test_noise_tile() {
	// El nodo es un hash estable de (ix, iy, seed) y cambia con la semilla.
	const u8 a = proc::tile_node(3u, 5u, 7u);
	check(a == proc::tile_node(3u, 5u, 7u), "tile_node determinista");
	check(a != proc::tile_node(3u, 5u, 8u) || a != proc::tile_node(4u, 5u, 7u),
	      "tile_node varia con nodo/seed");
	// div255 exacto y smoothstep en los extremos y monótono.
	check(proc::div255(0u) == 0u && proc::div255(255u) == 1u && proc::div255(65025u) == 255u,
	      "div255 exacto (0/255/65025)");
	check(proc::smoothstep_q8(0u) == 0u && proc::smoothstep_q8(255u) == 255u,
	      "smoothstep_q8 fija los extremos");
	bool smooth_mono = true;
	u8 prev = 0u;
	for (u16 i = 0u; i <= 255u; ++i) {
		const u8 v = proc::smoothstep_q8(u8(i));
		smooth_mono = smooth_mono && v >= prev;
		prev = v;
	}
	check(smooth_mono, "smoothstep_q8 monotono");
	// Tileabilidad: el campo se repite cada `period << cell_log2` píxeles.
	const u16 period_px = u16(8u << 3u); // period=8 nodos, celda de 8 px → 64 px
	bool tile_ok = true;
	for (u16 i = 0u; i < 64u; ++i) {
		const u16 x = u16(i * 7u + 13u);
		tile_ok = tile_ok && proc::tile_noise2(x, i, 3u, 8u, 0x123u) ==
					       proc::tile_noise2(u16(x + period_px), i, 3u, 8u, 0x123u);
	}
	check(tile_ok, "tile_noise2 tileable en period<<cell_log2");
	// fBm en rango y con detalle (no constante).
	u8 lo = 255u, hi = 0u;
	for (u16 y = 0u; y < 64u; ++y) {
		for (u16 x = 0u; x < 64u; ++x) {
			const u8 v = proc::tile_fbm2(x, y, 3u, 8u, 3u, 0xBEE5u);
			if (v < lo) lo = v;
			if (v > hi) hi = v;
		}
	}
	check(hi - lo >= 40u, "tile_fbm2 usa un rango amplio (parches)");
}

// ---------------------------------------------------------------------------
//  2. Gramática de adyacencia (el conocimiento de compatibilidad)
// ---------------------------------------------------------------------------

void test_grammar() {
	check(proc::tile_noise2(4u, 4u, 3u, 8u, 1u) == proc::tile_noise2(4u, 4u, 3u, 8u, 1u),
	      "tile_noise2 determinista");
	using terr::Biome;
	check(terr::biome_adjacent(Biome::Water, Biome::Sand), "agua-arena compatible (playa)");
	check(terr::biome_adjacent(Biome::Water, Biome::Grass), "agua-hierba compatible (orilla)");
	check(!terr::biome_adjacent(Biome::Water, Biome::Dirt), "agua-tierra prohibido");
	check(!terr::biome_adjacent(Biome::Water, Biome::Rock), "agua-roca prohibido");
	check(terr::biome_adjacent(Biome::Grass, Biome::Dirt) &&
		      terr::biome_adjacent(Biome::Rock, Biome::Grass),
	      "tierra/roca-hierba compatibles");
}

// ---------------------------------------------------------------------------
//  3. Set de biomas: cualquier máscara es representable y deduplica por rotación
// ---------------------------------------------------------------------------

template <class S>
void test_set_scalar(const char* tag) {
	proc::TextureAtlas<16, 16, S> atlas {};
	atlas.generate(0x7E5A1u);
	// Capacidad para el barrido completo de 256 máscaras en sus 4 orientaciones.
	terr::BiomeTileSet<16, 16, 320> set {};
	const u8 sides_a[4] = {u8(terr::Biome::Sand), u8(terr::Biome::Grass), u8(terr::Biome::Grass),
			       u8(terr::Biome::Grass)};
	const u8 sides_b[4] = {u8(terr::Biome::Grass), u8(terr::Biome::Grass), u8(terr::Biome::Grass),
			       u8(terr::Biome::Sand)};
	proc::EdgeSides<terr::Edge> fe {};
	const u16 p1 = set.tile_for(u8(terr::Biome::Grass), sides_a, terr::Feature::None, fe, u8(terr::Biome::Grass),
				    atlas);
	const u16 p2 = set.tile_for(u8(terr::Biome::Grass), sides_a, terr::Feature::None, fe, u8(terr::Biome::Grass),
				    atlas);
	check(p1 == p2, "misma mascara -> misma pieza");
	const u16 p3 = set.tile_for(u8(terr::Biome::Grass), sides_b, terr::Feature::None, fe, u8(terr::Biome::Grass),
				    atlas);
	// sides_b es sides_a girada 90° (Sand al oeste): debe reutilizar el pintado con giro.
	check(p3 != 0u && set.count <= 4u, "reutiliza el pintado (giro < 3 piezas nuevas)");
	// La pieza devuelta casa con la máscara pedida (algún giro es idéntico a lo pedido).
	bool found = false;
	for (u8 k = 0u; k < 4u; ++k) {
		terr::BiomeSpec probe {};
		probe.base = u8(terr::Biome::Grass);
		probe.id = u8(terr::Biome::Grass);
		for (u8 i = 0u; i < 4u; ++i) probe.sides[i] = sides_b[i];
		if (terr::biome_spec_equal(set.specs[p3], terr::rotate_biome_spec(probe, k))) found = true;
	}
	check(found, "la pieza devuelta casa con la mascara pedida");
	// Toda máscara de campo es representable (4 lados de 4 materiales: 256 combinaciones).
	u16 inst = 0u;
	for (u8 n = 0u; n < 4u; ++n) {
		for (u8 e = 0u; e < 4u; ++e) {
			for (u8 s = 0u; s < 4u; ++s) {
				for (u8 w = 0u; w < 4u; ++w) {
					const u8 sd[4] = {n, e, s, w};
					const u16 p = set.tile_for(u8(terr::Biome::Grass), sd, terr::Feature::None, fe,
								   u8(terr::Biome::Grass), atlas);
					if (p < set.count) ++inst;
				}
			}
		}
	}
	check(inst == 256u, "las 256 mascaras de campo son representables");
	check(set.overflow == 0u, "sin truncar piezas");
}

void test_set() {
	test_set_scalar<MiniFloat16>("set de biomas MF");
	test_set_scalar<float>("set de biomas float");
}

// ---------------------------------------------------------------------------
//  4. Mundo generado: conectado, con gramática, determinista y en paleta
// ---------------------------------------------------------------------------

template <class S>
void test_world_scalar(const char* tag, terr::TerrainLayout<20, 14>& out_ref,
		       terr::BiomeTileSet<16, 16, 224>& set_ref) {
	proc::TextureAtlas<16, 16, S> atlas {};
	atlas.generate(0x7E5A1u);
	terr::BiomeTileSet<16, 16, 224> set {};
	terr::TerrainLayout<20, 14> lay {};
	const bool ok = terr::generate_biome_world<20, 14>(lay, set, atlas, 0xBEE5u);
	check(ok, tag); // conectado por construcción y sin truncar piezas
	check(terr::biome_world_connected(lay, set), tag);
	// Gramática: ningún par adyacente de materiales de campo prohibido.
	bool grammar_ok = true;
	for (u16 y = 0u; y < 14u; ++y) {
		for (u16 x = 0u; x < 20u; ++x) {
			const auto mat_at = [&](u16 cx, u16 cy) {
				return terr::Biome(set.specs[lay.at(cx, cy)].base);
			};
			if (x + 1u < 20u) {
				grammar_ok = grammar_ok &&
					     terr::biome_adjacent(mat_at(x, y), mat_at(u16(x + 1u), y));
			}
			if (y + 1u < 14u) {
				grammar_ok = grammar_ok &&
					     terr::biome_adjacent(mat_at(x, y), mat_at(x, u16(y + 1u)));
			}
		}
	}
	check(grammar_ok, tag);
	// Todas las piezas en paleta (32 colores).
	bool in_palette = true;
	for (u16 t = 0u; t < set.count; ++t) {
		for (u16 i = 0u; i < 16u * 16u; ++i) {
			in_palette = in_palette && set.tiles[t].pixels[i] < 32u;
		}
	}
	check(in_palette, tag);
	// El mundo usa una variedad amplia de piezas (máscaras distintas) y no solo un puñado.
	check(set.count >= 40u, tag);
	out_ref = lay;
	set_ref = set;
}

// Huellas FNV-1a del mundo generado, con el MISMO método que la vía OFFLINE de la demo
// (`tools/gen_assets.cpp`): mismas semillas y misma librería ⇒ los MISMOS números. Ancla
// que la vía offline, la runtime (este test) y la de compilación (que usa esta misma
// función) producen el mismo mundo.
/// Actualiza una FNV-1a continuando desde `h` (el método de la vía offline: los objetos se
/// hashean byte a byte en secuencia, no por bloques).
u32 fnv_update(u32 h, const void* data, u32 n) {
	const u8* p = static_cast<const u8*>(data);
	for (u32 i = 0u; i < n; ++i) {
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

void test_world_hashes() {
	proc::TextureAtlas<16, 16, MiniFloat16> atlas {};
	atlas.generate(0x7E5A1u);
	terr::BiomeTileSet<16, 16, 224> set {};
	terr::TerrainLayout<20, 14> lay {};
	check(terr::generate_biome_world<20, 14>(lay, set, atlas, 0xBEE5u), "mundo de huellas ok");
	u32 h_pal = fnv_update(0x811C9DC5u, atlas.palette.color, sizeof(atlas.palette.color));
	u32 h_tex = 0x811C9DC5u;
	for (u16 m = 0u; m < proc::kMaterialCount; ++m) {
		for (u16 v = 0u; v < proc::kVariantsPerMaterial; ++v) {
			h_tex = fnv_update(h_tex, atlas.get(proc::MaterialId(m), u8(v)).pixels, 256u);
		}
	}
	u32 h_tiles = 0x811C9DC5u;
	for (u16 t = 0u; t < set.count; ++t) {
		h_tiles = fnv_update(h_tiles, set.tiles[t].pixels, 256u);
	}
	const u32 h_map = fnv_update(0x811C9DC5u, lay.cells, sizeof(lay.cells));
	check(h_pal == 0xA732D734u, "huella de paleta = via offline");
	check(h_tex == 0xC0A35557u, "huella de texturas = via offline");
	check(h_tiles == 0x740A678Cu, "huella de piezas = via offline");
	check(h_map == 0x093DDAB0u, "huella de mapa = via offline");
}

void test_world() {
	terr::TerrainLayout<20, 14> lay_a {};
	terr::BiomeTileSet<16, 16, 224> set_a {};
	test_world_scalar<MiniFloat16>("mundo de biomas MF", lay_a, set_a);
	// Determinismo: segunda corrida idéntica (layout y set).
	proc::TextureAtlas<16, 16, MiniFloat16> atlas {};
	atlas.generate(0x7E5A1u);
	terr::BiomeTileSet<16, 16, 224> set_b {};
	terr::TerrainLayout<20, 14> lay_b {};
	check(terr::generate_biome_world<20, 14>(lay_b, set_b, atlas, 0xBEE5u), "mundo B ok");
	check(std::memcmp(lay_a.cells, lay_b.cells, sizeof(lay_a.cells)) == 0,
	      "mundo de biomas determinista");
	// El mundo contiene las estructuras esperadas (rasgos en las piezas).
	bool gate = false, tower = false, bridge = false, house = false, cave = false;
	for (u16 i = 0u; i < set_a.count; ++i) {
		const terr::Feature f = set_a.specs[i].features;
		gate = gate || terr::has(f, terr::Feature::Gate);
		tower = tower || terr::has(f, terr::Feature::Tower);
		bridge = bridge || terr::has(f, terr::Feature::Bridge);
		house = house || terr::has(f, terr::Feature::House);
		cave = cave || terr::has(f, terr::Feature::CaveMouth);
	}
	check(gate && tower && house && cave, "mundo con castillo/alcalde/cueva");
	check(bridge, "mundo con puente (camino cruza el rio)");
	// Instanciación con `float` (test negativo de §1.11) del mundo completo.
	terr::TerrainLayout<20, 14> lay_f {};
	terr::BiomeTileSet<16, 16, 224> set_f {};
	test_world_scalar<float>("mundo de biomas float", lay_f, set_f);
}

// ---------------------------------------------------------------------------
//  Evidencia visual opcional: PPM del mundo
// ---------------------------------------------------------------------------

void dump_visual_evidence(const char* dir) {
	proc::TextureAtlas<16, 16, MiniFloat16> atlas {};
	atlas.generate(0x7E5A1u);
	terr::BiomeTileSet<16, 16, 224> set {};
	terr::TerrainLayout<20, 14> lay {};
	if (!terr::generate_biome_world<20, 14>(lay, set, atlas, 0xBEE5u)) {
		std::printf("[dump] ERROR: el mundo de biomas no quedo conectado\n");
	}
	char path[512];
	std::snprintf(path, sizeof(path), "%s/biome_map.ppm", dir);
	std::FILE* f = std::fopen(path, "wb");
	if (f == nullptr) return;
	std::fprintf(f, "P6\n%u %u\n255\n", unsigned(20u * 16u), unsigned(14u * 16u));
	for (u16 cy = 0u; cy < 14u; ++cy) {
		for (u16 row = 0u; row < 16u; ++row) {
			for (u16 cx = 0u; cx < 20u; ++cx) {
				for (u16 px = 0u; px < 16u; ++px) {
					const u8 v = set.tiles[lay.at(cx, cy)].at(px, row);
					const eng::u16 c = atlas.palette.color[v & 31u];
					const u8 rgb[3] = {u8(((c >> 8u) & 0xFu) * 17u),
							   u8(((c >> 4u) & 0xFu) * 17u),
							   u8((c & 0xFu) * 17u)};
					std::fwrite(rgb, 1u, 3u, f);
				}
			}
		}
	}
	std::fclose(f);
	std::printf("[dump] mundo de biomas %ux%u celdas, %u piezas -> %s\n",
		    unsigned(20u), unsigned(14u), unsigned(set.count), path);
}

} // namespace

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) g_dump_dir = argv[i + 1];
	}
	std::printf("== HOST-445 mundo de biomas (compatibilidad y set por mascaras) ==\n");
	test_noise_tile();
	test_grammar();
	test_set();
	test_world();
	test_world_hashes();
	if (g_dump_dir != nullptr) dump_visual_evidence(g_dump_dir);
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: mundo de biomas validado (gramatica, mascaras completas, conectado).\n");
	return 0;
}
