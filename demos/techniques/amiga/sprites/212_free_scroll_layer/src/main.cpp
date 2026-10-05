// Lanzar:
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --keep-running

// ============================================================================
// Demo 212 — fondo LIBRE (320 px, NO repetitivo) con effects::FreeScrollLayer
// ============================================================================
//
// **App de alto nivel**: usa la fachada `eng/api/api.hpp` y una sola capa
// `effects::FreeScrollLayer<20>` (= una imagen de 320 px, 20 columnas de 16). La capa resuelve el
// reparto de canales, el WAIT/head-start y la DATA por columna; aquí solo generamos la imagen.
// Ver `demos/techniques/amiga/sprites/209_free_scroll` (misma técnica, Copper a mano).
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

constexpr eng::u16 kBandTop = 48;
constexpr eng::u16 kBandLines = 80;
constexpr eng::u16 kCols = 20;        // 20 columnas = 320 px
constexpr eng::u16 kColWidth = 16;    // 16 px por columna
constexpr eng::u16 kScreenW = 320;
constexpr eng::u8  kChannels = 8;
constexpr eng::u32 kCuBytes = 48u * 1024u;
// Estructuras DMA de sprite: cabecera POS/CTL + DAT/DATB por línea + terminador POS/CTL.
constexpr eng::u16 kStructStride = static_cast<eng::u16>(2u + kBandLines * 2u + 2u);
constexpr eng::u32 kSpriteBytes = static_cast<eng::u32>(kCols) * kStructStride * 2u;

// COLOR00 fondo navy; colores 1/2/3 de cada par de sprite (suelo/cresta/cielo).
constexpr eng::Palette32 kPalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x000, 0x630, 0xff0, 0x24a, 0x000, 0x630, 0xff0, 0x24a,
	0x000, 0x630, 0xff0, 0x24a, 0x000, 0x630, 0xff0, 0x24a,
}};

struct FreeScrollDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// 384K chip (bitplanes 40K + copper 48K + margen), 8K slow (Bogo), 8K fast.
		if (!backend.configure_memory({ 384u * 1024u, 8u * 1024u, 8u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021201u); return;
		}
		m_bitplane = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(kCuBytes, 16);
		m_sprite = backend.memory_manager().chip().reserve<eng::SpriteTag>(kSpriteBytes, 16);
		if (!m_bitplane.valid() || !m_copper.valid() || !m_sprite.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021202u); return;
		}
		build_image();

		// La capa: una imagen de 320 px partida en 20 columnas. La DATA de cada columna es distinta
		// (no patrón repetido). La capa resuelve canales/WAIT/head-start.
		eng::effects::FreeScrollLayer<kCols>::Config cfg {};
		cfg.first_line = kBandTop;
		cfg.lines = kBandLines;
		cfg.channel_first = 0u;
		cfg.channels = kChannels;
		cfg.column_width = kColWidth;
		cfg.screen_width = kScreenW;
		cfg.bplcon2 = 0x0008u;   // fondo (sprites) detrás del playfield
		cfg.columns = kCols;
		cfg.tiles = m_tiles;
		cfg.dma_data = m_sprite.view.as_words().raw(); // estructuras DMA (Chip) rellenadas por attach()
		cfg.dma_stride = kStructStride;
		cfg.reset_at_end = true;   // sin columna fantasma hacia lo de abajo
		if (!m_layer.attach(cfg)) { eng::debug::mark_failed(g_eng_run_status, 0x00021203u); return; }
		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00021204u); return; }
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 0x00021200u);
	}
	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}
	void render(eng::amiga::AmigaBackend&, eng::GameContext& c) {
		eng::debug::probe_when_ready(g_eng_run_status, c.frame.frame_index);
	}

private:
	/// Imagen de fondo (320x80, 3 colores): un perfil de sierra por X (no repetitivo). `wy` = altura
	/// de la cresta en cada X; por debajo -> suelo (3), en la cresta -> valor 2, encima -> cielo (1).
	void build_image() {
		for (eng::u16 col = 0; col < kCols; ++col) {
			for (eng::u16 l = 0; l < kBandLines; ++l) {
				eng::u16 dat = 0u, datb = 0u;
				for (eng::u16 px = 0; px < kColWidth; ++px) {
					const eng::u16 x = static_cast<eng::u16>(col * kColWidth + px);
					// Damero de bloques de 8 px: 3 colores solidos (suelo/cresta/cielo), DISTINTO por
					// columna (cada columna desplaza el damero) -> fondo no repetitivo.
					const eng::u16 v = static_cast<eng::u16>(1u + (((x + col) >> 3u) & 1u) +
									 ((static_cast<eng::u16>(l) >> 3u) & 1u));
					// `v` = valor del pixel; bit 0 -> DAT, bit 1 -> DATB (bit 15 = pixel 0, izq.).
					if ((v & 1u) != 0u) { dat = static_cast<eng::u16>(dat | (0x8000u >> px)); }
					if ((v & 2u) != 0u) { datb = static_cast<eng::u16>(datb | (0x8000u >> px)); }
				}
				const eng::usize t = (static_cast<eng::usize>(col) * kBandLines + l) * 2u;
				m_tiles[t] = dat;
				m_tiles[t + 1u] = datb;
			}
		}
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper };
		// Registros de display: BPLCON0/BPLCON1/DIWSTRT/DIWSTOP; 40 B/fila, 4 planos, 320x256.
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		m_layer.emit_into(sched);   // la capa aporta su trozo de Copperlist
		sched.wait_line(0xf8);
		sched.end();
		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u16 m_tiles[kCols * kBandLines * 2u] {};
	eng::effects::FreeScrollLayer<kCols> m_layer {};
	eng::Block<eng::PlaneTag> m_bitplane {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::Block<eng::SpriteTag> m_sprite {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	FreeScrollDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
