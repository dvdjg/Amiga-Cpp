#pragma once

/// \\file memory_profile.hpp
/// Presupuestos declarativos de RAM para configuraciones Amiga populares. Son valores iniciales
/// editables por el juego/composition root, no un catálogo cerrado de máquinas soportadas.

#include <eng/memory/arena.hpp>

namespace eng::amiga {

/// Pools que el engine reservará para el juego. Fast es optativa: permanece en cero salvo que
/// el integrador detecte/declare expansión y decida asignarla al trabajo de CPU.
struct GameMemoryProfile {
	eng::MemoryConfig pools {};
	const char* name = "custom";
};

/// Pool A500 habitual: 384 KiB Chip para el juego + 448 KiB Slow; conserva 128 KiB Chip y 64 KiB
/// Slow para Exec/Workbench. No presupone Fast RAM.
inline constexpr GameMemoryProfile game_memory_a500 {
	eng::MemoryConfig {384u * 1024u, 448u * 1024u, 0u, 0u}, "A500 512K Chip + 512K Slow"};

/// Pool A1200 base: 1792 KiB de sus 2 MiB Chip; conserva 256 KiB para el sistema. Fast no asumida.
inline constexpr GameMemoryProfile game_memory_a1200 {
	eng::MemoryConfig {1792u * 1024u, 0u, 0u, 0u}, "A1200 2M Chip"};

/// Construye un presupuesto personalizado para una ampliación o distribución de pools distinta.
[[nodiscard]] constexpr GameMemoryProfile game_memory_custom(eng::MemoryConfig pools,
								     const char* name = "custom") noexcept {
	return {pools, name};
}

/// Comprueba los bloques contiguos requeridos por una política contra una instantánea de Exec.
/// La consulta es solo preflight: `AllocMem` puede seguir fallando si otra tarea reserva primero.
/// Para el contrato de `AvailMem(MEMF_* | MEMF_LARGEST)`, véase `amiga-bootcamp/06_exec_os/memory_management.md`,
/// sección «AvailMem — Query Free Memory».
[[nodiscard]] constexpr bool game_memory_fits(const eng::MemoryConfig& pools,
							      eng::u32 largest_chip,
							      eng::u32 largest_any,
							      eng::u32 largest_fast) noexcept {
	return (pools.chip_bytes == 0u || largest_chip >= pools.chip_bytes) &&
	       (pools.frame_bytes == 0u || largest_chip >= pools.frame_bytes) &&
	       (pools.slow_bytes == 0u || largest_any >= pools.slow_bytes) &&
	       (pools.fast_bytes == 0u || largest_fast >= pools.fast_bytes);
}

} // namespace eng::amiga
