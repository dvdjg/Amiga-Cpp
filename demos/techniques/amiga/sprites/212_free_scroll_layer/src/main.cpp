// Lanzar:
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --keep-running

// ============================================================================
// Demo 212 — Free Form Sprite Layer (fondo de sprites NO repetitivo) · efecto directo
// ============================================================================
//
// Los 8 canales de sprite cubren las **primeras 8 columnas de 16 px** por **DMA** (cada uno con
// su estructura en Chip RAM); el **Copper** reutiliza esos canales para las columnas restantes
// reescribiendo `SPRxPOS`/`SPRxDATB`/`SPRxDATA` (≥24 px entre reusos). Resultado: un fondo de
// **320 px donde cada columna es distinta** (sin patrón repetido). La CPU queda libre (devora
// DMA). Técnica: `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md` (Free Form),
// fuente Jeroen Knoester (powerprograms.nl/amiga/spr-layer.html).
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/backend.hpp>

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

namespace {

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u16 kScreenW = 320;
constexpr eng::u16 kHpos0 = 112;           // 1.ª columna 16 px a la izquierda (margen de scroll)
constexpr eng::u16 kHposStep = 16;         // columnas de 16 px contiguas
constexpr eng::u16 kColumns = 21;          // 21 columnas cubren el display con margen de scroll
constexpr eng::u8  kChannels = 8u;         // 8 canales DMA...
constexpr eng::u8  kDmaChannels = 8u;      // ...las 8 primeras columnas por DMA
constexpr eng::u16 kBandLine0 = 0u;
constexpr eng::u16 kBandLines = 255u;
constexpr eng::u16 kDmaStride = static_cast<eng::u16>(2u + kBandLines * 2u + 2u);
constexpr eng::u32 kSpriteBytes =
	static_cast<eng::u32>(kChannels) * kDmaStride * 2u;
constexpr eng::u32 kCuBytes = 64u * 1024u;

constexpr eng::Palette32 kPalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x000, 0x630, 0xff0, 0x24a, 0x000, 0x630, 0xff0, 0x24a,
	0x000, 0x630, 0xff0, 0x24a, 0x000, 0x630, 0xff0, 0x24a,
}};

struct FreeFormDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Chip: bitplanes 40K + copper 64K + estructuras 8K + margen.
		if (!backend.configure_memory({ 256u * 1024u, 8u * 1024u, 8u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021201u); return;
		}
		m_bitplane = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(kCuBytes, 16);
		m_sprite = backend.memory_manager().chip().reserve<eng::SpriteTag>(kSpriteBytes, 16);
		if (!m_bitplane.valid() || !m_copper.valid() || !m_sprite.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021202u); return;
		}
		build_image();

		// Estructuras DMA de los 8 canales = columnas 0..7 (DATA por línea). `SpriteLayer`
		// parchea su `POS` (scroll) y las columnas 8..19 las repite el Copper con su DATA.
		eng::u16* dma = m_sprite.view.as_words().data();
		for (eng::u8 ch = 0u; ch < kChannels; ++ch) {
			eng::u16* s = dma + static_cast<eng::u32>(ch) * kDmaStride;
			s[0] = 0u;                                                     // POS (parcheado)
			s[1] = static_cast<eng::u16>((kBandLine0 + kBandLines) << 8u); // CTL: VSTOP = fin de banda
			for (eng::u16 l = 0u; l < kBandLines; ++l) {
				const eng::usize t = (static_cast<eng::usize>(ch) * kBandLines + l) * 2u;
				s[2u + l * 2u + 0u] = m_image[t];
				s[2u + l * 2u + 1u] = m_image[t + 1u];
			}
			s[2u + kBandLines * 2u + 0u] = 0u; // terminador
			s[2u + kBandLines * 2u + 1u] = 0u;
		}

		eng::effects::FreeScrollLayer::Config cfg {};
		cfg.first_line = kBandLine0;
		cfg.lines = kBandLines;
		cfg.channels = kChannels;
		cfg.hpos0 = kHpos0;
		cfg.hpos_step = kHposStep;
		cfg.columns = kColumns;          // free form: 20 posiciones = 320 px no repetitivos
		cfg.image = m_image;             // DATA distinta por columna y línea
		cfg.bplcon2 = 0x0008u;
		cfg.arm_hpos = 0x40u;
		cfg.dma_channels = kDmaChannels;
		cfg.dma_data = eng::Span<eng::u16> {dma, static_cast<eng::usize>(kChannels) * kDmaStride};
		cfg.dma_stride = kDmaStride;
		if (!m_layer.attach(cfg)) { eng::debug::mark_failed(g_eng_run_status, 0x00021203u); return; }
		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00021204u); return; }
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 0x00021200u);
	}
	void update(eng::amiga::AmigaBackend&, eng::GameContext& c) {
		// Scroll suave a ~0 CPU: solo se reescriben las palabras `SPRxPOS` (no se re-emite la
		// copperlist). Ping-pong 0..15 px (una columna) para no dejar hueco a la izquierda.
		const eng::u16 tri = static_cast<eng::u16>(m_frame & 31u);
		m_layer.set_scroll(static_cast<eng::u16>(tri < 16u ? tri : 31u - tri));
		++m_frame;
		m_layer.patch(m_copper.view.as_words().data());
		eng::debug::mark_frame(g_eng_run_status, c.frame.frame_index);
	}
	void render(eng::amiga::AmigaBackend&, eng::GameContext& c) {
		eng::debug::probe_when_ready(g_eng_run_status, c.frame.frame_index);
	}

