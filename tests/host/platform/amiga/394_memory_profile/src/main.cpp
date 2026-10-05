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
	hw::HwInfo detected_a500 {};
	detected_a500.probed = true;
	detected_a500.chipset = hw::Chipset::OCS;
	detected_a500.chip_ram_bytes = 512u * 1024u;
	detected_a500.slow_ram_bytes = 512u * 1024u;
	const auto selected_a500 = game_memory_for_hardware(detected_a500);
	check(selected_a500.pools.chip_bytes == game_memory_a500.pools.chip_bytes &&
	      selected_a500.pools.slow_bytes == game_memory_a500.pools.slow_bytes,
	      "inventario A500 selecciona el presupuesto A500 recomendado");
	hw::HwInfo detected_a1200 {};
	detected_a1200.probed = true;
	detected_a1200.chipset = hw::Chipset::AGA;
	detected_a1200.caps.aga = true;
	detected_a1200.chip_ram_bytes = 2u * 1024u * 1024u;
	detected_a1200.fast_ram_bytes = 8u * 1024u * 1024u;
	const auto selected_a1200 = game_memory_for_hardware(detected_a1200);
	check(selected_a1200.pools.chip_bytes == game_memory_a1200.pools.chip_bytes,
	      "inventario AGA con 2 MiB Chip selecciona el presupuesto A1200");
	check(selected_a1200.pools.fast_bytes == 0u,
	      "el perfil automático no asigna Fast aunque el inventario la detecte");
	hw::HwInfo detected_a600 {};
	detected_a600.probed = true;
	detected_a600.chipset = hw::Chipset::ECS;
	detected_a600.chip_ram_bytes = 1u * 1024u * 1024u;
	const auto selected_a600 = game_memory_for_hardware(detected_a600);
	check(selected_a600.pools.chip_bytes == 896u * 1024u && selected_a600.pools.slow_bytes == 0u,
	      "inventario ECS con 1 MiB Chip usa presupuesto derivado y no presupone Slow");
	check(game_memory_for_hardware(hw::HwInfo {}).pools.chip_bytes == game_memory_a500.pools.chip_bytes,
	      "inventario sin sondear recurre al presupuesto A500 conservador");
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
	const auto constrained = game_memory_fit_available(game_memory_a500, 400u * 1024u,
								    400u * 1024u, 0u);
	check(constrained.pools.chip_bytes == 272u * 1024u &&
	      constrained.pools.slow_bytes == 64u * 1024u,
	      "perfil automático reduce pools según bloques libres y conserva headroom");
	check(game_memory_fits(constrained.pools, 400u * 1024u, 400u * 1024u, 0u),
	      "perfil automático ajustado cabe en la instantánea de Exec");
	if (failures != 0) return 1;
	std::printf("OK: game memory profiles and contiguous-block preflight.\n");
	return 0;
}
