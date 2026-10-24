// ============================================================================
// Demo 093 - Animación rígida legible de un robot por clips (G4)
// ============================================================================
//
// Un torso con cabeza y un brazo de dos segmentos. El torso es estático; el
// hombro y el codo siguen un clip de 24 ticks con dos tracks. La silueta se
// mantiene separada del cuerpo para que el movimiento cambie píxeles en cada
// frame, no solo dentro de una silueta monocolor.
//
// El HUD cuenta **frames visualmente distintos**: cada frame dibujado se resume
// en un hash del área 3D y solo incrementa el contador si la imagen cambia. El
// `F`/`FPS` del overlay es ese contador, no el número de vueltas del bucle.
//
//   clip estático -> Player::update -> Pose por track -> set_rotation(node)
//   -> collect_wire_edges (color por modelo) -> retro::wire_lines
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/093_rigid_anim --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/093_rigid_anim

#include <eng/api/api.hpp>
#include <eng/api/3d.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/polygon_fill.hpp>
#include <eng/retro/mesh_primitives.hpp>
#include <eng/retro/wireframe.hpp>

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
constexpr u16 kRowBytes = kWidth / 8u; // 40
constexpr u8 kPlanes = 4u;
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kBandY = kWorldH;
constexpr u16 kBandH = kHeight - kWorldH;
constexpr u8 kBuffers = 3u;
constexpr u8 kWorldPlanes = 2u; // cuerpo + cadena del brazo
constexpr u8 kHudColor = 15u;

constexpr s16 kFocal = 340;
constexpr s16 kCx = 160;
constexpr s16 kCy = 118;
constexpr s16 kNearZ = 64;
constexpr s16 kHalfW = 160;
constexpr s16 kHalfH = 104;

constexpr eng::Palette32 kPalette {{
	0x000, 0xfff, 0x0df, 0x7ef, 0xff0, 0x6f6, 0xf9c, 0xf24,
	0x333, 0x888, 0xccc, 0x05f, 0x444, 0x0a3, 0xa07, 0xfff,
}};

constexpr eng::debug::Version kVersion {1u, 0u};

/// Códigos de fallo de arranque (el runner los resuelve por `.map`).
enum : u32 {
	kBootMemory = 0x00009301u,
	kBootCompose = 0x00009302u,
	kBootModels = 0x00009303u,
	kBootCamera = 0x00009304u,
	kBootScene = 0x00009305u,
};

using Camera = eng::scene3d::Camera;
using CameraT = eng::math::scalar_traits<eng::coord>;
using CoordT = eng::math::scalar_traits<eng::coord>;
using BodyMesh = retro::MeshData<eng::coord, 64, 192, 48>;
using LimbMesh = retro::MeshData<eng::coord, 8, 24, 6>;

constexpr Camera kCamera {
	CameraT::from_int(kFocal), kCx, kCy, CameraT::from_int(kNearZ),
	CameraT::from_int(kHalfW), CameraT::from_int(kHalfH)};

constexpr math3d::Vec3t<eng::coord> kBodyBase = math3d::vec3<eng::coord>(0, 0, 320);
constexpr u16 kTrackCount = 2u;

/// Turns de compilación portables: en host son `float`; en 68000, Q4.12 cuantizado.
constexpr eng::real turn_value(double v) noexcept {
	return eng::math::scalar_const<eng::real>::from(v);
}

constexpr math3d::Vec3t<eng::real> turn_vec(double x, double y, double z) noexcept {
	return math3d::Vec3t<eng::real> {turn_value(x), turn_value(y), turn_value(z)};
}

