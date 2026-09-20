/// \file amiga_minimal_c2p.cpp
/// Servicio de **C2P** (chunky -> planar) por Blitter: las 13 fases de `fire-rgb`.

#include "amiga_minimal_internal.hpp"

using namespace eng::amiga::detail;

namespace eng::amiga {

bool MinimalBackend::c2p_4bpp_program(C2p4State& s) {
	if (s.chunky == nullptr) {
		return false;
	}
	u8* src = s.chunky;
	u8* dst = s.chunky + s.bytes;
	const u16 h = static_cast<u16>((static_cast<u32>(s.bytes) / 16u) << 6);
	const u16 bplsize = static_cast<u16>(s.bytes / 4u);
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	switch (s.phase) {
	case 0:
		custom_base[custom_bltamod_offset] = 4;
		custom_base[custom_bltbmod_offset] = 4;
		custom_base[custom_bltdmod_offset] = 4;
		custom_base[custom_bltcdat_offset] = 0x00ff;
		custom_base[custom_bltafwm_offset] = 0xffff;
		custom_base[custom_bltalwm_offset] = 0xffff;
		write_custom_pointer(custom_bltapt_offset, src + 4);
		write_custom_pointer(custom_bltbpt_offset, src);
		write_custom_pointer(custom_bltdpt_offset, dst);
		custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_c2p_abd | blt_minterm_c2p_out | blt_shift8);
		custom_base[custom_bltcon1_offset] = 0;
		custom_base[custom_bltsize_offset] = static_cast<u16>(2 | h);
		break;
	case 1:
		custom_base[custom_bltsize_offset] = static_cast<u16>(2 | h);
		break;
	case 2:
		write_custom_pointer(custom_bltapt_offset, src + s.bytes - 6);
		write_custom_pointer(custom_bltbpt_offset, src + s.bytes - 2);
		write_custom_pointer(custom_bltdpt_offset, dst + s.bytes - 2);
		custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_c2p_abd | blt_minterm_c2p_out2 | blt_shift8);
		custom_base[custom_bltcon1_offset] = blt_reverse;
		custom_base[custom_bltsize_offset] = static_cast<u16>(2 | h);
		break;
	case 3:
		custom_base[custom_bltsize_offset] = static_cast<u16>(2 | h);
		break;
	case 4:
		custom_base[custom_bltamod_offset] = 6;
		custom_base[custom_bltbmod_offset] = 6;
		custom_base[custom_bltdmod_offset] = 0;
		custom_base[custom_bltcdat_offset] = 0x0f0f;
		write_custom_pointer(custom_bltapt_offset, dst + 2);
		write_custom_pointer(custom_bltbpt_offset, dst);
		write_custom_pointer(custom_bltdpt_offset, s.planes[0]);
		custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_c2p_abd | blt_minterm_c2p_out | blt_shift4);
		custom_base[custom_bltcon1_offset] = 0;
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 5:
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 6:
		write_custom_pointer(custom_bltapt_offset, dst + 6);
		write_custom_pointer(custom_bltbpt_offset, dst + 4);
		write_custom_pointer(custom_bltdpt_offset, s.planes[2]);
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 7:
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 8:
		write_custom_pointer(custom_bltapt_offset, dst + s.bytes - 8);
		write_custom_pointer(custom_bltbpt_offset, dst + s.bytes - 6);
		write_custom_pointer(custom_bltdpt_offset, s.planes[1] + bplsize - 2);
		custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_c2p_abd | blt_minterm_c2p_out2 | blt_shift4);
		custom_base[custom_bltcon1_offset] = blt_reverse;
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 9:
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 10:
		write_custom_pointer(custom_bltapt_offset, dst + s.bytes - 4);
		write_custom_pointer(custom_bltbpt_offset, dst + s.bytes - 2);
		write_custom_pointer(custom_bltdpt_offset, s.planes[3] + bplsize - 2);
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 11:
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 12: // parcheo de BPLxPT: lo hace el llamador
	default:
		break;
	}
	s.phase++;
	return true;
}

bool MinimalBackend::c2p_4bpp_step(C2p4State& s) {
	if (!c2p_4bpp_program(s)) {
		return false;
	}
	return wait_blitter();
}

} // namespace eng::amiga
