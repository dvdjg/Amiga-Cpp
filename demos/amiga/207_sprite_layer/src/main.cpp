// ============================================================================
// Demo 207 — capa de fondo con sprites rearmados horizontalmente (SpriteLayer)
// ============================================================================
//
// `effects::SpriteLayer` cubre una banda con los 8 canales de sprite **rearmados
// horizontalmente** (misma línea, otra X): un patrón de 16 px que se repite a lo ancho,
// con **scroll** por frame. Es una capa de **fondo**: los sprites van detrás del
// playfield (`BPLCON2`), así que se ven donde el playfield es color 0.
//
//   bash ./tools/build/build-demo.sh demos/amiga/207_sprite_layer --debug
//   bash ./tools/run/run-demo.sh demos/amiga/207_sprite_layer --warp
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/api/effects.hpp>
#include <eng/graphics/sprite_manager.hpp>
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

namespace {

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u16 kBandLine0 = 60;    // primera línea del efecto
constexpr eng::u16 kBandLines = 40;    // líneas que cubre (40 x 8 x 10 words = 3200 words)
constexpr eng::u8  kChannels = 8;
constexpr eng::u16 kHpos0 = 32;
constexpr eng::u16 kHposStep = 16;     // columnas de 16 px contiguas
constexpr eng::u16 kSpriteWords = 4u;  // 1 línea (DAT/DATB) + terminador (sprite "armado")
constexpr eng::u16 kDmaHeight = kBandLines;
constexpr eng::u16 kDmaStride = static_cast<eng::u16>(2u + kDmaHeight * 2u + 2u); // POS+CTL + DATA + terminador
constexpr eng::u8  kDmaChannels = 4u;  // canales DMA (columna alta con cabecera); el resto, Copper

// COLOR00 fondo navy; COLOR17 = color 1 del par 0/1 (el patrón de la capa).
constexpr eng::Palette32 kPalette {{
	0x013, 0xf00, 0x0f0, 0x0ff, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x111, 0x0ff, 0x111, 0x111,   // COLOR16-19: color 1 = cian
	0x222, 0x0f0, 0x222, 0x222,   // par 2/3: verde
	0x333, 0xf80, 0x333, 0x333,   // par 4/5: naranja
	0x444, 0xf0f, 0x444, 0x444,   // par 6/7: magenta
}};

struct SpriteLayerDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020701u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(16384u, 16);
		m_sprite_block = backend.memory().chip.allocate_block<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteWords + kDmaChannels * kDmaStride) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020702u);
			return;
		}

		// Sprite "armado" válido (1 línea + terminador) al que apuntan los SPRxPT de reset.
		eng::Words<eng::SpriteTag> sd = m_sprite_block.view.as_words();
		sd[0] = 0xAAAAu; // DAT
		sd[1] = 0x0000u; // DATB
		sd[2] = 0u;      // terminador
		sd[3] = 0u;

		// Estructuras DMA (una por canal): [POS, CTL, DAT0, DATB0, ..., 0, 0]. El `POS` lo
		// parchea `SpriteLayer` con el scroll; el `CTL` fija VSTART..VSTOP de la banda.
		eng::u16* dma = sd.data() + kSpriteWords;
		for (eng::u8 ch = 0; ch < kDmaChannels; ++ch) {
			eng::u16* s = dma + static_cast<eng::u32>(ch) * kDmaStride;
			s[0] = 0u; // POS (parcheado por SpriteLayer)
			// CTL: VSTOP = first_line + height (N líneas de DATA = VSTOP - VSTART).
			s[1] = static_cast<eng::u16>((kBandLine0 + kDmaHeight) << 8);
			for (eng::u16 l = 0; l < kDmaHeight; ++l) {
				s[2u + l * 2u + 0u] = ((l & 1u) != 0u) ? 0x5555u : 0xAAAAu; // DAT
				s[2u + l * 2u + 1u] = 0x0000u;                              // DATB
			}
			s[2u + kDmaHeight * 2u + 0u] = 0u; // terminador del canal DMA
			s[2u + kDmaHeight * 2u + 1u] = 0u;
		}

		eng::effects::SpriteLayer::Config cfg {};
		cfg.first_line = kBandLine0;
		cfg.lines = kBandLines;
		cfg.channels = kChannels;
		cfg.hpos0 = kHpos0;
		cfg.hpos_step = kHposStep;
		cfg.data_high = 0xAAAAu; // patrón Copper: color 1 alternando
		cfg.data_low = 0x0000u;
		cfg.bplcon2 = 0x0008u;   // sprites DETRÁS de PF1 (capa de fondo)
		cfg.dma_channels = kDmaChannels;
		cfg.dma_data = dma;
		cfg.dma_stride = kDmaStride;
		if (!m_layer.attach(cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020703u);
			return;
		}

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020704u);
			return;
		}
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 0x00020700u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		// Reconstruye la lista cada frame con el scroll actual.
		m_layer.set_scroll(static_cast<eng::u16>(m_frame * 2u));
		++m_frame;
		if (build_copper()) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.view, kPlaneBytes);
		const eng::uintptr sprite_base =
			reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		for (eng::u8 c = 0; c < 8u; ++c) {
			sched.move(static_cast<eng::u16>(0x120u + c * 4u),
				   static_cast<eng::u16>(sprite_base >> 16));
			sched.move(static_cast<eng::u16>(0x122u + c * 4u),
				   static_cast<eng::u16>(sprite_base & 0xffffu));
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), 0x0000u);
			sched.move(static_cast<eng::u16>(0x140u + c * 8u), 0x0000u);
		}
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		// La capa emite BPLCON2 + los rearms horizontales (por línea y canal).
		m_layer.emit_into(sched);
		sched.wait_line(0xf8);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_copper_ok = false;
	eng::u32 m_frame = 0u;
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
	eng::effects::SpriteLayer m_layer {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	SpriteLayerDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
