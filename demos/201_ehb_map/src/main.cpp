#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/tile_demo.hpp>
#include <eng/core/sinetable.hpp>

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
// Se incbinan SOLO los ~222 KB listos (sin el raw indexado de 287 KB aparte) y
// la escena lo ALIA sin copia, de modo que el total de Chip (banco + display +
// copper) quepa en los 512 KB del A500 sin fast RAM.
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tilebank_xlimited\ng_tilebank_xlimited:\n"
	".align 2\n"
	".incbin \"out/ehb/tilebank.xlimited.bin\"\n"
	".globl g_tilebank_xlimited_size\ng_tilebank_xlimited_size:\n"
	".long . - g_tilebank_xlimited");
extern "C" const unsigned char g_tilebank_xlimited[];
extern "C" const unsigned int g_tilebank_xlimited_size;

namespace {

namespace field = eng::field;

// -----------------------------------------------------------------------------
// Demo 201 — MUESTRARIO de scroll 8-WAY X-Limited EHB (512 KB A500)
// -----------------------------------------------------------------------------
//
// QUÉ HACE
// --------
// Convierte la demo en un muestrario automático que recorre en bucle TODAS las
// posibilidades del scroll 8-way sobre el mapa real EHB (40x40, 6 planos):
//
//   STEPS  : barrido de jumps 1px -> 2 -> 3 -> 4 -> 6 -> 8 -> 10 -> 12 -> 14 -> 16
//            (de una columna nueva completa por frame a sub-píxel fluido). Cada
//            step se mantiene ~3s para ver la suavidad a todas las granularidades.
//   MODOS  : HORIZONTAL (H en ping-pong), VERTICAL (V), OBLICUO (diagonal que bota
//            en los 4 bordes), CIRCULAR y LISSAJOUS (dos senos en cuadratura).
//   MATRIZ : por cada step se recorren los 5 modos; al terminar se vuelve al
//            step 1. Total = 10 steps x 5 modos = 50 segmentos x ~3 s ≈ 2.5 min.
//
// El motor ejecuta CADA jump como sub-pasos ATÓMICOS de 1 px (paint-then-advance
// en `update_scroll`): nunca se muestra un píxel sin pintar y el coste de
// Blitter crece ∝ jump (1 columna por frame en el peor caso de 16 px).
//
//   RAM  : un solo bitmap interleaved de display + el banco incbinado ALIADO
//          (sin la 2ª copia interleaved) + un lienzo HUD fino -> total < 512 KB.
//   CPU  : la demo solo decide quién/dónde mover la cámara y programa registros;
//          los blits (1..n por frame) y la copperlist son cortos. La franja HUD
//          muestra EN VIVO blit_jobs / blit_words / copper_words / fps por
//          segmento, y el harness verifica 50 fps y el presupuesto por el canal
//          lateral (g_eng_frame_telemetry).
//
// -----------------------------------------------------------------------------
// CÓMO COMPILAR / LANZAR / VERIFICAR (tools/build y tools/run)
// -----------------------------------------------------------------------------
//   bash ./tools/build/build-demo.sh demos/201_ehb_map --debug --clean
//   bash ./tools/run/run-demo.sh demos/201_ehb_map [--sequence-frames N]
//   bash ./tools/analyze/analyze-demo.sh demos/201_ehb_map
//
// Parámetros de compilación (EXTRA_DEFINES="-D..."):
//   K_VIEWPORT_H (256) alto TOTAL (incluye la franja HUD). Con HUD el comparador
//                     de 8 bits del Copper limita main_h = viewport_h - hud ≤ 214:
//                     con hud=48 queda main=208 (holgado). No reducir mucho el
//                     hud con este viewport: un HUD fino (p. ej. 16) baja main a
//                     240 y el corkscrew solapa la banda de staging con la franja.
//   K_HUD_HEIGHT (48)  franja inferior con la telemetría en vivo (texto pequeño 1x).
//   K_SEG_FRAMES (150) frames por segmento (~3 s a 50 fps)
// -----------------------------------------------------------------------------

#ifndef K_VIEWPORT_H
#define K_VIEWPORT_H 256
#endif
#ifndef K_HUD_HEIGHT
#define K_HUD_HEIGHT 48
#endif
#ifndef K_SEG_FRAMES
#define K_SEG_FRAMES 150
#endif

constexpr eng::u32 kTileWidth = 16;
constexpr eng::u32 kTileHeight = 16;
constexpr eng::u32 kViewportW = 320;
constexpr eng::u32 kViewportH = K_VIEWPORT_H;
constexpr eng::u32 kHudH = K_HUD_HEIGHT;
constexpr eng::u32 kMainH = kViewportH - kHudH;   // alto que ve el campo de scroll
constexpr eng::u8  kPlanes = 6;                   // EHB
constexpr eng::u32 kMapW = 40;                    // tiles (mapa 40x40)
constexpr eng::u32 kMapH = 40;
constexpr eng::u32 kTileCount = 1149;             // tiles reales del tilebank
constexpr eng::u32 kSegFrames = K_SEG_FRAMES;     // ~3 s a 50 fps

// Geometría NTTP para el ScrollEngine (división por constantes -> fast_div).
// Con la franja HUD: display_height = (viewport_h - hud_height) + 2*tile_height.
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
eng::u16 g_palette[32] {};
eng::u16 g_hudPalette[16] {
	0x000, 0xfff, 0xf00, 0x0f0, 0x00f, 0xff0, 0x0ff, 0xf0f,
	0x844, 0x884, 0x448, 0x444, 0x222, 0x666, 0xaaa, 0xddd,
};

// -----------------------------------------------------------------------------
// Modos y steps del muestrario
// -----------------------------------------------------------------------------
enum class ShowcaseMode : eng::u8 { H, V, Oblique, Circle, Lissajous };
constexpr eng::u8 kModeCount = 5;
constexpr char const* kModeName[kModeCount] { "HORIZ", "VERT", "OBLIQ", "CIRC", "LISSAJ" };

// Steps representativos 1..16 (con intermedios) para ver la suavidad en todo el
// rango. 16 = columna nueva completa por frame (peor caso de Blitter; misma
// suavidad porque el motor la hace en sub-pasos de 1 px).
constexpr eng::u8 kSteps[] { 1, 2, 3, 4, 6, 8, 10, 12, 14, 16 };
constexpr eng::u32 kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

// Tabla de seno Q7 (amplitud 127 = 1.0) para círculo / Lissajous, generada en
// compile-time por el engine (`eng::SineTable`) sin float por frame.
constexpr eng::SineTable<127, 256> kSin {};
constexpr eng::u32 kSinSteps = 256;

// -----------------------------------------------------------------------------
// Conductor del muestrario: avanza la cámara por el modo actual y cambia de
// (step, modo) cada kSegFrames frames. Produce el (dx, dy) de cada frame.
// -----------------------------------------------------------------------------
class ShowcaseDriver {
public:
	void begin(eng::s32 maxX, eng::s32 maxY) {
		m_maxX = maxX; m_maxY = maxY;
		m_cx = maxX / 2; m_cy = maxY / 2;
		m_radius = (maxX < maxY ? maxX : maxY) / 2 - 8;
		if (m_radius < 0) m_radius = 0;
		reset();
	}

