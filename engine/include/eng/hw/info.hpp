#pragma once

/// \file info.hpp
/// **Inventario de hardware**: describe la máquina sobre la que corre el juego (familia Amiga)
/// para que la app **pregunte capacidades** y adapte su configuración (planos, Fast RAM, C2P de
/// Akiko, voces de audio…) sin conocer registros ni hacer `#ifdef` de máquina.
///
/// Es un POD **estable y consultable**: `probe()` lo rellena sondeando Exec, los custom chips,
/// la CIA, la ROM y los mapas de memoria; el resto del engine solo **lee**. Ver
/// `docs/engine/architecture/HARDWARE_INVENTORY.md`.
///
/// ```cpp
/// eng::hw::HwInfo hw {};
/// eng::hw::probe(hw);
/// if (eng::hw::has_fast_ram(hw)) { cache.use_fast(hw.fast_ram_bytes / 2); }
/// if (eng::hw::is_cd32(hw) && hw.caps.c2p_hw) { renderer.enable_akiko_c2p(true); }
/// ```

#include <eng/core/types.hpp>

namespace eng::hw {

// ---------------------------------------------------------------------------------------
// CPU / FPU / MMU
// ---------------------------------------------------------------------------------------

/// Familia de CPU (orden creciente: `>=` compara capacidad).
enum class CpuKind : u8 {
	Unknown = 0,
	M68000,
	M68010,
	M68020,
	M68030,
	M68040,
	M68060,
	Other,
};

/// Coprocesador de coma flotante.
enum class FpuKind : u8 {
	None = 0,
	I68881,
	I68882,
	Internal68040, ///< FPU integrada en 040
	Internal68060, ///< FPU integrada en 060
	Other,
};

// ---------------------------------------------------------------------------------------
// Chipset / modelo
// ---------------------------------------------------------------------------------------

/// Generación de circuitería.
enum class Chipset : u8 {
	Unknown = 0,
	OCS, ///< A1000/A500/A2000 originales
	ECS, ///< A500+/A600/A3000
	AGA, ///< A1200/A4000 (Alice + Lisa)
	Other,
};

/// Modelo de máquina (heurística; ver `HARDWARE_INVENTORY.md`).
enum class MachineModel : u8 {
	Unknown = 0,
	A1000,
	A500,
	A2000,
	A500Plus,
	A600,
	A3000,
	A1200,
	A4000,
	A4000T,
	CDTV,
	CD32,
	DraCo, ///< Draco/CS o clones (capacidades propias)
	Other,
};

// ---------------------------------------------------------------------------------------
// Memoria
// ---------------------------------------------------------------------------------------

/// Tipo de región de memoria.
enum class MemRegionKind : u8 {
	Chip = 0,   ///< accesible por Paula/Agnus/Denise (bus chip)
	Fast,       ///< expansión de 32 bits sin contienda del chipset
	Slow,       ///< "slow fast" en el bus chip ($C00000, trapdoor A500)
	Motherboard,///< otros mapeos de placa
	Other,
};

/// Una región de RAM.
struct MemRegion {
	MemRegionKind kind = MemRegionKind::Other;
	u32 base = 0;  ///< dirección base (0 si desconocida)
	u32 bytes = 0; ///< tamaño
};

constexpr u8 kMaxMemRegions = 12;

// ---------------------------------------------------------------------------------------
// Entrada
// ---------------------------------------------------------------------------------------

/// Dispositivo de un puerto de entrada.
enum class PortDevice : u8 {
	None = 0,
	Mouse,
	JoystickDigital, ///< 1 fuego + 4 direcciones
	JoystickAnalog,  ///< paddles
	Cd32Pad,         ///< multi-botón (7)
	LightPen,
	Other,
};

/// Estado de un puerto. `detected` = sondeado (p. ej. protocolo POT del CD32); si no, es la
/// **política** del juego (el hardware no enumera de forma fiable qué hay enchufado).
struct InputPortInfo {
	PortDevice device = PortDevice::None;
	u8 buttons = 0;      ///< botones disponibles/asumidos
	bool has_dirs = false;
	bool is_cd32_pad = false;
	bool detected = false; ///< true si se sondeó; false si es asumido
};

// ---------------------------------------------------------------------------------------
// Vídeo actual (modo en curso, no el máximo teórico)
// ---------------------------------------------------------------------------------------

/// Modo de display **activo**.
struct DisplayInfo {
	u16 width = 0;
	u16 height = 0;
	u8 depth = 0;       ///< bitplanes activos
	u32 max_colors = 0; ///< `1<<depth` (o 262144 en HAM8)
	bool lace = false;
	bool hires = false;
	bool superhires = false;
	bool ham = false;
	bool extrahalfbrite = false;
	bool double_scan = false; ///< AGA
};

// ---------------------------------------------------------------------------------------
// Capacidades agregadas
// ---------------------------------------------------------------------------------------

/// Booleanos de capacidad (consulta rápida; el detalle está en los campos tipados).
struct Caps {
	// Chipset
	bool ecs_denise = false;
	bool ecs_agnus = false; ///< Agnus de 1 MB / Fatter
	bool aga = false;
	bool alice = false; ///< AGA Alice (Agnus AGA)
	bool lisa = false;  ///< AGA Lisa (Denise AGA)
	bool akiko = false; ///< CD32 (C2P + extras)
	bool c2p_hw = false;///< conversión chunky→planar por hardware (Akiko)
	bool genlock = false;
	bool ntsc = false;  ///< true = NTSC, false = PAL (si se conoce)

