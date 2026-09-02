#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/field/xlimited_scene.hpp>

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

// Telemetría por frame (se lee por el canal lateral): blit_jobs, words y copper,
// para supervisar la homogeneidad de la carga del scroll por frame.
extern "C" {
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry {};
}

// Datos EHB del mapa render (paleta 64 RGB + mapa 40x40 -> índice de banco).
#include "../../../out/ehb/const_game_201.h"

// Banco de bloques X-Limited YA interleaved (1149 tiles, 6 planos EHB, layout
// 320 px de ancho interleaved por planelínea) producido en el HOST por
// tools/ehb/emit-xlimited-bank.mjs y cargado en la sección `.MEMF_CHIP`:
// elf2hunk la emite a un hunk HUNKF_CHIP -> LoadSeg lo carga en Chip RAM.
// Se incbinan SOLO los ~222 KB listos (sin el raw indexado de 287 KB aparte),
// para que el total de Chip (banco + display + copper) quepa en los 512 KB del
// A500 sin fast RAM.
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tilebank_xlimited\ng_tilebank_xlimited:\n"
	".align 2\n"
	".incbin \"out/ehb/tilebank.xlimited.bin\"\n"
	".globl g_tilebank_xlimited_size\ng_tilebank_xlimited_size:\n"
	".long . - g_tilebank_xlimited");
extern "C" const unsigned char g_tilebank_xlimited[];

namespace {

namespace field = eng::field;

// -----------------------------------------------------------------------------
// Demo 201 — EHB con scroll 8-WAY X-Limited (cámara CUADRADA y CIRCULAR)
// -----------------------------------------------------------------------------
//
// QUÉ HACE
// --------
// Sustituye el relleno estático (fill_planes por CPU) de la 201 por el motor
// X-Limited real: un solo bitmap interleaved con display wrapping que scrollea
// las 8 direcciones a 50fps, con las EHB (6 planos) reales del mapa de 40x40
// tiles («The Fan-tasty Tileset»). El banco de bloques interleaved se produce
// UNA vez en el HOST (tools/ehb/emit-xlimited-bank.mjs) y se incbina listo
// (~222 KB, sin la doble copia raw+interleaved que no cabría en la Chip RAM del
// A500); la escena lo ALIA directamente. La cámara recorre:
//
//   CAMINO CUADRADO (primer): barrido del PERÍMETRO de todo el mapa
//     (0,0) -> (maxX,0) -> (maxX,maxY) -> (0,maxY) -> vuelta, con borde-clamp
//     (map_wrap=0): el motor no deja pasar del borde. Revela el mapa completo
//     moviéndose en H pura y V pura (el caso duro del 8-way).
//
//   CAMINO CIRCULAR (después): la cámara recorre un círculo suave centrado en
//     el mapa, con dx y dy simultáneos (movimiento diagonal/curvo), el caso de
//     fluidez.
//
// El salto por frame es configurable (max_step ≤ 16 px): `update_scroll` lo
// ejecuta como sub-pasos ATÓMICOS de 1 px (paint-then-advance), de modo que
// nunca se muestra un píxel sin pintar y el coste de Blitter crece ∝ salto.
// -----------------------------------------------------------------------------
// CÓMO COMPILAR / LANZAR / VERIFICAR (tools/build y tools/run)
// -----------------------------------------------------------------------------
//   bash ./tools/build/build-demo.sh demos/201_ehb_map --debug --clean
//   bash ./tools/run/run-demo.sh demos/201_ehb_map
//   bash ./tools/analyze/analyze-demo.sh demos/201_ehb_map
//
// Parámetros de compilación (EXTRA_DEFINES="-D..."):
//   K_STEP       (16)    salto máx. px/frame por eje (1..16). 16 = columna nueva
//                        completa cada frame (más Blitter, misma fluidez).
//   K_VIEWPORT_H (256)   alto visible (288 con el split canónico usa 224; 256
//                        requiere linear_display, que aquí está activado).
//   K_CIRCLE_STEPS (256) pasos del círculo (potencia de dos).
// -----------------------------------------------------------------------------

#ifndef K_STEP
#define K_STEP 16
#endif
#ifndef K_VIEWPORT_H
#define K_VIEWPORT_H 256
#endif
#ifndef K_CIRCLE_STEPS
#define K_CIRCLE_STEPS 256
#endif

constexpr eng::u32 kTileWidth = 16;
constexpr eng::u32 kTileHeight = 16;
constexpr eng::u32 kViewportW = 320;
constexpr eng::u32 kViewportH = K_VIEWPORT_H;
constexpr eng::u8  kPlanes = 6;                 // EHB
constexpr eng::u32 kMapW = 40;                  // tiles (mapa 40x40)
constexpr eng::u32 kMapH = 40;
constexpr eng::u32 kTileCount = 1149;           // tiles reales del tilebank

// Geometría NTTP para el ScrollEngine (división por constantes -> fast_div).
// Con viewport 256 y corkscrew: display_height = 256 + 2*16 = 288 planos de
// display, display_planelines = 288*6.
constexpr eng::u32 kMainH = kViewportH;
constexpr eng::u32 kDisplayH = kMainH + 2u * kTileHeight;
constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileWidth,
	/*tile_height=*/       kTileHeight,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/kDisplayH * kPlanes,
	/*planes=*/            kPlanes,
};

