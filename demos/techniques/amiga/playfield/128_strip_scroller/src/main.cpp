// Demo 128: scroller por tiras (Copper ring + incoming strip) — camino rápido a 50 fps,
// con el **atlas "Beginning Fields"** (8 colores) del pueblecito.
//
// Usa `eng/field/strip_scroller.hpp` + `strip_composer.hpp`: cada frame planifica la tira entrante
// (`plan_strip_frame`), la compone desde el banco de tiles (`compose_column`, tiles SEPARADOS) solo
// al cruzar frontera de tile, la ejecuta por Blitter (`blitter_strip_column`) y parchea la
// copperlist (`strip_copper_values` + `StripComposer::patch`).
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/128_strip_scroller --release
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/128_strip_scroller

#include <eng/api/api.hpp>
#include <eng/field/strip_composer.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic, eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0,
};
}

// Atlas "Beginning Fields" a 8 colores (paleta 8 RGB + mapa 40x40 de ids de tile).
#include "../../../../../../out/assets/beginning-fields/8c/tilebank_8c_t16_mediancut_none_640x640.h"

// Banco X-Limited interleaved (3 planos, 320 px) producido por amiga-tiles; se copia por CPU al
// banco PACKED del scroller en `init` (tiles contiguos de 16x16x3) y no se vuelve a usar.
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tilebank_xlimited\ng_tilebank_xlimited:\n"
	".align 2\n"
	".incbin \"out/assets/beginning-fields/8c/tilebank_xlimited_8c_t16_mediancut_none.bin\"\n"
	".globl g_tilebank_xlimited_size\ng_tilebank_xlimited_size:\n"
	".long . - g_tilebank_xlimited");
extern "C" const unsigned char g_tilebank_xlimited[];
extern "C" const unsigned int g_tilebank_xlimited_size;

namespace {

constexpr eng::u16 kViewportW = 320u;
constexpr eng::u16 kViewportH = 256u;
constexpr eng::u16 kMapSide = 40u; // mapa toroidal de 40 columnas (tiles de 16 px)

// `MapWords` (periodo del mapa) deriva el anillo correcto (`visible + MapWords` = 60) y garantiza
// por construccion que la `span` del puntero es multiplo del periodo: el slot `s` vale siempre la
// columna `s % kMapSide` y no hay que repintar la ventana al envolver. Ver `StripScrollGeometry`
// y el invariante de contenido de HOST-244.
using Geom = eng::playfield::StripScrollGeometry<kViewportW, kViewportH, 3u, 16u, 16u, 2u, 1u,
					    false, 0u, 0u, kMapSide>;

constexpr eng::u16 kTileWords = Geom::tile_h * Geom::planes; // 48 (16x16 x 3 planos)
constexpr eng::u16 kBlocksPerRow = 20u;                     // tiles por fila del banco X-Limited
constexpr eng::u16 kBankRowBytes = 40u;                     // 320 px / 8
constexpr eng::u32 kBankBytes = 1180u * kTileWords * 2u;   // banco packed (tiles contiguos)
constexpr eng::u32 kColumnWords = Geom::column_planelines;
constexpr eng::u32 kColumnBytes = kColumnWords * 2u;
constexpr eng::u32 kRingBytes = static_cast<eng::u32>(Geom::ring_w_bytes) * Geom::planes *
				Geom::viewport_h;
constexpr eng::s32 kStepX = 2; // px/frame

/// Adaptador de mapa para `StripScrollController`: id del tile del atlas (toroidal).
struct TileMap {
	[[nodiscard]] eng::u16 tile_at(eng::u16 col, eng::u16 row) const noexcept {
		return static_cast<eng::u16>(kTileIndexedMap[row % kMapSide][col % kMapSide]);
	}
};

struct StripGame {
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_bank {};
	// La fachada de la capa de tiras: agrupa buffers + CPU/Blitter + Copper. La demo solo declara
	// mapa/banco/paleta/tamaños y conduce con `frame()`; no ve el compositor ni los `BPLxPT`.
	eng::playfield::StripScrollLayer<Geom, TileMap, eng::amiga::AmigaBackend> m_layer {};
	TileMap m_map {};
	eng::u16* m_bank_words = nullptr;
	eng::s32 m_scroll = 0;
	bool m_ready = false;

	/// Reempaqueta el banco X-Limited (interleaved 320 px) a tiles **contiguos** de 16x16x3.
	void build_packed_bank() {
		const eng::u8* const xlim = g_tilebank_xlimited;
		const eng::u32 block_rows =
			g_tilebank_xlimited_size / (16u * Geom::planes * kBankRowBytes);
		const eng::u32 tile_count = block_rows * kBlocksPerRow;
		for (eng::u32 t = 0u; t < tile_count; ++t) {
			const eng::u32 tx = t % kBlocksPerRow;
			const eng::u32 ty = t / kBlocksPerRow;
			for (eng::u32 r = 0u; r < Geom::tile_h; ++r) {
				for (eng::u32 p = 0u; p < Geom::planes; ++p) {
					const eng::u32 src =
						(ty * 16u * Geom::planes + r * Geom::planes + p) *
							kBankRowBytes + tx * 2u;
					m_bank_words[t * kTileWords + r * Geom::planes + p] =
						static_cast<eng::u16>((xlim[src] << 8) | xlim[src + 1u]);
				}
			}
		}
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({224u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012801u);
			return;
		}
		auto& mm = backend.memory_manager();
		m_bank = mm.chip().reserve<eng::PlaneTag>(kBankBytes, 16u);
		if (!m_bank.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012802u);
			return;
		}
		m_bank_words = reinterpret_cast<eng::u16*>(m_bank.data());
		build_packed_bank();

		// Paleta del atlas (8 colores RGB -> palabras Amiga).
		eng::Palette32 pal {};
		for (eng::u32 i = 0u; i < 8u; ++i) {
			const eng::u8 r = static_cast<eng::u8>(::kPalette[i * 3u]);
			const eng::u8 g = static_cast<eng::u8>(::kPalette[i * 3u + 1u]);
			const eng::u8 b = static_cast<eng::u8>(::kPalette[i * 3u + 2u]);
			pal.color[i] = static_cast<eng::u16>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}
		// La capa reserva anillo/columna, monta el compositor y pre-pinta el anillo (setup).
		m_layer.set_map(m_map);
		m_layer.set_bank(m_bank_words, kTileWords);
		m_layer.set_palette(pal.words());
		m_layer.set_sizes(kRingBytes, kColumnBytes, 1536u);
		if (!m_layer.begin(backend, mm)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012803u);
			return;
		}
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x12800000u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) return;
		const eng::s32 prev = m_scroll;
		m_scroll += kStepX;
		m_layer.frame(backend, m_scroll, 0, prev, 0); // pinta la tira si cruza + parchea el Copper
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

StripGame g_game {};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	eng::Engine engine {backend, g_game};
	engine.run_frames_polling(0xffff);
	return 0;
}
