// Demo 209: Beginning Fields a 3 planos (8 colores) con cámara de splines y
// fuego/flores animados por paleta. Es la variante de bajo coste de la 120: el
// mundo está pre-dibujado en un bitmap flat y el scroll SOLO mueve punteros
// (BPLxPT/BPLCON1); no hay blits por frame.
//
// Assets: `out/assets/tiled/beginning-fields-3bpp/` (png2amiga con
// `--mode lores --depth 3 --dither floyd-steinberg` sobre el composed.png de
// Beginning Fields; mapa planar + paleta de 8 colores). Las celdas vivas de
// fuego/flores usan los índices reservados 6/7; el render los cicla por Copper.
//
//   bash tools/build/build-demo.sh demos/techniques/amiga/playfield/209_tiled_beginning_fields_3bpp --release
//   bash tools/run/run-demo.sh demos/techniques/amiga/playfield/209_tiled_beginning_fields_3bpp

#include <eng/api/api.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/flat_playfield.hpp>
#include <eng/field/xlimited.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/scene/arc_path.hpp>

#include "../../../../../../out/assets/tiled/beginning-fields-3bpp/world_3bpp.h"

// Mundo planar interleaved `[fila][plano][80 B]` (640x640x3). Se carga en Chip
// y se copia al bitmap del playfield en `init`; después no se vuelve a tocar.
__asm__(".section world3.MEMF_CHIP, \"aw\"\n"
	".globl g_world3\ng_world3:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields-3bpp/world_3bpp.bin\"\n"
	".globl g_world3_size\ng_world3_size:\n"
	".long . - g_world3");
extern "C" const unsigned char g_world3[];
extern "C" const unsigned int g_world3_size;

