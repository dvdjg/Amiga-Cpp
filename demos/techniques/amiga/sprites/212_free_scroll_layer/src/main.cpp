// Lanzar:
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --keep-running

// ============================================================================
// Demo 212 — Free Form Sprite Layer: fondo de sprites HW a PANTALLA COMPLETA con scroll
// ============================================================================
//
// Un **mundo** ancho (escena: cielo + sierras + suelo, 40 columnas = 640 px, 3 colores) se ve por
// una **ventana** de 320 px que scrollea sobre él. Lo monta `effects::FreeFormSpriteLayer`: los 8
// canales de sprite dibujan las 8 primeras columnas por **DMA**; el **Copper** reutiliza esos
// canales para el resto reescribiendo `SPRxPOS`+`SPRxDATB`+`SPRxDATA` (sin `SPRxCTL`). **CPU libre**
// (devora DMA). El scroll fino (0..15 px) se aplica parcheando solo la `SPRxPOS` (~0 CPU); el paso
// de columna del mundo rellena las estructuras y re-emite. Técnica:
// `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md` (Free Form).
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

constexpr eng::u16 kHpos0 = 112;        // 1.ª columna 16 px a la izquierda (margen de scroll)
constexpr eng::u16 kHposStep = 16;      // columnas de 16 px contiguas
constexpr eng::u16 kViewCols = 21;      // columnas visibles (21*16 = 336 > 320, margen de scroll)
constexpr eng::u16 kWorldCols = 40;     // mundo = 640 px
constexpr eng::u8  kChannels = 8u;      // 8 canales DMA = 8 primeras columnas
constexpr eng::u16 kBandLines = 255u;
constexpr eng::u16 kDmaStride = static_cast<eng::u16>(2u + kBandLines * 2u + 2u);
constexpr eng::u32 kSpriteBytes = static_cast<eng::u32>(kChannels) * kDmaStride * 2u;
constexpr eng::u32 kCuBytes = 64u * 1024u;
constexpr eng::u16 kScrollRange = static_cast<eng::u16>((kWorldCols - kViewCols) * kHposStep); // 304

// COLOR16-19 = colores 1/2/3 del par de sprite 0/1: cielo, sierra, suelo. Los 8 canales van por
// PARES (0/1, 2/3, 4/5, 6/7) y cada par usa 4 registros distintos -> hay que poner los MISMOS 3
// colores en los 4 pares, o el fondo sale a barras (cada par con colores distintos).
constexpr eng::Palette32 kPalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x000, 0x58f, 0x630, 0x260,   // par 0/1
	0x000, 0x58f, 0x630, 0x260,   // par 2/3
	0x000, 0x58f, 0x630, 0x260,   // par 4/5
	0x000, 0x58f, 0x630, 0x260,   // par 6/7
}};