	void reset() {
		m_stepIdx = 0; m_modeIdx = 0;
		m_frameInSeg = 0;
		m_camX = m_cx; m_camY = 0;
		m_vx = 1; m_vy = 0;
		m_segBlitMax = 0; m_segWordsMax = 0; m_segCopper = 0;
	}

	eng::u8 step() const { return kSteps[m_stepIdx]; }
	ShowcaseMode mode() const { return static_cast<ShowcaseMode>(m_modeIdx); }
	const char* mode_name() const { return kModeName[m_modeIdx]; }
	eng::s32 cam_x() const { return m_camX; }
	eng::s32 cam_y() const { return m_camY; }

	// ¿Se acaba de cruzar a un nuevo segmento este frame? (para el HUD en render)
	bool just_changed() const { return m_justChanged; }

	// Carga medida del segmento que ACABA de terminar (el que se reporta en el
	// HUD): el nuevo segmento ya avanzó a su step/modo, pero mostramos los
	// números del anterior, que están completos.
	eng::u16 last_blit_max() const { return m_lastBlitMax; }
	eng::u16 last_words_max() const { return m_lastWordsMax; }
	eng::u16 last_copper() const { return m_lastCopper; }

	// Acumula la telemetría real del frame actual (jobs/words/copper) al
	// segmento en curso, para reportar el pico al terminar.
	void note_telemetry(eng::u16 jobs, eng::u16 words, eng::u16 copper) {
		if (words > m_segWordsMax) m_segWordsMax = words;
		if (jobs > m_segBlitMax) m_segBlitMax = jobs;
		m_segCopper = copper;
	}

