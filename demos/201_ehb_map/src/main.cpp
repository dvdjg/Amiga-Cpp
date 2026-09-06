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
// Demo 201 — RECORRIDO de scroll 8-WAY X-Limited EHB (512 KB A500)
// -----------------------------------------------------------------------------
//
// QUÉ HACE
// --------
// Recorre el mapa real EHB (40x40, 6 planos) con una secuencia LINEAL de fases
// que ejercita los 4 movimientos del corkscrew y los límites del mapa:
//
//   FASE 1 "H->FIN"  : scroll HORIZONTAL directo (derecha) hasta el borde del mapa.
//   FASE 2 "V->FIN"  : scroll VERTICAL directo (abajo) desde el borde derecho hasta
//                      el borde inferior (esquina inferior derecha del mapa).
//   FASE 3 "OBLIQ^"  : diagonal arriba-izquierda hasta tocar DE NUEVO el borde
//                      superior (si X choca antes que Y, el resto sube en vertical).
//   FASE 4 "LISSAJ"  : Lissajous ALEATORIO (centro/radio/fase/frecuencia variables)
//                      que ya NO abandona nunca: cada kSegFrames re-aleatoriza la
//                      órbita y continúa indefinidamente (sin cortes ni esperas).
//
// El salto MÁXIMO es de 4 px/frame por eje; el motor descompone cada avance en
// sub-pasos ATÓMICOS de 1 px (paint-then-advance en `update_scroll`), de modo que
// aunque se avancen 4 px nunca se muestra un píxel sin pintar y el recorrido es
// totalmente continuo. Con el fix de borde
// (clamp del origen en `add_draw`, xlimited.hpp) los pre-pintados de la pista de
// 22 bloques no salen del mapa: junto al borde derecho/inferior repiten la última
// columna/fila en lugar de leer tile 0 (edge_tile) y pintar contenido equivocado.
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
//   K_SEG_FRAMES (150) frames de cada sesión Lissajous aleatoria (~3 s a 50 fps)
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
// ORIGEN del juego en el mapa (píxeles de mapa, p. ej. 1000,1000 en un mapa
// gigante 16000x16000). La demo lo demuestra: tras el `fill` de setup hace un
// `pre_scroll` hasta ese origen (parte de la banda de staging y va pintando las
// columnas/filas entrantes) para que la 1ª IMAGEN se muestre correctamente en
// esa coordenada, sin re-pintar la pantalla. Default 0 = origen (0,0).
// Notas:
//  - El orígen es independiente del tamaño del tilebank (128 tiles de base); el
//    mapa es una rejilla de índices (`kRenderMap`), puede ser gigante.
//  - El anillo físico usa `display_height` como dominio de raster; la corrección
//    del remapeo de filas para mapas con varios wraps se valida por separado.
#ifndef K_START_X
#define K_START_X 0
#endif
#ifndef K_START_Y
#define K_START_Y 0
#endif
// AUTO-VERIFICADOR de framebuffer (diagnóstico). OFF por defecto: como el
// display lee el anillo a través del *walk* horizontal y el *fine scroll*
// (BPLCON1), el mapeo "mundo -> dirección" de la CPU (`byte_for`/`planeline_for`)
// NO coincide con lo que muestra el chip tras un scroll, y el contador daba
// falsos positivos (p. ej. 4 celdas ya en el estado inicial conocido-correcto).
// El método fiable es INSTRUMENTAR `add_draw` para rastrear el ORIGEN
// (mapx,mapy,¿dest?) de cada blit y comparar con el mapa. Compilar con
// `EXTRA_DEFINES="-DK_FB_SELFCHECK=1"` para activar el contador experimental.
// En RELEASE se deja 0 → no afecta al rendimiento.
#ifndef K_FB_SELFCHECK
#define K_FB_SELFCHECK 0
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

// Límites de scroll del motor (campo main = viewport - hud); coinciden con los
// que el `TourDriver` usa para parar cada fase (ver `DemoGame::init`).
constexpr eng::s32 kMaxScrollX = static_cast<eng::s32>(
	kMapW * kTileWidth - kViewportW);
