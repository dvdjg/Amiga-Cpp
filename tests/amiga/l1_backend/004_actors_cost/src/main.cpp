// ============================================================================
// L1-004 — Coste por vértice del pipeline 3D de CPU (actors)
// ============================================================================
//
// Sonda de COSTE (estática y determinista): mide con el contador de ciclos del
// periférico de depuración las tres fases de CPU que la demo 088 agrupa en la
// sección `actors`, sin dibujar nada dependiente de ellas:
//
//   1. `mesh_transform`   (rotación + traslación de los 8 vértices del cubo)
//   2. `camera_contains_vertices` (5 planos del frustum × 8 vértices)
//   3. proyección de los 8 vértices (`project_camera_vertex`)
//
// Cada fase se repite `kReps` veces con rotaciones distintas (deterministas) y se
// publica el coste medio **por vértice y repetición** en `detail`:
//
//   bits  0-9: transform  (ciclos/vértice / 4)
//   bits 10-19: frustum    (ciclos/vértice / 4)
//   bits 20-29: proyección (ciclos/vértice / 4)
//   bits 30-31: reservado
//
// El checksum de las tres fases se escribe en un global `volatile` (y en el HUD)
// para que el compilador no elimine el trabajo medido. `verify.sh` exige READY y
// unos valores no nulos; sirve como **A/B** de cualquier cambio en el pipeline.
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/retro/fixed_mesh.hpp>
#include <eng/retro/fixed_trig.hpp>

#include <eng/platform/amiga/entry.hpp>

namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u8 kPlanes = 1u;
constexpr u16 kRowBytes = kWidth / 8u;
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kBandY = 232u;
constexpr u16 kBandH = kHeight - kBandY;
constexpr u32 kReps = 64u;

constexpr eng::Palette32 kPalette {{0x000, 0xfff, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000}};
constexpr eng::debug::Version kVersion {1u, 0u};

enum : eng::u32 {
	kBootMemory = 0x00009A01u,
	kBootCompose = 0x00009A02u,
};

constexpr s16 kHalf = 150;
constexpr s16 kZ0 = 900;
constexpr s16 kFocal = 340;

using CamVec = eng::math3d::Vec3t<eng::coord>;
using CT = eng::math::scalar_traits<eng::coord>;
using CamV = eng::graphics::CamVertex<eng::coord>;

constexpr CamVec kCube[8] = {
	{-kHalf, -kHalf, -kHalf}, {kHalf, -kHalf, -kHalf}, {kHalf, kHalf, -kHalf}, {-kHalf, kHalf, -kHalf},
	{-kHalf, -kHalf, kHalf},  {kHalf, -kHalf, kHalf},  {kHalf, kHalf, kHalf},  {-kHalf, kHalf, kHalf},
};

// Checksum global (volatile): impide que el compilador elimine el trabajo medido.
extern "C" {
__attribute__((used)) volatile eng::u32 g_actors_checksum = 0;
}

struct ActorsCostProbe {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!backend.configure_memory({48u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = 1u;
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::display(res), scene::palette(kPalette, 0u, 1u))) {
			eng::debug::mark_failed(g_eng_run_status,
						kBootCompose + static_cast<eng::u32>(m_scene.config_error().code));
			return;
		}
		m_scene.takeover(backend);
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kHeight);

		eng::graphics::Camera3D<eng::coord> cam {};
		cam.focal = CT::from_int(kFocal);
		cam.cx = 160;
		cam.cy = 116;
		cam.near_z = CT::from_int(64);
		cam.half_w = CT::from_int(160);
		cam.half_h = CT::from_int(116);

		CamVec world[8] {};
		CamV wv[8] {};
		s16 sx[8] {};
		s16 sy[8] {};
		u32 sum = 0u;

		// 1) Transformación (rotación + traslación), 8 vértices por repetición.
		u32 t0 = eng::debug::DebugPeripheral::cycle_counter();
		for (u32 r = 0u; r < kReps; ++r) {
			eng::math::Affine<3, eng::real, eng::coord> model =
				eng::math::Affine<3, eng::real, eng::coord>::identity();
			eng::math3d::load_rotate(
				model.m,
				eng::retro::turns(static_cast<u16>(r * 16u + 512u)),
				eng::retro::turns(static_cast<u16>(r * 32u)),
				eng::retro::turns(static_cast<u16>(r * 5u)));
			eng::math3d::mesh_transform(eng::Span<const CamVec>(kCube, 8u), model,
						    eng::Span<CamVec>(world, 8u));
			sum += static_cast<u32>(CT::to_int(world[r & 7u].v[0])) +
			       static_cast<u32>(CT::to_int(world[r & 7u].v[2]));
		}
		u32 t1 = eng::debug::DebugPeripheral::cycle_counter();
		const u32 transform_cpv = (t1 - t0) / (kReps * 8u);

		// 2) Test de frustum por objeto (5 planos × 8 vértices), con los vértices en
		//    espacio de cámara como en la demo.
		for (u8 i = 0u; i < 8u; ++i) {
			wv[i] = CamV {world[i].v[0], world[i].v[1], world[i].v[2] + CT::from_int(kZ0)};
		}
		t0 = eng::debug::DebugPeripheral::cycle_counter();
		for (u32 r = 0u; r < kReps; ++r) {
			CamV v[8] {};
			for (u8 i = 0u; i < 8u; ++i) {
				v[i] = wv[i];
				v[i].x = static_cast<eng::coord>(v[i].x + CT::from_int(static_cast<s16>(r & 7u)));
			}
			if (eng::graphics::camera_contains_vertices(cam, v, 8u)) {
				sum += 1u;
			}
		}
		t1 = eng::debug::DebugPeripheral::cycle_counter();
		const u32 frustum_cpv = (t1 - t0) / (kReps * 8u);

		// 3) Proyección de los 8 vértices.
		t0 = eng::debug::DebugPeripheral::cycle_counter();
		for (u32 r = 0u; r < kReps; ++r) {
			for (u8 i = 0u; i < 8u; ++i) {
				CamV v = wv[i];
				v.x = static_cast<eng::coord>(v.x + CT::from_int(static_cast<s16>(r & 15u)));
				if (eng::graphics::project_camera_vertex(v, cam, sx[i], sy[i])) {
					sum += static_cast<u32>(sx[i]) + static_cast<u32>(sy[i]);
				}
			}
		}
		t1 = eng::debug::DebugPeripheral::cycle_counter();
		const u32 project_cpv = (t1 - t0) / (kReps * 8u);

		g_actors_checksum = sum;
		// Ciclos/vértice / 4 en 10 bits por fase; si alguna fase pasara de 4095 el valor
		// satura (indica un cambio enorme: revisar antes de fiarse).
		const auto pack = [](u32 cpv) -> u32 {
			const u32 q = cpv >> 2u;
			return q > 1023u ? 1023u : q;
		};
		const u32 detail = pack(transform_cpv) | (pack(frustum_cpv) << 10u) |
				   (pack(project_cpv) << 20u);

		playfield::ContiguousPlayfield pf {};
		(void)pf.bind_raw(planes.data(), static_cast<u32>(kPlaneBytes) * kPlanes, kWidth,
				  kHeight, kPlanes, kPlaneBytes);
		playfield::Surface hud {pf, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY), "090 ACTORS COST", 1u);
		char label[40] {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY + 10), label, 1u);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, detail);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}
	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	scene::Scene m_scene {};
	bool m_ready = false;
};

ActorsCostProbe g_game {};

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
