// ============================================================================
// Demo 089 — Sonda del relleno por plano (diagnóstico, estático)
// ============================================================================
//
// Sonda de diagnóstico del bug abierto del relleno por plano: dibuja UNA VEZ en
// `init` (sin update ni doble buffer, así una captura no puede pillar el blit a
// medias) dos polígonos con el mismo camino del engine que la demo 088:
//
//   - un RECTÁNGULO (aristas horizontales/verticales)
//   - un ROMBO (aristas inclinadas)
//
// sobre un display de UN plano (el plano de trabajo ES la imagen visible), y el
// resto de la vida de la demo no toca la pantalla. Sirve para separar «bug del
// fill/contorno» de «artefacto de captura/tearing»: si la captura estática
// muestra la misma falta de relleno, el defecto es del camino por plano.
//
// Usa exactamente `graphics::PlaneFillBuilder` + `AmigaBackend::fill_polygons_by_plane`
// (contorno XOR ONEDOT por plano + dedupe + area fill FILL_XOR descendente).

#include <eng/api/api.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <eng/platform/amiga/entry.hpp>

namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u8 kPlanes = 2u;
constexpr u16 kRowBytes = kWidth / 8u;
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;

constexpr eng::Palette32 kPalette {{
	0x000, 0x00f, 0xf00, 0xf0f, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

constexpr eng::debug::Version kVersion {1u, 0u};

enum : eng::u32 {
	kBootMemory = 0x00008901u,
	kBootCompose = 0x00008902u,
};

// Rectángulo (control) y rombo (aristas inclinadas), separados y grandes.
constexpr s16 kRectX[4] = {30, 120, 120, 30};
constexpr s16 kRectY[4] = {40, 40, 130, 130};
constexpr s16 kDiamX[4] = {220, 290, 220, 150};
constexpr s16 kDiamY[4] = {40, 110, 180, 110};

struct FillProbeDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!backend.configure_memory({48u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = 1u;
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::display(res), scene::palette(kPalette, 0u, 4u))) {
			eng::debug::mark_failed(g_eng_run_status,
						kBootCompose + static_cast<eng::u32>(m_scene.config_error().code));
			return;
		}
		m_scene.takeover(backend);
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		// Limpieza y dibujo UNA SOLA VEZ: el resultado queda estático en pantalla.
		(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kHeight);
		eng::graphics::PlaneFillBuilder<2> fb {};
		(void)fb.submit(kRectX, kRectY, 4u, 1u);
		(void)fb.submit(kDiamX, kDiamY, 4u, 2u);
		eng::Block<eng::MaskTag> mask = backend.memory_manager().chip().reserve<eng::MaskTag>(kPlaneBytes);
		if (!mask.valid()) { eng::debug::mark_failed(g_eng_run_status, kBootCompose); return; }
		(void)backend.fill_polygons_by_plane_spans(fb.faces().data(), fb.count(), planes, kRowBytes,
						     kPlaneBytes, kPlanes, kWidth, kHeight, mask.view);
		playfield::ContiguousPlayfield pf {};
		(void)pf.bind_raw(planes.data(), static_cast<u32>(kPlaneBytes) * kPlanes, kWidth, kHeight, kPlanes, kPlaneBytes);
		playfield::Surface hud {pf, playfield::SurfaceRect {0, 232, kWidth, 24}};
		(void)hud.draw_text_words(16, 236, "089 FILL PROBE", 1u);
		m_ready = true;
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}
	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	scene::Scene m_scene {};
	bool m_ready = false;
};

FillProbeDemo g_game {};

} // namespace

int main() {
	eng::amiga::ensure_boot();
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	backend.set_vblank_sleep(false);
	eng::Engine engine {backend, g_game};
	(void)eng::os::init(engine, 0u);
	engine.set_blit_service_enabled(false);
	engine.run_frames(0xffffu);
	return 0;
}