struct FreeFormDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// 256K chip (bitplanes 40K + copper 64K + estructuras 8K) + 64K fast (imagen del mundo).
		if (!backend.configure_memory({ 256u * 1024u, 8u * 1024u, 64u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021201u); return;
		}
		m_bitplane = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(kCuBytes, 16);
		m_sprite = backend.memory_manager().chip().reserve<eng::SpriteTag>(kSpriteBytes, 16);
		m_world_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(
			static_cast<eng::u32>(kWorldCols) * kBandLines * 2u * 2u, 16);
		if (!m_bitplane.valid() || !m_copper.valid() || !m_sprite.valid() ||
		    !m_world_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021202u); return;
		}
		m_world = m_world_block.view.as_words().data();
		build_world();

		eng::effects::FreeFormSpriteLayer::Config cfg {};
		cfg.first_line = 0u;
		cfg.lines = kBandLines;
		cfg.hpos0 = kHpos0;
		cfg.hpos_step = kHposStep;
		cfg.columns = kViewCols;
		cfg.bplcon2 = 0x0008u;   // fondo (sprites) detrás del playfield
		cfg.arm_hpos = 0x40u;    // WAIT del rearmado
		cfg.channels = kChannels;
		cfg.dma_channels = kChannels;
		cfg.image = eng::Span<const eng::u16> {m_world, // el MUNDO; la ventana la fija `window_col`
			static_cast<eng::usize>(kWorldCols) * kBandLines * 2u};
		cfg.dma_data = eng::Span<eng::u16> {m_sprite.view.as_words().data(),
						    static_cast<eng::usize>(kChannels) * kDmaStride};
		cfg.dma_stride = kDmaStride;
		if (!m_layer.attach(cfg)) { eng::debug::mark_failed(g_eng_run_status, 0x00021203u); return; }
		fill_window(0u);
		m_last_window = 0u; // la ventana inicial ya esta montada
		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00021204u); return; }
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 0x00021200u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& c) {
		// Scroll: ping-pong 1 px/frame por el mundo. El fino (0..15) va en `SPRxPOS`; el grueso
		// (columna nueva cada 16 px) en la DATA. **Todo con el Blitter** -> CPU libre.
		const eng::u16 t = static_cast<eng::u16>(c.frame.frame_index % static_cast<eng::u32>(2u * kScrollRange));
		const eng::u16 s = static_cast<eng::u16>(t < kScrollRange ? t : 2u * kScrollRange - t);
		const eng::u16 window = static_cast<eng::u16>(s / kHposStep);
		m_layer.set_scroll(static_cast<eng::u16>(s % kHposStep));
		eng::u16* w = m_copper.view.as_words().data();
		const eng::u16 base = m_layer.pos_word_base();
		const eng::u16 stride = m_layer.pos_line_stride_words();
		const eng::u16 cop_cols = static_cast<eng::u16>(kViewCols - kChannels); // columnas Copper
		if (window != m_last_window) { // entra/sale una columna del mundo
			m_layer.set_window_col(window);
			for (eng::u16 j = 0u; j < cop_cols; ++j) {
				const eng::u16 src_col = static_cast<eng::u16>(window + kChannels + j);
				const eng::u16* src = m_world + static_cast<eng::usize>(src_col) * kBandLines * 2u;
				backend.blitter_copy_words_strided(src, w + base + j * 6u + 2u, 2u, kBandLines,
								   0u, static_cast<eng::u16>(stride - 2u));
			}
			fill_window(window); // estructuras DMA de los 8 canales (ventana)
			m_last_window = window;
		}
		for (eng::u16 j = 0u; j < cop_cols; ++j) { // `SPRxPOS` (fino) por Blitter
			backend.blitter_fill_words_strided(w + base + j * 6u,
							   m_layer.column_pos(static_cast<eng::u16>(kChannels + j)),
							   kBandLines, stride);
		}
		for (eng::u16 i = 0u; i < kChannels; ++i) { // reposición de fin de línea
			backend.blitter_fill_words_strided(w + base + cop_cols * 6u + i * 2u,
							   m_layer.column_pos(i), kBandLines, stride);
		}
		eng::debug::mark_frame(g_eng_run_status, c.frame.frame_index);
	}
	void render(eng::amiga::AmigaBackend&, eng::GameContext& c) {
		eng::debug::probe_when_ready(g_eng_run_status, c.frame.frame_index);
	}

