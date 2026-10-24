// ============================================================================
// Demo 096 - Silhouette swarm: muchos props planos a 25 fps
// ============================================================================
//
// Escena de juego: una nave central y una flota de drones/asteroides de un solo
// color. La ruta de silueta hace UN fill por objeto; la ruta por cara hace uno
// por cara. El HUD alterna SIL/FACE para comparar el coste real.

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
constexpr u8 kBuffers = 3u;
constexpr u16 kObjects = 2u;
constexpr u16 kModeFrames = 96u; ///< alterna SIL/FACE cada ~3,8 s a 25 fps.
#ifndef K096_FACE
#define K096_FACE 0
#endif
#ifndef K096_AUTO
#define K096_AUTO 0
#endif

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
	kBootMemory = 0x00009601u,
	kBootCompose = 0x00009602u,
	kBootMask = 0x00009603u,
	kBootModels = 0x00009604u,
	kBootCamera = 0x00009605u,
	kBootScene = 0x00009606u,
};

/// Posiciones de la flota en una elipse; la nave central queda fija.
struct Slot {
	s16 x;
	s16 y;
	s16 z;
};

constexpr Slot kSlots[kObjects] = {
	{0, 62, 300}, {-95, 20, 340},
};

struct SilhouetteDemo {
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

		retro::MeshData<eng::coord, 8, 24, 6> box;
		(void)retro::fill_box(box, math3d::vec3(14, 14, 14));
		m_model = m_models.add_mesh(box.view(), 1u);
		if (!m_model.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootModels);
			return;
		}
		if (!m_world.set_camera(kCamera)) {
			eng::debug::mark_failed(g_eng_run_status, kBootCamera);
			return;
		}
		for (u16 i = 0u; i < kObjects; ++i) {
			const auto n = m_world.spawn({.model = m_model,
						      .position = math3d::vec3(kSlots[i].x,
									       kSlots[i].y,
									       kSlots[i].z)});
			if (!n.valid()) {
				eng::debug::mark_failed(g_eng_run_status, kBootScene);
				return;
			}
			m_nodes[i] = n;
			m_colors[i] = static_cast<u8>(i == 0u ? 3u : 2u);
		}
		// Fondo de juego: rocas/props estaticos dibujados UNA vez por buffer con la
		// misma ruta de silueta. No cuestan nada por frame y dan sensacion de flota.
		if (!m_static.set_camera(kCamera)) {
			eng::debug::mark_failed(g_eng_run_status, kBootCamera);
			return;
		}
		constexpr Slot kRocks[8] = {
			{-150, -92, 520}, {-80, -96, 600}, {80, -90, 560}, {150, -88, 500},
			{-155, -78, 540}, {-70, -74, 610}, {75, -80, 570}, {150, -84, 510},
		};
		for (u16 i = 0u; i < 8u; ++i) {
			const auto n = m_static.spawn({.model = m_model,
						       .position = math3d::vec3(kRocks[i].x,
										kRocks[i].y,
										kRocks[i].z)});
			if (!n.valid()) {
				eng::debug::mark_failed(g_eng_run_status, kBootScene);
				return;
			}
		}
		for (u8 b = 0u; b < m_display.buffer_count(); ++b) {
			const eng::PlaneBytes planes = m_display.buffer(b);
			(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(planes.size()), kWidth,
						kHeight, kPlanes, kPlaneBytes);
			eng::playfield::Surface world {
				m_canvas, eng::playfield::SurfaceRect {0, 0, kWidth, kWorldH}};
			(void)m_static.render_silhouette(m_models, world);
			(void)backend.wait_blitter();
		}

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
		clear_objects(backend, planes);

		m_phase = static_cast<u16>((m_phase + 7u) & 4095u);
		const s32 s = retro::kSinQ12[m_phase];
		const s32 c = retro::kSinQ12[(m_phase + 1024u) & 4095u];
		for (u16 i = 1u; i < kObjects; ++i) {
			const s16 ox = static_cast<s16>((s * (8 + i)) >> 5);
			const s16 oy = static_cast<s16>((c * (4 + i)) >> 6);
			(void)m_world.set_position(m_nodes[i], math3d::vec3(
				static_cast<s16>(kSlots[i].x + ox),
				static_cast<s16>(kSlots[i].y + oy), kSlots[i].z));
		}
		math3d::Mat3<> rot {};
		math3d::load_rotate(rot, retro::turns(m_phase), retro::turns(0u),
				    retro::turns(static_cast<u16>(m_phase * 2u)));
		(void)m_world.set_rotation(m_nodes[0], rot);

		bool face_mode = K096_FACE != 0;
