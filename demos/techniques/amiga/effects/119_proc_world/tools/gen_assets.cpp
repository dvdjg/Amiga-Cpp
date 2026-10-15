// ============================================================================
// Generador de assets (HOST) de la demo 119 — PROC WORLD.
// ============================================================================
//
// Genera con la MISMA librería del engine (`eng::gfx::proc`, `MiniFloat16`) el atlas y
// el set de piezas de terreno que la demo incrusta, y los vuelca a un header C++
// (`out/assets/proc-world/proc_world_assets.h`). Se ejecuta desde `src/prebuild.sh`
// (hook de `tools/build/build-demo.sh`).
//
// Por qué en host y no en carga en el Amiga: la generación completa cuesta ~3,4 G de
// ciclos en un 68000 (medido con el periférico de depuración; el ruido paga
// `__mulsi3` en cada hash y una textura de 32×32 ronda 50 M de ciclos). Los assets
// son deterministas byte a byte (`MiniFloat16` es aritmética entera; lo ancla
// `HOST-443`), así que el host los cocina una vez y la demo solo ALIASA el resultado.
//
//   g++ -std=gnu++23 -O2 -Iengine/include tools/gen_assets.cpp -o gen_assets && gen_assets <out.h>

#include <cstdio>
#include <cstring>

#include <eng/core/math/minifloat.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/terrain.hpp>
#include <eng/graphics/proc/terrain_place.hpp>

namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
using eng::u16;
using eng::u32;
using MF = eng::math::MiniFloat16;

// Semillas compartidas con la demo y con la evidencia host (HOST-442/443): el contenido
// incrustado es EXACTAMENTE el que muestran los dumps de `out/playground/proc-bitmaps/`.
constexpr u32 kAtlasSeed = 0x7E5A1u;
constexpr u32 kSetSeed = 0xBEE5u;

int main(int argc, char** argv) {
	const char* out_path = argc > 1 ? argv[1] : "out/assets/proc-world/proc_world_assets.h";
	std::FILE* f = std::fopen(out_path, "wb");
	if (f == nullptr) {
		std::printf("gen_assets: no se puede escribir %s\n", out_path);
		return 1;
	}

	proc::TextureAtlas<32, 32, MF> atlas {};
	atlas.generate(kAtlasSeed);
	// Capacidad: catálogo (45) + rotaciones de relleno cocidas aquí + las derivadas del
	// río/caminos que el colocador añade en carga (9). 45 + 24 + 9 = 78 <= 80.
	terr::TerrainTileSet<32, 32, 80> set {};
	set.generate(kSetSeed, atlas, 6u);
	// **Rotaciones de relleno pre-derivadas** (bordes todo `Grass` del catálogo: hierba,
	// árboles, árboles+rocas, sierra, lago, cultivos y casas): el colocador de la demo
	// elige entre ellas y el mapa no repite la misma textura; cocerlas aquí evita pagar
	// `add_variant` (1 KB transformado por variante) en el 68000 de carga. Solo el
	// catálogo (no las 12 variaciones extra, que ya aportan riqueza con otros seeds).
	{
		constexpr u16 catalog_n =
			u16(sizeof(terr::kCastlePieces) / sizeof(terr::kCastlePieces[0]) +
			    sizeof(terr::kRiverPieces) / sizeof(terr::kRiverPieces[0]) +
			    sizeof(terr::kNaturePieces) / sizeof(terr::kNaturePieces[0]));
		for (u16 i = 0; i < catalog_n; ++i) {
			const auto& e = set.specs[i].edges;
			if (e.n != terr::Edge::Grass || e.e != terr::Edge::Grass ||
			    e.s != terr::Edge::Grass || e.w != terr::Edge::Grass) {
				continue;
			}
			(void)set.add_variant(i, proc::Transform::Rot90);
			(void)set.add_variant(i, proc::Transform::Rot180);
			(void)set.add_variant(i, proc::Transform::Rot270);
		}
	}

	std::fprintf(f, "// Generado por demos/techniques/amiga/effects/119_proc_world/tools/gen_assets.cpp\n");
	std::fprintf(f, "// NO EDITAR a mano: regenerar con src/prebuild.sh (o borrando este fichero).\n");
	std::fprintf(f, "#pragma once\n\n");
	std::fprintf(f, "// Paleta RGB444 del atlas (32 colores compartidos por todo el mundo; 5 planos).\n");
	std::fprintf(f, "static constexpr unsigned short kWorldPalette[32] = {\n\t");
	for (u16 i = 0; i < 32u; ++i) {
		std::fprintf(f, "0x%04X%s", unsigned(atlas.palette.color[i]), i + 1u < 32u ? ", " : "");
		if ((i % 8u) == 7u && i + 1u < 32u) std::fprintf(f, "\n\t");
	}
	std::fprintf(f, "\n};\n\n");

	std::fprintf(f, "// Piezas ya pintadas (índices de paleta, 32x32 = 1024 B cada una).\n");
	std::fprintf(f, "static constexpr unsigned char kWorldTiles[%u][1024] = {\n", unsigned(set.count));
	for (u16 t = 0; t < set.count; ++t) {
		std::fprintf(f, "\t{");
		for (u16 i = 0; i < 1024u; ++i) {
			if ((i % 24u) == 0u) std::fprintf(f, "\n\t\t");
			std::fprintf(f, "%u,", unsigned(set.tiles[t].pixels[i]));
		}
		std::fprintf(f, "\n\t},\n");
	}
	std::fprintf(f, "};\n\n");

	std::fprintf(f, "// Especificación de cada pieza (bordes N/E/S/W + máscara de rasgos).\n");
	std::fprintf(f, "struct WorldSpecData { unsigned char n, e, s, w; unsigned short features; };\n");
	std::fprintf(f, "static constexpr WorldSpecData kWorldSpecs[%u] = {\n", unsigned(set.count));
	for (u16 t = 0; t < set.count; ++t) {
		const terr::TileSpec& s = set.specs[t];
		std::fprintf(f, "\t{%u, %u, %u, %u, 0x%04X},\n", unsigned(eng::util::to_underlying(s.edges.n)),
			     unsigned(eng::util::to_underlying(s.edges.e)),
			     unsigned(eng::util::to_underlying(s.edges.s)),
			     unsigned(eng::util::to_underlying(s.edges.w)),
			     unsigned(eng::util::to_underlying(s.features)));
	}
	std::fprintf(f, "};\n\n");
	std::fprintf(f, "static constexpr unsigned kWorldTileCount = %u;\n", unsigned(set.count));
	std::fclose(f);

	std::printf("gen_assets: %u piezas + paleta -> %s\n", unsigned(set.count), out_path);
	return 0;
}
