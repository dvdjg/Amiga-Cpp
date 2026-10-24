// ============================================================================
// Demo 095 - Collision 3D: trigger OBB + capas + siluetas Blitter
// ============================================================================
//
// Un cubo jugador recorre una puerta estatica. La puerta es un trigger OBB con
// filtro de capas; `trigger_sphere` decide si el jugador esta dentro y cambia el
// color de la puerta. El render es por silueta convexa con Blitter (sin CPU).

#include <eng/api/3d.hpp>
#include <eng/api/api.hpp>
#include <eng/core/util/collision3d.hpp>
#include <eng/debug/host_hud.hpp>
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

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kWorldH = 208u;
constexpr u16 kRowBytes = kWidth / 8u;
constexpr u8 kPlanes = 4u;
constexpr u8 kWorldPlanes = 2u;
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
constexpr eng::debug::Version kVersion {1u, 0u};
using Camera = eng::scene3d::Camera;
using CT = eng::math::scalar_traits<eng::coord>;
constexpr Camera kCamera {CT::from_int(kFocal), kCx, kCy, CT::from_int(kNearZ),
			  CT::from_int(kHalfW), CT::from_int(kHalfH)};

enum : u32 {
	kBootMemory = 0x00009501u,
	kBootCompose = 0x00009502u,
	kBootMask = 0x00009503u,
	kBootModels = 0x00009504u,
	kBootCamera = 0x00009505u,
	kBootScene = 0x00009506u,
};

struct CollisionDemo {
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

		retro::MeshData<eng::coord, 8, 24, 6> gate_mesh;
		retro::MeshData<eng::coord, 8, 24, 6> player_mesh;
		(void)retro::fill_box(gate_mesh, math3d::vec3(24, 44, 12));
		(void)retro::fill_box(player_mesh, math3d::vec3(10, 10, 10));
		m_gate_model = m_models.add_mesh(gate_mesh.view(), 2u);
		m_player_model = m_models.add_mesh(player_mesh.view(), 3u);
		if (!m_gate_model.valid() || !m_player_model.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootModels);
			return;
		}
		if (!m_world.set_camera(kCamera)) {
			eng::debug::mark_failed(g_eng_run_status, kBootCamera);
			return;
		}
		m_gate = m_world.spawn({.model = m_gate_model, .position = math3d::vec3(0, 0, 300)});
		m_player = m_world.spawn({.model = m_player_model,
					  .position = math3d::vec3(0, 0, 285)});
		if (!m_gate.valid() || !m_player.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootScene);
			return;
		}
		build_trigger();
		m_ready = true;
		update(backend, context);
		update(backend, context);
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) return;
		m_display.commit();
		const eng::PlaneBytes planes = m_display.back();
		(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(planes.size()), kWidth,
					kHeight, kPlanes, kPlaneBytes);
		clear_world(backend, planes);

		m_phase = static_cast<u16>((m_phase + 10u) & 4095u);
		const s32 sx = retro::kSinQ12[m_phase];
		const s16 px = static_cast<s16>((sx * 56) >> 12);
		(void)m_world.set_position(m_player, math3d::vec3(px, 0, 285));

		eng::collision3d::Sphere3<eng::coord> sphere {
			math3d::vec3(px, 0, 285), CT::from_int(10)};
		m_inside = eng::collision3d::trigger_sphere(
			m_trigger, sphere, eng::collision3d::CollisionFilter {2u, 0xffffu});

		const auto meshes = scene_meshes();
		eng::playfield::Surface world {m_canvas,
					       eng::playfield::SurfaceRect {0, 0, kWidth, kWorldH}};
		const u32 drawn = m_world.render_silhouette(meshes, world, [&](scene3d::NodeId id, u32) {
			if (id == m_gate) return static_cast<u8>(m_inside ? 6u : 2u);
			return static_cast<u8>(4u);
		});
		eng::util::TextLine<40> stats {};
		stats << (m_inside ? "IN" : "OUT") << " L" << m_trigger.filter.layer
		      << " D" << drawn;
		m_host_hud.draw(backend.debug(), "095 COLLISION3D", kVersion,
				context.frame.frame_index, eng::debug::prof_clock(),
				stats.c_str());
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	void build_trigger() {
		m_trigger.volume.center = math3d::vec3(0, 0, 300);
		m_trigger.volume.axis[0] = math3d::vec3(1, 0, 0);
		m_trigger.volume.axis[1] = math3d::vec3(0, 1, 0);
		m_trigger.volume.axis[2] = math3d::vec3(0, 0, 1);
		m_trigger.volume.half = math3d::vec3(18, 32, 12);
		m_trigger.filter.layer = 1u;
		m_trigger.filter.mask = 2u;
		m_trigger.active = true;
	}

	eng::Span<const scene3d::Mesh> scene_meshes() {
		m_meshes[0] = m_models.mesh(m_gate_model).operator*();
		m_meshes[1] = m_models.mesh(m_player_model).operator*();
		return eng::Span<const scene3d::Mesh>(m_meshes, 2u);
	}

	void clear_world(eng::amiga::AmigaBackend& backend, eng::PlaneBytes planes) {
		constexpr s16 x0 = 48;
		constexpr s16 y0 = 32;
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
		(void)band.draw_text_words(16, 8, "095 COLLISION3D", 15u);
	}

	bool m_ready = false;
	bool m_inside = false;
	comp::Scene m_display {};
	eng::playfield::ContiguousPlayfield m_canvas {};
	eng::amiga::PolygonFillService m_fill {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_mask {};
	scene3d::Scene<4, 8> m_world {};
	scene3d::ModelTable<2, 8, 24, 6, 2> m_models {};
	scene3d::Mesh m_meshes[2] {};
	scene3d::ModelHandle m_gate_model {};
	scene3d::ModelHandle m_player_model {};
	scene3d::NodeId m_gate {};
	scene3d::NodeId m_player {};
	eng::collision3d::Trigger3D<eng::coord> m_trigger {};
	eng::debug::HostHud m_host_hud {};
	u16 m_phase = 0u;
};

CollisionDemo g_game {};

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