namespace {

namespace playfield = eng::playfield;

constexpr eng::u16 kWorldW = 640u;
constexpr eng::u16 kWorldH = 640u;
constexpr eng::u16 kViewW = 320u;
constexpr eng::u16 kViewH = 256u;
constexpr eng::u8 kPlanes = 3u;
constexpr eng::u16 kFetchBytes = 42u;
constexpr eng::debug::Version kVersion {1u, 0u};
constexpr eng::u32 kReadyDetail = 0x20900000u;
#if defined(DEMO_TURBO)
constexpr eng::s32 kScrollPxPerFrame = 16; // modo turbo explícito (build)
#else
constexpr eng::s32 kScrollPxPerFrame = 6;  // visita normal: 6 px/frame constantes
constexpr eng::u16 kStopEveryFrames = 110u; // ~2,2 s de recorrido entre paradas
constexpr eng::u16 kStopFrames = 80u;       // ~1,6 s de parada para ver flores
#endif

// Paleta viva: 0..5 del mapa; 6 = flores, 7 = fuego. Solo hay 8 colores, así
// que los índices reservados se eligen **dentro de la gama del mapa**: crema
// tostada para las flores y ocre/marrón para la llama. Evitamos amarillos o
// rosas saturados que chirríen con el resto de la escena.
constexpr eng::u16 kFlowerCycle[4] = {0x0fea, 0x0eda, 0x0fda, 0x0eca};
constexpr eng::u16 kFireCycle[4] = {0x0f00, 0x0f40, 0x0f80, 0x0fc0}; // rojo -> naranja -> amarillo



/// Catmull-Rom en **16.8**: `p*` y el retorno son Q8 (px<<8), `t` es Q8 (0..256).
/// Los productos intermedios caben en 32 bits (sin `__mulsi3`/heap; es una
/// llamada por frame, no por píxel). El compositor convierte a píxel entero.
constexpr eng::s32 catmull_q8(eng::s32 p0, eng::s32 p1, eng::s32 p2, eng::s32 p3,
			      eng::s32 t) {
	const eng::s32 t2 = (t * t) >> 8;
	const eng::s32 t3 = (t2 * t) >> 8;
	const eng::s32 v = 2 * p1 * 256 + (-p0 + p2) * t +
			   (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
			   (-p0 + 3 * p1 - 3 * p2 + p3) * t3;
	return v / 512;
}

/// Punto de la cámara (esquina superior izquierda del viewport) dentro de los
/// límites del mundo. `x` mínimo 1: el mapper flat reserva una palabra antes del
/// borde izquierdo (ver `map_flat_scroll`).
struct CameraPoint {
	eng::s16 x;
	eng::s16 y;
};

constexpr CameraPoint kTour[] = {
	{16, 0}, {320, 0}, {16, 120}, {320, 240},
	{16, 384}, {320, 384}, {160, 192}, {16, 0},
};
constexpr eng::u8 kTourCount = sizeof(kTour) / sizeof(kTour[0]);

struct DemoGame {
	playfield::FlatScrollPlayfield m_pf {};
	playfield::XlimitedDisplayComposer m_comp {};
	eng::debug::HostHud m_hud {};
	eng::u16 m_palette[8] {};
	/// Recorrido cerrado por longitud de arco: 8 segmentos × 32 muestras.
	eng::scene::ArcPath<512> m_path {};
	eng::u32 m_dist_q8 = 0u;    ///< distancia recorrida (Q8)
	eng::u16 m_stop_frames = 0u; ///< frames restantes de parada
	eng::u16 m_run_frames = 0u;  ///< frames desde la última parada
	eng::s32 m_cam_x = 16;
	eng::s32 m_cam_y = 0;
	bool m_ready = false;

	/// Cuece la Catmull-Rom a una polilínea densa y mide su longitud de arco.
	/// El sampling (32 puntos por tramo) hace que el error de interpolación sea
	/// inferior a 1 px; el avance por frame usa la tabla, no `t` del segmento.
	void build_path() {
		constexpr eng::u16 kSamplesPerSegment = 32u;
		m_path.clear();
		for (eng::u8 seg = 0u; seg < kTourCount; ++seg) {
			const eng::u8 i0 = static_cast<eng::u8>((seg + kTourCount - 1u) % kTourCount);
			const eng::u8 i1 = seg;
			const eng::u8 i2 = static_cast<eng::u8>((seg + 1u) % kTourCount);
			const eng::u8 i3 = static_cast<eng::u8>((seg + 2u) % kTourCount);
			const eng::s32 x0 = static_cast<eng::s32>(kTour[i0].x) << 8;
			const eng::s32 x1 = static_cast<eng::s32>(kTour[i1].x) << 8;
			const eng::s32 x2 = static_cast<eng::s32>(kTour[i2].x) << 8;
			const eng::s32 x3 = static_cast<eng::s32>(kTour[i3].x) << 8;
			const eng::s32 y0 = static_cast<eng::s32>(kTour[i0].y) << 8;
			const eng::s32 y1 = static_cast<eng::s32>(kTour[i1].y) << 8;
			const eng::s32 y2 = static_cast<eng::s32>(kTour[i2].y) << 8;
			const eng::s32 y3 = static_cast<eng::s32>(kTour[i3].y) << 8;
			for (eng::u16 k = 0u; k < kSamplesPerSegment; ++k) {
				const eng::s32 t = static_cast<eng::s32>(k) * 8; // Q8: 0..248
				(void)m_path.push({catmull_q8(x0, x1, x2, x3, t),
					     catmull_q8(y0, y1, y2, y3, t)});
			}
		}
		// Cierre: el primer punto repetido convierte la polilínea en bucle.
		(void)m_path.push({static_cast<eng::s32>(kTour[0].x) << 8,
			     static_cast<eng::s32>(kTour[0].y) << 8});
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({224u * 1024u, 16u * 1024u, 0u})) {
			eng::amiga::mark_failed(eng::debug::BootFail::Memory);
			return;
		}
		if (!m_pf.begin(backend.memory_manager(),
				{kWorldW, kWorldH, kViewW, kViewH, kPlanes, kFetchBytes})) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		for (eng::u16 i = 0u; i < 8u; ++i) m_palette[i] = kPalette3[i];
		m_palette[6] = kFlowerCycle[0];
		m_palette[7] = kFireCycle[0];
		// Copia única del mundo planar al bitmap interleaved del playfield.
		eng::u8* dst = m_pf.bitmap().bytes().data();
		for (eng::u32 i = 0u; i < g_world3_size && i < TILED3_WORLD_BYTES; ++i) {
			dst[i] = g_world3[i];
		}
		build_path();
		playfield::XlimitedDisplayComposer::Config cfg {};
		cfg.palette = eng::PaletteWords {m_palette, 8u};
		cfg.copper_bytes = 1536u;
		cfg.planes = kPlanes;
		cfg.ddfstrt = 0x0030u; // fetch ancho: 42 B/fila (coincide con BPLMOD)
		cfg.ddfstop = 0x00d0u;
#if defined(DEMO_FREEZE_FIRE)
		// Diagnóstico: fija la cámara sobre la celda de fuego (tile 16,29) para
		// verificar la animación de paleta sin que el scroll la saque de plano.
		m_cam_x = 96;
		m_cam_y = 360;
#endif
		m_pf.cam_x().position = m_cam_x;
		m_pf.cam_y().position = m_cam_y;
		if (!m_comp.init(backend.memory_manager(), cfg) ||
		    !m_comp.compose(m_pf.hardware_view())) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		m_comp.takeover(backend);
		m_ready = true;
		eng::amiga::mark_ready(kReadyDetail);
	}

	/// Catmull-Rom cerrado sobre `kTour`; el paso Q8 se fija para ~6 px/frame
	/// (la longitud se aproxima con Manhattan, suficiente para una visita suave).
	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		if (!m_ready) return;
#if defined(DEMO_FREEZE_FIRE)
		const eng::u32 tick = context.frame.frame_index;
		m_palette[6] = kFlowerCycle[(tick >> 3) & 3u];
		m_palette[7] = kFireCycle[(tick >> 3) & 3u];
		return;
#else
		// Avance **por longitud de arco**: la tabla ya tiene la distancia real
		// entre muestras de la Catmull-Rom, así que cada frame avanza los mismos
		// px aunque la curva acelere en los cuspides. Las paradas congelan la
		// distancia, no el reloj: las flores siguen animando.
		if (m_stop_frames != 0u) {
			--m_stop_frames;
		} else if (m_path.valid()) {
			m_dist_q8 += static_cast<eng::u32>(kScrollPxPerFrame) << 8;
			if (m_dist_q8 >= m_path.total_q8()) {
				m_dist_q8 -= m_path.total_q8();
				m_stop_frames = kStopFrames;
				m_run_frames = 0u;
			} else if (++m_run_frames >= kStopEveryFrames) {
				m_run_frames = 0u;
				m_stop_frames = kStopFrames;
			}
		}
		eng::math::Vec<2, eng::s32> cam_q8 {};
		if (m_path.sample(m_dist_q8, cam_q8)) {
			m_cam_x = cam_q8.v[0] >> 8;
			m_cam_y = cam_q8.v[1] >> 8;
		}
		// 16 px de guarda de fetch a la izquierda: el DDF $30 empieza 16 px antes
		// de la ventana y el bitmap del mundo no tiene margen negativo.
		if (m_cam_x < 16) m_cam_x = 16;
		if (m_cam_x > static_cast<eng::s32>(kWorldW - kViewW)) {
			m_cam_x = kWorldW - kViewW;
		}
		if (m_cam_y < 0) m_cam_y = 0;
		if (m_cam_y > static_cast<eng::s32>(kWorldH - kViewH)) {
			m_cam_y = kWorldH - kViewH;
		}
		m_pf.cam_x().position = m_cam_x;
		m_pf.cam_y().position = m_cam_y;
		// Los tres índices vivos ciclan a ~6 fps con colores vecinos. El agua no
		// se apaga al reanudar el scroll (evita el salto azul al salir de la
		// parada) y la llama no parpadea entre amarillo/rojo saturados.
		const eng::u32 f = context.frame.frame_index;
		m_palette[6] = kFlowerCycle[(f >> 3) & 3u];
		m_palette[7] = kFireCycle[(f >> 3) & 3u];
#endif
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (m_ready) {
			if (!m_comp.compose(m_pf.hardware_view())) {
				m_ready = false;
				eng::amiga::mark_failed(eng::debug::BootFail::Game);
				return;
			}
			m_comp.install(backend);
			eng::util::TextLine<96> stats;
			stats << "X=" << m_cam_x << " Y=" << m_cam_y
			      << " D=" << (m_dist_q8 >> 8) << " P=" << m_stop_frames;
			m_hud.draw(backend.debug(), "209 TILED 3BPP", kVersion,
				   context.frame.frame_index, eng::debug::prof_clock(),
				   stats.c_str());
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	eng::amiga::ensure_boot();
	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine {backend, game};
	if (!backend.takeover_system()) return 0;
	engine.run_frames(0xffffu);
	return 0;
}
