// ============================================================================
// Demo 088 — Relleno 3D compuesto por bitplane (pila 3D, paso A: colores)
// ============================================================================
//
// Paso A de la pila de tests de la vía **por plano** (la diseñada para juegos tipo
// Superhot/Castle Master): valida el ejemplo de color multi-plano antes de añadir
// rotación, clipping o mallas.
//
// Escena mínima: un cubo isométrico estático con TRES caras visibles y CUATRO colores
// sobre DOS bitplanes:
//
//   índice 0 = negro (fondo)   bitplanes 00
//   índice 1 = azul            bitplane 0
//   índice 2 = rojo            bitplane 1
//   índice 3 = magenta         bitplanes 0 y 1
//
// El relleno compuesto por plano (`graphics::fill_polygons_by_plane`, el camino
// validado por 203/077) hace DOS pasadas (una por plano): en la del plano 0 entran las
// caras azul y magenta; en la del plano 1, las rojo y magenta. La magenta se rellena
// en AMBAS, que es justo lo que produce su color compuesto. Las aristas compartidas
// por caras con bits distintos se dibujan una vez en cada plano; las compartidas por
// caras del mismo bit se cancelarían (no hay en esta escena, y es el comportamiento
// buscado para caras del mismo color).
//
// Regla de la escena (contrato del relleno por paridad): dentro de cada pasada los
// polígonos de ese plano NO se solapan; aquí las tres caras del cubo son disjuntas.
//
// HUD: franja inferior (filas 232..255) en el mismo bitmap de 2 planos, texto por
// palabras con `DiffText` para el contador de frames.

#include <eng/api/api.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/diff_text.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/retro/fixed_mesh.hpp>
#include <eng/core/math/arith.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/retro/fixed_trig.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/gfx3d.hpp>

#include <eng/platform/amiga/entry.hpp>

namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::u16;
using eng::s32;
using eng::u32;
using eng::u8;

// --- Display: 320x256, DOS planos + franja inferior en el mismo bitmap ---------
constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u8 kPlanes = 2u;
constexpr u16 kRowBytes = kWidth / 8u; // 40
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kWorldH = 232u;
constexpr u16 kBandY = kWorldH;
constexpr u16 kBandH = kHeight - kWorldH;

// Colores del ejemplo: negro 0, azul 1, rojo 2, magenta 3.
constexpr eng::Palette32 kPalette {{
	0x000, 0x00f, 0xf00, 0xf0f,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
}};

constexpr eng::debug::Version kVersion {1u, 0u}; // build inyectado por build-demo.sh



enum : eng::u32 {
	kBootMemory = 0x00008801u,
	kBootCompose = 0x00008802u,
	kBootMask = 0x00008803u,
};

/// Cubo 3D de medio lado `kHalf` en espacio de camara (+x derecha, +y abajo, +z
/// delante), centrado en z = kZ0. Caras CCW vistas desde fuera con un color por cara
/// (1..3) sobre 2 planos: las caras visibles de un convexo son disjuntas en pantalla,
/// que es el contrato del relleno por plano.
constexpr s16 kHalf = 150;
constexpr s16 kZ0 = 900;
constexpr s16 kFocal = 340;
using CamVec = eng::math3d::Vec3t<eng::coord>;

constexpr CamVec kCube[8] = {
	{-kHalf, -kHalf, -kHalf}, {kHalf, -kHalf, -kHalf}, {kHalf, kHalf, -kHalf}, {-kHalf, kHalf, -kHalf},
	{-kHalf, -kHalf, kHalf},  {kHalf, -kHalf, kHalf},  {kHalf, kHalf, kHalf},  {-kHalf, kHalf, kHalf},
};
// near(0,1,2,3) far(5,4,7,6) right(1,5,6,2) left(4,0,3,7) top(4,5,1,0) bottom(3,2,6,7)
// Caras con winding CCW visto desde FUERA (normal = cross(e1,e2) hacia afuera).
constexpr u8 kCubeFaces[6][4] = {
	{0, 3, 2, 1}, {4, 5, 6, 7}, {1, 2, 6, 5}, {0, 4, 7, 3}, {0, 1, 5, 4}, {3, 7, 6, 2},
};
constexpr u8 kCubeColors[6] = {1u, 2u, 3u, 1u, 2u, 3u};

