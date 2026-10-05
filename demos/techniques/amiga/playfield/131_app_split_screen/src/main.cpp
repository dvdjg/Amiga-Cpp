// Demo 131 - app_split_screen
// ----------------------------------------------------------------------------
// Tutorial (planner §7): **split-screen por la fachada**: el juego compone el **layout de bandas**
// (`plan_raster_layout` sobre las vistas de sus campos) y se lo entrega al `App` con
// **`app.present_layout(layout)`** — el `App` **materializa la copperlist (que posee) y toma el
// display**. Es el camino del juego que quiere dos ventanas/tramos con geometría propia sin bajar
// al `RasterLayout`/`Scheduler` a mano: declara en el vocabulario común y el `App` compone.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/131_app_split_screen --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/131_app_split_screen --warp

#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/scene/banded_target.hpp>
#include <eng/scene/display.hpp>
#include <eng/scene/raster_plan.hpp>

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

using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kScreenBpr = kWidth / 8u; // 40
constexpr u8 kPlanes = 3u;
constexpr u16 kPitch = 80u; // bytes/fila/plano (320 px + margen X)
constexpr u32 kFieldBytes = static_cast<u32>(kPitch) * kPlanes * kHeight;
constexpr u16 kSplitLine = 128u;

constexpr eng::Palette32 kPalette {{
	0x000, 0xf00, 0x00f, 0x0f0, 0xff0, 0xfff, 0x0ff, 0xf0f,
	0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0xaaa,
}};

// BOB (objeto) por banda: hoja 16×16 a 3 planos con padding (copia opaca limpia sin estela).
constexpr u16 kBobPadded = 32u;
constexpr u16 kBobVisible = 16u;
constexpr u8 kBobPad = 8u;
constexpr u32 kBobWordsPerPlaneRow = (kBobPadded / 16u) + 1u;
constexpr u32 kBobRowBytes = kBobWordsPerPlaneRow * 2u * kPlanes;
constexpr u32 kBobSheetBytes = kBobRowBytes * kBobPadded;