	/// Genera (dx,dy) de este frame y actualiza cámara/segmento.
	void step(eng::s32& dx, eng::s32& dy) {
		m_justChanged = false;
		// Si el segmento en curso agotó sus frames, avanzar al siguiente y avisar
		// (el HUD se redibuja en render() con los datos del segmento completado).
		if (m_frameInSeg >= kSegFrames) {
			advance_segment();
			m_justChanged = true;
		}
		compute_motion(dx, dy);
		++m_frameInSeg;
	}

private:
	void advance_segment() {
		// Reportar el segmento que termina (guardarlo), y avanzar en la matriz
		// (step interno, modo externo). Al cerrar el último step se vuelve al 1.
		m_lastBlitMax = m_segBlitMax; m_lastWordsMax = m_segWordsMax; m_lastCopper = m_segCopper;
		++m_modeIdx;
		if (m_modeIdx >= kModeCount) { m_modeIdx = 0; ++m_stepIdx; }
		if (m_stepIdx >= kStepCount) { m_stepIdx = 0; }
		m_frameInSeg = 0;
		m_segBlitMax = 0; m_segWordsMax = 0; m_segCopper = 0;
		// Inicializar la velocidad del nuevo segmento según su dirección.
		switch (mode()) {
			case ShowcaseMode::H: m_vx = 1; m_vy = 0; break;
			case ShowcaseMode::V: m_vx = 0; m_vy = 1; break;
			case ShowcaseMode::Oblique: m_vx = 1; m_vy = 1; break;
			default: break;
		}
	}

	// Calcula (dx,dy) <= step por eje según el modo actual.
	void compute_motion(eng::s32& dx, eng::s32& dy) {
		dx = 0; dy = 0;
		const eng::s32 step = kSteps[m_stepIdx];
		switch (mode()) {
			case ShowcaseMode::H: velocity_move(step, 1, 0, dx, dy); break;
			case ShowcaseMode::V: velocity_move(step, 0, 1, dx, dy); break;
			case ShowcaseMode::Oblique: velocity_move(step, m_vx, m_vy, dx, dy); break;
			case ShowcaseMode::Circle: circle_move(step, dx, dy); break;
			case ShowcaseMode::Lissajous: lissajous_move(step, dx, dy); break;
		}
	}

	// Movimiento rectilíneo (H/V/oblicuo) que bota en los bordes del mapa.
	void velocity_move(eng::s32 step, eng::s32 vx, eng::s32 vy, eng::s32& dx, eng::s32& dy) {
		// Aplica velocidad y refleja el eje cuando choca contra un borde.
		eng::s32 nx = m_camX + vx * step;
		if (nx < 0) { nx = -nx; m_vx = -m_vx; }
		else if (nx > m_maxX) { nx = 2 * m_maxX - nx; m_vx = -m_vx; }
		if (nx < 0) nx = 0; if (nx > m_maxX) nx = m_maxX;
		eng::s32 ny = m_camY + vy * step;
		if (ny < 0) { ny = -ny; m_vy = -m_vy; }
		else if (ny > m_maxY) { ny = 2 * m_maxY - ny; m_vy = -m_vy; }
		if (ny < 0) ny = 0; if (ny > m_maxY) ny = m_maxY;
		dx = nx - m_camX; dy = ny - m_camY;
		m_camX = nx; m_camY = ny;
	}

	// Círculo centrado en el mapa (radio fijo dentro de los límites).
	void circle_move(eng::s32 step, eng::s32& dx, eng::s32& dy) {
		const eng::u8 t = static_cast<eng::u8>(m_phase);
		m_phase = (m_phase + 1u) & (kSinSteps - 1u);
		const eng::s32 c = kSin[t];
		const eng::s32 s = kSin[static_cast<eng::u8>(t + kSinSteps / 4u)];
		const eng::s32 tx = m_cx + c * m_radius / 127;
		const eng::s32 ty = m_cy + s * m_radius / 127;
		approach_target(step, tx, ty, dx, dy);
	}

