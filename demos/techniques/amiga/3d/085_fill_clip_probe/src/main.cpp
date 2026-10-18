// ============================================================================
// Demo 085 - SONDA DE RECORTE 2D del relleno por plano (Blitter)
// ============================================================================
//
// Protocolo de etapas (`PROTOCOLO_ETAPAS_GRAFICOS.md`): caso MINIMO y aislado del
// camino "recorte 2D + relleno por plano" que usan las demos 3D, para depurar el
// comportamiento cuando un poligono queda PARCIALMENTE fuera de la pantalla.
//
// Escena (pocos elementos, colores planos, facil de leer por vision):
//   - Un TRIANGULO solido grande que barre horizontalmente y cruza el borde derecho
//     (dentro -> mitad fuera -> dentro): el caso del bug de `clip_polygon_pass`
//     (se perdia el cruce de la arista de cierre y el poligono visible desaparecia).
//   - Un PENTAGONO fijo medio fuera por abajo-izquierda (parcial constante).
//   - Un CUADRADO pequeno de referencia, siempre dentro.
//   - HUD minimo (nombre+version+frame) en la banda superior, fuera del area de relleno.
//
// Ablaciones:
//   -DCLIP_PROBE_CPU      : rellenar con la referencia CPU (`fill_polygons_by_plane_cpu`)
//                           en vez del Blitter (equivalencia pixel a pixel A/B).
//   -DCLIP_PROBE_NO_CLIP  : no recortar (los poligonos van tal cual al fill del Blitter).
//
// Aviso de construccion: este TU va a -O2 (el vectorizador de gcc a -O3 miscompila
// el render; DT-038); no subir sin comprobacion visual.
// ============================================================================

#define ENG_SCALAR_RETRO16 // host/preview: instancia retro (coord q0)
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/api/api.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/retro/lib2d.hpp>
#include <eng/platform/amiga/backend.hpp>

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

namespace scene = eng::graphics::composition;
namespace playfield = eng::playfield;
namespace retro = eng::retro;

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 4; // 4 planos estandar: la sonda rellena los planos 0-1
constexpr eng::u16 kRowBytes = kWidth / 8u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kRowBytes) * kHeight;

constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);
static_assert(scene::valid_scene(kRes, scene::ocs_a500), "085: EHB 320x256 en A500");

// Paleta: 0 fondo azul oscuro, 1 rojo, 2 verde, 3 blanco (los planos 0-1); el resto EHB
// replica la mitad de brillo.
constexpr eng::Palette32 kPalette {{
	0x012, 0x911, 0x191, 0xfff, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

constexpr eng::u16 kHudY = 8;      // banda de texto (fuera del area de relleno)
constexpr eng::u16 kFillY0 = 24;   // el relleno/clear empieza debajo del HUD

/// Una cara de la sonda: vertices en pantalla + color (bits de plano 0-1).
struct ProbeFace {
	eng::s16 xs[6];
	eng::s16 ys[6];
	eng::u8 n;
	eng::u8 color;
};

struct DemoGame {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			scene::chip_bytes_for(kRes),
			8u * 1024u,
			4u * 1024u,
		});
		m_scene_ok = m_memory_ok &&
			     scene::compose(m_scene, backend.memory_manager(), kRes, scene::ocs_a500,
					    scene::display(scene::kPal320x256, scene::kBplcon0_Ehb),
					    scene::palette(kPalette, 0u, 32u));
		if (!m_memory_ok || !m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008501u);
			return;
		}
		// Toma el display con el copperlist de la escena (patrón del 077): sin esto la
		// sonda rellena sus planos pero la pantalla sigue siendo la del SO.
		backend.install_raster(m_scene);
		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, 0u);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		const eng::u32 f = context.frame.frame_index;
		if (!m_ready_done) {
			m_ready_done = true;
		}
		// --- Geometria de la sonda (determinista por frame) --------------------------------
		ProbeFace faces[3];
		// 1) Triangulo que barre: de x=100 (dentro) a x=280 (300 px de barrido; el lado
		//    derecho del triangulo llega a x+80, asi que cruza el borde 319 de sobra).
#if defined(CLIP_PROBE_FREEZE_X)
		// Estado CONGELADO (A/B determinista CPU vs Blitter: freeze-diff pixel a pixel).
		const eng::s32 tri_x = CLIP_PROBE_FREEZE_X;
#else
		const eng::s32 tri_x = 100 + static_cast<eng::s32>((f * 2u) % 300u);
