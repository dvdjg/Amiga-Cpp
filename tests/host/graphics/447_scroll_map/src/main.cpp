// ============================================================================
// Test HOST-447: mapa grande con scroll (`TerrainLayout` → `TileLayerMap`).
// ============================================================================
//
// Cubre el uso del mundo de biomas como **mapa lógico de scroll**: se genera un mundo
// grande (40×28 celdas de 16×16), se envuelve como `eng::playfield::TileLayerMap` (sin
// conversión de datos: `cells`/`width`/`height`) con **wrap toroidal** y se verifica la
// semántica que necesita una cámara de scroll: muestreo de ventanas por `tile_at` en
// offsets crecientes (incluido el cruce de la costura del wrap), coincidencia exacta con
// el layout indexado a mano, validez de índices y `edge_tile` fuera del mundo sin wrap.
// Es el puente host del paso «mapa grande + scroll» (la integración con el driver
// X-Limited va aparte; ver la nota final del README).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/447_scroll_map

#include <cstdio>
#include <cstring>

#include <eng/core/math/minifloat.hpp>
#include <eng/field/tile_map.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/terrain_biome.hpp>

namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
using eng::u16;
using eng::u32;
using eng::u8;
using eng::math::MiniFloat16;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

constexpr u16 kMapW = 40u;
constexpr u16 kMapH = 28u;
constexpr u16 kCamW = 20u;
constexpr u16 kCamH = 14u;

void test_scroll_window() {
	// Mundo grande (40×28): 16 veces las celdas de la demo; el set memoiza por máscara,
	// así que el número de piezas distintas crece muy por debajo del de celdas.
	proc::TextureAtlas<16, 16, MiniFloat16> atlas {};
	atlas.generate(0x7E5A1u);
	terr::BiomeTileSet<16, 16, 320> set {};
	terr::TerrainLayout<kMapW, kMapH> lay {};
	check(terr::generate_biome_world<kMapW, kMapH>(lay, set, atlas, 0xBEE5u),
	      "mundo grande (40x28) conectado");
	check(set.count < 320u, "el set memoiza por mascara (no crece con las celdas)");

	// Envoltorio como mapa lógico de scroll: toroidal en X (mundo «sin costura» lateral),
	// acotado en Y, `edge_tile` = 0 fuera del mundo.
	eng::playfield::TileLayerMap map {};
	map.cells = lay.span();
	map.width = kMapW;
	map.height = kMapH;
	map.wrap_x = kMapW;
	map.edge_tile = 0u;

	// 1) Ventanas de cámara en offsets crecientes (incluye cruces de la costura): cada
	//    celda muestreada debe coincidir con el layout indexado a mano con módulo X.
	bool windows_ok = true;
	bool wrap_seen = false;
	for (u32 off = 0u; off < u32(kMapW) * 3u; off += 3u) {
		for (u16 cy = 0u; cy < kCamH; ++cy) {
			for (u16 cx = 0u; cx < kCamW; ++cx) {
				const eng::s32 wx = eng::s32(off) + eng::s32(cx);
				const eng::s32 wy = eng::s32(cy);
				const u16 want = lay.at(u16(u32(wx) % kMapW), cy);
				const u16 got = map.tile_at(wx, wy);
				windows_ok = windows_ok && got == want && got < set.count;
				if (eng::s32(u32(wx) % kMapW) != wx) wrap_seen = true;
			}
		}
	}
	check(windows_ok, "ventanas de scroll: tile_at == layout con wrap (indices validos)");
	check(wrap_seen, "el barrido cruza la costura del wrap");

	// 2) Offset envuelto negativo (scroll hacia atrás): -1 ≡ width-1.
	bool wrap_neg = true;
	for (u16 cy = 0u; cy < kCamH; ++cy) {
		wrap_neg = wrap_neg && map.tile_at(-1, eng::s32(cy)) == lay.at(kMapW - 1u, cy);
		wrap_neg = wrap_neg && map.tile_at(-kMapW - 5, eng::s32(cy)) == lay.at(kMapW - 5u, cy);
	}
	check(wrap_neg, "wrap tambien hacia atras (offsets negativos)");

	// 3) Sin wrap en Y: fuera del mundo -> edge_tile (y la costura horizontal sigue).
	check(map.tile_at(3, -1) == 0u && map.tile_at(3, eng::s32(kMapH)) == 0u,
	      "fuera del mundo en Y -> edge_tile");
	check(map.tile_at(3, eng::s32(kMapH - 1u)) == lay.at(3u, kMapH - 1u),
	      "ultima fila dentro del mundo");

	// 4) Ancla concreta del wrap: un tile con agua al oeste del mundo aparece en la
	//    ventana desplazada `+kMapW` exactamente en la misma celda de pantalla.
	u16 water_x = kMapW, water_y = 0u;
	for (u16 y = 0u; y < kMapH && water_x == kMapW; ++y) {
		for (u16 x = 0u; x < kMapW; ++x) {
			const terr::BiomeSpec& s = set.specs[lay.at(x, y)];
			if (s.base == u8(terr::Biome::Water)) {
				water_x = x;
				water_y = y;
				break;
			}
		}
	}
	check(water_x < kMapW, "el mundo grande contiene agua");
	const u16 a = map.tile_at(eng::s32(water_x), eng::s32(water_y));
	const u16 b = map.tile_at(eng::s32(water_x) + eng::s32(kMapW), eng::s32(water_y));
	check(a == b && a == lay.at(water_x, water_y), "agua identica un periodo despues");
}

} // namespace

int main() {
	std::printf("== HOST-447 mapa grande con scroll (TileLayerMap) ==\n");
	test_scroll_window();
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: ventanas de scroll y wrap del mapa grande validados.\n");
	return 0;
}