#if K096_AUTO
		face_mode = ((context.frame.frame_index / kModeFrames) & 1u) != 0u;
#endif
		m_meshes[0] = m_models.mesh(m_model).operator*();
		const auto meshes = eng::Span<const scene3d::Mesh>(m_meshes, 1u);
		eng::playfield::Surface world = world_surface();
		u32 drawn = 0u;
		if (face_mode) {
			drawn = m_world.render(meshes, world,
					       [&](scene3d::NodeId id, u32) {
						       return color_of(id);
					       });
		} else {
			drawn = m_world.render_silhouette(meshes, world,
							  [&](scene3d::NodeId id, u32) {
								  return color_of(id);
							  });
		}
		(void)backend.wait_blitter();

		eng::util::TextLine<40> stats {};
		stats << (face_mode ? "FACE" : "SIL") << " O" << kObjects << " D" << drawn;
		m_host_hud.draw(backend.debug(), "096 SILHOUETTE SWARM", kVersion,
				context.frame.frame_index, eng::debug::prof_clock(),
				stats.c_str());
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	eng::playfield::Surface world_surface() {
		return eng::playfield::Surface {
			m_canvas, eng::playfield::SurfaceRect {0, 0, kWidth, kWorldH}};
	}

	u8 color_of(scene3d::NodeId id) const {
		for (u16 i = 0u; i < kObjects; ++i) {
			if (m_nodes[i] == id) return m_colors[i];
		}
		return 1u;
	}

	void clear_objects(eng::amiga::AmigaBackend& backend, eng::PlaneBytes planes) {
		for (u16 i = 0u; i < kObjects; ++i) {
			const s16 sx = static_cast<s16>(kCx + (kSlots[i].x * kFocal) / kSlots[i].z);
			const s16 sy = static_cast<s16>(kCy + (kSlots[i].y * kFocal) / kSlots[i].z);
			const s16 x0 = static_cast<s16>(sx > 48 ? (sx - 48) & ~15 : 0);
			const s16 x1 = static_cast<s16>(sx < 272 ? sx + 48 : 319);
			const s16 y0 = static_cast<s16>(sy > 40 ? sy - 40 : 0);
			const s16 y1 = static_cast<s16>(sy < 168 ? sy + 40 : 207);
			const u16 words = static_cast<u16>(((x1 | 15) - x0 + 16) >> 4);
			for (u8 p = 0u; p < kWorldPlanes; ++p) {
				const eng::PlaneBytes plane =
					planes.subspan(static_cast<u32>(p) * kPlaneBytes, kPlaneBytes);
				(void)backend.blitter_clear_rect(plane, kRowBytes, static_cast<u16>(x0),
								 y0, words,
								 static_cast<u16>(y1 - y0 + 1), false);
			}
		}
	}

	void draw_band() {
		eng::playfield::Surface band {
			m_canvas, eng::playfield::SurfaceRect {0, static_cast<s16>(kBandY), kWidth,
							       static_cast<s16>(kBandH)}};
		(void)band.draw_text_words(16, 8, "096 SILHOUETTE SWARM", 15u);
	}

	bool m_ready = false;
	comp::Scene m_display {};
	eng::playfield::ContiguousPlayfield m_canvas {};
	eng::amiga::PolygonFillService m_fill {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_mask {};
	scene3d::Scene<kObjects + 2u, 8> m_world {};
	scene3d::Scene<10, 8> m_static {};
	scene3d::ModelTable<1, 8, 24, 6, 1> m_models {};
	scene3d::Mesh m_meshes[1] {};
	scene3d::ModelHandle m_model {};
	scene3d::NodeId m_nodes[kObjects] {};
	u8 m_colors[kObjects] {};
	eng::debug::HostHud m_host_hud {};
	u16 m_phase = 0u;
};

SilhouetteDemo g_game {};

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
