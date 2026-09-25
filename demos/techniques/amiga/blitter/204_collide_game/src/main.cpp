// Demo 204 - Juego minimo con COLISION pixel-perfect por Blitter.
//
// Bucle de juego que usa `blitter_collide` (AND $80 + escaneo) por frame: el jugador
// (un rombo de 16x8) se mueve en pasos de 16 px y el obstaculo (mismo rombo) esta fijo.
// Cada frame se OR-ponen ambas mascaras en dos bandas de 1 plano a su posicion
// (`blitter_or_bobs`), se hace el AND con el Blitter y se escanea el resultado; si hay
// solape pixel-perfect, se enciende una barra de aviso (flash) unos frames.
//
// **Fachada de juego**: la demo usa `eng::App` (bucle + pantalla + servicios de Blitter).
// El dibujo va por `app.screen()` (contexto de alto nivel) y la colision por los
// `app.device().blitter_*` (operaciones de dominio, sin nombrar el backend). Ver
// `docs/engine/architecture/PUBLIC_GAME_API.md` §2 y §5/12.
//
// Display 256x256x4 con doble buffer (`app.present()` -> `Scene::commit`).
#include <eng/api/api.hpp>          // fachada: escena, dibujo, paleta, blitter preparado, run_status
#include <eng/api/game.hpp>         // App/Screen + servicios de Blitter
#include <eng/platform/amiga/backend.hpp>

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

namespace scene = eng::graphics::composition;

constexpr eng::u16 kWidth = 256;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kBytesPerRow = kWidth / 8u; // 32
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u8 kBuffers = 2;

constexpr eng::u16 kDiwstrt = 0x2ca1;
constexpr eng::u16 kDiwstop = 0x2ca1;
constexpr eng::u16 kDdfstrt = 0x0048;
constexpr eng::u16 kDdfstop = 0x00c0;
constexpr eng::u16 kBplcon0 = 0x4000;

constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "204: 256x256x4 en A500");