#endif
		faces[0] = ProbeFace {{static_cast<eng::s16>(tri_x), static_cast<eng::s16>(tri_x + 80),
				       static_cast<eng::s16>(tri_x + 80)},
			      {90, 90, 150}, 3u, 1u};
		// 2) Pentagono fijo medio fuera por abajo-izquierda.
		faces[1] = ProbeFace {{-20, 30, 60, 40, -20}, {200, 180, 220, 260, 260}, 5u, 2u};
		// 3) Cuadrado de referencia, siempre dentro.
		faces[2] = ProbeFace {{20, 60, 60, 20}, {40, 40, 80, 80}, 4u, 3u};
		// --- Recorte + submit ---------------------------------------------------------------		// CONVENCION CRITICA (lib2d, semiabierto): un punto con `x == maxX` esta FUERA, asi
		// que los vertices de CORTE caen exactamente en `maxX`. Si se pasa `width`/`height`,
		// el corte queda fuera del buffer (x=320/y=256) y el contorno del Blitter pinta en la
		// palabra vecina (corrupcion). El rect de recorte usa la ULTIMA columna/fila
		// INCLUIDA (width-1, height-1).
		const retro::Rect clip =
			retro::rect(0, static_cast<eng::s16>(kFillY0),
				    static_cast<eng::s16>(m_scene.width() - 1u),
				    static_cast<eng::s16>(m_scene.height() - 1u));
		eng::graphics::PlaneFillBuilder<3> fb;
		eng::retro::Vec2 in[8];
		eng::retro::Vec2 tmp[8];
		eng::u8 n_debug[3] = {0u, 0u, 0u};
		for (eng::u8 i = 0u; i < 3u; ++i) {
			eng::u8 n = faces[i].n;
			for (eng::u8 k = 0u; k < n; ++k) {
				in[k] = retro::v2(faces[i].xs[k], faces[i].ys[k]);
			}
#if !defined(CLIP_PROBE_NO_CLIP)
			n = static_cast<eng::u8>(retro::clip_polygon(
				clip, in, tmp, n,
				static_cast<eng::u8>(retro::PF_LEFT | retro::PF_TOP | retro::PF_RIGHT |
						     retro::PF_BOTTOM)));
#endif
			if (n < 3u) {
				n_debug[i] = n; // 0/1/2 = descartada y por que (validacion n<3)
				continue;
			}
			n_debug[i] = n;
			for (eng::u8 k = 0u; k < n; ++k) {
				// Coordenadas LOCALES a la caja de relleno (el dest empieza en kFillY0):
				// el fill barre [0, height) y las caras usan su sistema local (patrón de
				// la 080). Sin este desplazamiento el ala del polígono cae fuera de rango.
				m_fx[i][k] = in[k].x().v;
				m_fy[i][k] = static_cast<eng::s16>(in[k].y().v - kFillY0);
			}
			(void)fb.submit(m_fx[i], m_fy[i], n, faces[i].color);
		}
		// --- Relleno (clear + fill por plano) ----------------------------------------------
		// Strides REALES del perfil de escena (pueden llevar padding): nunca calcularlos a
		// mano a partir de width/planes.
		const eng::u16 rb = m_scene.row_bytes();
		const eng::u32 pb = m_scene.plane_bytes();
		const eng::PlaneBytes buf = m_scene.buffer(0u);
		const eng::u16 h = static_cast<eng::u16>(m_scene.height() - kFillY0);
		const eng::u32 dst_off = static_cast<eng::u32>(kFillY0) * rb;
#if defined(CLIP_PROBE_CPU)
		// Referencia CPU: clear + relleno de los planos 0-1 con el mismo builder.
		(void)backend.blitter_clear(buf.subspan(dst_off), m_scene.planes(), rb, pb,
					    m_scene.width(), h, true);
		(void)fb.fill_cpu(buf.subspan(dst_off), rb, pb, 2u, m_scene.width(), h);
#else
		if (fb.count() != 0u) {
			// El relleno cubre los planos 0-1 (colores 1..3); los planos 2-3 quedan del
			// clear. (El fill SOLO escribe los planos indicados.)
			(void)backend.fill_polygons_by_plane(fb.faces().data(), fb.count(),
							     buf.subspan(dst_off), rb, pb, 2u,
							     m_scene.width(), h);
		} else {
			(void)backend.blitter_clear(buf.subspan(dst_off), m_scene.planes(), rb, pb,
						    m_scene.width(), h, false);
		}
#endif
		// Traza al run-status (legible en run-report.json): n tras el recorte de cada cara
		// (bytes 0-2) + el nº de caras aceptadas (byte alto): distingue "recorte vacio" de
		// "fill roto" sin emulador vivo.
		g_eng_run_status.detail =
			(static_cast<eng::u32>(n_debug[0]) |
			 (static_cast<eng::u32>(n_debug[1]) << 8u) |
			 (static_cast<eng::u32>(n_debug[2]) << 16u) |
			 (static_cast<eng::u32>(fb.count()) << 24u));
		eng::debug::probe_when_ready(g_eng_run_status, f);
	}

private:
	scene::Scene m_scene {};
	bool m_memory_ok = false;
	bool m_scene_ok = false;
	bool m_ready_done = false;
	eng::s16 m_fx[3][8] {};
	eng::s16 m_fy[3][8] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffffu);
	return 0;
}