	// Lissajous: x = sin(t), y = sin((7/10)*t) -> curva cerrada densa.
	void lissajous_move(eng::s32 step, eng::s32& dx, eng::s32& dy) {
		const eng::u8 t = static_cast<eng::u8>(m_phase);
		m_phase = (m_phase + 1u) & (kSinSteps - 1u);
		// (7/10)*256 = 179: frecuencia 0.7 en el dominio de 256 pasos.
		const eng::s32 c = kSin[t];
		const eng::s32 s = kSin[static_cast<eng::u8>((static_cast<eng::u32>(t) * 179u) / 100u)];
		const eng::s32 tx = m_cx + c * m_radius / 127;
		const eng::s32 ty = m_cy + s * m_radius / 127;
		approach_target(step, tx, ty, dx, dy);
	}

	void approach_target(eng::s32 step, eng::s32 tx, eng::s32 ty, eng::s32& dx, eng::s32& dy) {
		if (tx < 0) tx = 0; if (tx > m_maxX) tx = m_maxX;
		if (ty < 0) ty = 0; if (ty > m_maxY) ty = m_maxY;
		dx = clamp_move(tx - m_camX, step); m_camX += dx;
		dy = clamp_move(ty - m_camY, step); m_camY += dy;
	}
	static eng::s32 clamp_move(eng::s32 d, eng::s32 step) {
		if (d > step) return step;
		if (d < -step) return -step;
		return d;
	}

