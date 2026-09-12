// Demo 080 - fire-rgb (PORTE 1:1 de demoscene-repo-orig/effects/fire-rgb/fire-rgb.c)
//
// Fuego en 80x64 -> chunky -> C2P por Blitter -> HAM6 320x256 con cuadruplicado de
// lineas por Copper. La simulacion de fuego (MainLoop/fastrand/RandomizeBottom) se
// copia VERBATIM (asm a mano); el C2P de 13 fases es `MinimalBackend::c2p_4bpp_step`
// (portado de ChunkyToPlanar) y los bits HAM fijos van por `set_bitplane_dat`.
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

// Tipos del original para las tablas copiadas tal cual.
using uint16_t = eng::u16;
using uint32_t = eng::u32;

namespace {

namespace copper = eng::copper;

constexpr eng::u16 kWidth = 80;      // ancho del fuego
constexpr eng::u16 kHeight = 64;     // alto del fuego
constexpr eng::u16 kScreenW = kWidth * 4;   // 320
constexpr eng::u16 kScreenH = kHeight * 4;  // 256
constexpr eng::u16 kBytesPerRow = kScreenW / 8; // 40
constexpr eng::u16 kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight; // 2560
constexpr eng::u32 kBitmapBytes = kPlaneBytes * kPlanes;                        // 10240
constexpr eng::u16 kChunkyBytes = static_cast<eng::u16>(kScreenW * kHeight / 2); // 10240 (4bpp)
constexpr eng::u32 kChunkyBuffer = kChunkyBytes * 2u;                           // chunky + planar

/// Paleta 0..15 a negro (CopLoadColor(cp, 0, 15, 0)): en HAM el color sale de los
/// bits de modificacion (planos 4/5), no de la paleta base.
constexpr eng::u16 kZeroPalette[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

} // namespace

// --- Datos: tabla de color del fuego generada en C++23 constexpr -------------
#include "data/dualtab.hpp"

namespace {

namespace amiga = eng::amiga;

short *chunky[2];
short *fire;
eng::u8* screen_planes[2][kPlanes];
short active = 0;

// --- Simulacion de fuego (VERBATIM de fire-rgb.c) ---------------------------

int fastrand(void) {
	static int m[2] = { static_cast<int>(0x3E50B28Cu), static_cast<int>(0xD461A7F9u) };
	int a, b;
	asm volatile("move.l (%2)+,%0\n"
		     "move.l (%2),%1\n"
		     "swap   %1\n"
		     "add.l  %0,(%2)\n"
		     "add.l  %1,-(%2)\n"
		     : "=d" (a), "=d" (b)
		     : "a" (m));
	return a;
}

void RandomizeBottom(void) {
	int r;
	short i;
	short *bufPtr;
	bufPtr = &(fire[kWidth * kHeight - 1]);
	for (i = 1; i <= kWidth * 2; i += 5) {
		r = fastrand();
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
		r >>= 6;
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
		r >>= 6;
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
		r >>= 6;
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
		r >>= 6;
		*bufPtr-- = static_cast<short>((r & 0x3F) * 4);
	}
}

void MainLoop(void) {
	short i;
	uint16_t* chunkyPtr = reinterpret_cast<uint16_t *>(chunky[active]);
	uint32_t* Aptr = reinterpret_cast<uint32_t *>(fire);
	uint32_t* Bptr = reinterpret_cast<uint32_t *>(&fire[kWidth - 1]);
	uint32_t* Cptr = reinterpret_cast<uint32_t *>(&fire[kWidth]);
	uint32_t* Dptr = reinterpret_cast<uint32_t *>(&fire[kWidth + 1]);
	uint32_t* Eptr = reinterpret_cast<uint32_t *>(&fire[kWidth * 2]);
	const uint32_t* dt = fire_rgb::kDualTab.v;

	for (i = 0; i < (kWidth * kHeight - 2 * kWidth) / 8; ++i) {
		uint32_t vl, hi, lo;

#define FIREITER() \
		vl = (*Eptr++) + (*Bptr++) + (*Dptr++) + (*Cptr++); \
		asm volatile( \
			"movel (%3,%2:w),%1\n" \
			"swap  %2\n" \
			"movel (%3,%2:w),%0\n" \
			: "=r" (hi), "=r" (lo) \
			: "d" (vl), "a" (dt)); \
		*chunkyPtr++ = static_cast<uint16_t>(hi); \
		*chunkyPtr++ = static_cast<uint16_t>(lo); \
		asm volatile( \
			"swap   %1\n" \
			"move.w %1,%0\n" \
			: "+d" (hi), "+d" (lo)); \
		*Aptr++ = hi;

		FIREITER();
		FIREITER();
		FIREITER();
		FIREITER();
	}
#undef FIREITER
	(void)i;
}

struct FireDemo {
	void init(amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00008001u); return; }

