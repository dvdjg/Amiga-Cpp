// Demo 204 — scroll de tiras **declarativo** por la fachada `App`.
//
// Tutorial (§7 de ROADMAP_GAME_API): el juego **no ve** el compositor, los buffers ni los `BPLxPT`.
// Monta una `field::StripScrollLayer` (mapa + banco + paleta + tamaños) y la **registra** con
// `app.add_scroll_layer(layer)`: el `App` la arranca (memoria + backend) y la conduce por frame
// (`pump_scroll_layers`), leyendo la variable de scroll que el juego avanza. El display lo posee la
// capa (su copperlist); el juego solo describe y avanza el scroll.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/204_app_strip_scroll --release
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/204_app_strip_scroll

#include <eng/api/api.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/field/scroll_route.hpp>
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
constexpr eng::u16 kMapSide = 40u;
constexpr eng::u16 kMapPx = kMapSide * 16u; // 640: alto (y ancho) del mapa en px

// El anillo es **ancho de mapa + solape** (X, tira) y **alto del bitmap** (RingLines): así el scroll
// Y es solo mover la punta de fila (sin split, el bitmap ya tiene las filas) y la tira X rellena
// columnas ENTERAS → X (tira) + Y (puntero) = vertical, diagonal y Lissajous.
//
// Alto del bitmap (`kRingLines`): debe caber el recorrido de la ruta (`RouteCamera` mueve 192 px en
// vertical/diagonal y radio 90): 256 + 192 = 448. Así **cada frame es distinto** (1 px/frame, sin
// saturar). Con 320 (Y=64) la fase vertical se queda clavada y se repiten frames. El anillo de 448
// es ~158 KB + el banco del atlas 111 KB ≈ 270 KB (cabe en A500); el mapa entero (640) serían 225 KB
// de anillo → A1200/1 MB.
constexpr eng::u16 kRingLines = 448u;
constexpr eng::s32 kYRange = static_cast<eng::s32>(kRingLines - kViewportH); // 192 px de recorrido Y
using Geom = eng::field::StripScrollGeometry<kViewportW, kViewportH, 3u, 16u, 16u, 2u, 1u,
					    false, 0u, kRingLines, kMapSide>;

constexpr eng::u16 kTileWords = Geom::tile_h * Geom::planes; // 48
constexpr eng::u16 kBlocksPerRow = 20u;
constexpr eng::u16 kBankRowBytes = 40u;
constexpr eng::u32 kBankBytes = 1180u * kTileWords * 2u;
constexpr eng::u32 kColumnBytes = static_cast<eng::u32>(Geom::column_planelines) * 2u;
constexpr eng::u32 kRingBytes =
	static_cast<eng::u32>(Geom::ring_w_bytes) * Geom::planes * Geom::ring_h;

// Ruta de scroll **continua** del engine (`playfield::ScrollRoute`): fases H/V/diagonal/circular/
// Lissajous por velocidad, sin saltos y con <= 1 px/frame por eje; la Y se acota al bitmap.
using Route = eng::playfield::ScrollRoute<static_cast<eng::u16>(kYRange)>;

struct AppStripGame {
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_bank {};
	// El `Map` de la capa es un **asset de tilemap** del engine (`TilemapView`: banco+mapa+paleta);
	// el juego no escribe un adaptador a mano.
	eng::field::StripScrollLayer<Geom, eng::field::TilemapView, eng::amiga::AmigaBackend> m_layer {};
	eng::field::TilemapView m_view {};
	eng::Palette32 m_pal {};

	// **Ruta de scroll** (fases H/V/diagonal/circular/Lissajous) que alimenta X e Y de la capa.
	Route m_route {};
	eng::s32 m_cam_x = 1;
	eng::s32 m_cam_y = m_route.y;

	void build_packed_bank(eng::u16* bank_words) {
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
					bank_words[t * kTileWords + r * Geom::planes + p] =
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
			eng::debug::mark_failed(g_eng_run_status, 0x00020401u);
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
		m_layer.set_tilemap(m_view);
		m_layer.set_sizes(kRingBytes, kColumnBytes, 1536u);
		m_layer.track_camera(&m_cam_x, &m_cam_y);
		if (!app.add_scroll_layer(m_layer)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020402u);
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
		if (app.frame() >= 4u) eng::debug::mark_ready(g_eng_run_status, 0x02040000u);
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
		eng::debug::mark_failed(g_eng_run_status, 0x00020403u);
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
		eng::debug::mark_failed(g_eng_run_status, 0x00020404u);
		return 0;
	}
	app.run(0xffffu);
	return 0;
}