constexpr eng::s32 kMaxScrollY = static_cast<eng::s32>(
	kMapH * kTileHeight - kMainH - kTileHeight);

// Geometría NTTP para el ScrollEngine (división por constantes -> fast_div).
// El ANILLO del corkscrew se dimensiona para el viewport TOTAL (256) + 2 tiles,
// no para el main (208): el HUD reduce el área visible, pero el walk
// plane-shifted del scroll horizontal necesita el anillo completo de 18 bloques
// para que `mapy` (hasta 17) no colisione en el módulo del bucle.
constexpr eng::u32 kDisplayH = kViewportH + 2u * kTileHeight;
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
// Fases del recorrido (secuencia lineal por el mapa, sin matriz steps x modos):
//   1) HORIZONTAL directo hasta el borde derecho,
//   2) VERTICAL directo hasta el borde inferior,
//   3) OBLICUO arriba-izquierda hasta tocar DE NUEVO el borde superior,
//   4) LISSAJOUS aleatorio (centro/radio/frecuencia variable) en bucle
//      INDEFINIDO: una vez alcanzada esta fase la demo ya no la abandona
//      (cada kSegFrames re-aleatoriza la órbita y sigue, sin esperas).
// Cada fase avanza con un salto MÁXIMO por eje y frame: el motor descompone ese
// avance en sub-pasos atómicos de 1 px (paint-then-advance, ver §7 de
// xlimited.hpp), así el barrido es continuo de borde a borde sin micro-pausas.
// El coste de Blitter por frame crece ∝ salto (1 tile-blit por px), así que para
// dar MARGEN al emulador y garantizar 50 fps en el caso límite (diagonal: 2 ejes
// a la vez) se usa K_PHASE_STEP=2 por defecto (2 sub-pasos/eje/frame ≈ 2-4
// tile-blits). Sube a 4 solo si el host/hardware va holgado; el canónico de
// Steger es 1 px/frame.
// -----------------------------------------------------------------------------
enum class TourPhase : eng::u8 { HToEnd, VToEnd, ObliqueToTop, Lissajous };
constexpr eng::u8 kPhaseCount = 4;
constexpr char const* kPhaseName[kPhaseCount] { "H->FIN", "V->FIN", "OBLIQ^", "LISSAJ" };
#ifndef K_PHASE_STEP
#define K_PHASE_STEP 2
#endif
constexpr eng::s32 kPhaseStep = K_PHASE_STEP; // SALTO MÁX. px/frame por eje (recorrido continuo)

// Tabla de seno Q7 (amplitud 128 = potencia de dos) para el Lissajous, generada
// en compile-time por el engine (`eng::SineTable`) sin float por frame. Con
// amplitud 128, normalizar (÷amplitud) es un `>> 7` (ver lissajous_move), sin
// división runtime ni multiplicación mágica.
constexpr eng::SineTable<128, 256> kSin {};
constexpr eng::u32 kSinSteps = 256;

// -----------------------------------------------------------------------------
// Conductor del recorrido: FSM de 4 fases. Cada fase H/V/oblicua corre hasta
// cumplir su objetivo (borde del mapa); la fase Lissajous re-aleatoriza el
// centro/radio/frecuencia cada kSegFrames pero ya NO se va de esa fase: sigue
// indefinidamente. Produce el (dx, dy) de cada frame (hasta 4 px por eje) y la
// telemetría del segmento para el HUD.
// -----------------------------------------------------------------------------
class TourDriver {
public:
	void begin(eng::s32 maxX, eng::s32 maxY, eng::s32 startX = 0, eng::s32 startY = 0) {
		m_maxX = maxX; m_maxY = maxY;
		m_startX = startX; m_startY = startY;
		reset();
	}

	void reset() {
		m_phaseIdx = 0;
		m_frameInSeg = 0;
		m_camX = m_startX; m_camY = m_startY; // se ubica en el origen del juego
		m_lc = 0x13579bdu; // semilla del LCG (aleatoriedad sin float)
		m_justChanged = false;
		randomize_lissajous();
		report_segment();
	}

	TourPhase phase() const { return static_cast<TourPhase>(m_phaseIdx); }
	const char* phase_name() const { return kPhaseName[m_phaseIdx]; }

