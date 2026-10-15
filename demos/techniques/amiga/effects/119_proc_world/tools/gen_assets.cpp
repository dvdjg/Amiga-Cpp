// ============================================================================
// Generador OFFLINE de assets (HOST) de la demo 119 — PROC WORLD.
// ============================================================================
//
// Vía **offline** del pipeline de la demo: genera con la MISMA librería del engine
// (`eng::gfx::proc`, `MiniFloat16`, mismas semillas) el atlas de materiales Y el mundo de
// biomas completo (layout + piezas pintadas + especificaciones) y los vuelca a un header
// C++ (`out/assets/proc-world/proc_world_assets.h`).
//
// La demo 119 consume la vía **en compilación** (`constexpr` en `src/main.cpp`); este
// generador produce EXACTAMENTE el mismo contenido (determinismo byte a byte: mismas
// semillas y misma librería — lo anclan `HOST-443`/`HOST-445`) y sirve para:
//   - consumidores que no puedan pagar la evaluación `constexpr` del compilador,
//   - otros proyectos/targets que quieran los datos como header,
//   - inspección/evidencia de los assets sin arrancar la demo.
//
//   g++ -std=gnu++23 -O2 -Iengine/include tools/gen_assets.cpp -o gen_assets
//   ./gen_assets out/assets/proc-world/proc_world_assets.h
//
// (Si algún día se quiere volver a precocinar la demo, basta añadir un `src/prebuild.sh`
// que invoque este binario; el hook de `build-demo.sh` lo detecta por nombre.)

#include <cstdio>
#include <cstring>

#include <eng/core/math/minifloat.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/terrain_biome.hpp>

namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
using eng::u16;
using eng::u32;
using eng::u8;
using MF = eng::math::MiniFloat16;

// Semillas compartidas con la demo y con la evidencia host (HOST-443/445): el contenido
// emitido es EXACTAMENTE el mundo de `out/playground/proc-bitmaps/v4/` y el que la demo
// genera en compilación.
constexpr u32 kAtlasSeed = 0x7E5A1u;
constexpr u32 kMapSeed = 0xBEE5u;
constexpr u16 kTile = 16u;
constexpr u16 kMapW = 20u;
constexpr u16 kMapH = 14u;
constexpr u16 kMaxTiles = 224u;

/// FNV-1a de 32 bits (para imprimir una huella comparable del mundo emitido).
static u32 fnv1a(u32 h, u8 b) {
	h ^= b;
	return h * 16777619u;
}
template <class T>
static u32 hash_obj(u32 h, const T& obj) {
	const u8* p = reinterpret_cast<const u8*>(&obj);
	for (eng::usize i = 0; i < sizeof(T); ++i) h = fnv1a(h, p[i]);
	return h;
}

