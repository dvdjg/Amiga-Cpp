// ============================================================================
// Test HOST-443: golden de la generación procedural (`eng/graphics/proc/`).
// ============================================================================
//
// Congela el resultado de la generación con `MiniFloat16` (aritmética entera: portable
// byte a byte entre compiladores/máquinas) mediante hashes FNV-1a de:
//   - la paleta del atlas (16 colores),
//   - las 22×3 texturas de material,
//   - las 42 piezas del catálogo de terreno,
//   - el mapa que produce el colocador (10×7, dos semillas).
//
// Si un refactor cambia el RENDER (colores, texturas, piezas o colocación), este test
// falla y hay que decidir de forma explícita: es la red que evita cambios visuales
// accidentales. Al aceptar un cambio visual, actualizar los valores de `kGolden*`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/443_proc_golden

#include <cstdio>

#include <eng/core/math/minifloat.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/terrain.hpp>
#include <eng/graphics/proc/terrain_place.hpp>

namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
using eng::u16;
using eng::u32;
using eng::u8;
using MF = eng::math::MiniFloat16;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// FNV-1a de 32 bits sobre un byte.
constexpr u32 fnv1a(u32 h, u8 b) {
	h ^= b;
	return h * 16777619u;
}

/// Hash FNV-1a de un rango de bytes.
u32 hash_bytes(u32 h, const void* data, u32 n) {
	const u8* p = static_cast<const u8*>(data);
	for (u32 i = 0; i < n; ++i) h = fnv1a(h, p[i]);
	return h;
}

// Valores dorados (semilla 0x7E5A1u / sets 0xBEE5u y 0x1234u). Regenerar tras un
// cambio visual ACEPTADO: anotar el nuevo valor y actualizarlo aquí.
constexpr u32 kGoldenPalette = 0x5C7DD62Eu;
constexpr u32 kGoldenTextures = 0x81CC37FBu;
constexpr u32 kGoldenTiles = 0xFDDBB016u;
constexpr u32 kGoldenMapSeedBEE5 = 0xBA2CDC19u;
constexpr u32 kGoldenMapSeed1234 = 0x85908855u;

} // namespace

int main() {
	std::printf("== HOST-443 proc golden (MiniFloat16, hashes FNV-1a) ==\n");

	proc::TextureAtlas<32, 32, MF> atlas {};
	atlas.generate(0x7E5A1u, 16u);

	u32 h_pal = hash_bytes(0x811C9DC5u, atlas.palette.color, sizeof(atlas.palette.color));
	std::printf("  paleta : 0x%08X\n", unsigned(h_pal));

	u32 h_tex = 0x811C9DC5u;
	for (u8 m = 0; m < proc::kMaterialCount; ++m) {
		for (u8 v = 0; v < proc::kVariantsPerMaterial; ++v) {
			h_tex = hash_bytes(h_tex, atlas.get(proc::MaterialId(m), v).pixels, 32u * 32u);
		}
	}
	std::printf("  texturas: 0x%08X\n", unsigned(h_tex));

	terr::TerrainTileSet<32, 32, 48> set {};
	set.generate(0xBEE5u, atlas, 6u);
	check(set.count == 42u, "catalogo de 42 piezas");
	u32 h_tiles = 0x811C9DC5u;
	for (u16 t = 0; t < set.count; ++t) {
		h_tiles = hash_bytes(h_tiles, set.tiles[t].pixels, 32u * 32u);
	}
	std::printf("  piezas : 0x%08X\n", unsigned(h_tiles));

	// Colocador con dos semillas (el mapa depende de la semilla; ambos quedan congelados).
	terr::TerrainLayout<10, 7> map_a {};
	const bool ok_a = terr::place_world(map_a, set, 0xBEE5u);
	check(ok_a && terr::layout_is_connected(map_a, set), "mapa semilla BEE5 conectado");
	const u32 h_map_a = hash_bytes(0x811C9DC5u, map_a.cells, sizeof(map_a.cells));
	std::printf("  mapa A : 0x%08X\n", unsigned(h_map_a));

	terr::TerrainTileSet<32, 32, 48> set_b {};
	set_b.generate(0xBEE5u, atlas, 6u);
	terr::TerrainLayout<10, 7> map_b {};
	const bool ok_b = terr::place_world(map_b, set_b, 0x1234u);
	check(ok_b && terr::layout_is_connected(map_b, set_b), "mapa semilla 1234 conectado");
	const u32 h_map_b = hash_bytes(0x811C9DC5u, map_b.cells, sizeof(map_b.cells));
	std::printf("  mapa B : 0x%08X\n", unsigned(h_map_b));

	// Determinismo dentro de la misma corrida (misma entrada -> mismo hash).
	u32 h_pal2 = hash_bytes(0x811C9DC5u, atlas.palette.color, sizeof(atlas.palette.color));
	check(h_pal2 == h_pal, "hash de paleta reproducible");

	// Golden: cualquier cambio visual cambia el hash -> revisar y actualizar si es deseado.
	check(h_pal == kGoldenPalette, "golden paleta");
	check(h_tex == kGoldenTextures, "golden texturas");
	check(h_tiles == kGoldenTiles, "golden piezas");
	check(h_map_a == kGoldenMapSeedBEE5, "golden mapa (BEE5)");
	check(h_map_b == kGoldenMapSeed1234, "golden mapa (1234)");

	if (g_fail != 0) {
		std::printf("%d fallo(s): si el cambio visual es ACEPTADO, actualiza kGolden* con los valores impresos.\n",
			    g_fail);
		return 1;
	}
	std::printf("OK: golden congelado (paleta, texturas, piezas y colocador con 2 semillas).\n");
	return 0;
}
