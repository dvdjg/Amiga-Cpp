// ============================================================================
// Test HOST-498: lógica pura de Caverna (colisión, dead-zone) y regresión de
// las filas de staging del mapa PF2 generado por el pipeline de assets.
// ============================================================================

#include <cstdio>
#include <cstdlib>

#include "../../../../../games/500_caverna/src/caverna_gameplay.hpp"

namespace {

constexpr eng::u16 kMapWidth = 256u;
constexpr eng::u16 kMapHeight = 20u;
eng::u16 g_cells[kMapWidth * kMapHeight] {};

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

bool load_map() {
	// El generador es la fuente del arte; se ejecuta aquí para que el test sea
	// autocontenido en un checkout limpio (los blobs viven en out/, gitignored).
	if (std::system("node tools/assets/gen-caverna.mjs >/dev/null 2>&1") != 0) {
		std::printf("[SKIP] no se pudo ejecutar node/tools/assets/gen-caverna.mjs\n");
		return false;
	}
	std::FILE* f = std::fopen("out/assets/caverna/map_pf2.bin", "rb");
	if (f == nullptr) return false;
	eng::u8 raw[kMapWidth * kMapHeight * 2u] {};
	const std::size_t n = std::fread(raw, 1u, sizeof(raw), f);
	std::fclose(f);
	if (n != sizeof(raw)) return false;
	for (eng::usize i = 0u; i < kMapWidth * kMapHeight; ++i) {
		g_cells[i] = static_cast<eng::u16>((static_cast<eng::u16>(raw[i * 2u]) << 8u) |
						   raw[i * 2u + 1u]);
	}
	return true;
}

} // namespace

int main() {
	std::printf("== HOST-498 caverna gameplay ==\n");
	if (!load_map()) {
		std::printf("[SKIP] faltan los blobs de Caverna\n");
		return 3;
	}
	const eng::Span<const eng::u16> cells {g_cells, kMapWidth * kMapHeight};

	// --- Regresión del bloque de staging -----------------------------------
	// El walk horizontal lee filas de mapa hasta 17; las filas 14..19 no son
	// visibles con y_mode=Off y deben quedar vacías. Si vuelven a tener terreno,
	// el scroll lo revela como un rectángulo en la parte alta (DT-046).
	for (eng::u16 y = 14u; y < kMapHeight; ++y) {
		for (eng::u16 x = 0u; x < kMapWidth; ++x) {
			if (cells[static_cast<eng::u32>(y) * kMapWidth + x] != 0u) {
				std::printf("[FAIL] staging PF2 no vacío en (%u,%u)\n", x, y);
				++g_fail;
				y = kMapHeight; // salir
				break;
			}
		}
	}
	check(cells[static_cast<eng::u32>(13u) * kMapWidth + 10u] >= 8u,
	      "fila 13 sigue siendo terreno visible");
	check(cells[static_cast<eng::u32>(8u) * kMapWidth + 20u] >= 12u,
	      "plataforma flotante en (20,8)");

	// --- Colisión de caja contra caras de tile -----------------------------
	check(cells[static_cast<eng::u32>(10u) * kMapWidth + 10u] >= caverna::kSolidFirst,
	      "tile de suelo sólido");
	check(caverna::box_hits_solid(cells, kMapWidth, kMapHeight, 10u * 16u, 10u * 16u, 16, 16),
	      "caja dentro del tile sólido");
	check(caverna::box_hits_solid(cells, kMapWidth, kMapHeight, 10u * 16u + 15u,
				      10u * 16u, 16, 16),
	      "cara derecha: caja que entra desde la izquierda");
	check(caverna::box_hits_solid(cells, kMapWidth, kMapHeight, 10u * 16u,
				      10u * 16u + 15u, 16, 16),
	      "cara superior: caja que cae encima");
	check(!caverna::box_hits_solid(cells, kMapWidth, kMapHeight, 10u * 16u, 0u, 16, 16),
	      "celda de aire no colisiona");

	// --- Cámara con dead-zone ----------------------------------------------
	check(caverna::dead_zone_camera(160, 0, 4096, 320, 96, 224) == 0,
	      "jugador dentro de la banda: cámara quieta");
	check(caverna::dead_zone_camera(300, 0, 4096, 320, 96, 224) == 76,
	      "jugador sale por la derecha: cámara le sigue");
	check(caverna::dead_zone_camera(4000, 0, 4096, 320, 96, 224) == 3776,
	      "cámara acotada al borde del mundo");

	// --- Contacto jugador/enemigo ------------------------------------------
	const eng::Box player { 100, 80, 16, 16 };
	const eng::Box below { 100, 92, 16, 16 };
	const eng::Box side { 112, 80, 16, 16 };
	check(caverna::classify_enemy_hit(player, 4, below) == caverna::EnemyHit::Stomp,
	      "caer encima es stomp");
	check(caverna::classify_enemy_hit(player, 0, side) == caverna::EnemyHit::Damage,
	      "contacto lateral es daño");
	check(caverna::classify_enemy_hit(player, 4, eng::Box {200, 80, 16, 16}) ==
		      caverna::EnemyHit::None,
	      "sin solape no hay golpe");

	if (g_fail != 0) {
		std::printf("FAIL: %d comprobaciones\n", g_fail);
		return 1;
	}
	std::printf("OK: Caverna gameplay + staging PF2 validados.\n");
	return 0;
}
