#include "amiga_minimal_internal.hpp"

#include <eng/hw/info.hpp>

#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>

/// \file amiga_minimal_hw.cpp
/// Implementación Amiga de `eng::hw::probe`: sondea Exec (`AttnFlags`, `MemList`, versión),
/// los custom chips (`DENISEID`, `VPOSR`, `BPLCON0`, `DIWSTRT/STOP`) y clasifica la RAM.
/// Referencias: AHRM 3.ª (registros BPLCON0/DENISEID/VPOSR), `docs/reference/ahrm/`,
/// `../amiga-bootcamp/01_hardware/video_timing.md` y `<exec/execbase.h>`.

namespace eng::hw {
namespace {

using eng::amiga::detail::custom_base;

// --- Registros custom (offset en palabras) ---------------------------------------------
constexpr u16 reg_deniseid = 0x07c / 2;
constexpr u16 reg_diwstrt = 0x08e / 2;
constexpr u16 reg_diwstop = 0x090 / 2;
constexpr u16 reg_bplcon0 = 0x100 / 2;

// BPLCON0 (AHRM 3.ª, "BPLCON0 - Bitplane Control"; los bits no se fijan por separado).
constexpr u16 bplcon0_lace = 1u << 2;
constexpr u16 bplcon0_dblpf = 1u << 10;
constexpr u16 bplcon0_homod = 1u << 11;
constexpr u16 bplcon0_hires = 1u << 15;
constexpr u16 bplcon0_shres = 1u << 6; // AGA (SuperHires)

// AttnFlags de ExecBase (ver <exec/execbase.h>).
constexpr u16 aff_68010 = 1u << 0;
constexpr u16 aff_68020 = 1u << 1;
constexpr u16 aff_68030 = 1u << 2;
constexpr u16 aff_68040 = 1u << 3;
constexpr u16 aff_68881 = 1u << 4;
constexpr u16 aff_68882 = 1u << 5;
constexpr u16 aff_fpu40 = 1u << 6;
constexpr u16 aff_68060 = 1u << 7;

struct ExecBase* exec_base() {
	return *reinterpret_cast<struct ExecBase**>(4u);
}

void probe_cpu(HwInfo& h, u16 attn) {
	h.cpu = CpuKind::M68000;
	if ((attn & aff_68060) != 0u) {
		h.cpu = CpuKind::M68060;
	} else if ((attn & aff_68040) != 0u) {
		h.cpu = CpuKind::M68040;
	} else if ((attn & aff_68030) != 0u) {
		h.cpu = CpuKind::M68030;
	} else if ((attn & aff_68020) != 0u) {
		h.cpu = CpuKind::M68020;
	} else if ((attn & aff_68010) != 0u) {
		h.cpu = CpuKind::M68010;
	}
	h.caps.cpu_32bit = cpu_at_least(h, CpuKind::M68020);
	h.caps.cpu_16bit_only = !h.caps.cpu_32bit;
	h.caps.has_mmu = cpu_at_least(h, CpuKind::M68030);

	h.fpu = FpuKind::None;
	h.caps.has_fpu = false;
	if (h.cpu == CpuKind::M68060) {
		h.fpu = FpuKind::Internal68060;
		h.caps.has_fpu = true;
	} else if ((attn & aff_fpu40) != 0u) {
		h.fpu = FpuKind::Internal68040;
		h.caps.has_fpu = true;
	} else if ((attn & aff_68882) != 0u) {
		h.fpu = FpuKind::I68882;
		h.caps.has_fpu = true;
	} else if ((attn & aff_68881) != 0u) {
		h.fpu = FpuKind::I68881;
		h.caps.has_fpu = true;
	}
}

void probe_chipset(HwInfo& h) {
	// DENISEID ($DFF07C, "chip revision level for Denise", AHRM 3.ª ap. B). Valores: Lisa (AGA)
	// 0x00F8, ECS Denise 0x00FC, OCS (no implementa el registro) 0x00FF/0x0000.
	const u16 denise = custom_base[reg_deniseid];
	h.caps.paula_4ch = true;

	if ((denise & 0xffu) == 0xf8u) {
		h.chipset = Chipset::AGA;
		h.caps.aga = true;
		h.caps.alice = true;
		h.caps.lisa = true;
		h.caps.ecs_denise = true;
		h.caps.ecs_agnus = true;
	} else if ((denise & 0xffu) == 0xfcu) {
		h.chipset = Chipset::ECS;
		h.caps.ecs_denise = true;
		h.caps.ecs_agnus = true;
	} else {
		h.chipset = Chipset::OCS;
	}

	// Akiko (CD32, C2P por hardware): el campo existe y `is_cd32`/`guess_model` lo usan, pero el
	// sondeo NO está implementado (no hay una detección verificada en este runner, solo A500).
	// Hasta entonces `caps.akiko`/`caps.c2p_hw` quedan en false; no se inventa un registro.
}

void probe_kickstart(HwInfo& h, struct ExecBase* sys) {
	if (sys == nullptr) {
		return;
	}
	h.kick_major = static_cast<u16>(sys->LibNode.lib_Version);
	h.kick_minor = static_cast<u16>(sys->LibNode.lib_Revision);
	h.exec_version = (static_cast<u32>(h.kick_major) << 16) | h.kick_minor;
	h.exec_revision = static_cast<u32>(sys->SoftVer);
	h.caps.ntsc = sys->VBlankFrequency >= 55u; // 50 PAL, 60 NTSC
}

void add_region(HwInfo& h, MemRegionKind kind, u32 base, u32 bytes) {
	if (bytes == 0u || h.mem_count >= kMaxMemRegions) {
		return;
	}
	h.mem[h.mem_count++] = MemRegion{ kind, base, bytes };
	switch (kind) {
	case MemRegionKind::Chip: h.chip_ram_bytes += bytes; break;
	case MemRegionKind::Fast: h.fast_ram_bytes += bytes; break;
	case MemRegionKind::Slow: h.slow_ram_bytes += bytes; break;
	default: break;
	}
}

void probe_ram(HwInfo& h, struct ExecBase* sys) {
	if (sys == nullptr) {
		return;
	}
	Forbid();
	struct Node* node = sys->MemList.lh_Head;
	for (; node->ln_Succ != nullptr; node = node->ln_Succ) {
		const auto* mh = reinterpret_cast<const struct MemHeader*>(node);
		const u32 base = reinterpret_cast<eng::u32>(mh->mh_Lower);
		const u32 bytes = reinterpret_cast<eng::u32>(mh->mh_Upper) - base;
		const MemRegionKind kind = classify_region(
			(mh->mh_Attributes & MEMF_CHIP) != 0u,
			(mh->mh_Attributes & MEMF_FAST) != 0u, base);
		add_region(h, kind, base, bytes);
	}
	Permit();
	h.total_ram_bytes = h.chip_ram_bytes + h.fast_ram_bytes + h.slow_ram_bytes;
}

void probe_display(HwInfo& h) {
	// Ventana de display (DIWSTRT/DIWSTOP), en unidades de baja resolución (AHRM 3.ª, cap. 3):
	//  - HSTOP real = byte escrito + 256; el ancho es HSTOP - HSTART.
	//  - El bit 8 del VSTOP es el complemento del bit 7 del byte escrito.
	//  - El alto/ancho en píxeles dobla en hires/interlaced.
	const u16 diwstrt = custom_base[reg_diwstrt];
	const u16 diwstop = custom_base[reg_diwstop];
	const u16 hstart = static_cast<u16>(diwstrt & 0xffu);
	const u16 hstop = static_cast<u16>((diwstop & 0xffu) + 0x100u);
	const u8 vstart = static_cast<u8>(diwstrt >> 8);
	const u8 vstop_w = static_cast<u8>(diwstop >> 8);
	const u16 vstop = (vstop_w & 0x80u) != 0u ? static_cast<u16>(vstop_w & 0x7fu)
						  : static_cast<u16>(0x100u | vstop_w);
	const u16 w_units = static_cast<u16>(hstop - hstart);
	const u16 h_units = static_cast<u16>(vstop - vstart);
	if (w_units == 0u || h_units == 0u) {
		return; // ventana sin programar: el display queda desconocido
	}

	// BPLCON0 es de solo escritura: la lectura puede no ser fiable, así que se valida contra
	// los planos máximos del chipset (si no cuadra, no se toca `display`).
	const u16 bplcon0 = custom_base[reg_bplcon0];
	const u8 planes = static_cast<u8>((bplcon0 >> 12) & 0x7u);
	if (planes == 0u || planes > max_planes(h)) {
		return;
	}
	const bool hires = (bplcon0 & bplcon0_hires) != 0u;
	const bool lace = (bplcon0 & bplcon0_lace) != 0u;
	const bool ham = (bplcon0 & bplcon0_homod) != 0u;
	const bool dblpf = (bplcon0 & bplcon0_dblpf) != 0u;
	const bool ehb = !ham && !dblpf && planes == 6u;

	set_display(h, static_cast<u16>(w_units << (hires ? 1 : 0)),
		    static_cast<u16>(h_units << (lace ? 1 : 0)), planes, hires, lace, ham, ehb);
	h.display.superhires = h.caps.aga && (bplcon0 & bplcon0_shres) != 0u;
}

void probe_input(HwInfo& h) {
	// El hardware no enumera de forma fiable qué hay enchufado: son valores ASUMIDOS
	// (ratón en el puerto 1, joystick en el 2). El sondeo real del CD32 pad se hace por
	// el protocolo POTGO (ver input_cd32) y marcaría `detected`.
	h.port1 = InputPortInfo{ PortDevice::Mouse, 2, false, false, false };
	h.port2 = InputPortInfo{ PortDevice::JoystickDigital, 1, true, false, false };
}

} // namespace

bool probe(HwInfo& out) {
	out = HwInfo{};
	struct ExecBase* sys = exec_base();

	probe_chipset(out);
	probe_kickstart(out, sys);
	probe_cpu(out, sys != nullptr ? sys->AttnFlags : 0u);
	probe_ram(out, sys);
	probe_display(out);
	probe_input(out);
	out.model = guess_model(out.caps, out.chipset, out.chip_ram_bytes);

	out.caps.cd_rom = out.model == MachineModel::CD32 || out.model == MachineModel::CDTV;
	out.caps.rtc = model_has_rtc(out.model);
	out.probed = true;
	return true;
}

} // namespace eng::hw