/// 0 fondo, 1 jugador, 2 obstaculo, 3 flash, 4 marco.
constexpr eng::Palette32 kPalette {{
	0x012, 0x0cf, 0xf33, 0xff0, 0x556, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

/// Mascara de rombo 16x8 (1 plano): 2 bytes por fila.
constexpr eng::u16 kMaskWords[8] = {
	0x03c0, 0x0ff0, 0x1ff8, 0x3ffc, 0x3ffc, 0x1ff8, 0x0ff0, 0x03c0,
};

constexpr eng::u16 kMaskRowBytes = 2u;
constexpr eng::u32 kMaskPlaneBytes = kMaskRowBytes * 8u; // 16
constexpr eng::u16 kBandWords = kBytesPerRow / 2u;       // 16
constexpr eng::u32 kBandPlaneBytes = kBytesPerRow * 8u;  // 256 (banda de 8 filas)
constexpr eng::u16 kPlayerY = 120u;
constexpr eng::u16 kHazardWord = 7u; ///< posicion del obstaculo en words (16 px)

struct CollideGame {
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);

		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = kBuffers;
		if (!scene::compose(m_scene, app.device().memory(), res, scene::ocs_a500,
				    scene::display(kDiwstrt, kDiwstop, kDdfstrt, kDdfstop, kBplcon0),
				    scene::palette(kPalette, 0u, 16u))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020402u);
			return;
		}
		app.bind_scene(m_scene);
		app.device().install_raster(m_scene); // rellenos de `screen()` por Blitter
		app.takeover();

		m_player_mask =
			app.device().memory().chip.template allocate_block<eng::PlaneTag>(kMaskPlaneBytes + 16u, 16u);
		m_hazard_mask =
			app.device().memory().chip.template allocate_block<eng::PlaneTag>(kMaskPlaneBytes + 16u, 16u);
		m_band_a =
			app.device().memory().chip.template allocate_block<eng::PlaneTag>(kBandPlaneBytes + 16u, 16u);
		m_band_b =
			app.device().memory().chip.template allocate_block<eng::PlaneTag>(kBandPlaneBytes + 16u, 16u);
		m_scan =
			app.device().memory().chip.template allocate_block<eng::PlaneTag>(kBandPlaneBytes + 16u, 16u);
		if (!m_player_mask.valid() || !m_hazard_mask.valid() || !m_band_a.valid() ||
		    !m_band_b.valid() || !m_scan.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020403u);
			return;
		}
		for (eng::u8 i = 0; i < 8u; ++i) {
			reinterpret_cast<eng::u16*>(m_player_mask.view.data())[i] = kMaskWords[i];
			reinterpret_cast<eng::u16*>(m_hazard_mask.view.data())[i] = kMaskWords[i];
		}

		for (eng::u8 b = 0; b < kBuffers; ++b) {
			app.device().blitter_clear(m_scene.buffer(b), kPlanes, kBytesPerRow, kPlaneBytes,
					  kWidth, kHeight, true);
		}
		eng::debug::mark_ready(g_eng_run_status, kHazardWord);
	}

	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
		app.device().wait_blitter();

		// 1) Posicion del jugador: avanza 1 word (16 px) y rebota.
		m_px = static_cast<eng::u16>(m_px + m_dir);
		if (m_px == 0u || m_px + 1u >= kBandWords) {
			m_dir = static_cast<eng::s8>(-m_dir);
			m_px = static_cast<eng::u16>(m_px + m_dir * 2);
		}

		// 2) Colision pixel-perfect por Blitter: bandas a 0, OR de las mascaras a su
		//    posicion (1 word de ancho, 8 filas), AND $80 + escaneo.
		app.device().blitter_clear(m_band_a.view, 1u, kBytesPerRow, kBandPlaneBytes, kWidth, 8u, true);
		app.device().blitter_clear(m_band_b.view, 1u, kBytesPerRow, kBandPlaneBytes, kWidth, 8u, true);
		const eng::graphics::OrBob pa {
			m_player_mask.view.data(),
			m_band_a.view.data() + static_cast<eng::u32>(m_px) * 2u, 0u};
		const eng::graphics::OrBob hb {
			m_hazard_mask.view.data(),
			m_band_b.view.data() + static_cast<eng::u32>(kHazardWord) * 2u, 0u};
		(void)app.device().blitter_or_bobs(eng::Span<const eng::graphics::OrBob> {&pa, 1u}, 1u, 8u,
						  static_cast<eng::s16>(kMaskRowBytes - 2u),
						  static_cast<eng::s16>(kBytesPerRow - 2u));
		(void)app.device().blitter_or_bobs(eng::Span<const eng::graphics::OrBob> {&hb, 1u}, 1u, 8u,
						  static_cast<eng::s16>(kMaskRowBytes - 2u),
						  static_cast<eng::s16>(kBytesPerRow - 2u));
		const bool hit = app.device().blitter_collide(m_band_a.view, m_band_b.view, m_scan.view,
						     1u, kBytesPerRow, kBandPlaneBytes,
						     kBandWords, 8u);
		if (hit) {
			m_flash = 6u;
			++m_hits;
		}

		// 3) Visual por el contexto de dibujo de alto nivel (`app.screen()`): marco,
		//    jugador, obstaculo y barra de aviso.
		auto s = app.screen();
		s.fill(eng::Box {0, 0, kWidth, kHeight}, 0u);
		s.fill(eng::Box {0, 0, kWidth, 2u}, 4u);
		s.fill(eng::Box {0, static_cast<eng::s16>(kHeight - 2), kWidth, 2u}, 4u);
		s.fill(eng::Box {static_cast<eng::s16>(m_px * 16u), static_cast<eng::s16>(kPlayerY), 16u,
				 8u},
		       1u);
		s.fill(eng::Box {static_cast<eng::s16>(kHazardWord * 16u),
				 static_cast<eng::s16>(kPlayerY), 16u, 8u},
		       2u);
		if (m_flash != 0u) {
			s.fill(eng::Box {0, 4, kWidth, 4u}, 3u);
			--m_flash;
		}
		app.present();
	}

	void render(auto& app) {
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

private:
	eng::u16 m_px = 1u;
	eng::s8 m_dir = 1;
	eng::u8 m_flash = 0u;
	eng::u16 m_hits = 0u;
	scene::Scene m_scene {};
	eng::Block<eng::PlaneTag> m_player_mask {};
	eng::Block<eng::PlaneTag> m_hazard_mask {};
	eng::Block<eng::PlaneTag> m_band_a {};
	eng::Block<eng::PlaneTag> m_band_b {};
	eng::Block<eng::PlaneTag> m_scan {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u})) {
		eng::debug::mark_failed(g_eng_run_status, 0x00020401u);
		return 0;
	}
	CollideGame game {};
	eng::App app {backend, game};
	app.run(0xffffu);

	return 0;
}