	// ¿Se acaba de cruzar a una fase/sesión nueva este frame? (para el HUD)
	bool just_changed() const { return m_justChanged; }

	// Telemetría del segmento que ACABA de terminar (reportada en el HUD).
	eng::u16 last_blit_max() const { return m_lastBlitMax; }
	eng::u16 last_words_max() const { return m_lastWordsMax; }
	eng::u16 last_copper() const { return m_lastCopper; }

	// Acumula la telemetría real del frame actual al segmento en curso.
	void note_telemetry(eng::u16 jobs, eng::u16 words, eng::u16 copper) {
		if (words > m_segWordsMax) m_segWordsMax = words;
		if (jobs > m_segBlitMax) m_segBlitMax = jobs;
		m_segCopper = copper;
	}

	/// Genera (dx,dy) de este frame, actualiza cámara y avanza de fase al cumplir.
	void step(eng::s32& dx, eng::s32& dy) {
		m_justChanged = false;
		compute_motion(dx, dy); // actualiza m_camX/m_camY
		++m_frameInSeg;
		bool finished = false;
		switch (phase()) {
			case TourPhase::HToEnd:      finished = m_camX >= m_maxX; break;
			case TourPhase::VToEnd:      finished = m_camY >= m_maxY; break;
			case TourPhase::ObliqueToTop: finished = m_camY <= 0;     break;
			case TourPhase::Lissajous:   finished = m_frameInSeg >= kSegFrames; break;
		}
		if (finished) {
			report_segment(); // guarda telemetría del segmento que termina
			if (phase() == TourPhase::Lissajous) {
				// Bucle curvo INDEFINIDO: re-aleatoriza la órbita y continúa sin
				// cortes. NO marca just_changed: el HUD solo se toca al cambiar de
				// FASE (4 veces en total), nunca por cada segmento, para no pagar
				// un redibujado del HUD (slow en vblank) que se notaría como una
				// parada durante el movimiento curvo.
				randomize_lissajous();
				m_justChanged = false;
			} else {
				m_phaseIdx = static_cast<eng::u8>(m_phaseIdx + 1u);
				m_justChanged = true;
			}
			m_frameInSeg = 0;
			m_segBlitMax = 0; m_segWordsMax = 0; m_segCopper = 0;
		}
	}

private:
	// Paso rectilíneo hasta el borde: devuelve el dx que lleva `cam` a `max` sin
	// pasarse (sub-paso final = resto). 0 si ya está en el borde.
	static eng::s32 step_to_end(eng::s32 cam, eng::s32 max, eng::s32 step) {
		if (cam >= max) return 0;
		const eng::s32 rem = max - cam;
		return rem < step ? rem : step;
	}

	// Calcula (dx,dy) del frame según la fase y actualiza la cámara.
	void compute_motion(eng::s32& dx, eng::s32& dy) {
		dx = 0; dy = 0;
		switch (phase()) {
			case TourPhase::HToEnd:
				dx = step_to_end(m_camX, m_maxX, kPhaseStep);
				m_camX += dx;
				break;
			case TourPhase::VToEnd:
				dy = step_to_end(m_camY, m_maxY, kPhaseStep);
				m_camY += dy;
				break;
			case TourPhase::ObliqueToTop: {
				// Diagonal arriba-izquierda desde la esquina inferior derecha
				// ("tocar de nuevo el borde superior"). Si X choca antes que Y,
				// el resto del recorrido sigue subiendo en vertical hasta Y=0.
				eng::s32 mx = m_camX - kPhaseStep, my = m_camY - kPhaseStep;
				if (mx < 0) mx = 0;
				if (my < 0) my = 0;
				dx = mx - m_camX; dy = my - m_camY;
				m_camX = mx; m_camY = my;
				break;
			}
			case TourPhase::Lissajous:
				lissajous_move(dx, dy);
				break;
		}
	}

