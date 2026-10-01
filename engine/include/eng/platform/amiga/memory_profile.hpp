#pragma once

/// \\file memory_profile.hpp
/// Presupuestos declarativos de RAM para configuraciones Amiga populares. Son valores iniciales
/// editables por el juego/composition root, no un catálogo cerrado de máquinas soportadas.

#include <eng/memory/arena.hpp>
#include <eng/hw/info.hpp>

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

/// Elige el presupuesto inicial desde el inventario sondeado. Los dos perfiles populares conservan
/// sus headrooms conocidos; otras configuraciones dejan 128 KiB Chip (256 KiB con ≥2 MiB) y 64 KiB
/// Slow para el sistema. Fast no se incluye automáticamente: el integrador la añade con `custom`.
/// Sin inventario sondeado, el fallback conservador es A500. No reemplaza el preflight de AvailMem.
[[nodiscard]] constexpr GameMemoryProfile game_memory_for_hardware(const hw::HwInfo& hardware) noexcept {
	if (!hardware.probed) return game_memory_a500;
	constexpr u32 kKiB = 1024u;
	constexpr u32 kA500PoolBytes = 512u * kKiB;
	constexpr u32 kA1200PoolBytes = 2u * 1024u * kKiB;
	if (hw::is_aga(hardware) && hardware.chip_ram_bytes >= kA1200PoolBytes)
		return game_memory_a1200;
	if (hardware.chipset == hw::Chipset::OCS && hardware.chip_ram_bytes >= kA500PoolBytes &&
	    hardware.slow_ram_bytes >= kA500PoolBytes)
		return game_memory_a500;
	const u32 chip_headroom = hardware.chip_ram_bytes >= kA1200PoolBytes ? 256u * kKiB : 128u * kKiB;
	const u32 slow_headroom = 64u * kKiB;
	const u32 chip = hardware.chip_ram_bytes > chip_headroom
			 ? hardware.chip_ram_bytes - chip_headroom
			 : 0u;
	const u32 slow = hardware.slow_ram_bytes > slow_headroom
			 ? hardware.slow_ram_bytes - slow_headroom
			 : 0u;
	return game_memory_custom(eng::MemoryConfig {chip, slow, 0u, 0u}, "detected memory");
}

/// Comprueba los bloques contiguos requeridos por una política contra una instantánea de Exec.
/// La consulta es solo preflight: `AllocMem` puede seguir fallando si otra tarea reserva primero.
/// Para el contrato de `AvailMem(MEMF_* | MEMF_LARGEST)`, véase `amiga-bootcamp/06_exec_os/memory_management.md`,
/// sección «AvailMem — Query Free Memory».
[[nodiscard]] constexpr bool game_memory_fits(const eng::MemoryConfig& pools,
							      eng::u32 largest_chip,
							      eng::u32 largest_any,
							      eng::u32 largest_fast) noexcept {
	const eng::u32 total_chip = pools.chip_bytes + pools.frame_bytes;
	return (total_chip == 0u || largest_chip >= total_chip) &&
	       (pools.slow_bytes == 0u || largest_any >= pools.slow_bytes) &&
	       (pools.fast_bytes == 0u || largest_fast >= pools.fast_bytes);
}

/// Ajusta un perfil automático al mayor bloque observado conservando headroom para Exec. Como `MEMF_ANY`
/// puede incluir bloques Chip que se reservarán primero, el pool Slow se calcula después de descontar
/// la reserva Chip/Frame; evita contar dos veces un mismo bloque en el preflight. Solo debe usarse para
/// la elección automática; los perfiles custom explícitos se validan y se respetan tal cual.
[[nodiscard]] constexpr GameMemoryProfile game_memory_fit_available(
	const GameMemoryProfile& preferred, eng::u32 largest_chip, eng::u32 largest_any,
	eng::u32 largest_fast, eng::u32 chip_headroom = 128u * 1024u,
	eng::u32 slow_headroom = 64u * 1024u) noexcept {
	if (game_memory_fits(preferred.pools, largest_chip, largest_any, largest_fast)) return preferred;
	auto pools = preferred.pools;
	const eng::u32 chip_room = largest_chip > chip_headroom ? largest_chip - chip_headroom : 0u;
	const eng::u32 chip_sum = pools.chip_bytes + pools.frame_bytes;
	if (chip_sum > chip_room) {
		pools.frame_bytes = pools.frame_bytes < chip_room ? pools.frame_bytes : chip_room;
		const eng::u32 chip_room_after_frame = chip_room - pools.frame_bytes;
		if (pools.chip_bytes > chip_room_after_frame) pools.chip_bytes = chip_room_after_frame;
	}
	const eng::u32 chip_claim = pools.chip_bytes + pools.frame_bytes;
	const eng::u32 any_remaining = largest_any > chip_claim ? largest_any - chip_claim : 0u;
	const eng::u32 slow_room = any_remaining > slow_headroom ? any_remaining - slow_headroom : 0u;
	if (pools.slow_bytes > slow_room) pools.slow_bytes = slow_room;
	if (pools.fast_bytes > largest_fast) pools.fast_bytes = largest_fast;
	return game_memory_custom(pools, "detected memory (headroom adjusted)");
}

} // namespace eng::amiga