struct Blit3dPlanesDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = 2u; // doble buffer: sin tearing (la captura cogia el fill a medias)
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::display(res), scene::palette(kPalette, 0u, 4u))) {
			eng::debug::mark_failed(g_eng_run_status,
						kBootCompose + static_cast<eng::u32>(m_scene.config_error().code));
			return;
		}
		// Máscara 1 bit de cookie-cut (Chip: la lee el Blitter; una por buffer de plano).
		m_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(kPlaneBytes);
		if (!m_mask.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootMask);
			return;
		}
		m_scene.takeover(backend);
		for (u8 b = 0u; b < 2u; ++b) {
			const eng::PlaneBytes planes = m_scene.buffer(b);
			(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kHeight);
			(void)m_pf.bind_raw(planes.data(), static_cast<u32>(kPlaneBytes) * kPlanes, kWidth,
					    kHeight, kPlanes, kPlaneBytes);
			draw_hud_static();
		}
		m_ready = true;
		ENG_PROF_INIT(3u);
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) return;
		ENG_PROF_FRAME();
		// **Publicacion diferida al INICIO del update**: el raster muestra siempre el buffer
		// completo anterior; el trasero se pinta despues (evita ver buffers en construccion).
		ENG_PROF_BEGIN(0u);
		m_scene.commit();
		const eng::PlaneBytes planes = m_scene.back();
		(void)m_pf.bind_raw(planes.data(), static_cast<u32>(kPlaneBytes) * kPlanes, kWidth, kHeight,
				    kPlanes, kPlaneBytes);

		// Rotacion 3D (Rx·Ry·Rz) con sin/cos q12 en vueltas.
		eng::math::Affine<3, eng::real, eng::coord> model =
			eng::math::Affine<3, eng::real, eng::coord>::identity();
		const u32 frame = context.frame.frame_index;
		eng::math3d::load_rotate(model.m, eng::retro::turns(static_cast<u16>(frame * 5u + 512u)),
					 eng::retro::turns(static_cast<u16>(frame * 16u)),
					 eng::retro::turns(static_cast<u16>(frame * 3u)));
		CamVec world[8] {};
		eng::math3d::mesh_transform(eng::Span<const CamVec>(kCube, 8u), model,
					    eng::Span<CamVec>(world, 8u));
		// Centro en camara: +z0 y la camara con focal/centro de pantalla.
		using CT = eng::math::scalar_traits<eng::coord>;
		eng::graphics::Camera3D<eng::coord> cam {};
		cam.focal = CT::from_int(kFocal);
		cam.cx = 160;
		cam.cy = 116;
		cam.near_z = CT::from_int(64);
		cam.half_w = CT::from_int(160);
		cam.half_h = CT::from_int(116);
		// Vertices en espacio de camara (z + kZ0) para el test de frustum del objeto.
		// Deriva lateral (kDrift): modo de VALIDACION del clip fino (entra/sale por el
		// borde). Off por defecto: los frames de cruce bajan a ~16-28 fps y rompen el
		// presupuesto; la evidencia de clip esta en VALIDATION.md.
		constexpr bool kDrift = false;
		s32 ox = 0;
		if constexpr (kDrift) {
			++m_phase;
			if (m_phase >= 400u) m_phase = 0u;
			ox = (m_phase < 200u) ? (-600 + static_cast<s32>(m_phase) * 6)
					      : (600 - static_cast<s32>(m_phase - 200u) * 6);
		}
		// Vertices de camara como Vec3t (dominio): el culling por normal de mesh3d sirve
		// para CUALQUIER convexo con winding CCW hacia fuera (icosaedro irregular incluido).
		eng::math3d::Vec3t<eng::coord> wv[8] {};
		for (u8 i = 0u; i < 8u; ++i) {
			wv[i] = eng::math3d::Vec3t<eng::coord> {{world[i].v[0] + CT::from_int(ox),
							    world[i].v[1],
							    world[i].v[2] + CT::from_int(kZ0)}};
		}
		// Fast path: si las 8 esquinas estan dentro del frustum, ninguna cara necesita clip
		// y la proyeccion se hace UNA vez por vertice (8), no por cara (hasta 24).
		bool all_inside = eng::graphics::camera_contains_vertices(cam, wv, 8u);
		s16 px[8] {}, py[8] {};
		if (all_inside) {
			for (u8 i = 0u; i < 8u; ++i) {
				const eng::graphics::CamVertex<eng::coord> cv {wv[i].v[0], wv[i].v[1],
									      wv[i].v[2]};
				all_inside = all_inside && eng::graphics::project_camera_vertex(
								  cv, cam, px[i], py[i]);
			}
		}

		// Vértices por cara: el builder guarda VISTAS, no copia; deben vivir hasta el fill.
		s16 xs[6][16] {};
		s16 ys[6][16] {};
		ENG_PROF_END(0u);
		ENG_PROF_BEGIN(1u);
		eng::graphics::PlaneFillBuilder<6> fb {};
		m_submitted = 0u;
		const eng::math3d::Vec3t<eng::coord> cam_origin {};
		for (u8 fi = 0u; fi < 6u; ++fi) {
			// Culling por NORMAL 3D (camara en el origen): cara trasera fuera ANTES de
			// proyectar/recortar; general para todo convexo con caras CCW desde fuera.
			if (!eng::math3d::face_visible(wv[kCubeFaces[fi][0]], wv[kCubeFaces[fi][1]],
						  wv[kCubeFaces[fi][2]], cam_origin)) {
				continue;
			}
			u8 m = 4u;
			if (all_inside) {
				for (u8 k = 0u; k < 4u; ++k) {
					const u8 v = kCubeFaces[fi][k];
					xs[fi][k] = px[v];
					ys[fi][k] = py[v];
				}
			} else {
				eng::graphics::CamVertex<eng::coord> cv[4] {};
				for (u8 k = 0u; k < 4u; ++k) {
					const auto& v = wv[kCubeFaces[fi][k]];
					cv[k] = eng::graphics::CamVertex<eng::coord> {v.v[0], v.v[1], v.v[2]};
				}
				eng::graphics::CamVertex<eng::coord> cl[16] {};
				m = eng::graphics::clip_camera_polygon(cv, 4u, cam, cl, 16u);
				if (m < 3u) continue;
				bool ok = true;
				for (u8 k = 0u; k < m; ++k) {
					ok = ok && eng::graphics::project_camera_vertex(cl[k], cam,
										       xs[fi][k], ys[fi][k]);
				}
				if (!ok) continue;
			}
			// Back-face culling por winding proyectado: en este sistema (+y abajo) una cara que mira a la camara proyecta area NEGATIVA (HOST-481); area >= 0 es trasera o degenerada.
			if (eng::graphics::screen_area2(xs[fi], ys[fi], m) >= 0) continue;
			if (fb.submit(xs[fi], ys[fi], m, kCubeColors[fi])) ++m_submitted;
		}
		ENG_PROF_END(1u);
		ENG_PROF_BEGIN(2u);
		if (fb.count() != 0u) {
			(void)backend.fill_polygons_by_plane(fb.faces().data(), fb.count(), planes,
							     kRowBytes, kPlaneBytes, kPlanes, kWidth, kWorldH);
		} else {
			(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kWorldH);
		}
		g_eng_run_status.detail = fb.count();
		ENG_PROF_END(2u);
		draw_hud_dynamic(frame);
		eng::debug::probe_when_ready(g_eng_run_status, frame);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	void draw_hud_static() {
		playfield::Surface hud {m_pf, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY), "088 BLIT3D PLANES", 3u);
		char label[40] {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY + 10), label, 3u);
	}

	void draw_hud_dynamic(u32 frame) {
		playfield::Surface hud {m_pf, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		char f[8] {};
		
		u8 q = 1u;
		f[0] = 'F';
		u32 v = frame;
		char tmp[6] {};
		u8 d = 0u;
		do {
			tmp[d++] = static_cast<char>('0' + (v % 10u));
			v /= 10u;
		} while (v != 0u && d < 5u);
		for (u8 k = 0u; k < d; ++k) f[q++] = tmp[d - 1u - k];
		f[q] = '\0';
		(void)m_frame_diff.paint(hud, 192, static_cast<s16>(kBandY + 10), f, 3u);
	}

	scene::Scene m_scene {};
	/// Playfield de la escena, enlazado en init (mismo buffer que pinta el Copper).
	playfield::ContiguousPlayfield m_pf {};
	playfield::DiffText<16> m_frame_diff {};
	eng::Block<eng::MaskTag> m_mask {};
	u32 m_submitted = 0u;
	u32 m_phase = 0u;
	bool m_ready = false;
};

Blit3dPlanesDemo g_game {};

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
