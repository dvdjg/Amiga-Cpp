// Demo 204 - Juego minimo con COLISION pixel-perfect por Blitter.
//
// Bucle de juego que usa `blitter_collide` (AND $80 + escaneo) por frame: el jugador
// (un rombo de 16x8) se mueve en pasos de 16 px y el obstaculo (mismo rombo) esta fijo.
// Cada frame se OR-ponen ambas mascaras en dos bandas de 1 plano a su posicion
// (`blitter_or_bobs`), se hace el AND con el Blitter y se escanea el resultado; si hay
// solape pixel-perfect, se enciende una barra de aviso (flash) unos frames.
//
// Es el consumidor de juego que faltaba de `blitter_collide` (verificado antes solo por
// el self-test de 077). La visualizacion (jugador/obstaculo) va por `Surface::fill_rect`.
//
// Display 256x256x4 con doble buffer (`Scene::commit`).
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/scene/compose.hpp>
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

namespace scene = eng::graphics::scene;
namespace field = eng::field;

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
constexpr eng::u16 kBplcon1 = 0x0000;

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
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020401u);
			return;
		}

		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = kBuffers;
		if (!scene::compose(m_scene, backend.memory(), res, scene::ocs_a500,
				    scene::display(kDiwstrt, kDiwstop, kDdfstrt, kDdfstop, kBplcon0),
				    scene::palette(kPalette, 0u, 16u))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020402u);
			return;
		}
		backend.install_raster(m_scene);
		m_scene.takeover(backend);

		m_player_mask = backend.memory().chip.allocate_block<eng::PlaneTag>(kMaskPlaneBytes + 16u, 16);
		m_hazard_mask = backend.memory().chip.allocate_block<eng::PlaneTag>(kMaskPlaneBytes + 16u, 16);
		m_band_a = backend.memory().chip.allocate_block<eng::PlaneTag>(kBandPlaneBytes + 16u, 16);
		m_band_b = backend.memory().chip.allocate_block<eng::PlaneTag>(kBandPlaneBytes + 16u, 16);
		m_scan = backend.memory().chip.allocate_block<eng::PlaneTag>(kBandPlaneBytes + 16u, 16);
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
			backend.blitter_clear(m_scene.buffer(b), kPlanes, kBytesPerRow, kPlaneBytes,
					      kWidth, kHeight, true);
		}
		eng::debug::mark_ready(g_eng_run_status, kHazardWord);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_memory_ok) {
			return;
		}
		backend.wait_blitter();
		eng::PlaneBytes planes = m_scene.back();

		// 1) Posicion del jugador: avanza 1 word (16 px) y rebota.
		m_px = static_cast<eng::u16>(m_px + m_dir);
		if (m_px == 0u || m_px + 1u >= kBandWords) {
			m_dir = static_cast<eng::s8>(-m_dir);
			m_px = static_cast<eng::u16>(m_px + m_dir * 2);
		}

		// 2) Colision pixel-perfect por Blitter: bandas a 0, OR de las mascaras a su
		//    posicion (1 word de ancho, 8 filas), AND $80 + escaneo.
		backend.blitter_clear(m_band_a.view, 1u, kBytesPerRow, kBandPlaneBytes, kWidth, 8u, true);
		backend.blitter_clear(m_band_b.view, 1u, kBytesPerRow, kBandPlaneBytes, kWidth, 8u, true);
		const eng::amiga::MinimalBackend::OrBobEntry pa {
			m_player_mask.view.data(),
			m_band_a.view.data() + static_cast<eng::u32>(m_px) * 2u, 0u};
		const eng::amiga::MinimalBackend::OrBobEntry hb {
			m_hazard_mask.view.data(),
			m_band_b.view.data() + static_cast<eng::u32>(kHazardWord) * 2u, 0u};
		(void)backend.blitter_or_bobs(&pa, 1u, 1u, 8u,
					      static_cast<eng::s16>(kMaskRowBytes - 2u),
					      static_cast<eng::s16>(kBytesPerRow - 2u));
		(void)backend.blitter_or_bobs(&hb, 1u, 1u, 8u,
					      static_cast<eng::s16>(kMaskRowBytes - 2u),
					      static_cast<eng::s16>(kBytesPerRow - 2u));
		const bool hit = backend.blitter_collide(m_band_a.view, m_band_b.view, m_scan.view,
							 1u, kBytesPerRow, kBandPlaneBytes,
							 kBandWords, 8u);
		if (hit) {
			m_flash = 6u;
			++m_hits;
		}

		// 3) Visual: marco, jugador, obstaculo y barra de aviso.
		field::Surface s = m_scene.surface();
		s.fill_rect(0, 0, kWidth, kHeight, 0u);
		s.fill_rect(0, 0, kWidth, 2u, 4u);
		s.fill_rect(0, static_cast<eng::s16>(kHeight - 2), kWidth, 2u, 4u);
		s.fill_rect(static_cast<eng::s16>(m_px * 16u), kPlayerY, 16u, 8u, 1u);
		s.fill_rect(static_cast<eng::s16>(kHazardWord * 16u), kPlayerY, 16u, 8u, 2u);
		if (m_flash != 0u) {
			s.fill_rect(0, 4, kWidth, 4u, 3u);
			--m_flash;
		}

		m_scene.commit();
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool m_memory_ok = false;
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

	eng::amiga::MinimalBackend backend {};
	CollideGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
