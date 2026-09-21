// ============================================================================
// Demo 122 — Doble buffer de bitmap + swap (COP1LC) con scroll por punteros
// ============================================================================
//
// Técnica: DOS bitmaps completos del mundo. El display lee el delantero mientras
// la aplicación escribe en el trasero; cada frame se conmuta el delantero
// (`flip()`) y el compositor instala la copperlist que lo apunta (swap de
// `COP1LC`). Es la base para escribir sobre filas visibles sin tearing: a
// diferencia del bitmap único (120), que solo puede tocar la banda de staging.
//
// CONTRASTE con 120/121: mismo mapper flat (`map_flat_scroll`, HOST-061) y mismo
// algoritmo de cámara; lo propio es el par de bitmaps + la conmutación.
//
// Superficie en el engine: `eng::field::DoubleBufferScrollPlayfield`. Art: atlas
// *Beginning Fields* a 8 colores (banco X-Limited incrustado en `.MEMF_CHIP`).

#include <eng/api/api.hpp>
#include <eng/field/double_buffer_playfield.hpp>
#include <eng/field/xlimited.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

#include "../../../../out/assets/beginning-fields/8c/tilebank_8c_t16_mediancut_none_640x640.h"

__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tilebank_xlimited\ng_tilebank_xlimited:\n"
	".align 2\n"
	".incbin \"out/assets/beginning-fields/8c/tilebank_xlimited_8c_t16_mediancut_none.bin\"\n"
	".globl g_tilebank_xlimited_size\ng_tilebank_xlimited_size:\n"
	".long . - g_tilebank_xlimited");
extern "C" const unsigned char g_tilebank_xlimited[];
extern "C" const unsigned int g_tilebank_xlimited_size;

namespace {

namespace field = eng::field;

constexpr eng::u16 kWorldW = 448;
constexpr eng::u16 kWorldH = 512;              // dos buffers de 56*512*3 = 86 KB
constexpr eng::u16 kViewW = 320;
constexpr eng::u16 kViewH = 256;
constexpr eng::u8  kPlanes = 3;
constexpr eng::u32 kRowBytes = kWorldW / 8u;   // 56
constexpr eng::u32 kCellsX = kWorldW / 16u;    // 28
constexpr eng::u32 kChipNeed =
	2u * (static_cast<eng::u32>(kRowBytes) * kWorldH * kPlanes) + 2u * 1536u + 64u;

struct DemoGame {
	field::DoubleBufferScrollPlayfield m_pf {};
	field::XlimitedDisplayComposer m_comp {};
	eng::u16 m_palette[8] {};
	eng::s32 m_dir_x = 1;
	eng::s32 m_dir_y = 1;
	bool m_ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({kChipNeed + 16u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012201u);
			return;
		}
		if (!m_pf.begin(backend.memory(), {kWorldW, kWorldH, kViewW, kViewH, kPlanes, 42u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012202u);
			return;
		}
		build_palette();
		// El mundo se pinta en AMBOS buffers (idénticos) antes de mostrar.
		fill_world(m_pf.buffer_bytes(0));
		fill_world(m_pf.buffer_bytes(1));

		field::XlimitedDisplayComposer::Config cfg {};
		cfg.palette = eng::PaletteWords { m_palette, 8u };
		cfg.copper_bytes = 1536u;
		cfg.planes = kPlanes;
		cfg.ddfstrt = 0x0030u;
		cfg.ddfstop = 0x00D0u;
		if (!m_comp.init(backend.memory(), cfg) || !m_comp.compose(m_pf.hardware_view())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012203u);
			return;
		}
		m_comp.takeover(backend);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x14000000u);
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) return;
		sweep(m_pf.cam_x(), m_dir_x);
		sweep(m_pf.cam_y(), m_dir_y);
		m_pf.flip(); // el display pasará a leer el otro bitmap (swap COP1LC)
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_ready) {
			if (m_comp.compose(m_pf.hardware_view())) {
				m_comp.install(backend);
				g_eng_run_status.detail = 0x14000000u |
					((static_cast<eng::u32>(m_pf.front_index()) & 1u) << 20) |
					((static_cast<eng::u32>(m_pf.cam_x().position) & 0xffu) << 12) |
					(static_cast<eng::u32>(m_pf.cam_y().position) & 0xfffu);
			} else {
				m_ready = false;
				eng::debug::mark_failed(g_eng_run_status, 0x00012210u);
			}
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static void sweep(field::BigBufferScroll& cam, eng::s32& dir) {
		const eng::s32 before = cam.position;
		cam.step(dir);
		if (cam.position == before) dir = -dir;
	}

	void build_palette() {
		for (eng::u32 i = 0; i < 8u; ++i) {
			const eng::u8 r = ::kPalette[i * 3u];
			const eng::u8 g = ::kPalette[i * 3u + 1u];
			const eng::u8 b = ::kPalette[i * 3u + 2u];
			m_palette[i] = static_cast<eng::u16>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}
	}

	void fill_world(eng::u8* const dst) {
		const eng::u8* const bank = g_tilebank_xlimited;
		constexpr eng::u32 kBankRowBytes = 320u / 8u;
		constexpr eng::u32 kBlocksPerRow = 320u / 16u;
		const eng::u32 block_rows =
			g_tilebank_xlimited_size / (16u * kPlanes * kBankRowBytes);
		const eng::u32 tile_count = block_rows * kBlocksPerRow;
		for (eng::u32 cy = 0; cy * 16u < kWorldH; ++cy) {
			for (eng::u32 cx = 0; cx < kCellsX; ++cx) {
				eng::u32 t = kTileIndexedMap[cy % 40u][cx % 40u];
				if (t >= tile_count) t = 0u;
				const eng::u32 bx = t % kBlocksPerRow;
				const eng::u32 by = t / kBlocksPerRow;
				for (eng::u32 r = 0; r < 16u; ++r) {
					const eng::u32 dy = cy * 16u + r;
					for (eng::u32 p = 0; p < kPlanes; ++p) {
						const eng::u32 src = (by * 16u * kPlanes + r * kPlanes + p) * kBankRowBytes + bx * 2u;
						const eng::u32 d = (dy * kPlanes + p) * kRowBytes + cx * 2u;
						dst[d] = bank[src];
						dst[d + 1u] = bank[src + 1u];
					}
				}
			}
		}
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