private:
	/// Perfil del terreno (sierras) en el px del mundo `x` (0..639): suma de ondas triangulares
	/// de periodo potencia de 2 (solo `&`, sin `%` -> sin libcalls).
	[[nodiscard]] static eng::u16 terrain_y(eng::u16 x) noexcept {
		const eng::s32 a = tri(x, 127u, 64u);
		const eng::s32 b = tri(static_cast<eng::u16>(x + 23u), 63u, 32u);
		const eng::s32 c2 = tri(static_cast<eng::u16>(x + 7u), 31u, 16u);
		return static_cast<eng::u16>(120 + a / 2 + b / 2 - c2);
	}
	[[nodiscard]] static eng::s32 tri(eng::u16 x, eng::u16 mask, eng::u16 period) noexcept {
		const eng::s32 m = static_cast<eng::s32>(x & mask); // mask = 2*period - 1
		return (m < static_cast<eng::s32>(period)) ? m : (2 * static_cast<eng::s32>(period) - m);
	}

	/// Genera el MUNDO (kiWorldCols x kBandLines, 3 colores): cielo (1) / sierra (2) / suelo (3).
	void build_world() {
		for (eng::u16 k = 0u; k < kWorldCols; ++k) {
			for (eng::u16 l = 0u; l < kBandLines; ++l) {
				eng::u16 dat = 0u, datb = 0u;
				for (eng::u16 px = 0u; px < kHposStep; ++px) {
					const eng::u16 x = static_cast<eng::u16>(k * kHposStep + px);
					const eng::u16 ty = terrain_y(x);
					// `v` 1..3: cielo / sierra (4 px de cresta) / suelo con textura.
					eng::u16 v = 1u;
					if (l >= ty) { v = 3u; }
					if (l >= ty && l < static_cast<eng::u16>(ty + 4u)) { v = 2u; }
					if (v == 3u && (((x >> 2u) + (l >> 2u)) & 1u) != 0u) { v = 2u; }
					if ((v & 1u) != 0u) { dat = static_cast<eng::u16>(dat | (0x8000u >> px)); }
					if ((v & 2u) != 0u) { datb = static_cast<eng::u16>(datb | (0x8000u >> px)); }
				}
				// Orden (DATB, DATA) en el mundo: la copia a la copperlist es directa.
				m_world[(static_cast<eng::usize>(k) * kBandLines + l) * 2u] = datb;
				m_world[(static_cast<eng::usize>(k) * kBandLines + l) * 2u + 1u] = dat;
			}
		}
	}

	/// Rellena las estructuras DMA de los 8 canales con las 8 primeras columnas de la **ventana**.
	void fill_window(eng::u16 window) {
		eng::u16* dma = m_sprite.view.as_words().data();
		for (eng::u8 ch = 0u; ch < kChannels; ++ch) {
			eng::u16* s = dma + static_cast<eng::u32>(ch) * kDmaStride;
			s[0] = 0u;                                                     // POS (parcheado)
			s[1] = static_cast<eng::u16>(kBandLines << 8u);                // CTL: VSTOP
			const eng::u16 col = static_cast<eng::u16>(window + ch);
			for (eng::u16 l = 0u; l < kBandLines; ++l) {
				const eng::usize t = (static_cast<eng::usize>(col) * kBandLines + l) * 2u;
				// La estructura es [.., DAT, DATB, ..]; el mundo va (DATB, DAT) -> swap.
				s[2u + l * 2u + 0u] = m_world[t + 1u]; // DAT
				s[2u + l * 2u + 1u] = m_world[t];     // DATB
			}
			s[2u + kBandLines * 2u + 0u] = 0u;
			s[2u + kBandLines * 2u + 1u] = 0u;
		}
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane.mem_view_chip(), kPlaneBytes);
		const eng::uintptr base = reinterpret_cast<eng::uintptr>(m_sprite.view.data());
		for (eng::u8 ch = 0u; ch < kChannels; ++ch) {
			const eng::uintptr addr = base + static_cast<eng::uintptr>(ch) * kDmaStride * 2u;
			sched.move(static_cast<eng::u16>(0x120u + ch * 4u), static_cast<eng::u16>(addr >> 16));
			sched.move(static_cast<eng::u16>(0x122u + ch * 4u), static_cast<eng::u16>(addr & 0xffffu));
			sched.move(static_cast<eng::u16>(0x142u + ch * 8u), 0x0000u);
			sched.move(static_cast<eng::u16>(0x140u + ch * 8u), 0x0000u);
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
	eng::u16 m_last_window = 0xffffu;
	// El MUNDO va en Chip RAM: lo lee el Blitter (copia) ademas de la CPU (emision).
	eng::Block<eng::PlaneTag> m_world_block {};
	eng::u16* m_world = nullptr;
	eng::effects::FreeFormSpriteLayer m_layer {};
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
