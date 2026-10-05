// Demo 130 — **escalera de motores** por la fachada `App` (geometría runtime, §7).
//
// Tutorial: el juego declara **dos configuraciones conocidas** (dos geometrías de capa de tiras) y
// las registra en una `ScrollLadder`; luego **una geometría cargada en runtime** (aquí simulada)
// elige el motor con `app.pick_scroll_engine(ladder, geometry)`, que lo arranca y lo conduce. Es el
// camino para el caso "el nivel/editor no conoce la geometría en compilación" **sin** tocar el motor
// NTTP: el juego aporta los motores que conoce y el `App` **elige por geometría**.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/130_app_scroll_ladder --release
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/130_app_scroll_ladder --warp

#include <eng/api/api.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic, eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0,
};
}

// Atlas "Beginning Fields" a 8 colores (paleta + mapa 40x40) y banco X-Limited interleaved.
#include "../../../../../../out/assets/beginning-fields/8c/tilebank_8c_t16_mediancut_none_640x640.h"

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
constexpr eng::u16 kTile = 16u;
constexpr eng::u8 kPlanes = 3u;
constexpr eng::u16 kMapSide = 40u;  // tiles de lado del atlas (período del mapa toroidal)
constexpr eng::u16 kYTravel = 192u; // recorrido vertical (px) de la ruta

// Dos **motores conocidos** (geometrías NTTP): A = 320×256 (el "nivel" que se cargará), B = 320×208.
using GeomA = eng::playfield::StripScrollGeometry<kViewportW, kViewportH, kPlanes, kTile, kTile, 2u,
						 1u, false, 0u, kViewportH + kYTravel, kMapSide>;
using GeomB = eng::playfield::StripScrollGeometry<kViewportW, 208u, kPlanes, kTile, kTile, 2u, 1u,
						 false, 0u, 208u + kYTravel, kMapSide>;
using LayerA = eng::playfield::StripScrollLayer<GeomA, eng::field::TilemapView, eng::amiga::AmigaBackend>;
using LayerB = eng::playfield::StripScrollLayer<GeomB, eng::field::TilemapView, eng::amiga::AmigaBackend>;
using Ladder = eng::playfield::ScrollLadder<eng::amiga::AmigaBackend, 2u>;

constexpr eng::u16 kTileWords = kTile * kPlanes; // 48
constexpr eng::u16 kBlocksPerRow = 20u;
constexpr eng::u16 kBankRowBytes = 40u;
constexpr eng::u32 kBankBytes = 1180u * kTileWords * 2u;

using Route = eng::playfield::ScrollRoute<112u>;

struct LadderGame {
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_bank {};
	LayerA m_a {};
	LayerB m_b {};
	Ladder m_ladder {};
	eng::field::TilemapView m_view {};
	eng::Palette32 m_pal {};
	Route m_route {};
	eng::s32 m_cam_x = 1;
	eng::s32 m_cam_y = m_route.y;

	void build_packed_bank(eng::u16* bank_words) {
		const eng::u8* const xlim = g_tilebank_xlimited;
		const eng::u32 block_rows = g_tilebank_xlimited_size / (kTile * kPlanes * kBankRowBytes);
		const eng::u32 tile_count = block_rows * kBlocksPerRow;
		for (eng::u32 t = 0u; t < tile_count; ++t) {
			const eng::u32 tx = t % kBlocksPerRow;
			const eng::u32 ty = t / kBlocksPerRow;
			for (eng::u32 r = 0u; r < kTile; ++r) {
				for (eng::u32 p = 0u; p < kPlanes; ++p) {
					const eng::u32 src =
						(ty * kTile * kPlanes + r * kPlanes + p) * kBankRowBytes + tx * 2u;
					bank_words[t * kTileWords + r * kPlanes + p] =
						static_cast<eng::u16>((xlim[src] << 8) | xlim[src + 1u]);
				}
			}
		}
	}

	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		auto& mm = app.device().memory_manager();
		m_bank = mm.chip().template reserve<eng::PlaneTag>(kBankBytes, 16u);
		if (!m_bank.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00013001u);
			return;
		}
		eng::u16* bank_words = reinterpret_cast<eng::u16*>(m_bank.data());
		build_packed_bank(bank_words);
		for (eng::u32 i = 0u; i < 8u; ++i) {
			const eng::u8 r = static_cast<eng::u8>(::kPalette[i * 3u]);
			const eng::u8 g = static_cast<eng::u8>(::kPalette[i * 3u + 1u]);
			const eng::u8 b = static_cast<eng::u8>(::kPalette[i * 3u + 2u]);
			m_pal.color[i] = static_cast<eng::u16>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}
		m_view.bank = bank_words;
		m_view.bank_stride_words = kTileWords;
		m_view.tiles = &kTileIndexedMap[0][0];
		m_view.cols = kMapSide;
		m_view.rows = kMapSide;
		m_view.palette = m_pal.words();

		// Configura **cada motor conocido** (plan declarativo + cámara) y los registra en la escalera
		// con la geometría que implementan.
		eng::playfield::ScrollPlan plan_a {};
		plan_a.viewport_w = kViewportW;
		plan_a.viewport_h = kViewportH;
		plan_a.planes = kPlanes;
		plan_a.map_period_words = kMapSide;
		plan_a.tilemap = m_view;
		m_a.set_plan(plan_a);
		m_a.track_camera(&m_cam_x, &m_cam_y);

		eng::playfield::ScrollPlan plan_b {};
		plan_b.viewport_w = kViewportW;
		plan_b.viewport_h = 208u;
		plan_b.planes = kPlanes;
		plan_b.map_period_words = kMapSide;
		plan_b.tilemap = m_view;
		m_b.set_plan(plan_b);
		m_b.track_camera(&m_cam_x, &m_cam_y);

		const auto ga = eng::playfield::runtime_scroll_geometry(kViewportW, kViewportH, kPlanes, kTile,
									kTile, 2u, 1u, false, 0u,
									kViewportH + kYTravel, kMapSide);
		const auto gb = eng::playfield::runtime_scroll_geometry(kViewportW, 208u, kPlanes, kTile, kTile,
									2u, 1u, false, 0u, 208u + kYTravel,
									kMapSide);
		if (!ga.has_value() || !gb.has_value() || !m_ladder.add(m_a, *ga) ||
		    !m_ladder.add(m_b, *gb)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00013002u);
			return;
		}

		// Geometría **cargada en runtime** (aquí, la de A): el `App` **elige** el motor y lo conduce.
		const auto loaded = eng::playfield::runtime_scroll_geometry(
			kViewportW, kViewportH, kPlanes, kTile, kTile, 2u, 1u, false, 0u, kViewportH + kYTravel,
			kMapSide);
		if (!loaded.has_value() || !app.pick_scroll_engine(m_ladder, *loaded)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00013003u);
			return;
		}
	}

	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
		m_route.advance(app.frame());
		m_cam_x = m_route.x;
		m_cam_y = m_route.y;
	}

	void render(auto& app) {
		if (app.frame() >= 4u) eng::debug::mark_ready(g_eng_run_status, 0x01300000u);
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 0u})) {
		eng::debug::mark_failed(g_eng_run_status, 0x00013004u);
		return 0;
	}
	eng::GameDisplay display {};
	display.width = kViewportW;
	display.height = kViewportH;
	display.color_depth = 1u;
	LadderGame game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00013005u);
		return 0;
	}
	app.run(0xffffu);
	return 0;
}
