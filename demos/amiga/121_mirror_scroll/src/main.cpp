// ============================================================================
// Demo 121 — Espejo vertical: scroll de 256 px SIN split de Copper
// ============================================================================
//
// Técnica: el bitmap contiene el bucle de display DUPLICADO (filas [0,288) y su
// copia en [288,576)); el display lee de forma CONTIGUA desde la cámara y **no
// hace falta split** del Copper. Es la alternativa recomendada para un viewport
// de scroll de 256 px, donde el comparador de 8 bits del Copper no permite un
// split móvil fiable (docs/guides/roadmap/CONSULTA-SPLIT-208.md).
//
// CONTRASTE: el corkscrew/XYLimited (107) usa un anillo compacto + split (barato
// en Chip RAM) pero limitado a campos ≤214 px; el espejo paga 2× el alto del
// bucle en memoria a cambio de scroll vertical completo y sin artefacto.
//
// Superficie y mapper en el engine: `eng::field::MirrorScrollPlayfield` (bitmap
// con espejo + cámara X saturada, Y envolvente) sobre el mapper flat
// `eng::field::map_flat_scroll` (HOST-061). Art: atlas *Beginning Fields* a 8
// colores desde el banco X-Limited incrustado en `.MEMF_CHIP`.

#include <eng/api/api.hpp>
#include <eng/field/mirror_playfield.hpp>
#include <eng/field/xlimited.hpp>
#include <eng/platform/amiga/backend.hpp>

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
constexpr eng::u16 kViewW = 320;
constexpr eng::u16 kViewH = 256;
constexpr eng::u16 kDisplayH = 288;            // bucle = viewport + 2*16 (se duplica)
constexpr eng::u8  kPlanes = 3;
constexpr eng::u32 kRowBytes = kWorldW / 8u;   // 56
constexpr eng::u32 kCellsX = kWorldW / 16u;    // 28
constexpr eng::u32 kChipNeed =
	static_cast<eng::u32>(kRowBytes) * (kDisplayH * 2u) * kPlanes + 2u * 1536u + 64u;

struct DemoGame {
	field::MirrorScrollPlayfield m_pf {};
	field::XlimitedDisplayComposer m_comp {};
	eng::u16 m_palette[8] {};
	bool m_ready = false;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({kChipNeed + 16u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012101u);
			return;
		}
		if (!m_pf.begin(backend.memory(), {kWorldW, kViewW, kViewH, kDisplayH, kPlanes, 42u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012102u);
			return;
		}
		build_palette();
		fill_loop();

		field::XlimitedDisplayComposer::Config cfg {};
		cfg.palette = eng::PaletteWords { m_palette, 8u };
		cfg.copper_bytes = 1536u;
		cfg.planes = kPlanes;
		cfg.ddfstrt = 0x0030u;
		cfg.ddfstop = 0x00D0u;
		if (!m_comp.init(backend.memory(), cfg) || !m_comp.compose(m_pf.hardware_view())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012103u);
			return;
		}
		m_comp.takeover(backend);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x13000000u);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) return;
		m_pf.advance(1, 1); // X satura/revee, Y envuelve (espejo)
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (m_ready) {
			if (m_comp.compose(m_pf.hardware_view())) {
				m_comp.install(backend);
				g_eng_run_status.detail = 0x13000000u |
					((static_cast<eng::u32>(m_pf.cam_x()) & 0xffu) << 12) |
					(static_cast<eng::u32>(m_pf.cam_y()) & 0xfffu);
			} else {
				m_ready = false;
				eng::debug::mark_failed(g_eng_run_status, 0x00012110u);
			}
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void build_palette() {
		for (eng::u32 i = 0; i < 8u; ++i) {
			const eng::u8 r = ::kPalette[i * 3u];
			const eng::u8 g = ::kPalette[i * 3u + 1u];
			const eng::u8 b = ::kPalette[i * 3u + 2u];
			m_palette[i] = static_cast<eng::u16>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}
	}

	/// Rellena el bucle [0, display_h) con tiles del atlas y lo duplica al espejo.
	void fill_loop() {
		const eng::u8* const bank = g_tilebank_xlimited;
		eng::u8* const dst = m_pf.bitmap().bytes().data();
		constexpr eng::u32 kBankRowBytes = 320u / 8u;
		constexpr eng::u32 kBlocksPerRow = 320u / 16u;
		const eng::u32 block_rows =
			g_tilebank_xlimited_size / (16u * kPlanes * kBankRowBytes);
		const eng::u32 tile_count = block_rows * kBlocksPerRow;
		for (eng::u32 cy = 0; cy * 16u < kDisplayH; ++cy) {
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
		// Espejo: duplicar las kDisplayH filas del bucle justo debajo.
		for (eng::u32 y = 0; y < kDisplayH; ++y) {
			for (eng::u32 p = 0; p < kPlanes; ++p) {
				const eng::u32 src = (y * kPlanes + p) * kRowBytes;
				const eng::u32 d = ((y + kDisplayH) * kPlanes + p) * kRowBytes;
				for (eng::u32 b = 0; b < kRowBytes; ++b) dst[d + b] = dst[src + b];
			}
		}
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