private:
	/// Imagen NO repetitiva (20x255, 3 colores): rejilla diagonal con fase distinta por columna.
	void build_image() {
		for (eng::u16 k = 0u; k < kColumns; ++k) {
			for (eng::u16 l = 0u; l < kBandLines; ++l) {
				eng::u16 dat = 0u, datb = 0u;
				for (eng::u16 px = 0u; px < kHposStep; ++px) {
					const eng::u16 x = static_cast<eng::u16>(k * kHposStep + px);
					// `v` 1..3: cada columna lleva una fase propia (`k*16`) -> sin repetición.
					// Rejilla de bloques de 8 px, con la fase vertical desplazada por columna
					// (`k`) -> cada columna es distinta (no repetitivo) pero sin discontinuidad.
					const eng::u16 v = static_cast<eng::u16>(
						1u + ((x >> 3u) & 1u) + ((static_cast<eng::u16>(l + k * 8u) >> 3u) & 1u));
					// Bit 15 = pixel 0 (izquierda); bit 0 -> DAT, bit 1 -> DATB.
					if ((v & 1u) != 0u) { dat = static_cast<eng::u16>(dat | (0x8000u >> px)); }
					if ((v & 2u) != 0u) { datb = static_cast<eng::u16>(datb | (0x8000u >> px)); }
				}
				m_image[(static_cast<eng::usize>(k) * kBandLines + l) * 2u] = dat;
				m_image[(static_cast<eng::usize>(k) * kBandLines + l) * 2u + 1u] = datb;
			}
		}
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane.mem_view_chip(), kPlaneBytes);
		// PT de cada canal a su estructura + estado inicial desarmado.
		const eng::uintptr base = reinterpret_cast<eng::uintptr>(m_sprite.view.data());
		for (eng::u8 c = 0u; c < kChannels; ++c) {
			const eng::uintptr addr = base + static_cast<eng::uintptr>(c) * kDmaStride * 2u;
			sched.move(static_cast<eng::u16>(0x120u + c * 4u), static_cast<eng::u16>(addr >> 16));
			sched.move(static_cast<eng::u16>(0x122u + c * 4u), static_cast<eng::u16>(addr & 0xffffu));
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), 0x0000u);
			sched.move(static_cast<eng::u16>(0x140u + c * 8u), 0x0000u);
		}
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		m_layer.bind(sched); // emite la capa y registra las palabras SPRxPOS (scroll ~0 CPU)
		sched.wait_line(0xf8);
		sched.end();
		m_copper_ptr = sched.data();
		return sched.ok();
	}

	const eng::u16* m_copper_ptr = nullptr;
	eng::u32 m_frame = 0u;
	eng::u16 m_image[kColumns * kBandLines * 2u] {};
	eng::effects::FreeScrollLayer m_layer {};
	eng::Block<eng::PlaneTag> m_bitplane {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::Block<eng::SpriteTag> m_sprite {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	FreeFormDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
