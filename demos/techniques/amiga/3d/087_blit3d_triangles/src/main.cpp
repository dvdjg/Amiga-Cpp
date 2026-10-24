// ============================================================================
// Demo 087 — Triángulos sólidos por Blitter (pila de tests 3D, paso 1).
// ============================================================================
//
// Primer eslabón de la pila de tests de la librería 3D acelerada por Blitter
// (`eng::graphics::render3d`, roadmap `ROADMAP_3D_BLITTER.md`): varios triángulos
// sólidos se mueven por la pantalla, rotan en 2D, se solapan (painter por
// profundidad) y entran/salen por las cuatro esquinas. Es el caso mínimo del
// pipeline: cámara -> clip de cámara (near + laterales) -> proyección acotada ->
// culling por área -> `Surface::fill_polygon` con `PolygonFillSink` (Blitter:
// máscara 1 bpp + contorno ONEDOT + area fill inclusivo + cookie-cut por plano).
//
// Franja inferior de depuración dentro del mismo bitmap (norma del repo): el área
// de juego son las filas 0..231 y la franja 1 bpp "lógica" las 232..255 (se pinta
// con `Surface::draw_text_words` por palabras; el contador con `DiffText`).
//
// Regla de la demo: lógica por la fachada (`eng::graphics::render3d`,
// `playfield::Surface`), nada de registros; el Blitter solo aparece por el hook
// del playfield. Medidas y alcance en `VALIDATION.md`.

#include <eng/api/api.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/diff_text.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/polygon_fill.hpp>

#include <eng/platform/amiga/entry.hpp>

namespace {

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;
namespace math3d = eng::math3d;

// --- Display: 320x256, 4 planos + franja inferior en el mismo bitmap ----------
constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u8 kPlanes = 4u;
constexpr u16 kRowBytes = kWidth / 8u; // 40
constexpr u32 kPlaneBytes = static_cast<u32>(kRowBytes) * kHeight;
constexpr u16 kWorldH = 232u; // filas de juego (0..231)
constexpr u16 kBandY = kWorldH; // franja de depuracion: 232..255
constexpr u16 kBandH = kHeight - kWorldH;
constexpr u16 kBandBytes = kRowBytes * kBandH;

// --- Paleta: fondo negro, triángulos 1..8, HUD 15 --------------------------
constexpr eng::Palette32 kPalette {{
	0x000, 0xf43, 0x4e4, 0x48f, 0xfd3, 0xf6c, 0x3fd, 0xffd, 0x9ef,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0xddd,
}};

constexpr eng::debug::Version kVersion {1u, 0u}; // el build lo inyecta build-demo.sh

enum : eng::u32 {
	kBootMemory = 0x00008701u,
	kBootCompose = 0x00008702u,
	kBootMask = 0x00008703u,
};

// --- Escena de triángulos ----------------------------------------------------
constexpr u8 kTriCount = 7u;
constexpr u8 kTriVerts = 3u;
constexpr u8 kMaxFaceVerts = 8u; // contrato de render3d
constexpr eng::coord cam_i(int v) { return eng::math::scalar_traits<eng::coord>::from_int(v); }
constexpr u8 kClipScratch = 16u;

struct Triangle {
	s16 cx = 0;    // centro en coordenadas de PANTALLA (se convierte a camara)
	s16 cy = 0;
	s16 vx = 0;    // velocidad en px/frame (pantalla)
	s16 vy = 0;
	s16 radius = 24;
	s16 z = 0;     // profundidad de camara (crudo 4.12): painter, mayor = lejos
	u16 angle = 0; // q12 (vueltas)
	u16 spin = 0;  // giro en vueltas/frame
	u8 color = 1;
};

/// Triángulos deterministas: centros y velocidades variadas; z fijo por triángulo
/// para que el painter por profundidad sea visible en los solapes.
constexpr Triangle kTrisIniciales[kTriCount] = {
	{40, 40, 3, 2, 30, 900, 0, 5, 1},
	{150, 60, 2, 3, 34, 1200, 700, 3, 2},
	{260, 30, -3, 2, 26, 700, 1400, 4, 3},
	{60, 170, 2, -2, 30, 1100, 2100, 6, 4},
	{190, 120, -2, 3, 36, 800, 2800, 5, 5},
	{280, 190, -3, -2, 28, 1300, 3500, 7, 6},
	{120, 200, 3, 2, 32, 1000, 4200, 4, 7},
};

/// Vértice de pantalla -> espacio de cámara a profundidad `z` (crudo 4.12).
/// `sx = cx + (x*focal)/z`  =>  `x = (sx-cx)*z/focal` (sin `__mulsi3`: 16x16).
int screen_to_cam_x(s16 sx, s16 z, const eng::graphics::Camera3D<eng::coord>& cam) {
	using T = eng::math::scalar_traits<eng::coord>;
	return T::to_int(T::mul_div(T::from_int(sx - cam.cx), T::from_int(z), cam.focal));
}
int screen_to_cam_y(s16 sy, s16 z, const eng::graphics::Camera3D<eng::coord>& cam) {
	using T = eng::math::scalar_traits<eng::coord>;
	return T::to_int(T::mul_div(T::from_int(sy - cam.cy), T::from_int(z), cam.focal));
}

static bool null_fill(void*, eng::u8*, eng::u8, eng::u32, eng::u32, eng::u16, eng::u16,
		      eng::u16, const eng::s16*, const eng::s16*, eng::u8, eng::u8) {
	return true;
}

struct Blit3dTriDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		if (!backend.configure_memory({96u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kBootMemory);
			return;
		}
		scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
		res.buffers = 1u; // fondo se limpia cada frame por Blitter
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::display(res), scene::palette(kPalette, 0u, 16u))) {
			eng::debug::mark_failed(g_eng_run_status,
						kBootCompose + static_cast<eng::u32>(m_scene.config_error().code));
			return;
		}
		m_scene.takeover(backend);

		// Máscara 1 bpp del relleno por cara (Chip; el Blitter la lee y escribe).
		m_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<u32>(kRowBytes) * kHeight + 16u, 16);
		if (!m_mask.valid()) {
			eng::debug::mark_failed(g_eng_run_status, kBootMask);
			return;
		}
		// Playfield de trabajo sobre el buffer de la escena + sink de Blitter.
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		(void)m_canvas.bind_raw(planes.data(), static_cast<u32>(kPlaneBytes) * kPlanes, kWidth,
					kHeight, kPlanes, kPlaneBytes);
		m_fill.backend = &backend;
		m_fill.mask = m_mask.view;