int main(int argc, char** argv) {
	const char* out_path = argc > 1 ? argv[1] : "out/assets/proc-world/proc_world_assets.h";
	std::FILE* f = std::fopen(out_path, "wb");
	if (f == nullptr) {
		std::printf("gen_assets: no se puede escribir %s\n", out_path);
		return 1;
	}

	proc::TextureAtlas<kTile, kTile, MF> atlas {};
	atlas.generate(kAtlasSeed);
	terr::BiomeTileSet<kTile, kTile, kMaxTiles> set {};
	terr::TerrainLayout<kMapW, kMapH> layout {};
	if (!terr::generate_biome_world<kMapW, kMapH>(layout, set, atlas, kMapSeed)) {
		std::printf("gen_assets: el mundo de biomas no quedo conectado\n");
		std::fclose(f);
		return 1;
	}

	std::fprintf(f, "// Generado por demos/techniques/amiga/effects/119_proc_world/tools/gen_assets.cpp\n");
	std::fprintf(f, "// NO EDITAR a mano: regenerar con tools/gen_assets (ver README de la demo).\n");
	std::fprintf(f, "#pragma once\n\n");

	std::fprintf(f, "// Paleta RGB444 del atlas (32 colores compartidos por todo el mundo; 5 planos).\n");
	std::fprintf(f, "static constexpr unsigned short kWorldPalette[32] = {\n\t");
	for (u16 i = 0; i < 32u; ++i) {
		std::fprintf(f, "0x%04X%s", unsigned(atlas.palette.color[i]), i + 1u < 32u ? ", " : "");
		if ((i % 8u) == 7u && i + 1u < 32u) std::fprintf(f, "\n\t");
	}
	std::fprintf(f, "\n};\n\n");

	std::fprintf(f, "// Texturas de material (indices de paleta, %ux%u = %u B cada una).\n", unsigned(kTile),
		     unsigned(kTile), unsigned(kTile) * kTile);
	std::fprintf(f, "static constexpr unsigned char kWorldTextures[%u][%u][%u] = {\n",
		     unsigned(proc::kMaterialCount), unsigned(proc::kVariantsPerMaterial),
		     unsigned(kTile) * kTile);
	for (u16 m = 0; m < proc::kMaterialCount; ++m) {
		std::fprintf(f, "\t{\n");
		for (u16 v = 0; v < proc::kVariantsPerMaterial; ++v) {
			std::fprintf(f, "\t{");
			const auto& t = atlas.get(proc::MaterialId(m), u8(v));
			for (u16 i = 0; i < kTile * kTile; ++i) {
				if ((i % 24u) == 0u) std::fprintf(f, "\n\t\t");
				std::fprintf(f, "%u,", unsigned(t.pixels[i]));
			}
			std::fprintf(f, "\n\t},\n");
		}
		std::fprintf(f, "\t},\n");
	}
	std::fprintf(f, "};\n\n");

	std::fprintf(f, "// Piezas del mundo ya pintadas (indices de paleta, %ux%u).\n", unsigned(kTile),
		     unsigned(kTile));
	std::fprintf(f, "static constexpr unsigned char kWorldTiles[%u][%u] = {\n", unsigned(set.count),
		     unsigned(kTile) * kTile);
	for (u16 t = 0; t < set.count; ++t) {
		std::fprintf(f, "\t{");
		for (u16 i = 0; i < kTile * kTile; ++i) {
			if ((i % 24u) == 0u) std::fprintf(f, "\n\t\t");
			std::fprintf(f, "%u,", unsigned(set.tiles[t].pixels[i]));
		}
		std::fprintf(f, "\n\t},\n");
	}
	std::fprintf(f, "};\n\n");

	std::fprintf(f, "// Especificacion de cada pieza (material + id expuesto + lados N/E/S/W + rasgos).\n");
	std::fprintf(f, "struct WorldSpecData { unsigned char base, id, n, e, s, w; unsigned short features; };\n");
	std::fprintf(f, "static constexpr WorldSpecData kWorldSpecs[%u] = {\n", unsigned(set.count));
	for (u16 t = 0; t < set.count; ++t) {
		const terr::BiomeSpec& s = set.specs[t];
		std::fprintf(f, "\t{%u, %u, %u, %u, %u, %u, 0x%04X},\n", unsigned(s.base), unsigned(s.id),
			     unsigned(s.sides[0]), unsigned(s.sides[1]), unsigned(s.sides[2]),
			     unsigned(s.sides[3]), unsigned(eng::util::to_underlying(s.features)));
	}
	std::fprintf(f, "};\n\n");
	std::fprintf(f, "static constexpr unsigned kWorldTileCount = %u;\n", unsigned(set.count));
	std::fprintf(f, "static constexpr unsigned kWorldMapW = %u;\n", unsigned(kMapW));
	std::fprintf(f, "static constexpr unsigned kWorldMapH = %u;\n", unsigned(kMapH));
	std::fprintf(f, "// Layout %ux%u: indice de pieza por celda (fila mayor).\n", unsigned(kMapW),
		     unsigned(kMapH));
	std::fprintf(f, "static constexpr unsigned short kWorldLayout[%u][%u] = {\n", unsigned(kMapH),
		     unsigned(kMapW));
	for (u16 y = 0; y < kMapH; ++y) {
		std::fprintf(f, "\t{");
		for (u16 x = 0; x < kMapW; ++x) {
			std::fprintf(f, "%u%s", unsigned(layout.at(x, y)), x + 1u < kMapW ? "," : "");
		}
		std::fprintf(f, "},\n");
	}
	std::fprintf(f, "};\n");
	std::fclose(f);

	// Huellas del contenido emitido (comparables entre generaciones y con la evidencia host).
	u32 h_pal = 0x811C9DC5u;
	h_pal = hash_obj(h_pal, atlas.palette.color);
	u32 h_tex = 0x811C9DC5u;
	for (u16 m = 0; m < proc::kMaterialCount; ++m) {
		for (u16 v = 0; v < proc::kVariantsPerMaterial; ++v) {
			h_tex = hash_obj(h_tex, atlas.get(proc::MaterialId(m), u8(v)).pixels);
		}
	}
	u32 h_tiles = 0x811C9DC5u;
	for (u16 t = 0; t < set.count; ++t) h_tiles = hash_obj(h_tiles, set.tiles[t].pixels);
	u32 h_map = hash_obj(0x811C9DC5u, layout.cells);

	std::printf("gen_assets: %u piezas (%ux%u) + atlas + layout %ux%u -> %s\n", unsigned(set.count),
		    unsigned(kTile), unsigned(kTile), unsigned(kMapW), unsigned(kMapH), out_path);
	std::printf("gen_assets: huellas paleta=0x%08X texturas=0x%08X piezas=0x%08X mapa=0x%08X\n",
		    unsigned(h_pal), unsigned(h_tex), unsigned(h_tiles), unsigned(h_map));
	return 0;
}
