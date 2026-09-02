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

// Banco raw de 1149 tiles (256 bytes por tile) en seccion `.MEMF_CHIP`:
// elf2hunk la emite a un hunk HUNKF_CHIP -> LoadSeg lo carga en Chip RAM. El demo
// lee este banco en la CPU para componer las bitplanes EHB (40x40 tiles, 320x256).
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tilebank_raw\ng_tilebank_raw:\n"
	".align 2\n"
	".incbin \"out/ehb/tilebank.raw.bin\"\n"
	".globl g_tilebank_raw_size\ng_tilebank_raw_size:\n"
	".long . - g_tilebank_raw");
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

		// Paleta: cargar las 32 BASES en COLOR0-31. kEhbPalette[64] de
		// const_game_201.h viene INTERCALADO (base0,half0,base1,half1,...), igual
		// que los indices del tilebank. El hardware EHB espera las 32 bases en
		// COLOR0-31 y genera los half (indices 32-63) como base/2. Por eso aqui se
		// toman SOLO los terminos pares (indices 2*i) = las bases. Emitir todo el
		// array (33..63 tambien) provocaria colores mal mapeados (los half no deben
		// escribirse; son automaticos).
		eng::u16 palette[32] {};
		for (eng::u8 i = 0; i < 32; ++i) {
			const eng::u8* c = &kEhbPalette[static_cast<eng::u8>(2u * i) * 3u];
			palette[i] = static_cast<eng::u16>(
				((c[0] >> 4) << 8) |
				((c[1] >> 4) << 4) |
				(c[2] >> 4));
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
				// tile es el indice de tile (0..1148); cada tile son kBankTileBytes
				// (256) dentro del banco. Coordenada (x%16, y%16) dentro del tile.
				// El tilebank guarda el indice con la convencion INTERCALADA de
				// slice-tiles.mjs: indice par 2k = base del color k, indice impar
				// 2k+1 = half del color k. El chipset EHB espera bases-primero:
				// 0-31 = base, 32-63 = half (generado como base/2). Convertir:
				//   base k (v=2k)               -> e = k        = v>>1
				//   half k (v=2k+1)             -> e = 32+k     = (v>>1)|0x20
				// (1 bit de 'intercalado' se vuelve el bit 5 = plano 6 del EHB.)
				const int v = g_tilebank_raw[tile * kBankTileBytes +
					(y % kTileH) * kTileW + (x % kTileW)];
				const int e = (v >> 1) | ((v & 1) << 5);
				const int byte = y * (kWidth / 8) + x / 8;
				const eng::u8 mask = static_cast<eng::u8>(0x80u >> (x & 7));
				for (int p = 0; p < 5; ++p) {
					if ((e & (1 << p)) != 0) planes[static_cast<eng::u32>(p) * kPlaneBytes + byte] |= mask;
				}
				if ((e & 0x20) != 0) planes[5 * kPlaneBytes + byte] |= mask; // bit half (EHB)
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