		m_block = backend.memory().chip.allocate(
			kChunkyBuffer * 2u + kBitmapBytes * 2u + static_cast<eng::u32>(kWidth) * kHeight * 2u + 16384u, 16);
		if (!m_block.valid()) { eng::debug::mark_failed(g_eng_run_status, 0x00008002u); return; }

		eng::u8* p = static_cast<eng::u8*>(m_block.data);
		m_chunky[0] = p; p += kChunkyBuffer;
		m_chunky[1] = p; p += kChunkyBuffer;
		for (int b = 0; b < 2; ++b) {
			for (eng::u8 pl = 0; pl < kPlanes; ++pl) { m_planes[b][pl] = p + pl * kPlaneBytes; }
			p += kBitmapBytes;
		}
		m_fire = reinterpret_cast<short*>(p); p += static_cast<eng::u32>(kWidth) * kHeight * 2u;
		m_copper = p;
		// Enlaza los globales que usan las funciones copiadas.
		chunky[0] = reinterpret_cast<short*>(m_chunky[0]);
		chunky[1] = reinterpret_cast<short*>(m_chunky[1]);
		fire = m_fire;

		for (eng::u8 b = 0; b < 2; ++b) {
			for (eng::u8 pl = 0; pl < kPlanes; ++pl) {
				eng::u8* q = m_planes[b][pl];
				for (eng::u32 i = 0; i < kPlaneBytes; ++i) q[i] = 0u;
			}
			for (eng::u32 i = 0; i < kChunkyBuffer; ++i) m_chunky[b][i] = 0u;
		}
		for (eng::u32 i = 0; i < static_cast<eng::u32>(kWidth) * kHeight; ++i) m_fire[i] = 0;

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00008003u); return; }
		backend.takeover_display(m_copper_ptr);

		// Bits HAM fijos de los planos 4/5 (rgbb: 0111 / 1100), como el original.
		backend.set_bitplane_dat(4, 0x7777);
		backend.set_bitplane_dat(5, 0xcccc);

		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, 0x0080u);
	}

	void update(amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok) return;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);

		RandomizeBottom();
		MainLoop();

		// C2P: 13 fases (sincrono) del plano `active` a sus bitplanes.
		amiga::MinimalBackend::C2p4State s {};
		s.chunky = m_chunky[active];
		s.bytes = kChunkyBytes;
		for (eng::u8 pl = 0; pl < kPlanes; ++pl) s.planes[pl] = m_planes[active][pl];
		for (eng::u8 ph = 0; ph < 13; ++ph) {
			if (!backend.c2p_4bpp_step(s)) { eng::debug::mark_failed(g_eng_run_status, 0x00008004u); return; }
		}

		// Swap de buffer (la copperlist apunta a los 4 planos de `active`).
		backend.install_copper_list(m_copper_ptrs[active]);
		active ^= 1;
	}

	void render(amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_copper() {
		// BPLCON0 = BPU(7)|COLOR|HAM (como SetupMode(MODE_HAM, 7) del original).
		constexpr eng::u16 kBplcon0 = 0x7a00;
		constexpr eng::u32 kPerList = 8192;
		eng::u8* base = m_copper;
		for (eng::u8 b = 0; b < 2; ++b) {
			copper::Scheduler sched {eng::MemoryBlock {base + static_cast<eng::u32>(b) * kPerList, kPerList, eng::MemoryKind::Chip}};
			sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, kBplcon0,
						  kPlanes, m_planes[b][0], kPlaneBytes);
			// Reordena BPLxPT como el original (bpl[3..0]).
			for (eng::u8 n = 0; n < kPlanes; ++n) {
				sched.move_bitplane_pointer(n, m_planes[b][kPlanes - 1 - n]);
			}
			sched.emit_palette(kZeroPalette, 0, 16); // CopLoadColor(0,15,0)
			// Cuadruplicado de lineas + bplcon1 alterno (MakeCopperList del original).
			for (eng::u16 i = 0; i < kScreenH; ++i) {
				sched.wait_line(i);
				const eng::u16 mod = ((i & 3u) != 3u) ? 0xffd8u : 0x0000u; // -40 repite fila
				sched.move(copper::Register::BPL1MOD, mod);
				sched.move(copper::Register::BPL2MOD, mod);
				sched.move(copper::Register::BPLCON1, (i & 1u) ? 0x0022u : 0x0000u);
			}
			sched.end();
			m_copper_ptrs[b] = sched.data();
			if (!sched.ok()) return false;
		}
		m_copper_ptr = m_copper_ptrs[0];
		return true;
	}

	bool m_memory_ok = false;
	bool m_init_ok = false;
	eng::MemoryBlock m_block {};
	eng::u8* m_chunky[2] = {nullptr, nullptr};
	eng::u8* m_planes[2][kPlanes] = {};
	short* m_fire = nullptr;
	eng::u8* m_copper = nullptr;
	const eng::u16* m_copper_ptr = nullptr;
	const eng::u16* m_copper_ptrs[2] = {nullptr, nullptr};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	amiga::MinimalBackend backend {};
	FireDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffff);

	return 0;
}