	// Lissajous de centro/radio/fase/frecuencia ALEATORIOS (re-aleatorizado al
	// terminar cada sesión de kSegFrames): x = sin(t), y = sin(φ + r·t).
	//
	// Continuidad garantizada: la posición está cuantizada a píxeles enteros y la
	// tabla Q7 tiene mesetas en los ápices (mismo valor varias muestras seguidas),
	// por lo que "perseguir" el target podía dar DX=DY=0 (frame parado). Aquí se
	// salta la muestra congelada y se persigue la siguiente con el salto ≤ 4, de
	// modo que NUNCA hay un frame sin movimiento (sin parones, sin esperas).
	void lissajous_move(eng::s32& dx, eng::s32& dy) {
		for (eng::u32 tries = 0; ; ++tries) {
			const eng::u8 t = static_cast<eng::u8>(m_phase);
			m_phase = (m_phase + 1u) & (kSinSteps - 1u);
			const eng::s32 c = kSin[t];
			const eng::s32 s = kSin[static_cast<eng::u8>(
				(m_phaseStart + (static_cast<eng::u32>(t) * m_ratioB) / 256u)) & (kSinSteps - 1u)];
			const eng::s32 tx = m_cx + ((c * m_radius) >> 7);
			const eng::s32 ty = m_cy + ((s * m_radius) >> 7);
			approach_target(kPhaseStep, tx, ty, dx, dy);
			if (dx != 0 || dy != 0) break; // ya hay movimiento este frame
			if (tries >= 12u) break;       // defensa: curva estática un tramo largo
		}
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

	void report_segment() {
		m_lastBlitMax = m_segBlitMax; m_lastWordsMax = m_segWordsMax; m_lastCopper = m_segCopper;
	}

	// LCG (Knuth) para la aleatoriedad de la fase Lissajous: sin float, sin heap.
	eng::u32 next_rand() {
		m_lc = m_lc * 1664525u + 1013904223u;
		return m_lc;
	}

	// Centro dentro del mapa con margen = RADIO (no un valor fijo): como la órbita
	// Lissajous barre un círculo de radio m_radius alrededor del centro, exigir
	// margen = radio garantiza que TODOS los targets caen dentro del mapa. Sin
	// esto (margen fijo 48 < radio hasta 103) la órbita se salía del borde, el
	// target quedaba clampado y la cámara se detenía ~1 s pegada a la pared.
	void randomize_lissajous() {
		m_radius = 48 + static_cast<eng::s32>(next_rand() % 56u); // 48..103
		const eng::s32 rx = m_maxX - 2 * m_radius;
		const eng::s32 ry = m_maxY - 2 * m_radius;
		eng::s32 cx = m_maxX / 2, cy = m_maxY / 2; // defensivo si el radio no cabe
		if (rx > 0) cx = m_radius + static_cast<eng::s32>(next_rand() % static_cast<eng::u32>(rx + 1));
		if (ry > 0) cy = m_radius + static_cast<eng::s32>(next_rand() % static_cast<eng::u32>(ry + 1));
		m_cx = cx; m_cy = cy;
		m_ratioB = 128u + (next_rand() & 0x3fu); // 0.50..0.75 (Q8)
		m_phaseStart = next_rand() & (kSinSteps - 1u);
	}

	eng::s32 m_maxX = 0, m_maxY = 0;
	eng::s32 m_startX = 0, m_startY = 0;
	eng::s32 m_cx = 0, m_cy = 0, m_radius = 0;
	eng::s32 m_camX = 0, m_camY = 0;
	eng::u32 m_ratioB = 0, m_phaseStart = 0, m_phase = 0;
	eng::u32 m_lc = 0;
	eng::u8 m_phaseIdx = 0;
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
	TourDriver driver {};
	eng::u32 frameOfDay = 0;
	bool ready = false;
	bool hudDirty = true;   // redibujar el HUD (inicio + cada segmento)
	// Estado del auto-verificador de framebuffer (diagnóstico).
	bool fbChecked = false;
	bool fbStartDone = false;
	eng::u16 fbStart = 0;      // celdas mal JUSTO tras el fill (cámara 0,0)
	eng::u16 fbMismatch = 0;   // nº de celdas (de 260) con píxeles mal
	eng::u16 fbFirstBad = 0;   // índice de la primera celda mal (cy*COLS+cx)

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		eng::debug::reset(g_eng_frame_telemetry);
		// 200 KB de arena Chip para display+copper+relleno; el banco incbinado se
		// alia sin copiar. Con los 512 KB del A500 (quickstart=a500,1) sobra.
		if (!backend.configure_memory({200u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020101u);
			return;
		}

		// Mapa 40x40: toroidal en X para que las bandas de guarda continúen por el
		// lado opuesto; Y permanece acotado porque el recorrido debe acabar abajo.
		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.hud.height = kHudH;       // franja inferior con números
		scene_cfg.hud.planes = 6;
		scene_cfg.hud.palette = g_hudPalette;
		scene_cfg.tile_width = kTileWidth;
		scene_cfg.tile_height = kTileHeight;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = true;
		scene_cfg.scroll_mode = eng::field::ScrollMode::EightWay;
		scene_cfg.linear_display = false; // main=208: el split queda dentro del rango WAIT OCS
		scene_cfg.max_step = 16;
		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(
			&kRenderMap[0][0], kMapW * kMapH);
		scene_cfg.map.width = kMapW;
		scene_cfg.map.height = kMapH;
		scene_cfg.map.wrap_x = kMapW;
		scene_cfg.map.wrap_y = kMapH;
		scene_cfg.map.edge_tile = 0;
		scene_cfg.map.empty_tile = 0xFFFF;
		// Bias de rejilla visible: con 1,1 el offset visible (0,0) muestra map[0][0].
		// El hardware XYLimited esconde los primeros 16 px (fila/columna de guarda);
		// el bias desplaza el contenido del anillo para que la 1ª fila/columna del
		// mapa SÍ sea visible (offset (0,0) = map[0][0] en la esquina superior).
		scene_cfg.visible_tile_bias_x = 1;
		scene_cfg.visible_tile_bias_y = 1;
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

		// ---------------------------------------------------------------------
		// CÓMO SE MONTA EL FRAMEBUFFER (dos fases, sin re-pintar la pantalla):
		//
		//  FASE A (SETUP)  `scene.fill(...)` rellena TODO el anillo del corkscrew
		//                  (bitmap interleaved de `bitmap_blocks_per_row` x
		//                  `display_blocks_per_col`) con el mapa empezando en el
		//                  bloque (0,0); ver `XlimitedScene::fill`.
		//  FASE B (INCR.)  `scene.update(plan,dx,dy,...)` por frame descompone el
		//                  avance en sub-pasos de 1 px (`update_scroll`) y pinta
		//                  la COLUMNA/FILA entrante al cruzar cada límite de 16 px
		//                  (scroll_right/left/down/up), distribuyendo la carga de
		//                  Blitter: tras 16 px de desplazamiento ya está dibujada
		//                  la fila/columna completa que entra.
		//
		//  Para ARRANCAR en un offset distinto de (0,0) — p. ej. (100,100) —, tras
		//  el `fill` basta `scene.pre_scroll(backend, plan, 100, 100)`: reutiliza
		//  el mismo scroll incremental (columnas/filas entrantes) para mover la
		//  cámara hasta ahí sin re-pintar la pantalla. El contrato de qué mundo se
		//  ve en pantalla (world_x=mapposx+sx, world_y=mapposy+sy) se
		//  modela en `tools/analyze/verify-201-framebuffer.mjs`; el módulo solo
		//  pertenece a la fila física del anillo, no al índice lógico del mapa.
		//  dice qué tiles/tiles deberían verse y recorta la referencia).
		// ---------------------------------------------------------------------
		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020102u);
			return;
		}
		// Límites de scroll del motor (el campo ve main_h = viewport - hud).
		const eng::s32 maxX = kMaxScrollX;
		const eng::s32 maxY = kMaxScrollY;
		driver.begin(maxX, maxY, K_START_X, K_START_Y);

		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020103u);
			return;
		}
		// ORIGEN ARBITRARIO: si el juego arranca en una coordenada del mapa
		// distinta de (0,0), el `fill` pinta el anillo desde el bloque 0 y este
		// `pre_scroll` avanza la cámara hasta el origen pintando incrementalmente
		// las columnas/filas entrantes (reutiliza el scroll correcto del engine,
		// sin re-pintar la pantalla). Con K_START_X=K_START_Y=0 no hace nada.
		if (K_START_X != 0 || K_START_Y != 0) {
			if (!scene.pre_scroll(backend, plan, K_START_X, K_START_Y)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00020105u);
				return;
			}
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020104u);
			return;
		}
		scene.install(backend);
		// Pinta el HUD inicial (fase 1, salto máx. 4 px/frame) con datos en 0.
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
		// Sin barrido previo del lienzo completo: draw_text rellena cada celda con
		// el fondo (bg) antes de pintar el glifo, y el resto de la franja quedó
		// negro desde el init. Así el coste por redibujado es solo texto.
		// Línea 1 (cian, ink 6): "OBLIQ^ P=4"
		{   char line[20]; eng::u8 i = 0;
			const char* mn = driver.phase_name();
			while (*mn && i < 19) line[i++] = *mn++;
			const char* sp = " P=";
			while (*sp && i < 19) line[i++] = *sp++;
			const eng::u8 st = static_cast<eng::u8>(kPhaseStep);
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

	// ---------------------------------------------------------------------
	// AUTO-VERIFICADOR DEL FRAMEBUFFER (diagnóstico, en-programa): compara lo
	// que hay REALMENTE en el anillo interleaved en las posiciones de mundo de
	// la ventana visible contra lo que DEBERÍA haber (mapa `kRenderMap` +
	// `tilebank_indexed.h`/banco). Si el scroll hubiera blitteado un tile
	// equivocado (`mapx/mapy` mal) o en la posición equivocada, se detecta aquí
	// y se publica en `g_eng_frame_telemetry.fillup_extra` (leíble por el canal
	// lateral con `--telemetry-samples`).
	//
	// Contrato (screen_to_world del engine): una fila de mundo se lee en
	// `planeline_for(wy)*bytes_per_row + byte_for(wx)` (igual que `set_pixel`),
	// y la fila visible en pantalla `sy` es `(mapposy + tile_height + sy) %
	// display_height`. Con eso podemos leer un píxel del framebuffer real.
	// ---------------------------------------------------------------------
	static eng::u32 w_dmod(eng::s32 v, eng::u32 mod) {
		const eng::s32 m = static_cast<eng::s32>(mod);
		return static_cast<eng::u32>(((v % m) + m) % m);
	}
	// Índice de tile que DEBERÍA verse en el mundo (wx,wy) [clamp a bordes].
	static eng::u16 fb_expected_index(eng::s32 wx, eng::s32 wy) {
		eng::s32 bx = wx / kTileWidth, by = wy / kTileHeight;
		const eng::s32 mw = static_cast<eng::s32>(kMapW), mh = static_cast<eng::s32>(kMapH);
		if (bx < 0) bx = 0; if (bx >= mw) bx = mw - 1;
		if (by < 0) by = 0; if (by >= mh) by = mh - 1;
		return kRenderMap[static_cast<eng::u32>(by)][static_cast<eng::u32>(bx)];
	}
	// Lee el índice EHB REAL (6 planos) del mundo (wx,wy) en el anillo.
	static eng::u16 fb_read_index(const field::XLimitedPlayfield<kScrollConsts>& pf,
		const eng::u8* fp, eng::u16 bpr, eng::u8 planes, eng::s32 wx, eng::s32 wy) {
		const eng::u32 base_pl = pf.planeline_for(wy);
		const eng::u32 byte = pf.byte_for(wx);
		const eng::u16 bit = static_cast<eng::u16>(15u - (static_cast<eng::u32>(wx) & 15u));
		eng::u16 idx = 0;
		for (eng::u8 p = 0; p < planes; ++p) {
			const eng::u32 off = (base_pl + p) * bpr + byte;
			const eng::u16 w = *reinterpret_cast<const eng::u16*>(fp + off);
			if ((w >> bit) & 1u) idx |= static_cast<eng::u16>(1u << p);
		}
		return idx;
	}
	// Nº de celdas visibles (20x13) con algún píxel que no coincide con lo
	// esperado. Muestrea 4x4 píxeles por celda (rápido y representativo).
	eng::u16 framebuffer_mismatch_tiles() {
		const auto& pf = scene.bg();
		const auto hw = pf.hardware_view();
		const eng::u8* fp = hw.bitplanes;
		const eng::u16 bpr = hw.bitmap_bytes_per_row;
		const eng::u8 planes = hw.planes;
		const eng::u16 dh = hw.display_height;
		const eng::s32 mapposx = pf.mapposx();
		const eng::s32 mapposy = pf.mapposy();
		constexpr eng::u16 COLS = kViewportW / kTileWidth;   // 20
		constexpr eng::u16 ROWS = kMainH / kTileHeight;      // 13
		eng::u16 bad = 0;
		for (eng::u16 cy = 0; cy < ROWS; ++cy) {
			for (eng::u16 cx = 0; cx < COLS; ++cx) {
				bool cellBad = false;
				for (eng::u16 py = 2; py < 14 && !cellBad; py += 4) {
					for (eng::u16 px = 2; px < 14; ++px) {
						const eng::s32 wx = mapposx + static_cast<eng::s32>(cx * kTileWidth + px);
						const eng::s32 wy = static_cast<eng::s32>(w_dmod(
							mapposy + static_cast<eng::s32>(kTileHeight) +
							static_cast<eng::s32>(cy * kTileHeight + py), dh));
						if (fb_expected_index(wx, wy) != fb_read_index(pf, fp, bpr, planes, wx, wy)) {
							cellBad = true; break;
						}
					}
				}
				if (cellBad) ++bad;
			}
		}
		// Guarda el índice de la primera celda mal (para localizarla).
		if (bad != 0 && fbFirstBad == 0u) {
			for (eng::u16 cy2 = 0; cy2 < ROWS; ++cy2)
				for (eng::u16 cx2 = 0; cx2 < COLS; ++cx2) {
					// re-testea esa celda (barato, solo para localizar)
					bool b = false;
					for (eng::u16 py = 2; py < 14 && !b; py += 4)
						for (eng::u16 px = 2; px < 14; ++px) {
							const eng::s32 wx = mapposx + static_cast<eng::s32>(cx2 * kTileWidth + px);
							const eng::s32 wy = static_cast<eng::s32>(w_dmod(
								mapposy + static_cast<eng::s32>(kTileHeight) +
								static_cast<eng::s32>(cy2 * kTileHeight + py), dh));
							if (fb_expected_index(wx, wy) != fb_read_index(pf, fp, bpr, planes, wx, wy)) { b = true; break; }
						}
					if (b) { fbFirstBad = static_cast<eng::u16>(cy2 * COLS + cx2); break; }
				}
		}
		return bad;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;

		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});

		// Referencia: justo tras el fill la cámara está en (0,0); el anillo debe
		// coincidir EXACTAMENTE con el mapa (si aquí ya hay mismatch, el propio
		// índice de dirección del verificador no está bien alineado).
