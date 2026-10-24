// ============================================================================
// Demo 092 - Personaje articulado: cadena PBD + esferas + fill por Blitter
// ============================================================================
//
// Tres partes rigidas (torso, cadera y pierna) forman una cadena articulada con
// pivotes y masa. `eng::sim3d::ArticulatedBody3D` integra gravedad y resuelve los
// joints de bola por PBD; cada parte tiene una esfera de colision y masa/inercia.
// El render es por silueta convexa con `PolygonFillService` y Blitter (sin CPU).

#include <eng/api/3d.hpp>
#include <eng/api/api.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/platform/amiga/polygon_fill.hpp>
#include <eng/retro/mesh_primitives.hpp>
#include <eng/sim/articulated_body3d.hpp>

namespace {

namespace comp = eng::graphics::composition;
namespace math3d = eng::math3d;
namespace scene3d = eng::scene3d;
namespace retro = eng::retro;
using eng::s16;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kWorldH = 208u;
constexpr u16 kRowBytes = kWidth / 8u;
constexpr u8 kPlanes = 4u;
#ifndef K092_WIRE
#define K092_WIRE 0
#endif
constexpr u8 kWorldPlanes = K092_WIRE ? 1u : 2u; ///< siluetas con 2 planos (colores 1..3).
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kBandY = kWorldH;
constexpr u16 kBandH = kHeight - kWorldH;
constexpr u8 kBuffers = 2u;

constexpr s16 kFocal = 340;
constexpr s16 kCx = 160;
constexpr s16 kCy = 104;
constexpr s16 kNearZ = 64;
constexpr s16 kHalfW = 160;
constexpr s16 kHalfH = 104;

constexpr eng::Palette32 kPalette {{
	0x000, 0xf80, 0x0df, 0x7ef, 0xff0, 0x6f6, 0xf9c, 0xf24,
	0x333, 0x888, 0xccc, 0x05f, 0x444, 0x0a3, 0xa07, 0xfff,
}};
constexpr u8 kHudColor = 15u;
constexpr eng::debug::Version kVersion {1u, 0u};

enum : u32 {
	kBootMemory = 0x00009201u,
	kBootCompose = 0x00009202u,
	kBootMask = 0x00009203u,
	kBootModels = 0x00009204u,
	kBootCamera = 0x00009205u,
	kBootScene = 0x00009206u,
};

using Camera = eng::scene3d::Camera;
using CameraT = eng::math::scalar_traits<eng::coord>;
using CT = eng::math::scalar_traits<eng::coord>;
constexpr Camera kCamera {
	CameraT::from_int(kFocal), kCx, kCy, CameraT::from_int(kNearZ),
	CameraT::from_int(kHalfW), CameraT::from_int(kHalfH)};

struct ArticulatedDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!backend.configure_memory({256u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		comp::SceneResources res = comp::planar(kWidth, kHeight, kPlanes);
		res.buffers = kBuffers;
		if (!comp::compose(m_display, backend.memory_manager(), res, comp::ocs_a500,
				   comp::display(res), comp::palette(kPalette, 0u, 16u))) {
			eng::debug::mark_failed(g_eng_run_status, kBootCompose);
			return;
		}
		m_display.takeover(backend);

		m_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kRowBytes) * kHeight + 16u, 16);
		if (!m_mask.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootMask);
			return;
		}
		m_fill.backend = &backend;
		m_fill.mask = m_mask.view;
		m_canvas.set_polygon_fill_sink(m_fill.sink());
		m_canvas.set_rasterizer(&eng::playfield::kBlitterRaster);
		for (u8 b = 0u; b < m_display.buffer_count(); ++b) {
			const eng::PlaneBytes planes = m_display.buffer(b);
			(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(planes.size()), kWidth,
						kHeight, kPlanes, kPlaneBytes);
			(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth,
						    kHeight);
			draw_band();
		}

		retro::MeshData<eng::coord, 8, 24, 6> torso;
		retro::MeshData<eng::coord, 8, 24, 6> hip;
		retro::MeshData<eng::coord, 8, 24, 6> leg;
		(void)retro::fill_box(torso, math3d::vec3(16, 22, 10));
		(void)retro::fill_box(hip, math3d::vec3(12, 10, 10));
		(void)retro::fill_box(leg, math3d::vec3(9, 24, 9));
		const auto torso_model = m_models.add_mesh(torso.view(), 1u);
		const auto hip_model = m_models.add_mesh(hip.view(), 2u);
		const auto leg_model = m_models.add_mesh(leg.view(), 3u);
		if (!torso_model.valid() || !hip_model.valid() || !leg_model.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootModels);
			return;
		}
		if (!m_world.set_camera(kCamera)) {
			eng::debug::mark_failed(g_eng_run_status, kBootCamera);
			return;
		}

		m_torso = m_world.spawn({.model = torso_model, .position = math3d::vec3(0, 80, 320)});
		m_hip = m_world.spawn({.model = hip_model, .position = math3d::vec3(0, 58, 320)});
		m_leg = m_world.spawn({.model = leg_model, .position = math3d::vec3(0, 34, 320)});
		if (!m_torso.valid() || !m_hip.valid() || !m_leg.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootScene);
			return;
		}

		m_body.add_part(-1, math3d::vec3(0, 80, 320), CT::zero());
		m_body.add_part(0, math3d::vec3(18, -30, 0), CT::from_int(2));
		m_body.add_part(1, math3d::vec3(-20, -24, 0), CT::from_int(1));
		m_body.add_sphere(0u, math3d::vec3(0, 0, 0), CT::from_int(14), CT::from_int(4));
		m_body.add_sphere(1u, math3d::vec3(0, 0, 0), CT::from_int(10), CT::from_int(2));
		m_body.add_sphere(2u, math3d::vec3(0, 0, 0), CT::from_int(8), CT::from_int(1));
		const auto unit = eng::math::scalar_traits<eng::real>::from_int(1);
		const auto limit = eng::math::div_norm(
			unit, eng::math::scalar_traits<eng::real>::from_int(6));
		const auto swing = eng::math::div_norm(
			eng::math::div_norm(
				eng::math::div_norm(unit,
						    eng::math::scalar_traits<eng::real>::from_int(4)),
				eng::math::scalar_traits<eng::real>::from_int(4)),
			eng::math::scalar_traits<eng::real>::from_int(2));
		(void)m_body.set_hinge(1u, 2u, -limit, limit);
		(void)m_body.set_hinge(2u, 2u, -limit, limit);
		m_swing = swing;
		m_body.finalize();
		sync_scene();

		m_ready = true;
		update(backend, context);
		update(backend, context);
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) {
			return;
		}
		m_display.commit();
		const eng::PlaneBytes planes = m_display.back();
		(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(planes.size()), kWidth,
					kHeight, kPlanes, kPlaneBytes);
		clear_world(backend, planes);

