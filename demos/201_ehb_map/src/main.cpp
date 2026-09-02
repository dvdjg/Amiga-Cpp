#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/graphics/copper/scheduler.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

// Datos EHB del mapa render (paleta 64 RGB + mapa 40x40 -> Ã­ndice de banco).
#include "../../../out/ehb/const_game_201.h"

// EXPERIMENTO mini (banco de 256 B) para discriminar si LoadSeg falla con el
// exe grande. Stub temporal; se restaura el incbin real cuando se confirme el boot.
__asm__(".section .rodata\n"
	".globl g_tilebank_raw\ng_tilebank_raw:\n"
	".fill 256,1,0x33");
extern "C" const unsigned char g_tilebank_raw[];

namespace {

struct DemoGame {
	static constexpr int kTileW = 16, kTileH = 16, kBW = 40, kBH = 40;
	static constexpr int kWidth = 320, kHeight = 256;   // EHB 320x256 (16x16 tiles â†’ 20x15?)
	static constexpr int kPlaneBytes = (kWidth / 8) * kHeight;
	static constexpr int kBankTileBytes = 256;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Planos EHB (6 planos) + copperlist. Los +16 absorben el padding de
		// alineacion de AllocMem (AmigaOS 1.3 alinea a 8, no 16). Sin estos
		// bytes, el copperlist falla si el puntero base tiene offset 8-mod-16.
		// Ver regla detallada en engine/include/eng/memory/arena.hpp allocate().
    const eng::u32 need = static_cast<eng::u32>(6u) * kPlaneBytes + 4096u + 16u;
		if (!backend.configure_memory({need, 16 * 1024u, 8 * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020101u);
			return;
		}
		m_planes = backend.memory().chip.allocate(static_cast<eng::u32>(6u) * kPlaneBytes, 16);
		m_copper = backend.memory().chip.allocate(4096u, 16);
		if (!m_planes.valid() || !m_copper.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020102u);
			return;
		}

		// Componer las planes EHB desde banco+mapa (una Ãºnica vez, CPU).
		fill_planes();

		// Paleta: los 32 COLORx base en palabras Amiga 444 (EHB genera las 32 half).
		eng::u16 palette[32] {};
		for (eng::u8 i = 0; i < 32; ++i) {
			palette[i] = static_cast<eng::u16>(
				((kEhbPalette[i * 3] >> 4) << 8) |
				((kEhbPalette[i * 3 + 1] >> 4) << 4) |
				(kEhbPalette[i * 3 + 2] >> 4));
		}

		eng::copper::Scheduler sched { m_copper };
		// Display paramétrico (320x256 lowres PAL, 40 B/fila, 6 planos EHB).
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 40u, 0x6200, 6,
			static_cast<const eng::u8*>(m_planes.data), kPlaneBytes);
		sched.emit_palette(palette, 0, 32);
		sched.end();
		backend.install_copper_list(static_cast<const eng::u16*>(m_copper.data));

		eng::debug::mark_ready(g_eng_run_status, 0x00020100u);
	}

	void fill_planes() {
		eng::u8* planes = static_cast<eng::u8*>(m_planes.data);
		// Limpiar primero.
		for (eng::u32 b = 0; b < static_cast<eng::u32>(6u) * kPlaneBytes; ++b) planes[b] = 0;
		for (int y = 0; y < kHeight; ++y) {
			for (int x = 0; x < kWidth; ++x) {
				const int cx = x / kTileW, cy = y / kTileH;
				const int tile = kRenderMap[cy * kBW + cx];
				const int v = g_tilebank_raw[(tile >> 4) & 255u]; // mini-test sin banco grande
				const int byte = y * (kWidth / 8) + x / 8;
				const eng::u8 mask = static_cast<eng::u8>(0x80u >> (x & 7));
				for (int p = 0; p < 5; ++p) {
					if ((v & (1 << p)) != 0) planes[static_cast<eng::u32>(p) * kPlaneBytes + byte] |= mask;
				}
				if ((v & 0x20) != 0) planes[5 * kPlaneBytes + byte] |= mask; // bit half (EHB)
			}
		}
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		// EstÃ¡tico: sin scroll.
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	eng::MemoryBlock m_planes {};
	eng::MemoryBlock m_copper {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}