	eng::s32 m_maxX = 0, m_maxY = 0;
	eng::s32 m_cx = 0, m_cy = 0, m_radius = 0;
	eng::s32 m_camX = 0, m_camY = 0;
	eng::s32 m_vx = 0, m_vy = 0;
	eng::u32 m_phase = 0;
	eng::u8 m_stepIdx = 0, m_modeIdx = 0;
	eng::u32 m_frameInSeg = 0;
	eng::u16 m_segBlitMax = 0, m_segWordsMax = 0, m_segCopper = 0;
	eng::u16 m_lastBlitMax = 0, m_lastWordsMax = 0, m_lastCopper = 0;
	bool m_justChanged = false;
};

// -----------------------------------------------------------------------------
// Mini-fuente 5x7 (A-Z + 0-9) para el HUD, almacenada como 7 bytes/glifo con el
// bit 4 (0x10) como columna más a la izquierda. Se pinta con `set_pixel` sobre
// el lienzo HUD (CPU). Como esto compite con el DMA de bitplanes durante el
// frame visible, SOLO se redibujan al cambiar de segmento (~cada 3 s).
// -----------------------------------------------------------------------------
constexpr eng::u8 kFont[][7] {
	// A..Z
	{0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}, // A
	{0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}, // B
	{0x0e,0x11,0x10,0x10,0x10,0x11,0x0e}, // C
	{0x1e,0x11,0x11,0x11,0x11,0x11,0x1e}, // D
	{0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}, // E
	{0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}, // F
	{0x0e,0x11,0x10,0x17,0x11,0x11,0x0f}, // G
	{0x11,0x11,0x11,0x1f,0x11,0x11,0x11}, // H
	{0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}, // I
	{0x07,0x02,0x02,0x02,0x02,0x12,0x0c}, // J
	{0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
	{0x10,0x10,0x10,0x10,0x10,0x10,0x1f}, // L
	{0x11,0x1b,0x15,0x15,0x11,0x11,0x11}, // M
	{0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N
	{0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}, // O
	{0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}, // P
	{0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}, // Q
	{0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}, // R
	{0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}, // S
	{0x1f,0x04,0x04,0x04,0x04,0x04,0x04}, // T
	{0x11,0x11,0x11,0x11,0x11,0x11,0x0e}, // U
	{0x11,0x11,0x11,0x11,0x11,0x0a,0x04}, // V
	{0x11,0x11,0x11,0x15,0x15,0x1b,0x11}, // W
	{0x11,0x0a,0x04,0x04,0x04,0x0a,0x11}, // X
	{0x11,0x0a,0x04,0x04,0x04,0x04,0x04}, // Y
	{0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}, // Z
	// 0..9
	{0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}, // 0
	{0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}, // 1
	{0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}, // 2
	{0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e}, // 3
	{0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}, // 4
	{0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e}, // 5
	{0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e}, // 6
	{0x1f,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
	{0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}, // 8
	{0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e}, // 9
};

/// Índice de glifo para un carácter (A..Z, 0..9); -1 si no se soporta.
constexpr eng::s8 font_index(char ch) {
	if (ch >= 'A' && ch <= 'Z') return static_cast<eng::s8>(ch - 'A');
	if (ch >= '0' && ch <= '9') return static_cast<eng::s8>(26 + (ch - '0'));
	return -1;
}

void draw_glyph(field::Surface& s, eng::s32 px, eng::s32 py, eng::s32 scale, eng::s8 gi, eng::u8 ink) {
	if (gi < 0) return;
	for (eng::u8 r = 0; r < 7; ++r) {
		const eng::u8 bits = kFont[gi][r];
		for (eng::u8 c = 0; c < 5; ++c) {
			if ((bits & (1u << (4u - c))) == 0u) continue;
			for (eng::s32 sy = 0; sy < scale; ++sy)
				for (eng::s32 sx = 0; sx < scale; ++sx)
					s.set_pixel(px + c * scale + sx, py + r * scale + sy, ink);
		}
	}
}

// Dibuja un string de ancho fijo SOBRE un fondo ya rellenado; `ink` marca los
// píxeles del glifo y `bg` rellena las celdas no usadas (para borrar lo previo).
void draw_text(field::Surface& s, eng::s32 x, eng::s32 y, eng::s32 scale, const char* t, eng::u8 ink, eng::u8 bg) {
	const eng::s32 gw = 6 * scale;          // avance de celda (5 + 1 de espacio)
	const eng::s32 gh = 7 * scale;
	eng::s32 cx = x;
	for (; *t != '\0'; ++t) {
		if (bg != ink) s.fill_rect(cx, y, gw, gh, bg);
		if (*t != ' ') draw_glyph(s, cx, y, scale, font_index(*t), ink);
		cx += gw;
	}
}

// Número decimal a cadena (p. ej. 12345 -> "12345").
void u16_to_str(eng::u16 v, char* out) {
	char tmp[6] = {};
	eng::u8 i = 0;
	do { tmp[i++] = static_cast<char>('0' + (v % 10u)); v = static_cast<eng::u16>(v / 10u); }
	while (v != 0u && i < 5u);
	eng::u8 j = 0;
	while (i > 0u) { out[j++] = tmp[--i]; }
	out[j] = '\0';
}

// -----------------------------------------------------------------------------
// Juego
// -----------------------------------------------------------------------------
struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	ShowcaseDriver driver {};
	eng::u32 frameOfDay = 0;
	bool ready = false;
	bool hudDirty = true;   // redibujar el HUD (inicio + cada segmento)

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		eng::debug::reset(g_eng_frame_telemetry);
		// 200 KB de arena Chip para display+copper+relleno; el banco incbinado se
		// alia sin copiar. Con los 512 KB del A500 (quickstart=a500,1) sobra.
		if (!backend.configure_memory({200u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020101u);
			return;
		}

		// Mapa 40x40 edge-clamped (wrap_x=wrap_y=0): el motor bloquea el avance
		// en los bordes, que es lo que permite el barrido con rebote/curva.
		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.hud_height = kHudH;       // franja inferior con números
		scene_cfg.hud_planes = 6;
		scene_cfg.hud_palette = g_hudPalette;
		scene_cfg.tile_width = kTileWidth;
		scene_cfg.tile_height = kTileHeight;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = true;
		scene_cfg.scroll_mode = eng::field::ScrollMode::EightWay;
		scene_cfg.linear_display = false; // main 208: el split canónico separa el HUD
		scene_cfg.max_step = 16;
		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(
			static_cast<const eng::u16*>(kRenderMap), kMapW * kMapH);
		scene_cfg.map.width = kMapW;
		scene_cfg.map.height = kMapH;
		scene_cfg.map.wrap_x = 0;
		scene_cfg.map.wrap_y = 0;
		scene_cfg.map.edge_tile = 0;
		scene_cfg.map.empty_tile = 0xFFFF;
		scene_cfg.tileset_count = static_cast<eng::u16>(kTileCount);
		scene_cfg.blocks_prebuilt = g_tilebank_xlimited;
		scene_cfg.blocks_prebuilt_size = g_tilebank_xlimited_size;
		// Paleta EHB: 32 bases -> COLOR0..31 (el hardware genera los half).
		for (eng::u8 i = 0; i < 32; ++i) {
			const eng::u8* c = &kEhbPalette[static_cast<eng::u32>(i) * 3u];
			g_palette[i] = static_cast<eng::u16>(
				((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4));
		}
		scene_cfg.palette = g_palette;

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020102u);
			return;
		}
		// Límites de scroll del motor (el campo ve main_h = viewport - hud).
		const eng::s32 maxX = static_cast<eng::s32>(
			kMapW * kTileWidth - kViewportW - kTileWidth);
		const eng::s32 maxY = static_cast<eng::s32>(
			kMapH * kTileHeight - kMainH - kTileHeight);
		driver.begin(maxX, maxY);

		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020103u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020104u);
			return;
		}
		scene.install(backend);
		// Pinta el HUD inicial (step 1 / primer modo) con datos en 0.
		draw_hud();

		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x00020100u);
	}

	// Redibuja la franja HUD (solo al cambiar de segmento). Se llama desde
	// render() (durante el vblank) para no competir con el DMA en scanlines
	// visibles. HUD con texto pequeño (1x): modo/step + carga del segmento
	// sobre fondo oscuro limpio.
	void draw_hud() {
		auto hud = scene.hud_surface();
		// Fondo negro limpio (color 0); draw_text rellena cada celda con el mismo
		// fondo, así que cada línea queda borrada al redibujar.
		hud.fill_rect(0, 0, kViewportW, kHudH, 0);
		// Línea 1 (cian, ink 6): "HORIZ STEP=16"
		{   char line[20]; eng::u8 i = 0;
			const char* mn = driver.mode_name();
			while (*mn && i < 19) line[i++] = *mn++;
			const char* sp = " STEP=";
			while (*sp && i < 19) line[i++] = *sp++;
			const eng::u8 st = driver.step();
			if (st >= 10) line[i++] = static_cast<char>('0' + st / 10u);
			line[i++] = static_cast<char>('0' + st % 10u);
			line[i] = '\0';
			draw_text(hud, 2, 3, 1, line, 6, 0);
		}
		// Línea 2 (gris claro, ink 14): "BLITS=nn WORDS=nnnnn"
		{   char n1[7], n2[7]; u16_to_str(driver.last_blit_max(), n1); u16_to_str(driver.last_words_max(), n2);
			char line[28]; eng::u8 i = 0;
			const char* parts[4] { "BLITS=", n1, " WORDS=", n2 };
			for (eng::u8 p = 0; p < 4; ++p) { const char* t = parts[p]; while (*t && i < 27) line[i++] = *t++; }
			line[i] = '\0';
			draw_text(hud, 2, 17, 1, line, 14, 0);
		}
		// Línea 3 (azul, ink 4): "COPPER=nnn FPS=50"
		{   char n1[7], n2[7]; u16_to_str(driver.last_copper(), n1); u16_to_str(50, n2);
			char line[24]; eng::u8 i = 0;
			const char* parts[4] { "COPPER=", n1, " FPS=", n2 };
			for (eng::u8 p = 0; p < 4; ++p) { const char* t = parts[p]; while (*t && i < 23) line[i++] = *t++; }
			line[i] = '\0';
			draw_text(hud, 2, 31, 1, line, 4, 0);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;

		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});

		eng::s32 dx = 0, dy = 0;
		driver.step(dx, dy);
		// update_scroll aplica max_step por eje como sub-pasos atómicos de 1 px y
		// clampa en los bordes (map_wrap=0) sin fallar. El step real del driver
		// (1..16) es lo que se ejercita aquí.
		scene.update(plan, dx, dy, context.frame.frame_index);

		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00020117u);
			return;
		}
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00020110u);
			return;
		}

		// Telemetría por frame (leer por canal lateral `mem <addr> <len>`).
		auto& tel = g_eng_frame_telemetry;
		tel.frame = context.frame.frame_index;
		const eng::u32 w = plan.blit_budget().words;
		tel.blit_jobs = plan.blit_job_count();
		tel.blit_words = static_cast<eng::u16>(w > 0xffffu ? 0xffffu : w);
		tel.copper_words = scene.copper_words();
		tel.fillup_extra = 0;
		// TEMP-DEBUG: expone por el canal lateral la geometría runtime de la zona HUD.
		tel.reserved[0] = scene.debug_hud_planes();
		tel.reserved[1] = scene.debug_hud_raster();
		tel.reserved[2] = scene.debug_main_h();
		// Acumula en el segmento en curso para reportar el pico en el HUD.
		driver.note_telemetry(tel.blit_jobs, tel.blit_words, tel.copper_words);
		++frameOfDay;
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (ready) {
			if (hudDirty) { draw_hud(); hudDirty = false; }
			// Redibuja el HUD al cambiar de segmento (una vez cada ~3 s).
			if (driver.just_changed()) { draw_hud(); }
			scene.install(backend);
		}
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