	// CPU
	bool cpu_16bit_only = false; ///< 68000/010
	bool cpu_32bit = false;      ///< 020+
	bool has_fpu = false;
	bool has_mmu = false;

	// Audio / otros
	bool paula_4ch = true;
	bool cd_rom = false; ///< CDTV / CD32
	bool rtc = false;
};

// ---------------------------------------------------------------------------------------
// Inventario
// ---------------------------------------------------------------------------------------

/// Inventario de hardware completo. POD de tamaño fijo (sin heap).
struct HwInfo {
	MachineModel model = MachineModel::Unknown;
	Chipset chipset = Chipset::Unknown;
	CpuKind cpu = CpuKind::Unknown;
	FpuKind fpu = FpuKind::None;

	u32 exec_version = 0;  ///< Exec: `(major<<16)|minor`
	u32 exec_revision = 0;
	u16 kick_major = 0;    ///< Kickstart (≈ versión de Exec); 0 si desconocido
	u16 kick_minor = 0;

	u32 chip_ram_bytes = 0;
	u32 fast_ram_bytes = 0;
	u32 slow_ram_bytes = 0;
	u32 total_ram_bytes = 0;
	u8 mem_count = 0;
	MemRegion mem[kMaxMemRegions] {};

	Caps caps {};
	DisplayInfo display {};

	InputPortInfo port1 {}; ///< puerto 1 (ratón habitual)
	InputPortInfo port2 {}; ///< puerto 2 (joystick/pad)

