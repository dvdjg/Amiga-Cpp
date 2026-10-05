// Lanzar:
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/211_risky_woods_layer --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/211_risky_woods_layer --keep-running

// ============================================================================
// Demo 211 — fondo de 3 franjas por sprites via effects::RiskyWoodsLayer (ALTO NIVEL)
// ============================================================================
//
// Equivalente a la 208_risky_woods pero **sin Copper a mano**: cada franja es una
// `effects::RiskyWoodsLayer` (reposicion de SPRxPOS; la capa resuelve el reparto de canales, el
// WAIT/head-start, la paleta por banda y el reset). El juego solo describe las capas.
//
//   A: 8 canales, patron 128 px   · B: 6 canales, 96 px · C: 4 pares attached, 64 px
//
// Entra por `eng/api/api.hpp` (la fachada ya incluye `effects.hpp`).
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

constexpr eng::u16 kBandLines = 80;
constexpr eng::u16 kColWidth = 16;
constexpr eng::u16 kScreenW = 320;
constexpr eng::u16 kStride = static_cast<eng::u16>(2u + kBandLines * 2u + 2u); // 164
constexpr eng::u32 kCuBytes = 48u * 1024u;

// Paletas por banda (COLOR16-19 del par 0): 3 colores del fondo de esa franja.
constexpr eng::u16 kPalA[4] { 0x000, 0x630, 0xff0, 0x24a }; // marron/amarillo/azul
constexpr eng::u16 kPalC[16] { 0x000, 0x630, 0x950, 0xc60, 0xfc0, 0xff0, 0xcf0, 0x8f0,
			       0x0f0, 0x0f8, 0x0cf, 0x09f, 0x60f, 0x90f, 0xc0f, 0xf0f }; // arcoiris

// Perfil de la figura por banda (3 colores), calculado con constexpr (sin mul/div en runtime).
[[nodiscard]] constexpr eng::u16 fig3(eng::u16 px, eng::u16 l, eng::u16 pattern, eng::u8 band) {
	const eng::u16 half = static_cast<eng::u16>(pattern / 2u);
	const eng::u16 d = static_cast<eng::u16>(px < half ? (half - px) : (px - half));
	const eng::u16 wy = static_cast<eng::u16>(40u - (10u * static_cast<eng::u32>(d) / half));
	(void)band;
	if (l + 1u < wy) { return 1u; }
	if (l > wy + 1u) { return 3u; }
	return 2u;
}
[[nodiscard]] constexpr eng::u16 fig15(eng::u16 px, eng::u16 l, eng::u16 pattern) {
	const eng::s16 dx = static_cast<eng::s16>(px) - static_cast<eng::s16>(pattern / 2u);
	const eng::s16 dy = static_cast<eng::s16>(l) - 40;
	return static_cast<eng::u16>((static_cast<eng::u16>((dx * dx + dy * dy) >> 7)) & 15u);
}

struct LayerDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// 384K chip (bitplanes 40K + copper 48K + sprites + margen), 8K slow (Bogo), 8K fast.
		if (!backend.configure_memory({ 384u * 1024u, 8u * 1024u, 8u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021101u); return;
		}
		m_bitplane = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(kCuBytes, 16);
		m_sprite = backend.memory_manager().chip().reserve<eng::SpriteTag>(8u * kStride * 2u, 16);
		if (!m_bitplane.valid() || !m_copper.valid() || !m_sprite.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021102u); return;
		}
		build_band(m_sprite.view.as_words().data(), 0, 128u, false);
		build_band(m_sprite.view.as_words().data(), 1, 96u, false);
		build_band(m_sprite.view.as_words().data(), 2, 64u, true);
		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00021103u); return; }
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 0x00021100u);
	}
	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}
	void render(eng::amiga::AmigaBackend&, eng::GameContext& c) {
		eng::debug::probe_when_ready(g_eng_run_status, c.frame.frame_index);
	}

