// Demo 132 — scroll de tiras con **geometría cargada en RUNTIME** (paso 3 de §7).
//
// Tutorial: el motor de tiras consume una **`RuntimeScrollGeometry`** —la misma geometría del anillo
// que el NTTP `StripScrollGeometry`, pero calculada con **valores de runtime** (`runtime_scroll_geometry`,
// como la traería un editor o un formato no conocido en compilación)—. La capa se declara
// `StripScrollLayer<RuntimeScrollGeometry, …>`, se le fija la geometría con `set_geometry` y se
// **registra** con `app.add_scroll_layer` como cualquier otra: el `App` la arranca y la conduce. Así
// el juego **no necesita conocer la geometría al compilar** (el NTTP por nivel sigue siendo el rápido).
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/132_runtime_geometry --release
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/132_runtime_geometry

// Un solo include de fachada: trae la capa de scroll, la ruta de cámara y el asset de tilemap
// (`eng/api/scroll.hpp`) sin que el juego incluya `eng/field/*`.
#include <eng/api/api.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/field/runtime_scroll_geometry.hpp>
#include <eng/field/strip_layer.hpp>
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
constexpr eng::u16 kYTravel = 192u; // recorrido vertical (px) que exige la ruta

// El motor de tiras con **geometría runtime**: `StripScrollLayer<RuntimeScrollGeometry, …>`. El
// juego fija la geometría con `set_geometry` (aquí, valores "cargados") y luego liga el asset
// (`set_tilemap`), que **deriva los tamaños**. Intervienen X (tira) e Y (punta de fila) →
// vertical/diagonal/Lissajous. Anillo (256+192=448) ~158 KB + banco del atlas 111 KB ≈ 270 KB.
using Layer = eng::playfield::StripScrollLayer<eng::playfield::RuntimeScrollGeometry,
					       eng::playfield::TilemapView, eng::amiga::AmigaBackend>;

constexpr eng::u16 kTileWords = kTile * kPlanes; // 48
constexpr eng::u16 kBlocksPerRow = 20u;
constexpr eng::u16 kBankRowBytes = 40u;
constexpr eng::u32 kBankBytes = 1180u * kTileWords * 2u;

// Ruta de scroll **continua** del engine (`playfield::ScrollRoute`): fases H/V/diagonal/circular/
// Lissajous por velocidad, sin saltos y con <= 1 px/frame por eje; la Y se acota al bitmap.
using Route = eng::playfield::ScrollRoute<112u>; // misma ruta que la 205 (comparables)

struct AppStripGame {
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_bank {};
	// El `Map` de la capa es un **asset de tilemap** del engine (`TilemapView`: banco+mapa+paleta);
	// el juego no escribe un adaptador ni declara la geometría del motor.
	Layer m_layer {};
	eng::field::TilemapView m_view {};
	eng::Palette32 m_pal {};

	// **Ruta de scroll** (fases H/V/diagonal/circular/Lissajous) que alimenta X e Y de la capa.
	Route m_route {};
	eng::s32 m_cam_x = 1;
	eng::s32 m_cam_y = m_route.y;

	void build_packed_bank(eng::u16* bank_words) {
		const eng::u8* const xlim = g_tilebank_xlimited;
		const eng::u32 block_rows =
			g_tilebank_xlimited_size / (kTile * kPlanes * kBankRowBytes);
		const eng::u32 tile_count = block_rows * kBlocksPerRow;
		for (eng::u32 t = 0u; t < tile_count; ++t) {
			const eng::u32 tx = t % kBlocksPerRow;
			const eng::u32 ty = t / kBlocksPerRow;
			for (eng::u32 r = 0u; r < kTile; ++r) {
				for (eng::u32 p = 0u; p < kPlanes; ++p) {
					const eng::u32 src =
						(ty * kTile * kPlanes + r * kPlanes + p) *
							kBankRowBytes + tx * 2u;
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
			eng::debug::mark_failed(g_eng_run_status, 0x00013201u);
			return;
		}
		eng::u16* bank_words = reinterpret_cast<eng::u16*>(m_bank.data());
		build_packed_bank(bank_words);

		for (eng::u32 i = 0u; i < 8u; ++i) {
			const eng::u8 r = static_cast<eng::u8>(::kPalette[i * 3u]);
			const eng::u8 g = static_cast<eng::u8>(::kPalette[i * 3u + 1u]);
			const eng::u8 b = static_cast<eng::u8>(::kPalette[i * 3u + 2u]);
			m_pal.color[i] =
				static_cast<eng::u16>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}

		// Liga el **asset de tilemap** (banco + mapa del atlas + paleta) a la capa; el juego solo
		// DESCRIBE. `App` arranca la capa y la conduce.
		m_view.bank = bank_words;
		m_view.bank_stride_words = kTileWords;
		m_view.tiles = &kTileIndexedMap[0][0];
		m_view.cols = kMapSide;
		m_view.rows = kMapSide;
		m_view.palette = m_pal.words();
		// Setup **declarativo** con el vocabulario común (`ScrollPlan`): el juego describe
		// geometría/política/contenido y la capa deriva tamaños. `App` la arranca y la conduce.
		eng::playfield::ScrollPlan plan {};
		plan.viewport_w = kViewportW;
		plan.viewport_h = kViewportH;
		plan.planes = kPlanes;
		plan.map_period_words = kMapSide;
		plan.tilemap = m_view;
		// **Geometría cargada en RUNTIME** (como la traería un editor): mismos valores e
		// invariantes que el NTTP, pero calculados aquí; el motor la consume igual. El NTTP por
		// nivel sigue siendo el rápido; esta vía permite nivel/editor de geometría no conocida.
		const auto geo = eng::playfield::runtime_scroll_geometry(
			kViewportW, kViewportH, kPlanes, kTile, kTile, 2u, 1u, false, 0u,
			static_cast<eng::u16>(kViewportH + kYTravel), kMapSide);
		if (!geo.has_value()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00013205u);
			return;
		}
		m_layer.set_geometry(*geo);
		m_layer.set_plan(plan);
		m_layer.track_camera(&m_cam_x, &m_cam_y);
		if (!app.add_scroll_layer(m_layer, eng::scene::LayerRole::Background,
					  eng::scene::LayerPlacement {})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00013202u);
			return;
		}
	}

	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
		// La ruta recorre las fases; la capa lee `m_cam_x`/`m_cam_y` al ser conducida por el `App`.
		m_route.advance(app.frame());
		m_cam_x = m_route.x;
		m_cam_y = m_route.y;
	}

	void render(auto& app) {
		// La capa posee el display (su copperlist); el juego no dibuja ni hace `present`.
		if (app.frame() >= 4u) eng::debug::mark_ready(g_eng_run_status, 0x13200000u);
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	// Anillo = ancho del mapa (60 words) x 3 planos x 448 ≈ 158 KB; + banco del atlas (111 KB) +
	// display de `App`. Presupuesto Chip explícito (~300 KB, dentro de lo libre tras Kickstart).
	if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 0u})) {
		eng::debug::mark_failed(g_eng_run_status, 0x00013203u);
		return 0;
	}
	// El display base lo sobreescribe la capa de scroll (su compositor hace el takeover); basta un
	// display mínimo para que `App::start()` componga: deja memoria Chip para el anillo de la capa.
	eng::GameDisplay display {};
	display.width = kViewportW;
	display.height = kViewportH;
	display.color_depth = 1u;

	AppStripGame game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00013204u);
		return 0;
	}
	app.run(0xffffu);
	return 0;
}