#if defined(ADV087_NULL_SINK)
		m_canvas.set_polygon_fill_sink(eng::playfield::PolygonFillSink {&m_fill, &null_fill});
#elif !defined(ADV087_NO_SINK)
		m_canvas.set_polygon_fill_sink(m_fill.sink());
#endif

		// Limpieza inicial (todo el bitmap, incluida la franja).
		(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kHeight);
		draw_hud_static();

		copy_tris();
		m_ready = true;
		ENG_PROF_INIT(3u);
		eng::amiga::mark_ready();
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) return;
		ENG_PROF_FRAME();
		ENG_PROF_BEGIN(0u); // move + cull + clip (CPU)
		const u8 n = kTriCount;
		for (u8 i = 0u; i < n; ++i) {
			Triangle& t = m_tris[i];
			t.cx = static_cast<s16>(t.cx + t.vx);
			t.cy = static_cast<s16>(t.cy + t.vy);
			// Rebote fuera del área de juego: las trayectorias cruzan las esquinas.
			if (t.cx < -70) {
				t.cx = -70;
				t.vx = static_cast<s16>(-t.vx);
			}
			if (t.cx > static_cast<s16>(kWidth) + 70) {
				t.cx = static_cast<s16>(kWidth) + 70;
				t.vx = static_cast<s16>(-t.vx);
			}
			if (t.cy < -70) {
				t.cy = -70;
				t.vy = static_cast<s16>(-t.vy);
			}
			if (t.cy > static_cast<s16>(kWorldH) + 70) {
				t.cy = static_cast<s16>(kWorldH) + 70;
				t.vy = static_cast<s16>(-t.vy);
			}
			t.angle = static_cast<u16>(t.angle + t.spin);
		}
		ENG_PROF_END(0u);

		ENG_PROF_BEGIN(1u); // clear
		const eng::PlaneBytes planes = m_scene.buffer(0u);
#if !defined(ADV087_NO_CLEAR)
		(void)backend.blitter_clear(planes, kPlanes, kRowBytes, kPlaneBytes, kWidth, kWorldH);
#endif
		ENG_PROF_END(1u);
		ENG_PROF_BEGIN(3u); // caras
#if !defined(ADV087_NO_FACES)
		u8 nf = n;
#if defined(ADV087_ONE_FACE)
		nf = 1u;
#endif
		for (u8 k = 0u; k < nf; ++k) {
			(void)draw_tri(m_paint_order[k]);
		}
#endif
		ENG_PROF_END(3u);

		ENG_PROF_BEGIN(2u); // HUD
		draw_hud_dynamic(context.frame.frame_index);
		ENG_PROF_END(2u);
		ENG_PROF_END(0u);

		m_scene.commit();
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext&) {}