// Paleta EHB: solo las 32 BASES (índices 0..31). El hardware EHB genera los
// half (32..63) automáticamente como base/2; el bit 0 de BPLCON4 (puesto por el
// compositor single con planes=6) activa ese modo. kEhbPalette[64] ya viene en
// convención BASES-PRIMERO desde slice-tiles.mjs.

/// Controlador de cámara reutilizable (cuadrado -> circular). Trabaja en
/// píxeles de MUNDO [0, maxX] x [0, maxY] y produce por frame un (dx, dy) que
/// `update_scroll` ejecuta como sub-pasos atómicos de 1 px con borde-clamp.
class CameraPath {
public:
	/// maxX/maxY = límite de scroll del motor (map_w*tw - viewport_w - tw).
	void begin(eng::s32 maxX, eng::s32 maxY) {
		m_maxX = maxX; m_maxY = maxY;
		m_cx = maxX / 2; m_cy = maxY / 2;
		m_radius = (maxX < maxY ? maxX : maxY) / 2 - 8;
		if (m_radius < 0) m_radius = 0;
		reset();
	}
	void reset() { m_mode = 0; m_targetX = 0; m_targetY = 0; m_frame = 0; m_squareStep = 0; }

	/// Devuelve el (dx, dy) de ESTE frame (corrige hasta `step` px por eje).
	/// Alterna CUADRADO (borde-clamp, revela todo el mapa) y CIRCULAR.
	void step(eng::s32 step, eng::s32& dx, eng::s32& dy) {
		switch (m_mode) {
			case 0u: // CUADRADO: barrido del perímetro, borde-clamp.
				if (m_targetX == m_camX && m_targetY == m_camY) next_square_target();
				approach_target(step, dx, dy);
				break;
			default: { // CIRCULAR: cámara sobre un círculo centrado en el mapa.
				const eng::u32 t = m_frame & (kCircleSteps - 1u);
				const eng::s32 vy = static_cast<eng::s32>(kCircle[(t + kCircleSteps / 4u) & (kCircleSteps - 1u)]);
				m_targetX = m_cx + (static_cast<eng::s32>(kCircle[t]) * m_radius >> 7);
				m_targetY = m_cy + (vy * m_radius >> 7);
				approach_target(step, dx, dy);
				// Una vuelta (pasos) completa -> vuelta al cuadrado.
				if ((m_frame & (kCircleSteps - 1u)) == (kCircleSteps - 1u)) {
					m_mode = 0u; m_squareStep = 0u; next_square_target();
				}
			}
		}
		++m_frame;
	}

private:
	static constexpr eng::u32 kCircleSteps = K_CIRCLE_STEPS;
	static constexpr eng::SineTable<128, kCircleSteps> kCircle {};

	void next_square_target() {
		switch (m_squareStep) {
			case 0: m_targetX = m_maxX; m_targetY = 0;      break; // derecha (borde sup)
			case 1: m_targetX = m_maxX; m_targetY = m_maxY; break; // abajo (borde der)
			case 2: m_targetX = 0;      m_targetY = m_maxY; break; // izquierda (borde inf)
			case 3: m_targetX = 0;      m_targetY = 0;      break; // arriba (borde izq)
			default:
				// Perímetro completo -> pasar a circular.
				m_mode = 1u; m_squareStep = 0u;
				m_targetX = m_camX; m_targetY = m_camY; (void)0; return;
		}
		m_squareStep = static_cast<eng::u8>((m_squareStep + 1u) & 3u);
	}
	void approach_target(eng::s32 step, eng::s32& dx, eng::s32& dy) {
		dx = clamp_move(m_targetX - m_camX, step); m_camX += dx;
		dy = clamp_move(m_targetY - m_camY, step); m_camY += dy;
	}
	static eng::s32 clamp_move(eng::s32 d, eng::s32 step) {
		if (d > step) return step;
		if (d < -step) return -step;
		return d;
	}

