// ============================================================================
// Demo 091 - Escena 3D retenida: jerarquía, rotación y materiales
// ============================================================================
//
// Tutorial de la fachada `eng::scene3d` (G0–G3 de `ROADMAP_3D_GAME_API.md`):
// tres cubos viven en una `Scene<8, 8>` con handles generacionales. El cubo raíz
// gira en Y/Z; dos cubos hijos cuelgan de él con `set_parent` y giran sobre sí
// mismos, de modo que heredan la rotación del padre. Los materiales (color por
// cara) viajan en una `ModelTable`; el render no conoce planos, Blitter,
// máscaras ni registros.
//
// El display y el relleno son la infraestructura ya validada de las demos 3D:
// `composition::Scene` posee los bitplanes; `PolygonFillService` conecta
// `Surface::fill_polygon` con el Blitter (máscara 1 bpp + cookie-cut). La lógica
// 3D de la demo es solo `Scene`/`ModelTable`.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/091_scene3d_nodes --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/091_scene3d_nodes

#include <eng/api/api.hpp>
#include <eng/api/3d.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/platform/amiga/polygon_fill.hpp>
#include <eng/retro/fixed_trig.hpp>
#include <eng/retro/mesh_primitives.hpp>

namespace {

namespace comp = eng::graphics::composition;
namespace math3d = eng::math3d;
namespace scene3d = eng::scene3d;
namespace retro = eng::retro;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

// --- Display: 320x256, 4 planos; filas 208..255 para el rótulo -----------------
constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kWorldH = 208u;
constexpr u16 kRowBytes = kWidth / 8u; // 40
constexpr u8 kPlanes = 1u; // alambre 1 bpp: 1 job de línea por arista (presupuesto)
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kBandY = kWorldH;
constexpr u16 kBandH = kHeight - kWorldH;
constexpr u8 kBuffers = 1u;

// --- Cámara ------------------------------------------------------------------
constexpr s16 kFocal = 340;
constexpr s16 kCx = 160;
constexpr s16 kCy = 104;
constexpr s16 kNearZ = 64;
constexpr s16 kHalfW = 160;
constexpr s16 kHalfH = 104;

constexpr eng::Palette32 kPalette {{
	0x013, 0xf80, 0x0df, 0x7ef, 0xff0, 0x6f6, 0xf9c, 0xf24,
	0x333, 0x888, 0xccc, 0x05f, 0x444, 0x0a3, 0xa07, 0xfff,
}};
constexpr u8 kHudColor = 1u; // blanco (COLOR01; display de 1 plano)
constexpr eng::debug::Version kVersion {1u, 0u};
constexpr u16 kPhaseStep = 12u;

/// `1` = alambre (por defecto): aísla la escena/jerarquía del coste del relleno.
/// `0` = caras rellenas por Blitter (variante de referencia; el relleno por cara
/// sigue por debajo del suelo de 25 fps hasta cerrar P8).
#ifndef K091_WIRE
#define K091_WIRE 1
#endif
static_assert(K091_WIRE == 0 || K091_WIRE == 1, "K091_WIRE debe ser 0 o 1");

/// Códigos de fallo de arranque (el runner los resuelve por `.map`).
enum : u32 {
	kBootMemory = 0x00009101u,
	kBootCompose = 0x00009102u,
	kBootMask = 0x00009103u,
	kBootModels = 0x00009104u,
	kBootCamera = 0x00009105u,
	kBootScene = 0x00009106u,
};

using Camera = eng::scene3d::Camera;
using CameraT = eng::math::scalar_traits<eng::coord>;

constexpr Camera kCamera {
	CameraT::from_int(kFocal), kCx, kCy, CameraT::from_int(kNearZ),
	CameraT::from_int(kHalfW), CameraT::from_int(kHalfH)};

struct Scene3dDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		comp::SceneResources res = comp::planar(kWidth, kHeight, kPlanes);
		res.buffers = kBuffers;
		if (!comp::compose(m_display, backend.memory_manager(), res, comp::ocs_a500,
				   comp::display(res), comp::palette(kPalette, 0u, 2u))) {
			eng::debug::mark_failed(
				g_eng_run_status,
				kBootCompose + static_cast<u32>(m_display.config_error().code));
			return;
		}
		m_display.takeover(backend);