	bool probed = false; ///< true si `probe()` lo rellenó
};

// ---------------------------------------------------------------------------------------
// Consultas (baratas, sin hardware)
// ---------------------------------------------------------------------------------------

/// `true` si la CPU es al menos `k` (p. ej. `cpu_at_least(hw, CpuKind::M68020)`).
[[nodiscard]] constexpr bool cpu_at_least(const HwInfo& h, CpuKind k) noexcept {
	return k != CpuKind::Unknown && static_cast<u8>(h.cpu) >= static_cast<u8>(k) &&
	       h.cpu != CpuKind::Other;
}
[[nodiscard]] constexpr bool has_fast_ram(const HwInfo& h) noexcept { return h.fast_ram_bytes > 0u; }
[[nodiscard]] constexpr bool is_aga(const HwInfo& h) noexcept { return h.caps.aga; }
[[nodiscard]] constexpr bool is_ecs(const HwInfo& h) noexcept {
	return h.chipset == Chipset::ECS || h.caps.aga;
}
[[nodiscard]] constexpr bool is_cd32(const HwInfo& h) noexcept {
	return h.model == MachineModel::CD32 || h.caps.akiko;
}
[[nodiscard]] constexpr bool can_hires(const HwInfo& h) noexcept {
	return h.chipset == Chipset::ECS || h.caps.ecs_denise || h.caps.aga;
}
/// Planos máximos de un playfield lores (6 en OCS/ECS, 8 en AGA).
[[nodiscard]] constexpr u8 max_planes(const HwInfo& h) noexcept { return h.caps.aga ? 8u : 6u; }
/// Colores máximos indexados (no HAM/EHB).
[[nodiscard]] constexpr u32 max_indexed_colors(const HwInfo& h) noexcept {
	return static_cast<u32>(1u) << max_planes(h);
}

/// Heurística de modelo a partir de capacidades ya sondeadas. **Pura** (testeable sin hardware):
/// A1200 y A4000 son ambos AGA y solo se distinguen por la RAM de chip (aproximación);
/// A600 y A500+ tampoco se distinguen sin la ROM de producto.
[[nodiscard]] constexpr MachineModel guess_model(const Caps& caps, Chipset chipset,
						 u32 chip_ram_bytes) noexcept {
	if (caps.akiko) {
		return MachineModel::CD32;
	}
	if (caps.aga) {
		return chip_ram_bytes > 2u * 1024u * 1024u ? MachineModel::A4000 : MachineModel::A1200;
	}
	if (chipset == Chipset::ECS) {
		return MachineModel::A600;
	}
	if (chipset == Chipset::OCS) {
		return MachineModel::A500;
	}
	return MachineModel::Other;
}

/// Clasifica una región de RAM por atributos de Exec y dirección base. `is_chip`/`is_fast` son
/// los bits `MEMF_CHIP`/`MEMF_FAST`. El **slow RAM** (ranger, $C00000-$D80000) es MEMF_CHIP pero
/// Agnus no lo ve, así que se clasifica por dirección.
[[nodiscard]] constexpr MemRegionKind classify_region(bool is_chip, bool is_fast,
						      u32 base) noexcept {
	if (base >= 0x00c00000u && base < 0x00d80000u) {
		return MemRegionKind::Slow;
	}
	if (is_chip) {
		return MemRegionKind::Chip;
	}
	if (is_fast) {
		return MemRegionKind::Fast;
	}
	if (base >= 0x00200000u && base < 0x00a00000u) {
		return MemRegionKind::Fast; // Zorro II (autoconfig)
	}
	return MemRegionKind::Other;
}

/// Reloj de tiempo real: lo tienen A500+ en adelante (el A500/A1000 no).
[[nodiscard]] constexpr bool model_has_rtc(MachineModel m) noexcept {
	switch (m) {
	case MachineModel::A500Plus:
	case MachineModel::A600:
	case MachineModel::A1200:
	case MachineModel::A3000:
	case MachineModel::A4000:
	case MachineModel::A4000T:
	case MachineModel::CDTV:
	case MachineModel::CD32:
		return true;
	default:
		return false;
	}
}

/// Fija el modo de display **vigente** (lo llama el engine al programar un modo). El display
/// no se puede leer de forma fiable de los registros (BPLCON0 es de solo escritura), así que
/// la app/el engine lo declaran aquí; `probe()` solo da una estimación validada al arranque.
inline void set_display(HwInfo& h, u16 width, u16 height, u8 depth, bool hires = false,
			bool lace = false, bool ham = false, bool ehb = false) noexcept {
	h.display.width = width;
	h.display.height = height;
	h.display.depth = depth;
	h.display.hires = hires;
	h.display.lace = lace;
	h.display.ham = ham;
	h.display.extrahalfbrite = ehb;
	if (ham) {
		h.display.max_colors = depth >= 8u ? 262144u : 4096u;
	} else if (ehb) {
		h.display.max_colors = 64u;
	} else {
		h.display.max_colors = depth < 32u ? (1u << depth) : 0u;
	}
}

/// Fija el display desde una estructura ya construida.
inline void set_display(HwInfo& h, const DisplayInfo& d) noexcept { h.display = d; }

/// Nombres legibles (tablas `constexpr`; no hay almacenamiento).
[[nodiscard]] constexpr const char* cpu_name(CpuKind k) noexcept {
	switch (k) {
		case CpuKind::M68000: return "68000";
		case CpuKind::M68010: return "68010";
		case CpuKind::M68020: return "68020";
		case CpuKind::M68030: return "68030";
		case CpuKind::M68040: return "68040";
		case CpuKind::M68060: return "68060";
		case CpuKind::Other: return "other";
		default: return "?";
	}
}
[[nodiscard]] constexpr const char* chipset_name(Chipset c) noexcept {
	switch (c) {
		case Chipset::OCS: return "OCS";
		case Chipset::ECS: return "ECS";
		case Chipset::AGA: return "AGA";
		case Chipset::Other: return "other";
		default: return "?";
	}
}
[[nodiscard]] constexpr const char* model_name(MachineModel m) noexcept {
	switch (m) {
		case MachineModel::A1000: return "A1000";
		case MachineModel::A500: return "A500";
		case MachineModel::A2000: return "A2000";
		case MachineModel::A500Plus: return "A500+";
		case MachineModel::A600: return "A600";
		case MachineModel::A3000: return "A3000";
		case MachineModel::A1200: return "A1200";
		case MachineModel::A4000: return "A4000";
		case MachineModel::A4000T: return "A4000T";
		case MachineModel::CDTV: return "CDTV";
		case MachineModel::CD32: return "CD32";
		case MachineModel::DraCo: return "DraCo";
		case MachineModel::Other: return "other";
		default: return "?";
	}
}

/// **Sondea el hardware** y rellena `out` (lo que se pueda determinar). Seguro tras el arranque
/// (mini-SO/Exec); sin Exec usa mapas fijos y pruebas de escritura. Devuelve `false` si no se pudo
/// sondear nada (deja `out` con los valores por defecto). Lo implementa el backend.
[[nodiscard]] bool probe(HwInfo& out);

} // namespace eng::hw