#if K_FB_SELFCHECK
		if (!fbStartDone) {
			fbStart = framebuffer_mismatch_tiles();
			fbStartDone = true;
		}
#endif

		eng::s32 dx = 0, dy = 0;
		driver.step(dx, dy);
		// update_scroll descompone el avance (hasta 4 px/frame por eje) en
		// sub-pasos atómicos de 1 px (paint-then-advance) y clampa el avance en
		// los bordes (map_wrap=0): el barrido es continuo, sin esperas ni
		// píxeles sin pintar.
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
		// Auto-verificador de framebuffer (experimental, ver K_FB_SELFCHECK):
		// compara lo pintado en el anillo con lo esperado. Como el mapeo
		// mundo->dirección CPU no refleja el *walk*, el contador NO es fiable;
		// por eso está OFF por defecto (no publica valores engañosos).
#if K_FB_SELFCHECK
		if (!fbChecked && scene.bg().mapposx() >= kMaxScrollX && scene.bg().mapposy() == 0) {
			fbMismatch = framebuffer_mismatch_tiles();
			fbChecked = true;
			g_eng_run_status.detail = 0x00020100u | static_cast<eng::u32>(fbStart) |
				(static_cast<eng::u32>(fbMismatch) << 8u);
		}
#endif
		tel.fillup_extra = fbMismatch;
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