		m_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kRowBytes) * kHeight + 16u, 16);
		if (!m_mask.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootMask);
			return;
		}
		const eng::PlaneBytes planes = m_display.buffer(0u);
		(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(kPlaneBytes) * kPlanes,
					kWidth, kHeight, kPlanes, kPlaneBytes);
		m_fill.backend = &backend;
		m_fill.mask = m_mask.view;
		m_canvas.set_polygon_fill_sink(m_fill.sink());
		// El alambre encola líneas por Blitter en el `FramePlan` (la ruta CPU de
		// `draw_line` cuesta ~1500 ciclos/píxel y no cabe en el presupuesto).
		m_canvas.set_rasterizer(&eng::playfield::kBlitterRaster);

		(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kHeight);
		draw_band();

		// Modelos: cubo raíz (28), cubo pequeño (14) y cubo pequeño (10) con
		// materiales distintos. Todos son primitivas `constexpr` en rodata.
		retro::MeshData<eng::coord, 8, 24, 6> big;
		retro::MeshData<eng::coord, 8, 24, 6> small;
		(void)retro::fill_box(big, math3d::vec3(28, 28, 28));
		(void)retro::fill_box(small, math3d::vec3(16, 16, 16));
		const auto big_model = m_models.add_mesh(big.view(), 3u);
		const auto small_model = m_models.add_mesh(small.view(), 5u);
		if (!big_model.valid() || !small_model.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootModels);
			return;
		}

		if (!m_world.set_camera(kCamera)) {
			eng::debug::mark_failed(g_eng_run_status, kBootCamera);
			return;
		}
		m_root = m_world.spawn({.model = big_model, .position = math3d::vec3(0, 0, 320)});
		m_child_a = m_world.spawn({.model = small_model,
					   .position = math3d::vec3(64, 0, 0),
					   .parent = m_root});
		if (!m_root.valid() || !m_child_a.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootScene);
			return;
		}
		m_ready = true;
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) {
			return;
		}
		const eng::PlaneBytes planes = m_display.buffer(0u);
		(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kWorldH);

		m_phase = static_cast<u16>((m_phase + kPhaseStep) & 4095u);
		math3d::Mat3<> root_rot {};
		math3d::load_rotate(root_rot, retro::turns(m_phase),
				    retro::turns(static_cast<u16>((m_phase * 2u) & 4095u)),
				    retro::turns(static_cast<u16>((m_phase * 3u) & 4095u)));
		(void)m_world.set_rotation(m_root, root_rot);

		math3d::Mat3<> child_a_rot {};
		math3d::load_rotate(child_a_rot, retro::turns(static_cast<u16>((m_phase * 5u) & 4095u)),
				    retro::turns(static_cast<u16>((m_phase * 7u) & 4095u)),
				    retro::turns(0u));
		(void)m_world.set_rotation(m_child_a, child_a_rot);

		eng::playfield::Surface world {m_canvas,
					       eng::playfield::SurfaceRect {0, 0, kWidth, kWorldH}};
		eng::util::TextLine<40> stats {};
#if K091_WIRE
		m_plan.clear();
		const u32 edges = m_world.render_wire(
			m_models, world, kHudColor, eng::Ref<eng::graphics::FramePlan> {m_plan});
		const bool plan_ok = backend.execute_frame_plan(m_plan);
		stats << 'E' << edges << " J" << m_plan.blit_job_count()
		      << (plan_ok ? " OK" : " ERR") << (m_plan.ok() ? " ok" : " bad");
#else
		const u32 faces = m_world.render(m_models, world);
		stats << 'F' << faces;
#endif

		m_host_hud.draw(backend.debug(), "091 SCENE3D NODES", kVersion,
				context.frame.frame_index, eng::debug::prof_clock(), stats.c_str());
		m_display.commit();
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	void draw_band() {
		eng::playfield::Surface band {
			m_canvas, eng::playfield::SurfaceRect {0, static_cast<s16>(kBandY), kWidth,
							       static_cast<s16>(kBandH)}};
		(void)band.draw_text_words(16, 8, "091 SCENE3D NODES", kHudColor);
	}

	bool m_ready = false;
	comp::Scene m_display {};
	eng::playfield::ContiguousPlayfield m_canvas {};
	eng::amiga::PolygonFillService m_fill {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_mask {};
	scene3d::Scene<8, 8> m_world {};
	scene3d::ModelTable<2, 8, 24, 6, 2> m_models {};
	scene3d::NodeId m_root {};
	scene3d::NodeId m_child_a {};
	eng::graphics::FramePlan m_plan {};
	eng::debug::HostHud m_host_hud {};
	u16 m_phase = 0u;
};

Scene3dDemo g_game {};

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