/// Juego sobre la fachada `App`: split-screen de dos campos (bandas) con el vocabulario del planner.
struct AppSplitGame {
	static constexpr eng::scene::BandSpan kBands[2] = {
		{0u, kSplitLine, eng::scene::LayerRole::Foreground},
		{kSplitLine, static_cast<eng::u16>(kHeight - kSplitLine), eng::scene::LayerRole::Foreground}};

	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		auto& mm = app.memory_manager();
		m_top = mm.chip().template reserve<eng::PlaneTag>(kFieldBytes, 16u);
		m_bottom = mm.chip().template reserve<eng::PlaneTag>(kFieldBytes, 16u);
		m_sheet = mm.chip().template reserve<eng::BobTag>(kBobSheetBytes, 16u);
		if (!m_top.valid() || !m_bottom.valid() || !m_sheet.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00013101u);
			return;
		}
		fill_field(m_top.view.data(), 1u);
		fill_field(m_bottom.view.data(), 2u);
		configure_view(m_v_top, m_top.view.data());
		configure_view(m_v_bottom, m_bottom.view.data());

		// Declaración de la escena (vocabulario del planner): split-screen = 2 capas en banda.
		(void)m_scene_plan.add(eng::scene::LayerRole::Foreground,
				       eng::scene::LayerPlacement {0u, kSplitLine, 0u});
		(void)m_scene_plan.add(eng::scene::LayerRole::Foreground,
				       eng::scene::LayerPlacement {kSplitLine,
								   static_cast<eng::u16>(kHeight - kSplitLine), 0u});
		// El **RasterLayout** se deriva del plan; el `App` lo materializa y toma el display.
		const eng::playfield::PlayfieldHardwareView views[2] = {m_v_top, m_v_bottom};
		eng::scene::RasterLayout layout {};
		if (!eng::scene::plan_raster_layout(
			    m_scene_plan,
			    eng::Span<const eng::playfield::PlayfieldHardwareView> {views, 2u}, layout)
			     .has_value()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00013102u);
			return;
		}
		m_band_top = layout[0];
		m_band_bottom = layout[1];
		layout[0].palette = kPalette.words();
		layout[0].palette_colors = 16u;

		build_bob();

		// **El `App` posee la composición**: materializa la copperlist del layout y hace el takeover.
		if (!app.present_layout(layout)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00013103u);
			return;
		}
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x13100000u);
	}

	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
		if (!m_ready) {
			return;
		}
		const u32 f = app.frame();
		const u32 period = 2u * kWidth;
		const u32 px = f % period;
		const u16 x = static_cast<u16>(px < kWidth ? px : period - 1u - px);

		// Borra la barra anterior (restaura el fondo de cada banda) y dibuja la nueva repartida.
		draw_bar(m_last_x, 1u, kTopBg);
		draw_bar(m_last_x, 2u, kBottomBg);
		draw_bar(m_last_x + 1u, 1u, kTopBg);
		draw_bar(m_last_x + 1u, 2u, kBottomBg);
		for (u16 i = 0u; i < 2u; ++i) {
			draw_bar(x + i, 1u, 5u);
			draw_bar(x + i, 2u, 5u);
		}
		m_last_x = x;

		// Marcadores (Fast BOBs) por banda: el `App` los emite en el `BobTarget` de SU banda.
		m_bobs[0] = {static_cast<s16>(40 + (f % 160u)), 48, 0u, true};
		m_bobs[1] = {static_cast<s16>(280 - (f % 160u)), 192, 0u, true};
		const eng::graphics::BobTarget targets[2] = {m_band_top.bob_target(),
							     m_band_bottom.bob_target()};
		(void)app.emit_bobs_banded(m_bobs, eng::Span<const eng::scene::BandSpan> {kBands, 2u},
					   eng::Span<const eng::graphics::BobTarget> {targets, 2u});

		g_eng_run_status.detail = 0x13100000u | (x & 0xffffu);
	}

	void render(auto& app) {
		app.present();
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

private:
	static constexpr u8 kTopBg = 1u;
	static constexpr u8 kBottomBg = 2u;

	static void configure_view(eng::playfield::PlayfieldHardwareView& v, u8* base) {
		v.planes = kPlanes;
		v.bitmap_bytes_per_row = kPitch;
		v.bitmap_height = kHeight;
		v.viewport_w = kWidth;
		v.viewport_h = kHeight;
		v.display_height = kHeight;
		v.plane_bytes = kFieldBytes;
		v.real_base = eng::Address<eng::MemoryKind::Chip>::from_storage(base);
		v.bpl1mod = static_cast<u16>(static_cast<u32>(kPitch) * kPlanes - kScreenBpr);
		v.bpl2mod = v.bpl1mod;
	}

	static void fill_field(u8* data, u8 color) {
		for (u32 i = 0u; i < kFieldBytes; ++i) {
			data[i] = 0u;
		}
		for (u16 y = 0u; y < kHeight; ++y) {
			for (u8 p = 0u; p < kPlanes; ++p) {
				if ((color & (1u << p)) == 0u) {
					continue;
				}
				u8* row = data + (static_cast<u32>(y) * kPlanes + p) * kPitch;
				for (u16 b = 0u; b < kPitch; ++b) {
					row[b] = 0xffu;
				}
			}
		}
	}

	/// Columna de 1 px repartida por banda: `for_each_band_part` decide qué trozo va a qué campo.
	void draw_bar(u16 x, u16 band, u8 color) {
		if (band > 2u) {
			return;
		}
		u8* data = band == 1u ? m_top.view.data() : m_bottom.view.data();
		const s32 band_top = band == 1u ? 0 : static_cast<s32>(kSplitLine);
		const eng::Box column {static_cast<s16>(x), 0, 1u, kHeight};
		eng::scene::for_each_band_part(
			column, eng::Span<const eng::scene::BandSpan> {kBands, 2u},
			[&](u16 bi, const eng::Box& part) {
				if (bi + 1u != band) {
					return;
				}
				const u16 y0 = static_cast<u16>(band == 1u ? part.y : (part.y - band_top));
				for (u16 k = 0u; k < part.h; ++k) {
					for (u8 p = 0u; p < kPlanes; ++p) {
						u8* pxp = data + ((static_cast<u32>(y0 + k) * kPlanes + p) * kPitch) +
							  (x >> 3u);
						const u8 bit = static_cast<u8>(0x80u >> (x & 7u));
						if ((color & (1u << p)) != 0u) {
							*pxp = static_cast<u8>(*pxp | bit);
						} else {
							*pxp = static_cast<u8>(*pxp & ~bit);
						}
					}
				}
			});
	}

	void build_bob() {
		eng::u8* s = m_sheet.view.data();
		for (u32 i = 0u; i < kBobSheetBytes; ++i) {
			s[i] = 0u;
		}
		for (u16 y = kBobPad; y < kBobPad + kBobVisible; ++y) {
			for (u8 p = 0u; p < kPlanes; ++p) {
				if ((5u & (1u << p)) == 0u) {
					continue;
				}
				eng::u8* row = s + (static_cast<u32>(y) * kPlanes + p) * (kBobWordsPerPlaneRow * 2u);
				row[1] = 0xffu;
				row[2] = 0xffu;
			}
		}
		eng::graphics::Bob bob {};
		bob.sheet = m_sheet.mem_view_chip();
		bob.width = kBobPadded;
		bob.height = kBobPadded;
		bob.planes = kPlanes;
		bob.layout = eng::graphics::BobLayout::Interleaved;
		bob.draw = eng::graphics::BobDraw::Opaque;
		m_sprite = eng::graphics::Sprite {bob};
		m_bobs.set_sheet(m_sprite, kBobPad, kBobPad);
		m_bobs.resize(2u);
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_top {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_bottom {};
	eng::Block<eng::BobTag> m_sheet {};
	eng::graphics::Sprite m_sprite {};
	eng::scene::FastBobLayer m_bobs {};
	eng::playfield::PlayfieldHardwareView m_v_top {};
	eng::playfield::PlayfieldHardwareView m_v_bottom {};
	eng::scene::Band m_band_top {};
	eng::scene::Band m_band_bottom {};
	eng::scene::ScenePlan<2u> m_scene_plan {};
	u16 m_last_x = 0u;
	bool m_ready = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_memory({256u * 1024u, 16u * 1024u, 0u})) {
		eng::debug::mark_failed(g_eng_run_status, 0x00013104u);
		return 0;
	}
	// Display base mínimo: lo sobreescribe el layout que compone el `App` en `init`.
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 1u;

	AppSplitGame game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00013105u);
		return 0;
	}
	app.run(0xffffu);
	return 0;
}