private:
	void copy_tris() {
		for (u8 i = 0u; i < kTriCount; ++i) {
			m_tris[i] = kTrisIniciales[i];
		}
		// Orden de pintado fijo (lejos -> cerca): painter por profundidad.
		u8 order[kTriCount] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
		for (u8 i = 0u; i < kTriCount; ++i) {
			for (u8 j = static_cast<u8>(i + 1u); j < kTriCount; ++j) {
				if (m_tris[order[j]].z > m_tris[order[i]].z) {
					const u8 tmp = order[i];
					order[i] = order[j];
					order[j] = tmp;
				}
			}
		}
		for (u8 i = 0u; i < kTriCount; ++i) m_paint_order[i] = order[i];
	}

	/// Rellena la cara del triángulo `idx` por el pipeline de `render3d`.
	u32 draw_tri(u8 idx) {
		const Triangle& t = m_tris[idx];
		// Tres vértices de un triángulo equilátero en pantalla (rotado): el offset va con
		// sin/cos q12 y producto 16×16 (`mul_wide`, sin `__mulsi3`).
		static constexpr s16 kAng[3] = {0, 1365, 2731}; // 0, 120, 240 grados (vueltas)
		math3d::Vec3t<eng::coord> verts[3] {};
		for (u8 k = 0u; k < 3u; ++k) {
			const u16 a = static_cast<u16>(t.angle + kAng[k]);
			const s16 sr = eng::retro::sin(eng::retro::turns(a)).v;
			const s16 cr = eng::retro::cos(eng::retro::turns(a)).v;
			const s16 ox =
				static_cast<s16>(static_cast<s32>(eng::math::mul_wide(sr, t.radius)) >> 12);
			const s16 oy =
				static_cast<s16>(static_cast<s32>(eng::math::mul_wide(cr, t.radius)) >> 12);
			const s16 sx = static_cast<s16>(t.cx + ox);
			const s16 sy = static_cast<s16>(t.cy + oy);
			verts[k] = math3d::vec3<eng::coord>(screen_to_cam_x(sx, t.z, m_cam),
							    screen_to_cam_y(sy, t.z, m_cam), t.z);
		}
		// Winding CCW desde fuera: con la tabla de angulos 0/120/240 el orden natural
		// proyecta area negativa en pantalla (y hacia abajo) y el renderer lo acepta.
		const u16 ids[3] = {0u, 1u, 2u};
		const math3d::FaceSpan span {0u, 3u};
		const math3d::PolyMeshViewT<eng::coord> mesh {
			eng::Span<const math3d::Vec3t<eng::coord>>(verts, 3u),
			eng::Span<const u16>(ids, 3u),
			eng::Span<const math3d::FaceSpan>(&span, 1u)};
		math3d::Vec3t<eng::coord> world[3] {};
		eng::graphics::CamVertex<eng::coord> scratch[kClipScratch] {};
		s16 xs[kClipScratch] {};
		s16 ys[kClipScratch] {};
		const eng::math::Affine<3, eng::coord, eng::coord> ident =
			eng::math::Affine<3, eng::coord, eng::coord>::identity();
		playfield::Surface surf = m_surface();
#if defined(ADV087_NO_FILL)
		(void)mesh;
		(void)ident;
		(void)surf;
		(void)world;
		(void)scratch;
		(void)xs;
		(void)ys;
		return 0u;
#else
		ENG_PROF_BEGIN(4u);
		const u32 drawn = eng::graphics::render_solid(mesh, ident, m_cam, surf,
						   [&t](u32) { return t.color; }, world, scratch,
						   xs, ys);
		ENG_PROF_END(4u);
		return drawn;
#endif
	}

	/// Superficie de trabajo (área de juego) sobre el canvas de la escena.
	playfield::Surface m_surface() {
		return playfield::Surface {m_canvas, playfield::SurfaceRect {0, 0, kWidth, kWorldH}};
	}

	void draw_hud_static() {
		const eng::PlaneBytes planes = m_scene.buffer(0u);
		eng::PlaneBytes band {const_cast<u8*>(planes.data()) + static_cast<u32>(kBandY) * kRowBytes, static_cast<eng::usize>(kPlaneBytes) * kPlanes};
		(void)m_fill.backend->blitter_clear(band, kPlanes, kRowBytes, kPlaneBytes, kWidth, kBandH);
		playfield::Surface hud {m_canvas, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY), "087 BLIT3D TRIANGLES", 15u);
		char label[40] {};
		(void)eng::debug::version_label(kVersion, label, sizeof(label));
		(void)hud.draw_text_words(16, static_cast<s16>(kBandY + 10), label, 15u);
	}

	void draw_hud_dynamic(u32 frame) {
		playfield::Surface hud {m_canvas, playfield::SurfaceRect {0, kBandY, kWidth, kBandH}};
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
		(void)m_frame_diff.paint(hud, 192, static_cast<s16>(kBandY + 10), f, 15u);

	}

	// --- Estado ---
	bool m_ready = false;
	scene::Scene m_scene {};
	playfield::ContiguousPlayfield m_canvas {};
	eng::amiga::PolygonFillService m_fill {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_mask {};
	Triangle m_tris[kTriCount] {};
	u8 m_paint_order[kTriCount] {};
	eng::graphics::Camera3D<eng::coord> m_cam {cam_i(4096), 160, 116, cam_i(64), cam_i(160), cam_i(116)};
	playfield::DiffText<16> m_frame_diff {};

};

Blit3dTriDemo g_game {};

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