#ifndef K092_NOPHYS
		m_body.set_angular_velocity(1u, {eng::real{}, eng::real{}, m_swing});
		m_body.set_angular_velocity(2u, {eng::real{}, eng::real{}, -m_swing});
		m_swing = -m_swing;
		m_body.step(CT::from_int(1), CT::from_int(-1), CT::zero(), 4u);
#endif
		sync_scene();

		eng::playfield::Surface world {m_canvas,
					       eng::playfield::SurfaceRect {0, 0, kWidth, kWorldH}};
#ifndef K092_NORENDER
#if K092_WIRE
		m_plan.clear();
		const u32 edges = m_world.render_wire(
			m_models, world, 1u, eng::Ref<eng::graphics::FramePlan> {m_plan},
			eng::playfield::RasterOp::Xor, true, true);
		const bool plan_ok = backend.execute_frame_plan(m_plan);
		const u32 silhouettes = edges;
		(void)plan_ok;
#else
		const u32 silhouettes = m_world.render_silhouette(m_models, world);
		(void)backend.wait_blitter();
#endif
#else
		const u32 silhouettes = 0u;
#endif
		eng::util::TextLine<40> stats {};
		stats << 'P' << m_body.part_count << " S" << m_body.sphere_count << " F"
		      << silhouettes;
		m_host_hud.draw(backend.debug(), "092 ARTICULATED CHARACTER", kVersion,
				context.frame.frame_index, eng::debug::prof_clock(),
				stats.c_str());
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	void sync_scene() {
		(void)m_world.set_position(m_torso, m_body.parts[0].position);
		(void)m_world.set_position(m_hip, m_body.parts[1].position);
		(void)m_world.set_position(m_leg, m_body.parts[2].position);
		(void)m_world.set_rotation(m_torso, m_body.parts[0].world_orientation);
		(void)m_world.set_rotation(m_hip, m_body.parts[1].world_orientation);
		(void)m_world.set_rotation(m_leg, m_body.parts[2].world_orientation);
	}

	void clear_world(eng::amiga::AmigaBackend& backend, eng::PlaneBytes planes) {
		constexpr s16 x0 = 64;
		constexpr s16 y0 = 48;
		constexpr s16 x1 = 272;
		constexpr s16 y1 = 204;
		constexpr u16 words = static_cast<u16>((x1 - x0) >> 4);
		for (u8 p = 0u; p < kWorldPlanes; ++p) {
			const eng::PlaneBytes plane =
				planes.subspan(static_cast<u32>(p) * kPlaneBytes, kPlaneBytes);
			(void)backend.blitter_clear_rect(plane, kRowBytes, static_cast<u16>(x0), y0,
							 words, static_cast<u16>(y1 - y0), false);
		}
	}

	void draw_band() {
		eng::playfield::Surface band {
			m_canvas, eng::playfield::SurfaceRect {0, static_cast<s16>(kBandY), kWidth,
							       static_cast<s16>(kBandH)}};
		(void)band.draw_text_words(16, 8, "092 ARTICULATED CHARACTER", kHudColor);
	}

	bool m_ready = false;
	comp::Scene m_display {};
	eng::playfield::ContiguousPlayfield m_canvas {};
	eng::amiga::PolygonFillService m_fill {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_mask {};
	scene3d::Scene<4, 8> m_world {};
	scene3d::ModelTable<3, 8, 24, 6, 1> m_models {};
	eng::sim3d::ArticulatedBody3D<eng::coord, 3, 3> m_body {};
	eng::real m_swing {};
	scene3d::NodeId m_torso {};
	scene3d::NodeId m_hip {};
	scene3d::NodeId m_leg {};
	eng::graphics::FramePlan m_plan {};
	eng::debug::HostHud m_host_hud {};
};

ArticulatedDemo g_game {};

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