	eng::s32 m_maxX = 0, m_maxY = 0;
	eng::s32 m_cx = 0, m_cy = 0, m_radius = 0;
	eng::s32 m_camX = 0, m_camY = 0;
	eng::s32 m_targetX = 0, m_targetY = 0;
	eng::u32 m_frame = 0;
	eng::u8 m_mode = 0;        // 0 = cuadrado, 1 = circular
	eng::u8 m_squareStep = 0;  // 0..3 vértices del perímetro
	// En 201 el camino es un bucle infinito cuadrado->circular->cuadrado: la
	// transición ocurre al completar cada perímetro (next_square_target default).
};

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	CameraPath camera {};
	eng::u16 m_palette[32] {};
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		eng::debug::reset(g_eng_frame_telemetry);
		// Banco de bloques EHB interleaved (~222 KB) ya está incbinado en .MEMF_CHIP
		// (LoadSeg lo carga ANTES de main). Con 512 KB totales de Chip en el A500,
		// quedan ~290 KB para la arena de la escena: display 6 planos (256+linear,
		// ~156 KB) + copper + relleno inicial. Reservamos 200 KB de arena Chip y lo
		// dejamos a la escena partir de ahí.
		if (!backend.configure_memory({200u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020101u);
			return;
		}

		// Mapa 40x40 edge-clamped (wrap_x=wrap_y=0): el motor bloquea el avance
		// en los bordes, que es lo que permite el barrido cuadrado con clamp.
		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTileWidth;
		scene_cfg.tile_height = kTileHeight;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = true;
		scene_cfg.scroll_mode = eng::field::ScrollMode::EightWay;
		scene_cfg.linear_display = true; // viewport 256: el split canónico no es fiable
		scene_cfg.max_step = static_cast<eng::u8>(K_STEP);
		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(
			static_cast<const eng::u16*>(kRenderMap), kMapW * kMapH);
		scene_cfg.map.width = kMapW;
		scene_cfg.map.height = kMapH;
		scene_cfg.map.wrap_x = 0;
		scene_cfg.map.wrap_y = 0;
		scene_cfg.map.edge_tile = 0;
		// No hay tile "vacío": todos los del banco se pintan.
		scene_cfg.map.empty_tile = 0xFFFF;
		scene_cfg.tileset_count = static_cast<eng::u16>(kTileCount);
		// Banco pre-construido en el host (~222 KB): la escena lo ALIA sin copia.
		scene_cfg.blocks_prebuilt = g_tilebank_xlimited;
		scene_cfg.blocks_prebuilt_size = kTileCount * 192u; // 16*6 planos * 2 B
		// Paleta EHB: 32 bases -> COLOR0..31 (el hardware genera los half).
		for (eng::u8 i = 0; i < 32; ++i) {
			const eng::u8* c = &kEhbPalette[static_cast<eng::u32>(i) * 3u];
			m_palette[i] = static_cast<eng::u16>(
				((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4));
		}
		scene_cfg.palette = m_palette;

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020102u);
			return;
		}
		eng::debug::mark_failed(g_eng_run_status, 0x000201A1u); // TEMP: after begin
		// Límites de scroll del motor (coinciden con el clamp de update_scroll).
		const eng::s32 maxX = static_cast<eng::s32>(
			kMapW * kTileWidth - kViewportW - kTileWidth);
		const eng::s32 maxY = static_cast<eng::s32>(
			kMapH * kTileHeight - kViewportH - kTileHeight);
		camera.begin(maxX, maxY);

		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020103u);
			return;
		}
		eng::debug::mark_failed(g_eng_run_status, 0x000201A2u); // TEMP: after fill
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020104u);
			return;
		}
		eng::debug::mark_failed(g_eng_run_status, 0x000201A3u); // TEMP: after compose
		scene.install(backend);

		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x00020100u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;

		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});
		eng::s32 dx = 0, dy = 0;
		camera.step(static_cast<eng::s32>(K_STEP), dx, dy);
		// update_scroll aplica max_step por eje como sub-pasos atómicos de 1 px y
		// clampa en los bordes (map_wrap=0) sin fallar.
		scene.update(plan, dx, dy, context.frame.frame_index);

		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0002010bu);
			return;
		}
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0002010du);
			return;
		}

		// Telemetría de carga por frame (leer por canal lateral `mem <addr> <len>`).
		auto& tel = g_eng_frame_telemetry;
		tel.frame = context.frame.frame_index;
		const eng::u32 w = plan.blit_budget().words;
		tel.blit_jobs = plan.blit_job_count();
		tel.blit_words = static_cast<eng::u16>(w > 0xffffu ? 0xffffu : w);
		tel.copper_words = scene.copper_words();
		tel.fillup_extra = 0;
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (ready) scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