// Dos tracks: hombro y codo. El clip dura 24 ticks; a ~20 fps el brazo recorre
// ~13° por frame, suficiente para que cada frame cambie la silueta. El último
// key coincide con el primero para el loop.
const eng::anim3d::Keyframe kKeys[] = {
	{0, math3d::vec3(0, 0, 0), turn_vec(0.0, 0.0, 0.0)},
	{6, math3d::vec3(0, 0, 0), turn_vec(0.0, 0.0, -0.35)},
	{12, math3d::vec3(0, 0, 0), turn_vec(0.0, 0.0, 0.0)},
	{0, math3d::vec3(0, 0, 0), turn_vec(0.0, 0.0, 0.0)},
	{6, math3d::vec3(0, 0, 0), turn_vec(0.0, 0.0, -0.20)},
	{12, math3d::vec3(0, 0, 0), turn_vec(0.0, 0.0, 0.0)},
};
const eng::anim3d::Track kTracks[] = {{0, 0, 3}, {1, 3, 3}};
const eng::anim3d::Event kEvents[] = {{3, 1}, {9, 2}};
const eng::anim3d::Clip kClip {
	eng::Span<const eng::anim3d::Keyframe>(kKeys, 6),
	eng::Span<const eng::anim3d::Track>(kTracks, 2),
	eng::Span<const eng::anim3d::Event>(kEvents, 2), 12, true};