private:
	void build_band(eng::u16* data, eng::u8 b, eng::u16 pattern, bool attach) {
		const eng::u8 channels = static_cast<eng::u8>(attach ? 8u : (b == 1u ? 6u : 8u));
		for (eng::u8 c = 0; c < channels; ++c) {
			eng::u16* s = data + b * 8u * kStride + static_cast<eng::u16>(c) * kStride;
			s[0] = 0u;
			s[1] = static_cast<eng::u16>((kBands[b].top + kBandLines) << 8);
			for (eng::u16 l = 0; l < kBandLines; ++l) {
				eng::u16 dat = 0u, datb = 0u;
				for (eng::u16 px = 0; px < kColWidth; ++px) {
					eng::u16 pxg = static_cast<eng::u16>((c * kColWidth + px) % pattern);
					const eng::u16 v = attach
						? fig15(pxg, l, pattern)
						: fig3(pxg, l, pattern, b);
					// `v` = valor del pixel; bit 0 -> DAT, bit 1 -> DATB (bit 15 = pixel 0, izq.).
					if ((v & 1u) != 0u) { dat = static_cast<eng::u16>(dat | (0x8000u >> px)); }
					if ((v & 2u) != 0u) { datb = static_cast<eng::u16>(datb | (0x8000u >> px)); }
				}
				s[2u + l * 2u + 0u] = dat;
				s[2u + l * 2u + 1u] = datb;
			}
			s[2u + kBandLines * 2u + 0u] = 0u;
			s[2u + kBandLines * 2u + 1u] = 0u;
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
		// Una capa por franja; la capa resuelve canales, WAIT, paleta y reset.
		for (eng::u8 b = 0; b < 3u; ++b) {
			eng::effects::RiskyWoodsLayer::Config cfg {};
			cfg.first_line = kBands[b].top;
			cfg.lines = kBandLines;
			cfg.channel_first = kBands[b].channel_first;
			cfg.channels = kBands[b].channels;
			cfg.column_width = kColWidth;
			// `RiskyWoodsLayer::screen_width` es el **borde derecho** de la cobertura (desde x=0):
			// borde izquierdo del display (128) + ancho (320) = 448.
			cfg.screen_width = static_cast<eng::u16>(128u + kScreenW);
			cfg.bplcon2 = 0x0008u;          // sprites (fondo) detras del playfield
			cfg.arm_hpos = 0x30u;           // WAIT tras el fetch DMA (hpos = px/2)
			cfg.head_start = 32u;           // 1.ª columna en X = 0x30*2 + 32 = 128 (borde display)
			cfg.attach = kBands[b].attach;
			cfg.burst_no_wait = kBands[b].attach; // el par attached no lleva WAIT intermedio
			cfg.dma_data = eng::Span<eng::u16> {m_sprite.view.as_words().data() + b * 8u * kStride,
							   static_cast<eng::usize>(8u * kStride)};
			cfg.dma_stride = kStride;
			cfg.reset_at_end = true;        // sin columna fantasma hacia la franja siguiente
			if (b == 0u) { cfg.palette = kPalA; cfg.palette_first_reg = 16u; }
			if (b == 2u) { cfg.palette = kPalC; cfg.palette_first_reg = 16u; }
			if (!m_layers[b].attach(cfg)) { return false; }
			m_layers[b].emit_into(sched);
		}
		sched.wait_line(0xf8);
		sched.end();
		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	struct Band { eng::u16 top; eng::u8 channels; bool attach; eng::u8 channel_first; };
	static constexpr Band kBands[3] {
		{ 48u, 8u, false, 0u },
		{ 129u, 6u, false, 2u },
		{ 210u, 8u, true, 0u },
	};
	static constexpr eng::Palette32 kPalette {{
		0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
		0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
		0x000, 0x630, 0xff0, 0x24a, 0x000, 0x630, 0xff0, 0x24a,
		0x000, 0x630, 0xff0, 0x24a, 0x000, 0x630, 0xff0, 0x24a,
	}};

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::effects::RiskyWoodsLayer m_layers[3] {};
	eng::Block<eng::PlaneTag> m_bitplane {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::Block<eng::SpriteTag> m_sprite {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	LayerDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
