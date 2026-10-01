// ============================================================================
// Test HOST-394: política declarativa de RAM de aplicación Amiga.
// ============================================================================
//
// Comprueba los pools A500/A1200 recomendados, que el override permite asignar Fast RAM cuando
// existe, y que la prueba de bloque contiguo rechaza configuraciones que no caben. La comprobación
// es deliberadamente pura; el runner A500 valida AvailMem/AllocMem sobre Exec.

#include <cstdio>

#include <eng/platform/amiga/memory_profile.hpp>

namespace {
int failures = 0;
void check(bool value, const char* message) {
	if (value) return;
	std::printf("[FAIL] %s\n", message);
	++failures;
}
} // namespace

int main() {
	using namespace eng;
	using namespace eng::amiga;
	check(game_memory_a500.pools.chip_bytes == 384u * 1024u, "A500 deja 128 KiB Chip para el OS");
	check(game_memory_a500.pools.slow_bytes == 448u * 1024u, "A500 reserva el pool Slow popular");
	check(game_memory_a500.pools.fast_bytes == 0u, "A500 no presupone Fast RAM");
	check(game_memory_a1200.pools.chip_bytes == 1792u * 1024u, "A1200 deja 256 KiB Chip para el OS");
	const auto expanded = game_memory_custom(MemoryConfig {512u * 1024u, 0u, 8u * 1024u, 0u},
						 "A500 custom Chip pool");
	check(expanded.name != nullptr && expanded.pools.frame_bytes == 8u * 1024u,
	      "un juego puede declarar un presupuesto de producto propio");
	check(game_memory_fits(expanded.pools, 600u * 1024u, 0u, 0u),
	      "pool cabe en bloques contiguos libres");
	check(!game_memory_fits(expanded.pools, 500u * 1024u, 0u, 0u),
	      "pool Chip que excede el mayor bloque se rechaza");
	check(!game_memory_fits(MemoryConfig {0u, 0u, 16u * 1024u, 0u}, 8u * 1024u, 0u, 0u),
	      "el scratch de frame también requiere un bloque Chip contiguo");
	check(!game_memory_fits(MemoryConfig {128u, 0u, 0u, 4096u}, 1024u, 0u, 2048u),
	      "Fast explícita también se valida, no se ignora");
	if (failures != 0) return 1;
	std::printf("OK: game memory profiles and contiguous-block preflight.\n");
	return 0;
}