struct RigidAnimDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!backend.configure_memory({256u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		comp::SceneResources res = comp::planar(kWidth, kHeight, kPlanes);
		res.buffers = kBuffers;
		if (!comp::compose(m_display, backend.memory_manager(), res, comp::ocs_a500,
				   comp::display(res), comp::palette(kPalette, 0u, 16u))) {
			eng::debug::mark_failed(
				g_eng_run_status,
				kBootCompose + static_cast<u32>(m_display.config_error().code));
			return;
		}
		m_display.takeover(backend);

		for (u8 b = 0u; b < m_display.buffer_count(); ++b) {
			const eng::PlaneBytes planes = m_display.buffer(b);
			(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(planes.size()), kWidth,
						kHeight, kPlanes, kPlaneBytes);
			(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth,
						    kHeight);
			draw_band();
		}

		if (!build_models()) {
			eng::debug::mark_failed(g_eng_run_status, kBootModels);
			return;
		}
		if (!m_world.set_camera(kCamera)) {
			eng::debug::mark_failed(g_eng_run_status, kBootCamera);
			return;
		}
		if (!build_scene()) {
			eng::debug::mark_failed(g_eng_run_status, kBootScene);
			return;
		}
		m_targets[0] = m_arm;
		m_targets[1] = m_fore;
		// Cuerpo y cámara fijos: su capa se hornea UNA vez en el plano 0 de cada
		// buffer. El brazo vive en el plano 1 y es lo único que se limpia/dibuja
		// por frame.
		if (!bake_static(backend)) {
			eng::debug::mark_failed(g_eng_run_status, kBootScene);
			return;
		}
		m_anim.play(kClip);
		m_ready = true;
		// Completar dos páginas antes de READY.
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
					kHeight, kWorldPlanes, kPlaneBytes);
		// El plano 0 (cuerpo+fondo) está horneado; solo se limpia el plano del brazo.
		const eng::PlaneBytes arm_plane = planes.subspan(kPlaneBytes);
		(void)backend.blitter_clear(arm_plane, 1u, kRowBytes, kPlaneBytes, kWidth,
					    kWorldH, false);

		(void)m_anim.update(1u);
		u16 event_id = 0u;
		while (m_anim.take_event(event_id)) {
			++m_beats;
		}
		for (u16 i = 0u; i < kTrackCount; ++i) {
			eng::anim3d::Pose pose {};
			if (!m_anim.sample(i, pose)) {
				continue;
			}
			(void)m_world.set_rotation(m_targets[i],
						   eng::anim3d::rotation_from_turns(pose.rotation));
		}

		eng::graphics::WireScreenEdge edges[96] {};
		const u32 count = m_world.collect_wire_edges(
			m_models, eng::Span<eng::graphics::WireScreenEdge>(edges, 96u), 2u,
			true, false, true);
		const u32 drawn = eng::retro::wire_lines(
			backend, planes, kRowBytes, kPlaneBytes, kWorldPlanes,
			eng::Span<const eng::graphics::WireScreenEdge>(edges, count), true);

		// Contador visual: hash barato del área 3D (muestreado, sin multiplicar).
		const u32 hash = visual_hash(eng::Span<const eng::graphics::WireScreenEdge>(edges, count));
		if (hash != m_last_hash) {
			m_last_hash = hash;
			++m_visual_frames;
		}
		eng::util::TextLine<48> stats {};
		stats << 'T' << m_anim.tick() << " L" << drawn << " B" << m_beats;
		m_host_hud.draw(backend.debug(), "093 RIGID ANIM", kVersion,
				m_visual_frames, eng::debug::prof_clock(), stats.c_str());
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	bool bake_static(eng::amiga::AmigaBackend& backend) {
		(void)m_world.set_visible(m_arm, false);
		(void)m_world.set_visible(m_fore, false);
		eng::graphics::WireScreenEdge static_edges[160] {};
		u32 count = m_world.collect_wire_edges(
			m_models, eng::Span<eng::graphics::WireScreenEdge>(static_edges, 160u),
			1u, true, false, true);
		static const eng::graphics::WireScreenEdge kGround[] = {
			{64, 184, 256, 184, 1u},
			{80, 194, 240, 194, 1u},
		};
		for (const auto& edge : kGround) {
			if (count < 160u) static_edges[count++] = edge;
		}
		for (u8 b = 0u; b < m_display.buffer_count(); ++b) {
			const eng::PlaneBytes planes = m_display.buffer(b);
			(void)eng::retro::wire_lines(
				backend, planes, kRowBytes, kPlaneBytes, 1u,
				eng::Span<const eng::graphics::WireScreenEdge>(static_edges, count),
				true);
		}
		(void)m_world.set_visible(m_arm, true);
		(void)m_world.set_visible(m_fore, true);
		(void)m_world.set_visible(m_body, false);
		return count > 0u;
	}

	static u32 visual_hash(eng::Span<const eng::graphics::WireScreenEdge> edges) noexcept {
		// Firma de las aristas proyectadas del brazo: si ninguna coordenada cambia,
		// el raster es idéntico. Mucho más barato que hashear el framebuffer.
		u32 h = 0x811c9dc5u;
		for (u32 i = 0u; i < edges.size(); ++i) {
			h = (h << 5u) - h + static_cast<u16>(edges[i].x0);
			h = (h << 5u) - h + static_cast<u16>(edges[i].y0);
			h = (h << 5u) - h + static_cast<u16>(edges[i].x1);
			h = (h << 5u) - h + static_cast<u16>(edges[i].y1);
		}
		return h;
	}

	static void append_box(BodyMesh& mesh, math3d::Vec3t<eng::coord> center,
			       math3d::Vec3t<eng::coord> half) {
		const u16 base = mesh.nv;
		const s32 cx = CoordT::to_int(center.v[0]);
		const s32 cy = CoordT::to_int(center.v[1]);
		const s32 cz = CoordT::to_int(center.v[2]);
		const s32 hx = CoordT::to_int(half.v[0]);
		const s32 hy = CoordT::to_int(half.v[1]);
		const s32 hz = CoordT::to_int(half.v[2]);
		const s32 xs[8] = {cx - hx, cx + hx, cx + hx, cx - hx,
				   cx - hx, cx + hx, cx + hx, cx - hx};
		const s32 ys[8] = {cy - hy, cy - hy, cy + hy, cy + hy,
				   cy - hy, cy - hy, cy + hy, cy + hy};
		const s32 zs[8] = {cz - hz, cz - hz, cz - hz, cz - hz,
				   cz + hz, cz + hz, cz + hz, cz + hz};
		for (u16 i = 0u; i < 8u; ++i) {
			(void)mesh.add_vertex(math3d::vec3<eng::coord>(xs[i], ys[i], zs[i]));
		}
		const u16 faces[6][4] = {
			{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6},
			{0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4},
		};
		for (u16 f = 0u; f < 6u; ++f) {
			const u16 ids[4] = {static_cast<u16>(base + faces[f][0]),
					    static_cast<u16>(base + faces[f][1]),
					    static_cast<u16>(base + faces[f][2]),
					    static_cast<u16>(base + faces[f][3])};
			(void)mesh.add_face(ids, 4u);
		}
	}

	static void offset_y(LimbMesh& mesh, int dy) {
		for (u16 i = 0u; i < mesh.nv; ++i) {
			mesh.verts[i].v[1] =
				CoordT::from_int(CoordT::to_int(mesh.verts[i].v[1]) + dy);
		}
	}

	bool build_models() {
		BodyMesh body;
		LimbMesh arm;
		LimbMesh fore;
		// Cuerpo completo en una sola malla estática: torso, cabeza, piernas,
		// pies y brazo izquierdo. Se hornea una vez; no cuesta por frame.
		append_box(body, math3d::vec3(0, -4, 0), math3d::vec3(12, 26, 10));
		append_box(body, math3d::vec3(0, -38, 0), math3d::vec3(8, 8, 8));
		append_box(body, math3d::vec3(-16, -30, 0), math3d::vec3(4, 12, 4));
		append_box(body, math3d::vec3(-16, -50, 0), math3d::vec3(3, 10, 3));
		append_box(body, math3d::vec3(-7, 28, 0), math3d::vec3(5, 20, 6));
		append_box(body, math3d::vec3(7, 28, 0), math3d::vec3(5, 20, 6));
		append_box(body, math3d::vec3(-7, 52, 0), math3d::vec3(7, 4, 8));
		append_box(body, math3d::vec3(7, 52, 0), math3d::vec3(7, 4, 8));
		(void)retro::fill_box(arm, math3d::vec3(5, 14, 5));
		(void)retro::fill_box(fore, math3d::vec3(4, 12, 4));
		offset_y(arm, -14);
		offset_y(fore, -12);

		m_model_body = m_models.add_mesh(body.view(), 1u);
		m_model_arm = m_models.add_mesh(arm.view(), 2u);
		m_model_fore = m_models.add_mesh(fore.view(), 2u);
		return m_model_body.valid() && m_model_arm.valid() && m_model_fore.valid();
	}

	bool build_scene() {
		m_body = m_world.spawn({.model = m_model_body, .position = kBodyBase});
		m_arm = m_world.spawn({.model = m_model_arm,
				      .position = math3d::vec3(16, -18, 0),
				      .parent = m_body});
		m_fore = m_world.spawn({.model = m_model_fore,
				       .position = math3d::vec3(0, -28, 0),
				       .parent = m_arm});
		return m_body.valid() && m_arm.valid() && m_fore.valid();
	}

	void draw_band() {
		eng::playfield::Surface band {
			m_canvas, eng::playfield::SurfaceRect {0, static_cast<s16>(kBandY), kWidth,
							       static_cast<s16>(kBandH)}};
		(void)band.draw_text_words(16, 8, "093 RIGID ANIM", kHudColor);
	}

	bool m_ready = false;
	comp::Scene m_display {};
	eng::playfield::ContiguousPlayfield m_canvas {};
	scene3d::Scene<4, 64, 128> m_world {};
	scene3d::ModelTable<3, 64, 192, 48, 2> m_models {};
	scene3d::ModelHandle m_model_body {};
	scene3d::ModelHandle m_model_arm {};
	scene3d::ModelHandle m_model_fore {};
	scene3d::NodeId m_body {};
	scene3d::NodeId m_arm {};
	scene3d::NodeId m_fore {};
	scene3d::NodeId m_targets[kTrackCount] {};
	eng::anim3d::Player<kTrackCount, 4> m_anim {};
	eng::debug::HostHud m_host_hud {};
	u32 m_last_hash = 0u;
	u32 m_visual_frames = 0u;
	u16 m_beats = 0u;
};

RigidAnimDemo g_game {};

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
