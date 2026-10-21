// ============================================================================
// Demo 080 (3d): fotogramas intermedios por paneo de camara (Full/Fake)
// ============================================================================
//
// Presentación Full/Fake con barridos de cámara de 120° en yaw y pitch, seguidos de una
// trayectoria Lissajous combinada. Tras cada Full, 1–3 Fake frames desplazan la ventana
// dentro del framebuffer sobredimensionado según el tiempo real del render.
//
//   framebuffer 352×256; viewport 320×208; guardas X=16 px, Y=24 filas
//
// - **Full**: clipping general de cámara + sala de colores separados (techo verde, fondo
//   azul) y paneles de color singular; relleno por plano con el Blitter.
// - **Fake**: no se toca el FB; solo se parchean los `BPLxPT` (coarse) y
//   `BPLCON1` fino para X y avanza el puntero por filas completas para Y. La amplitud
//   del Lissajous y las velocidades respetan ambas guardas durante el render rebanado.
// - HUD de depuracion en la franja inferior (48 lineas): desde la linea 252 el
//   Copper conmuta a **1 bitplane** con la tira del HUD y COLOR00/01 propios;
//   texto por palabras (`Surface::draw_text_words`).
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window

#include <eng/api/api.hpp>
#include <eng/core/types/types.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/fill_pattern.hpp>
#include <eng/graphics/mesh_renderer.hpp>
#include <eng/graphics/pan_window.hpp>
#include <eng/graphics/polygon_planes.hpp>
#include <eng/graphics/present_controller.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/retro/fixed_trig.hpp>
#include <eng/retro/lib2d.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

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
using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

// --- Geometria del display ----------------------------------------------------------
// FB de escena 352x256 (viewport 320x208 con márgenes de 16 px en X y 24 filas en Y).
// Las guardas permiten Fake frames durante giros combinados; la cámara se recorta al FB.
// La ventana visible mantiene 208 filas y la conmutación al HUD cae en 44+208=252.
constexpr u16 kFbW = 352u;
constexpr u16 kFbH = 256u;
constexpr u16 kRowBytes = kFbW / 8u;      // 44 B por fila de plano
constexpr u16 kViewportW = 320u;
constexpr u16 kViewportH = 208u;
constexpr u16 kCenterS = (kFbW - kViewportW) / 2u; // 16
constexpr u16 kMarginY = (kFbH - kViewportH) / 2u; // 24 filas completas para paneo de pitch
constexpr u8 kPlanes = 4u;
constexpr u8 kBuffers = 2u;
constexpr u16 kSceneBytes = kRowBytes * kFbH;
constexpr u8 kHudLineChars = 38u; ///< ancho fijo del repintado del HUD (304 px desde x=16)
// DDF $30 (1 palabra de margen para el fino continuo) .. $D8 (22 palabras = 352 px,
// que es lo que el validador exige cubrir del ancho).
constexpr u16 kDdfstrt = eng::graphics::fine_scroll_ddfstrt;
constexpr u16 kDdfstop = 0x00d8u;
constexpr u16 kDiwstrt = 0x2c81u;
constexpr u16 kDiwstop = 0x2cc1u;

// --- HUD (franja inferior, 1 bitplane) ----------------------------------------------
constexpr u16 kHudLines = 48u;
constexpr u16 kHudLine = 44u + kViewportH;        // 252; el HUD no se desplaza con la escena
constexpr u16 kHudBytes = kRowBytes * kHudLines;
constexpr u16 kHudColor0 = 0x000u;
constexpr u16 kHudColor1 = 0xfffu;

// --- Camara -------------------------------------------------------------------------
// `project_perspective`: sx = cx + x*focal/z, sy = cy + y*focal/z (y positivo = abajo).
// focal 256 -> escala 1 px por unidad de mundo a z=256. yaw = pan/focal rad; en vueltas
// de q12 (65536 por vuelta) = pan * 41 (+-1%).
constexpr s16 kFocal = 256;
constexpr s16 kCx = static_cast<s16>(kCenterS + kViewportW / 2u); // 176
constexpr s16 kCy = static_cast<s16>(kMarginY + kViewportH / 2u); // centro de la ventana: 112

constexpr s16 kRoomX = 160;
constexpr s16 kRoomY = 96;
constexpr s16 kRoomZ0 = 32; // la sala llega cerca de cámara para seguir visible al mirar ±60°
constexpr s16 kRoomZ1 = 1024;

// ±268 px en el mapeo focal=256 representan ±60° (barrido total de 120°).
constexpr s32 kSweepPan = 268;
constexpr s32 kYawCullPanLimit = 65; // poses congeladas HOST-478: ±13/±65, no el barrido amplio
constexpr s32 kYawSweepStep = 1;   // 0,22°/update; conserva margen para la publicación diferida
constexpr s32 kPitchSweepStep = 1; // 0,22°/update; el Fake vertical avanza una fila
constexpr s32 kLissajousPan = 180; // ±40° por eje para la fase combinada
#ifndef K080_LISSAJOUS_PHASE_STEP
#define K080_LISSAJOUS_PHASE_STEP 5
#endif
#ifndef K080_CUBE_FREEZE
#define K080_CUBE_FREEZE 0
#endif
#ifndef K080_CUBE_PROBE
#define K080_CUBE_PROBE 0
#endif
#ifndef K080_CUBE_START_PHASE
#define K080_CUBE_START_PHASE 0
#endif
static_assert(K080_LISSAJOUS_PHASE_STEP > 0 && K080_LISSAJOUS_PHASE_STEP <= 64,
	      "K080_LISSAJOUS_PHASE_STEP debe estar entre 1 y 64");
static_assert(K080_CUBE_FREEZE == 0 || K080_CUBE_FREEZE == 1,
	      "K080_CUBE_FREEZE congela las cajas para capturas A/B");
static_assert(K080_CUBE_PROBE == 0 || K080_CUBE_PROBE == 1,
	      "K080_CUBE_PROBE muestra una sola caja centrada para validar sus caras");
static_assert(K080_CUBE_START_PHASE < eng::retro::kAngleSteps,
	      "K080_CUBE_START_PHASE es un índice de la tabla seno");
constexpr u16 kLissajousPhaseStep = K080_LISSAJOUS_PHASE_STEP;
// Default 5 = 819 updates/ciclo, Δpitch máx. < 2,8 filas/update; el override solo permite
// someter el mismo giro combinado a otras velocidades en builds de validación.
constexpr u16 kLissajousFrames = 4096u / kLissajousPhaseStep;
constexpr u32 kRoomFaceCount = 5u; // suelo, techo, paredes laterales y pared del fondo
constexpr u32 kPanelCount = 4u;
constexpr u32 kFaceCount = 9u;
constexpr u8 kCubeCount = 2u;
constexpr u8 kCubeFaceCount = 6u;
constexpr u8 kCubeFaceSlots = kCubeCount * kCubeFaceCount;
constexpr s16 kCubeHalf = 28;
constexpr s16 kCubeOrbitX = 100;
constexpr s16 kCubeOrbitZ = 80;
constexpr s16 kCubeOrbitCenterZ = 480;
constexpr u16 kCubePhaseStep = 5u; // órbita común; spins individuales multiplican la fase

enum class MotionMode : u8 { YawSweep, PitchSweep, Lissajous };

// Paleta (4 planos, 16 colores). Sala: índices 1..3; paneles: 4, 8 y 12, elegidos
// conservando sus colores de marcador. Las cajas escriben los cuatro planos y usan índices
// distintos por cara (4..15), así sus colores no dependen del color de fondo.
constexpr eng::Palette32 kPalette {{
	0x124, 0x986, 0x485, 0x269, 0xfd0, 0xfff, 0xf0f, 0x0f0,
	0x0ff, 0x00f, 0xff0, 0x08f, 0xf80, 0x8f0, 0x0af, 0x80f,
}};

constexpr eng::debug::Version kVersion {1u, 0u};

// Secciones de perfil (`profile.mjs`): donde se va el frame.
enum : eng::u8 {
	kProfFrame = 0, ///< update() completo
	kProfFull = 1,  ///< pasada completa (proyeccion+relleno)
	kProfFill = 2,  ///< fill_polygons_by_plane (Blitter)
	kProfPatch = 3, ///< patch_window (punteros + BPLCON1)
	kProfHud = 4,   ///< HUD completo
	kProfHudCtor = 5,  ///< construccion del Surface + formato de lineas
	kProfHudLine1 = 6, ///< impresion de la linea 3 (FRAME/FULL/FAKE)
	kProfHudLine2 = 7, ///< impresion de la linea 4 (OFF/FAC/VER/PAN)
	kProfWait = 8,     ///< wait_blitter inicial del Full
	kProfClip = 9,     ///< transform+proyeccion+recorte+submit de las caras
};

// Patrón de relleno de la SALA: **mezcla de dos índices con damero a nivel de PÍXEL** (el
// uso principal del catálogo: crear colores extra fundiendo dos base). Los planos 0 y 1 de
// la sala llevan el damero y su inverso construidos con `fill_dither_plane_cpu` (2 filas
// por plano, periodo 2), y el backend los aplica por fases (`D = A & B`) tras el fill.
// Los paneles quedan SÓLIDOS (el marcador naranja conserva su color único).
// POR DEFECTO la sala va SÓLIDA (colores planos, más simple y mejor coste de relleno);
// `K080_PATTERN` activa el damero de mezcla A/B como opción.
#if defined(K080_PATTERN)
constexpr bool kRoomDither = true;
#else
constexpr bool kRoomDither = false;
#endif
#ifndef K080_PITCH_TEST
#define K080_PITCH_TEST 0
#endif
#ifndef K080_LISSAJOUS_TEST
#define K080_LISSAJOUS_TEST 0
#endif
static_assert(K080_PITCH_TEST == 0 || K080_PITCH_TEST == 1,
	      "K080_PITCH_TEST fuerza el barrido pitch para validación");
static_assert(K080_LISSAJOUS_TEST == 0 || K080_LISSAJOUS_TEST == 1,
	      "K080_LISSAJOUS_TEST fuerza la trayectoria Lissajous para validación");
static_assert(K080_PITCH_TEST == 0 || K080_LISSAJOUS_TEST == 0,
	      "selecciona una sola trayectoria de validación");
constexpr bool kForcePitch = K080_PITCH_TEST != 0;
constexpr bool kForceLissajous = K080_LISSAJOUS_TEST != 0;
constexpr eng::u8 kRoomDitherA = 1u; // índice base A (marrón) de la mezcla
constexpr eng::u8 kRoomDitherB = 2u; // índice base B (verde)

// Los márgenes son 15 px X y 24 filas Y. `RenderPacing` elige 0..3 Fakes usando el máximo
// de las dos últimas duraciones Full: una medida cara sube la racha inmediatamente y una
// barata aislada no la reduce. El camino predeterminado rinde un Full y luego inserta los
// Fakes que justifica su coste; `K080_SLICE_FULL` conserva el render rebanado como A/B.
#if defined(K080_SLICE_FULL) && !defined(K080_NO_SLICE)
constexpr bool kSliceEnabled = true;
#else
constexpr bool kSliceEnabled = false;
#endif
constexpr eng::u8 kFakeCap = 3u;
constexpr eng::graphics::PanWindowPolicy kPanPolicyBase {
	15, static_cast<s16>(kMarginY), kFakeCap};

// --- Escena: sala con techo verde separado del fondo azul + cuatro paneles ----------
constexpr u8 kMaxVerts = 16u; // margen para clip de cámara seguido del rectángulo del framebuffer

struct FaceSpec {
	eng::math3d::Vec3 v[4];
	u8 color;
};

constexpr eng::math3d::Vec3 p3(s16 x, s16 y, s16 z) {
	return eng::math3d::vec3(x, y, z);
}
constexpr eng::math3d::Vec3 kPanAnchor = p3(100, 0, 640); // centro del panel naranja único

// Los paneles usan bits 2-3; el naranja es marcador único. El sesgo de profundidad evita
// aristas horizontales exactas, que el trazador EOR descarta.
constexpr FaceSpec kPanelFaces[kPanelCount] = {
	{{p3(-112, -80, 635), p3(-88, -80, 645), p3(-88, 80, 645), p3(-112, 80, 635)}, 8u},
	{{p3(-72, -80, 635), p3(-48, -80, 645), p3(-48, 80, 645), p3(-72, 80, 635)}, 4u},
	{{p3(48, -80, 635), p3(72, -80, 645), p3(72, 80, 645), p3(48, 80, 635)}, 8u},
	{{p3(88, -80, 635), p3(112, -80, 645), p3(112, 80, 645), p3(88, 80, 635)}, 12u},
};

constexpr FaceSpec kFaces[kFaceCount] = {
	{{p3(-kRoomX, kRoomY, kRoomZ0), p3(-kRoomX, kRoomY, kRoomZ1),
	  p3(kRoomX, kRoomY, kRoomZ1), p3(kRoomX, kRoomY, kRoomZ0)}, 1u},
	{{p3(-kRoomX, -kRoomY, kRoomZ0), p3(kRoomX, -kRoomY, kRoomZ0),
	  p3(kRoomX, -kRoomY, kRoomZ1), p3(-kRoomX, -kRoomY, kRoomZ1)}, 2u},
	{{p3(-kRoomX, -kRoomY, kRoomZ0), p3(-kRoomX, -kRoomY, kRoomZ1),
	  p3(-kRoomX, kRoomY, kRoomZ1), p3(-kRoomX, kRoomY, kRoomZ0)}, 3u},
	{{p3(kRoomX, -kRoomY, kRoomZ0), p3(kRoomX, kRoomY, kRoomZ0),
	  p3(kRoomX, kRoomY, kRoomZ1), p3(kRoomX, -kRoomY, kRoomZ1)}, 3u},
	{{p3(-kRoomX, -kRoomY, kRoomZ1), p3(kRoomX, -kRoomY, kRoomZ1),
	  p3(kRoomX, kRoomY, kRoomZ1), p3(-kRoomX, kRoomY, kRoomZ1)}, 3u},
	kPanelFaces[0], kPanelFaces[1], kPanelFaces[2], kPanelFaces[3],
};

// Caja convexa con el mismo winding ya validado por HOST-481. Las órbitas tienen
// posiciones opuestas alrededor de (0,0,kCubeOrbitCenterZ); al cruzar la vista frontal,
// una caja queda claramente delante de la otra y el pintor por profundidad las compone.
constexpr eng::math3d::Vec3 kCubeVertices[8] = {
	p3(-kCubeHalf, -kCubeHalf, -kCubeHalf), p3(kCubeHalf, -kCubeHalf, -kCubeHalf),
	p3(kCubeHalf, kCubeHalf, -kCubeHalf), p3(-kCubeHalf, kCubeHalf, -kCubeHalf),
	p3(-kCubeHalf, -kCubeHalf, kCubeHalf), p3(kCubeHalf, -kCubeHalf, kCubeHalf),
	p3(kCubeHalf, kCubeHalf, kCubeHalf), p3(-kCubeHalf, kCubeHalf, kCubeHalf),
};
constexpr u8 kCubeFaceIndices[kCubeFaceCount][4] = {
	{0u, 3u, 2u, 1u}, {4u, 5u, 6u, 7u}, {1u, 2u, 6u, 5u},
	{0u, 4u, 7u, 3u}, {0u, 1u, 5u, 4u}, {3u, 7u, 6u, 2u},
};
constexpr u8 kCubeFaceColors[kCubeCount][kCubeFaceCount] = {
	{4u, 5u, 6u, 7u, 8u, 9u},       // caja A: seis índices RGB distintos
	{10u, 11u, 12u, 13u, 14u, 15u}, // caja B: otros seis índices RGB distintos
};

// El clipping y el clear cubren todo el framebuffer: los giros de ±60° llevan caras y
// paneles fuera del viewport central y deben poder entrar por cualquiera de sus bordes.
constexpr s16 kRoomBoxY = 0;
constexpr u16 kRoomBoxH = kFbH;
// Reparto del relleno de sala (F2) por PLANO: una llamada por plano sobre la caja completa
// (mismo contorno y misma paridad que la llamada de 2 planos; solo cambia la frontera de
// llamada, no la geometria). Cada etapa cuesta ~1/2 del fill de sala y el resultado es
// pixel-identico (sin costura de corte: el area fill no ve bordes artificiales).

// Limpiar toda la guarda de paneles evita restos cuando una barra entra/sale del frustum.
constexpr s16 kPanelBoxX = 0;
constexpr u16 kPanelBoxW = kFbW;      // 352 px (22 palabras)
constexpr s16 kPanelBoxY = 0;
constexpr u16 kPanelBoxH = kFbH;

/// Decimal de `v` en `out` (NUL incluido); devuelve los caracteres escritos.
u8 u16_to_str(u16 v, char* out) {
	char tmp[6] {};
	u8 n = 0u;
	do {
		tmp[n++] = static_cast<char>('0' + static_cast<u8>(v % 10u));
		v = static_cast<u16>(v / 10u);
	} while (v != 0u && n < 5u);
	for (u8 i = 0u; i < n; ++i) {
		out[i] = tmp[n - 1u - i];
	}
	out[n] = '\0';
	return n;
}

/// Decimal con signo (para el paneo del HUD).
u8 i16_to_str(s16 v, char* out) {
	u8 n = 0u;
	if (v < 0) {
		out[n++] = '-';
		v = static_cast<s16>(-v);
	}
	return static_cast<u8>(n + u16_to_str(static_cast<u16>(v), out + n));
}

/// Concatena `src` en `dst`: `len` = longitud actual; devuelve la nueva.
u8 str_cat(char* dst, u8 len, const char* src) {
	for (u8 i = 0u; src[i] != '\0' && len < 60u; ++i) {
		dst[len++] = src[i];
	}
	dst[len] = '\0';
	return len;
}

/// Rellena `text` con espacios hasta `width` caracteres (repintado de ancho fijo del HUD).
void pad_to(char* text, u8 len, u8 width) {
	while (len < width && len < 62u) {
		text[len++] = ' ';
	}
	text[len] = '\0';
}

/// `true` si TODOS los vertices caen dentro del rectangulo (fast path del recorte).
bool box_inside(const eng::retro::Vec2* v, u32 n, const eng::retro::Rect& r) {
	for (u32 k = 0u; k < n; ++k) {
		if (!(v[k].x().v >= r.minX.v && v[k].x().v < r.maxX.v && v[k].y().v >= r.minY.v &&
		      v[k].y().v < r.maxY.v)) {
			return false;
		}
	}
	return true;
}

/// `true` si TODOS los vertices quedan a un mismo lado del rectangulo (poligono fuera).
bool box_outside(const eng::retro::Vec2* v, u32 n, const eng::retro::Rect& r) {
	bool left = true;
	bool right = true;
	bool top = true;
	bool bottom = true;
	for (u32 k = 0u; k < n; ++k) {
		left = left && (v[k].x().v < r.minX.v);
		right = right && (v[k].x().v >= r.maxX.v);
		top = top && (v[k].y().v < r.minY.v);
		bottom = bottom && (v[k].y().v >= r.maxY.v);
	}
	return left || right || top || bottom;
}

struct PanWindowDemo {
	struct ScreenPan {
		s32 x = 0;
		s32 y = 0;
	};
	struct CubeScreenFace {
		s16 x[kMaxVerts] {};
		s16 y[kMaxVerts] {};
		s32 depth = 0;
		u8 count = 0u;
		u8 color = 0u;
	};

	/// Vista mundo→cámara; la misma matriz alimenta el Full y la predicción del ancla.
	static eng::math3d::Affine3<> camera_view(s32 yaw_pan, s32 pitch_pan) {
		eng::math3d::Affine3<> view = eng::math3d::Affine3<>::identity();
		const s16 yaw_steps = static_cast<s16>(-((yaw_pan * 10430) >> 12));
		const s16 pitch_steps = static_cast<s16>((pitch_pan * 10430) >> 12);
		eng::math3d::load_rotate(view.m, eng::retro::turns(static_cast<u16>(pitch_steps)),
					 eng::retro::turns(static_cast<u16>(yaw_steps)),
					 eng::retro::turns(0u));
		return view;
	}

	/// Convierte el movimiento de la cara naranja proyectada en el offset de ventana X/Y.
	/// La aproximación es local a la profundidad del ancla (z≈640), no a toda la escena.
	static ScreenPan projected_anchor_pan(s32 yaw_pan, s32 pitch_pan) {
		const eng::math3d::Affine3<> view = camera_view(yaw_pan, pitch_pan);
		const eng::math3d::Vec3 world[1] {kPanAnchor};
		eng::math3d::Vec3 camera[1] {};
		eng::math3d::mesh_transform(eng::Span<const eng::math3d::Vec3>(world, 1u), view,
					    eng::Span<eng::math3d::Vec3>(camera, 1u));
		if (camera[0].v[2].v <= 4) {
			return {};
		}
		s16 sx = 0;
		s16 sy = 0;
		eng::graphics::project_perspective(camera[0], kFocal, kCx, kCy, sx, sy);
		return ScreenPan {static_cast<s32>(kCx) - sx, static_cast<s32>(kCy) - sy};
	}

	// --- Setup -----------------------------------------------------------------------
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// **Espera de VBlank ACTIVA** (`set_vblank_sleep(false)`, variante 2 de la ficha
		// 212): bajo un SO vivo la ROM reescribe INTENA y apaga VERTB; con el `Wait()` por
		// defecto la tarea se duerme para siempre (cuelgue intermitente ~50%, DT-011). La
		// espera activa re-arma el latido en cada vuelta y es inmune. Mismo setup que
		// 119/126/066/001_adv_walk.
		backend.set_vblank_sleep(false);
		// Chip: 2 buffers de escena de 4 planos (73 KB) + tira del HUD (2 KB) +
		// 2 bloques de copperlist (8 KB) + margen.
		if (!backend.configure_memory({160u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008001u);
			return;
		}

		// La tira del HUD se reserva ANTES de componer: la etapa de display la
		// referencia (con una palabra de guarda delante, que el fetch puede leer).
		m_hud = backend.memory_manager().chip().reserve<eng::PlaneTag>(kHudBytes + 2u, 16);
		if (!m_hud.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008002u);
			return;
		}
		(void)backend.blitter_clear(m_hud.view.subspan(2u, kHudBytes), 1u, kRowBytes,
					    kHudBytes, kFbW, kHudLines);
		(void)m_hud_pf.bind_raw(m_hud.view.data() + 2u, kHudBytes, kFbW, kHudLines, 1u,
					kHudBytes);

		// Planos de patrón de la sala (1 bit): el damero de mezcla A/B de cada plano ocupa
		// 2 filas (periodo 2) — 88 B por plano, 4 filas en total; los lee el Blitter por el
		// canal A de la aplicación por fases. Se construyen AQUÍ una sola vez (CPU).
		m_pattern = backend.memory_manager().chip().reserve<eng::MaskTag>(
			4u * static_cast<eng::u32>(kRowBytes), 16);
		if (!m_pattern.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008005u);
			return;
		}
		const bool d0 = eng::graphics::fill_dither_plane_cpu(
			m_pattern.view.first(2u * kRowBytes), kRowBytes, kRoomDitherA, kRoomDitherB, 0u);
		const bool d1 = eng::graphics::fill_dither_plane_cpu(
			m_pattern.view.subspan(2u * kRowBytes, 2u * kRowBytes), kRowBytes,
			kRoomDitherA, kRoomDitherB, 1u);
		if (!d0 || !d1) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008006u);
			return;
		}

		scene::SceneResources res {};
		res.width = kFbW;
		res.height = kViewportH + kHudLines; // ventana visible 208 + HUD 48 = 256 filas
		res.rows = kFbH;                    // 256 filas físicas: viewport más guarda vertical
		res.planes = kPlanes;
		res.buffers = kBuffers;
		res.copper_bytes = 4096u;
		if (!scene::compose(m_scene, backend.memory_manager(), res, scene::ocs_a500,
				    scene::palette(kPalette, 0u, 16u),
				    [this](scene::Scene& sc) { emit_display(sc); })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008003u);
			return;
		}
		m_ready = true;

		// Los dos buffers parten limpios: el fill limpia por plano, pero el primer
		// frame debe verse bien sin depender del estado de la memoria.
		for (u8 b = 0u; b < kBuffers; ++b) {
			(void)backend.blitter_clear(m_scene.buffer(b), kPlanes, kRowBytes, kSceneBytes,
						    kFbW, kFbH, true);
		}
		draw_hud_static();

		m_scene.takeover(backend); // muestra el buffer 0; el primer Full dibuja el 1
		m_show_buf = 0u;
		m_draw_buf = 1u;
		// El framebuffer tiene 24 filas de guarda arriba y abajo; centrar la ventana antes
		// del primer update evita enseñar una vista desplazada durante el arranque.
		patch_window(kCenterS, kMarginY);

		// Guarda de la emisión (DT-040/DT-032): el slot de BPLCON1 debe apuntar a su MOVE
		// en la lista MATERIALIZADA. Si el Plan reordena/inserta al materializar, el parche
		// por frame escribiria la palabra equivocada (p. ej. el opcode de otro MOVE) y el
		// Copper ejecutaria basura. Falla en init ANTES de corromper nada.
		{
			const eng::Span<u16> words = m_scene.plan().active_span();
			if (!m_bplcon1_slot.valid() || m_bplcon1_slot.opcode_word() >= words.size() ||
			    words[m_bplcon1_slot.opcode_word()] != 0x0102u) {
				eng::debug::mark_failed(g_eng_run_status, 0x00008007u);
				return;
			}
		}
		ENG_PROF_INIT(10u);
		// Controlador de presentación (F2): política + pacing + publicación diferida; el
		// camino asíncrono (cola IRQ) se aconseja solo con Fast RAM (`eng::hw`).
		eng::hw::HwInfo hw {};
		(void)eng::hw::probe(hw);
		m_present.configure(eng::graphics::PresentControllerConfig {kPanPolicyBase, kFakeCap},
				    hw);
		const ScreenPan anchor = projected_anchor_pan(m_yaw_pan, m_pitch_pan);
		m_present.reset(anchor.x, anchor.y);
		eng::debug::mark_ready(g_eng_run_status, 0u);
	}

	// --- Frame: politica Full/Fake + render o paneo ----------------------------------
	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready || !m_scene.ok()) {
			return;
		}
		const u32 f = context.frame.frame_index;
		apply_camera_motion();
#if defined(K080_OFFSET_TEST)
		// PRUEBA DE AISLAMIENTO (VALIDATION §3): cámara congelada y ciclo de offsets 0..16
		// offsets 0..16 (8 frames cada uno) sobre la ventana. Con el contenido fijo,
		// cada desplazamiento pedido debe producir EXACTAMENTE ese desplazamiento.
		m_yaw_pan = -128;
		m_pitch_pan = 0;
#endif
#if defined(K080_PAN_FREEZE)
		// Vista estática para verificación geométrica (captura sin movimiento de cámara).
		if constexpr (kForcePitch) {
			m_yaw_pan = 0;
			m_pitch_pan = static_cast<s16>(K080_PAN_FREEZE);
		} else {
			m_yaw_pan = static_cast<s16>(K080_PAN_FREEZE);
			m_pitch_pan = 0;
		}
#endif
		// El pan de ventana no es el ángulo: es el desplazamiento estimado del panel naranja
		// al proyectarlo en pantalla. Cada Full vuelve a anclar esta estimación a su pose real.
		const ScreenPan window_pan = projected_anchor_pan(m_yaw_pan, m_pitch_pan);
		const s32 pan_x = window_pan.x;
		const s32 pan_y = window_pan.y;
		ENG_PROF_FRAME();
		ENG_PROF_BEGIN(kProfFrame);
		// **Presentación (F2)**: al inicio de campo publica el Full pendiente y la política
		// decide si toca otro Full o una Fake. En el modo rebanado A/B se conservan el Full
		// anterior y sus desplazamientos mientras avanzan las etapas del nuevo render.
		eng::graphics::PresentStep pstep {};
		if (m_stage == Stage::None) {
			pstep = m_present.begin(pan_x, pan_y);
			if (pstep.publish) {
				m_show_buf = m_pending_buf;
				m_draw_buf = static_cast<u8>(m_pending_buf ^ 1u);
			}
			if (pstep.full) {
				if (kSliceEnabled) {
					// Cada Full se reparte entre VBlanks; el recorrido limita el delta para que
					// la ventana Fake permanezca dentro de las guardas X/Y durante las etapas.
					m_stage = Stage::Clip;
					m_stage_yaw = m_yaw_pan;
					m_stage_pitch = m_pitch_pan;
					m_stage_cube_phase = m_cube_phase;
					m_stage_window_x = pan_x;
					m_stage_window_y = pan_y;
					m_stage_cycles = 0u;
				} else {
					// Ruta de diagnóstico sin rebanado: las etapas se encadenan en este update.
					ENG_PROF_BEGIN(kProfFull);
#if defined(ENG_AMIGA)
					const eng::u32 full_c0 = eng::debug::prof_clock();
#endif
					m_stage_cube_phase = m_cube_phase;
					render_full_now(backend, m_yaw_pan, m_pitch_pan);
					ENG_PROF_END(kProfFull);
					m_pending_buf = m_draw_buf;
					m_present.commit(pan_x, pan_y);
#if defined(ENG_AMIGA)
					m_present.observe_full(eng::debug::prof_clock() - full_c0);
#endif
				}
			}
		} else {
			// Pasada en curso: ventana de la presentacion sobre el Full publicado
			// (clampeada al margen de la politica). El paneo puede ir por delante del
			// Full en curso; al publicarse, `begin()` devuelve el offset real.
			s32 off_x = pan_x - m_present.shown_pan_x();
			s32 off_y = pan_y - m_present.shown_pan_y();
			if (off_x > kPanPolicyBase.margin_x) {
				off_x = kPanPolicyBase.margin_x;
			}
			if (off_x < -kPanPolicyBase.margin_x) {
				off_x = -kPanPolicyBase.margin_x;
			}
			if (off_y > kPanPolicyBase.margin_y) {
				off_y = kPanPolicyBase.margin_y;
			}
			if (off_y < -kPanPolicyBase.margin_y) {
				off_y = -kPanPolicyBase.margin_y;
			}
			pstep.offset_x = static_cast<s16>(off_x);
			pstep.offset_y = static_cast<s16>(off_y);
			pstep.publish = false;
			pstep.full = false;
		}
#if defined(K080_OFFSET_TEST)
		{
			static constexpr s16 kCycle[17] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
							   12, 13, 14, 15, 16};
			pstep.full = false;
			pstep.publish = false;
			if constexpr (kForcePitch) {
				pstep.offset_y = static_cast<s16>(kCycle[(f / 8u) % 9u] - 4);
			} else {
				pstep.offset_x = kCycle[(f / 8u) % 17u];
			}
		}
#endif

		// Ventana del frame PRESENTADO: el paneo avanzado respecto al Full publicado,
		// clampeado al margen (S en 1..31 = off −15..+15). El render de un Full nuevo no
		// cambia lo mostrado hasta su publicación.
		const u16 s = static_cast<u16>(kCenterS + pstep.offset_x);
		const u16 t = static_cast<u16>(static_cast<s32>(kMarginY) + pstep.offset_y);
		if (pstep.publish) {
			++m_fulls;
		} else {
			++m_fakes;
		}
		if (m_stage != Stage::None) {
			run_stage(backend); // Clip en el mismo update en que se pidió; luego una etapa
		}
		// El HUD se repinta cada 32 frames (~0,6 s con el bucle rebanado): suficiente para
		// telemetria y deja el camino del frame en 1 campo (una linea de texto por
		// palabras cuesta ~190k ciclos medidos con el display activo).
#if !defined(K080_SKIP_HUD)
		if ((f & 31u) == 0u) {
			ENG_PROF_BEGIN(kProfHud);
			draw_hud_dynamic(f, pstep);
			ENG_PROF_END(kProfHud);
		}
#endif
		ENG_PROF_BEGIN(kProfPatch);
#if !defined(K080_SKIP_PATCH)
		patch_window(s, t);
#endif
		ENG_PROF_END(kProfPatch);

		// El verificador lee el eje de la pasada pura; Lissajous publica X y se contrasta por imagen.
		const bool active_pitch = m_motion_mode == MotionMode::PitchSweep;
		const s16 active_offset = active_pitch ? pstep.offset_y : pstep.offset_x;
		const s32 active_pan = active_pitch ? pan_y : pan_x;
		// A1=Full publicado; A2=Fake; A3=se inicia un Full por coste/guarda mientras el
		// buffer anterior sigue visible con offset clampado. El verificador distingue A3.
		const u8 status_tag = pstep.publish ? 0xa1u : (pstep.full ? 0xa3u : 0xa2u);
		g_eng_run_status.detail =
			(static_cast<u32>(status_tag) << 24u) |
			(static_cast<u32>(static_cast<u8>(active_offset + 128)) << 16u) |
			(static_cast<u32>(static_cast<u16>(active_pan)) & 0xffffu);

#if !defined(K080_OFFSET_TEST) && !defined(K080_PAN_FREEZE)
		advance_camera_motion();
#endif
#if !defined(K080_OFFSET_TEST) && !K080_CUBE_FREEZE
		m_cube_phase = static_cast<u16>((m_cube_phase + kCubePhaseStep) &
						(eng::retro::kAngleSteps - 1u));
#endif
		ENG_PROF_END(kProfFrame);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Escala la tabla Q12 a la unidad de paneo: 268 representa 60° de cámara.
	static s32 lissajous_pan(u16 phase, s32 amplitude) {
		const s32 sine = eng::retro::kSinQ12[phase & (eng::retro::kAngleSteps - 1u)];
		return (sine * amplitude) >> 12;
	}

	/// Prepara la pose del update actual usando tabla de seno, sin trigonometría en runtime.
	void apply_camera_motion() {
		switch (m_motion_mode) {
		case MotionMode::YawSweep:
			m_yaw_pan = m_sweep_pan;
			m_pitch_pan = 0;
			break;
		case MotionMode::PitchSweep:
			m_yaw_pan = 0;
			m_pitch_pan = m_sweep_pan;
			break;
		case MotionMode::Lissajous:
			m_yaw_pan = lissajous_pan(m_lissajous_phase, kLissajousPan);
			m_pitch_pan = lissajous_pan(static_cast<u16>(m_lissajous_phase * 2u),
						   kLissajousPan);
			break;
		}
	}

	/// Recorre yaw −60°..+60°, después pitch −60°..+60° y cierra con una curva 1:2.
	void advance_camera_motion() {
		if (m_motion_mode == MotionMode::Lissajous) {
			m_lissajous_phase = static_cast<u16>(m_lissajous_phase + kLissajousPhaseStep);
			++m_lissajous_frames;
			if (m_lissajous_frames >= kLissajousFrames) {
				m_lissajous_phase = 0u;
				m_lissajous_frames = 0u;
				if (!kForceLissajous) {
					m_motion_mode = MotionMode::YawSweep;
					m_sweep_pan = 0;
					m_sweep_dir = 1;
				}
			}
			return;
		}

		const s32 previous = m_sweep_pan;
		const s32 step = m_motion_mode == MotionMode::YawSweep ? kYawSweepStep : kPitchSweepStep;
		s32 next = previous + m_sweep_dir * step;
		if (next >= kSweepPan) {
			next = kSweepPan;
			m_sweep_dir = -1;
		} else if (next <= -kSweepPan) {
			next = -kSweepPan;
			m_sweep_dir = 1;
		}
		if (!kForcePitch && !kForceLissajous && m_sweep_dir > 0 && previous < 0 && next >= 0) {
			m_sweep_pan = 0;
			m_sweep_dir = 1;
			if (m_motion_mode == MotionMode::YawSweep) {
				m_motion_mode = MotionMode::PitchSweep;
			} else {
				m_motion_mode = MotionMode::Lissajous;
				m_lissajous_phase = 0u;
				m_lissajous_frames = 0u;
			}
			return;
		}
		m_sweep_pan = next;
	}

	// --- Display: emitido una vez; punteros y BPLCON1 parcheables --------------------
	void emit_display(scene::Scene& sc) {
		auto& s = sc.scheduler();
		s.move(eng::copper::Register::DMACON,
		       static_cast<u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
				eng::copper::DmaCopper | eng::copper::DmaBitplane));
		s.move(eng::copper::Register::BPLCON0,
		       scene::bplcon0_for(scene::SceneMode::Standard, kPlanes));
		// El fino de BPLCON1 se parchea con un `MoveSlot` explicito sobre la lista activa
		// (mismo camino que los punteros): el `PatchHandle` de `patchable()` reescribe a
		// traves del emisor del Plan y en esta escena (sin `commit()` por frame) acaba
		// tocando la palabra de opcode de la lista (corrupcion + ciclo de IRQ agotado).
		m_bplcon1_slot = s.move_at(eng::copper::Register::BPLCON1, 0x0000u);
		s.move(eng::copper::Register::BPLCON2, 0x0000u);
		s.move(eng::copper::Register::BPL1MOD, 0x0000u);
		s.move(eng::copper::Register::BPL2MOD, 0x0000u);
		s.move(eng::copper::Register::DIWSTRT, kDiwstrt);
		s.move(eng::copper::Register::DIWSTOP, kDiwstop);
		s.move(eng::copper::Register::DDFSTRT, kDdfstrt);
		s.move(eng::copper::Register::DDFSTOP, kDdfstop);
		const eng::ChipPlaneView planes = sc.chip_planes();
		for (u8 p = 0u; p < kPlanes; ++p) {
			const eng::Address<eng::MemoryKind::Chip> ip =
				planes.address(static_cast<s32>(p) * sc.plane_bytes());
			m_plane_hi[p] = s.move_at(eng::copper::bitplane_pointer_high_register(p),
						  static_cast<u16>(ip.value >> 16u));
			m_plane_lo[p] = s.move_at(eng::copper::bitplane_pointer_low_register(p),
						  static_cast<u16>(ip.value & 0xffffu));
			// Registra el par como parche de la escena: `takeover()` patchea los
			// punteros al buffer mostrado (sin esto, el Scene escribe con slots
			// invalidos y corrompe la primera palabra de la lista).
			sc.set_plane_patch(p, scene::Patch32 {m_plane_hi[p], m_plane_lo[p]});
		}
		// Franja del HUD: a partir de la linea 252, UN plano con la tira, sin fino y
		// con COLOR00/01 propios (la paleta de escena se re-emite al principio de la
		// lista cada frame: el override es local a la banda).
		s.wait_line(static_cast<u8>(kHudLine));
		s.move(eng::copper::Register::BPLCON0,
		       scene::bplcon0_for(scene::SceneMode::Standard, 1u));
		s.move(eng::copper::Register::BPLCON1, 0x0000u);
		s.move(eng::copper::color_register(0u), kHudColor0);
		s.move(eng::copper::color_register(1u), kHudColor1);
		s.move_bitplane_pointer(0u, m_hud.mem_view().address(2));
	}

	// --- Parcheo de la ventana (por frame) -------------------------------------------
	/// Escribe `BPLxPT` (coarse) y `BPLCON1` (fino) para mostrar la ventana `s` del
	/// buffer `buf`. Convencion: `fine_scroll_coarse(s)` da el puntero y `fine_delay(s)`
	/// el retardo de BPLCON1 (display_start == s, continuo 15->0).
	void patch_window(u16 s, u16 t) {
		const eng::PlaneBytes planes = m_scene.buffer(m_show_buf);
		const u32 coarse_bytes = static_cast<u32>(eng::graphics::fine_scroll_coarse(s)) >> 3u;
		const u32 row_offset = static_cast<u32>(t) * kRowBytes;
#if !defined(K080_PATCH_NO_PLANES)
		eng::Span<u16> words = m_scene.plan().active_span();
		for (u8 p = 0u; p < kPlanes; ++p) {
			const u8* pc = planes.data() + static_cast<u32>(p) * kSceneBytes + row_offset +
					coarse_bytes;
			const u32 addr =
				eng::Address<eng::MemoryKind::Chip>::from_storage(pc).value;
			eng::copper::patch_data(words, m_plane_hi[p], static_cast<u16>(addr >> 16u));
			eng::copper::patch_data(words, m_plane_lo[p], static_cast<u16>(addr & 0xffffu));
		}
#endif
#if !defined(K080_PATCH_NO_BPLCON1)
		const u16 fine = eng::graphics::fine_delay(s);
		eng::copper::patch_data(m_scene.plan().active_span(), m_bplcon1_slot,
					static_cast<u16>(fine | (fine << 4u)));
#endif
	}

	// --- Pasada completa por ETAPAS (F2) ----------------------------------------------
	// Clip -> sala (dos planos) -> paneles; entre etapas se muestra el Full anterior
	// desplazado. Los recorridos limitan la velocidad para que el delta permanezca dentro
	// de ambas guardas durante la publicación diferida.
	enum class Stage : u8 { None, Clip, Room0, Room1, Panels, Cubes };
	void stage_clip(s32 pan_x, s32 pan_y) {
		// Sin `wait_blitter` inicial: los fills son sincronos (esperan al Blitter al
		// terminar) y el buffer de dibujo no se publica hasta completar la pasada.
		// El paneo de cámara se calibra a 4096 pasos/vuelta; 268 unidades son ±60°.
		const eng::math3d::Affine3<> view = camera_view(pan_x, pan_y);
		const bool use_general_clip = pan_x < -kYawCullPanLimit || pan_x > kYawCullPanLimit ||
					      pan_y < -64 || pan_y > 64;
		using CT = eng::math::scalar_traits<eng::coord>;
		const eng::graphics::Camera3D<eng::coord> camera {
			CT::from_int(kFocal), kCx, kCy, CT::from_int(4),
			CT::from_int(kFbW / 2u), CT::from_int(kFbH / 2u)};
#if !defined(K080_NO_CULL)
		// --- Culling de entrada (3D_GAME_ARCHITECTURE §Visibilidad) -------------------------
		// Mismas formulas que `eng::math3d::aabb_visible` (frustum.hpp): AABB de la cara
		// contra la cuna yaw + franja + profundidad. Aqui el escalar partido de la demo
		// (ratio q12 de la MISMA matriz `view` x longitud entera) se resuelve con
		// `mul_wide` (muls.w): la via generica usa un unico escalar; promover al engine
		// como vista `ratio/longitud` cuando haya un segundo consumidor (deuda anotada).
		// Conservador: centro y semiextensiones DOBLADOS (2c = min+max, 2h = max-min) para
		// no dividir a 2; margen de 3 unidades de mundo absorbe la cuantizacion.
		const s16 sin_yaw = view.m.m[2][0].v; // q12: sin(yaw) del propio view
		const s16 cos_yaw = view.m.m[0][0].v; // q12
		constexpr s32 kHalfWQ12 =
			(static_cast<s32>(kFbW / 2u) << 12) / static_cast<s32>(kFocal); // incluye la guarda X
		const auto mul_q12 = [](s16 a, s16 b) -> s16 {
			return static_cast<s16>(eng::math::mul_wide(a, b) >> 12);
		};
		const s32 lx = cos_yaw + mul_q12(static_cast<s16>(kHalfWQ12), sin_yaw);
		const s32 lz = mul_q12(static_cast<s16>(kHalfWQ12), cos_yaw) - sin_yaw;
		const s32 rx = mul_q12(static_cast<s16>(kHalfWQ12), sin_yaw) - cos_yaw;
		const s32 rz = mul_q12(static_cast<s16>(kHalfWQ12), cos_yaw) + sin_yaw;
		constexpr s32 kMarginQ2 = 3 * 8192;  // 2 x 3 unidades de mundo en q12
		constexpr s32 kMarginY2 = 3 * 2;     // 2 x 3 unidades de mundo (franja, crudo)
		constexpr s32 kNearQ13 = 4 * 8192;   // 2 x near(4) x 2^12
		constexpr s32 kFarQ13 = 1536 * 8192; // 2 x far(1536) x 2^12
		constexpr s32 kYB2 =
			(static_cast<s32>(kFbH / 2u) * 1536 / static_cast<s32>(kFocal)) * 2;
#endif
		// Proyeccion + recorte 2D. El recorte de Sutherland-Hodgman (4 pasadas) solo se
		// paga cuando la cara CRUZA el borde: si su AABB esta dentro se envia tal cual y
		// si esta fuera se descarta (fast path; la mayoria de las caras en la vista tipica).
		const eng::retro::Rect clip =
			eng::retro::rect(0, 0, static_cast<s16>(kFbW), static_cast<s16>(kFbH));
		const eng::retro::Rect room_box =
			eng::retro::rect(0, kRoomBoxY, static_cast<s16>(kFbW),
					 static_cast<s16>(kRoomBoxY) + static_cast<s16>(kRoomBoxH));
		eng::retro::Vec2 in[kMaxVerts];
		eng::retro::Vec2 tmp[kMaxVerts];

		u32 verts = 0u;

		ENG_PROF_BEGIN(kProfClip);
		for (u32 i = 0u; i < kFaceCount; ++i) {
			// El culling yaw-only solo se usa en su rango A/B validado; pitch y giros amplios
			// se resuelven con clipping de cámara para no descartar caras que entran al frustum.
#if !defined(K080_NO_CULL)
			if (pan_y == 0 && pan_x >= -kYawCullPanLimit && pan_x <= kYawCullPanLimit) {
			{
				// AABB crudo de la cara; centro/extension DOBLADOS (2c = min+max,
				// 2h = max-min) para no dividir a 2. Test conservador (cruzar = visible).
				const s16 x0 = eng::math3d::coord_at(kFaces[i].v[0], 0);
				const s16 x1 = eng::math3d::coord_at(kFaces[i].v[1], 0);
				const s16 x2 = eng::math3d::coord_at(kFaces[i].v[2], 0);
				const s16 x3 = eng::math3d::coord_at(kFaces[i].v[3], 0);
				const s16 y0 = eng::math3d::coord_at(kFaces[i].v[0], 1);
				const s16 y1 = eng::math3d::coord_at(kFaces[i].v[1], 1);
				const s16 y2 = eng::math3d::coord_at(kFaces[i].v[2], 1);
				const s16 y3 = eng::math3d::coord_at(kFaces[i].v[3], 1);
				const s16 z0 = eng::math3d::coord_at(kFaces[i].v[0], 2);
				const s16 z1 = eng::math3d::coord_at(kFaces[i].v[1], 2);
				const s16 z2v = eng::math3d::coord_at(kFaces[i].v[2], 2);
				const s16 z3 = eng::math3d::coord_at(kFaces[i].v[3], 2);
				const auto mn = [](s16 a, s16 b) { return a < b ? a : b; };
				const auto mx = [](s16 a, s16 b) { return a > b ? a : b; };
				const s32 c2x = mn(mn(x0, x1), mn(x2, x3)) + mx(mx(x0, x1), mx(x2, x3));
				const s32 e2x = mx(mx(x0, x1), mx(x2, x3)) - mn(mn(x0, x1), mn(x2, x3));
				const s32 c2y = mn(mn(y0, y1), mn(y2, y3)) + mx(mx(y0, y1), mx(y2, y3));
				const s32 e2y = mx(mx(y0, y1), mx(y2, y3)) - mn(mn(y0, y1), mn(y2, y3));
				const s32 c2z = mn(mn(z0, z1), mn(z2v, z3)) + mx(mx(z0, z1), mx(z2v, z3));
				const s32 e2z = mx(mx(z0, z1), mx(z2v, z3)) - mn(mn(z0, z1), mn(z2v, z3));
				const auto mw = [](s32 a, s32 b) -> s32 {
					return eng::math::mul_wide(static_cast<s16>(a), static_cast<s16>(b));
				};
				const auto mabs = [](s32 v) -> s32 { return v < 0 ? -v : v; };
				const s32 l2 = mw(c2x, lx) + mw(c2z, lz);
				const s32 le2 = mw(e2x, mabs(lx)) + mw(e2z, mabs(lz));
				const s32 r2 = mw(c2x, rx) + mw(c2z, rz);
				const s32 re2 = mw(e2x, mabs(rx)) + mw(e2z, mabs(rz));
				const s32 zq2 = mw(c2x, sin_yaw) + mw(c2z, cos_yaw);
				const s32 zeq2 = mw(e2x, mabs(sin_yaw)) + mw(e2z, mabs(cos_yaw));
				if (l2 + le2 + kMarginQ2 < 0 || r2 + re2 + kMarginQ2 < 0 ||
				    c2y + e2y + kMarginY2 < -kYB2 ||
				    c2y - e2y - kMarginY2 > kYB2 ||
				    zq2 + zeq2 + kMarginQ2 < kNearQ13 ||
				    zq2 - zeq2 - kMarginQ2 > kFarQ13) {
					m_fn[i] = 0u; // cull: la cara no se transforma/proyecta/recorta/rellena
					continue;
				}
			}
			}
#endif
			eng::math3d::Vec3 cam[4];
			eng::math3d::mesh_transform(eng::Span<const eng::math3d::Vec3>(kFaces[i].v, 4),
						    view, eng::Span<eng::math3d::Vec3>(cam, 4));
			u32 n = 4u;
			const bool face_needs_camera_clip =
				use_general_clip ||
				!eng::graphics::camera_vertices_at_or_beyond_near(camera, cam, 4u);
			if (face_needs_camera_clip) {
				// La cámara de ángulo moderado también puede cruzar near con las caras cercanas
				// de la sala (z=32): recortar antes de dividir evita proyecciones invertidas.
				eng::graphics::CamVertex<eng::coord> cam_face[4];
				eng::graphics::CamVertex<eng::coord> clipped[16];
				for (u8 k = 0u; k < 4u; ++k) {
					cam_face[k] = {cam[k].v[0], cam[k].v[1], cam[k].v[2]};
				}
				const u8 clipped_n = eng::graphics::clip_camera_polygon(
					cam_face, 4u, camera, clipped, 16u);
				if (clipped_n < 3u) {
					m_fn[i] = 0u;
					continue;
				}
				n = clipped_n;
				for (u8 k = 0u; k < clipped_n; ++k) {
					s16 sx = 0, sy = 0;
					if (!eng::graphics::project_camera_vertex(clipped[k], camera, sx, sy)) {
						n = 0u;
						break;
					}
					in[k] = eng::retro::v2(sx, sy);
				}
				if (n < 3u) {
					m_fn[i] = 0u;
					continue;
				}
			} else {
				// En la franja central sin cruces de near, la proyección directa es el fast path.
				for (u32 k = 0u; k < 4u; ++k) {
					s16 sx = 0;
					s16 sy = 0;
					eng::graphics::project_perspective(cam[k], kFocal, kCx, kCy, sx, sy);
					in[k] = eng::retro::v2(sx, sy);
			}
			}
			if (box_inside(in, n, clip)) {
				// Fast path: la cara completa cabe en el framebuffer sobredimensionado.
			} else if (box_outside(in, n, clip)) {
				n = 0u;
			} else {
				n = eng::retro::clip_polygon(
					clip, in, tmp, n,
					static_cast<u8>(eng::retro::PF_LEFT | eng::retro::PF_TOP |
							eng::retro::PF_RIGHT | eng::retro::PF_BOTTOM));
			}
			if (n < 3u) {
				m_fn[i] = 0u;
				continue;
			}
			if (i < kRoomFaceCount) {
				// Las caras de la sala comparten el clip y la caja del framebuffer.
				if (!box_inside(in, n, room_box)) {
					n = box_outside(in, n, room_box)
						    ? 0u
						    : eng::retro::clip_polygon(
							      room_box, in, tmp, n,
							      static_cast<u8>(eng::retro::PF_LEFT |
									      eng::retro::PF_TOP |
									      eng::retro::PF_RIGHT |
									      eng::retro::PF_BOTTOM));
				}
				if (n < 3u) {
					m_fn[i] = 0u;
					continue;
				}
				// Compacta vertices consecutivos duplicados (los crea el clip en los
				// bordes): una arista degenerada no aporta cruce al area fill y puede
				// desequilibrar la paridad de la fila (DT-042).
				u32 w = 0u;
				for (u32 k = 0u; k < n; ++k) {
					const s16 x = in[k].x().v;
					const s16 y = static_cast<s16>(in[k].y().v - kRoomBoxY);
					if (w != 0u && m_fx[i][w - 1u] == x && m_fy[i][w - 1u] == y) {
						continue;
					}
					m_fx[i][w] = x;
					m_fy[i][w] = y;
					++w;
				}
				if (w >= 2u && m_fx[i][0] == m_fx[i][w - 1u] &&
				    m_fy[i][0] == m_fy[i][w - 1u]) {
					--w;
				}
				m_fn[i] = static_cast<u8>(w);
				if (w < 3u) {
					m_fn[i] = 0u;
					continue;
				}
				verts += w;
			} else {
				// Paneles: se guardan en coords globales; el bloque de paneles los
				// recorta a su caja fija y los desplaza. Se compactan duplicados
				// consecutivos igual que en la sala (paridad del area fill).
				u32 w = 0u;
				for (u32 k = 0u; k < n; ++k) {
					const s16 x = in[k].x().v;
					const s16 y = in[k].y().v;
					if (w != 0u && m_fx[i][w - 1u] == x && m_fy[i][w - 1u] == y) {
						continue;
					}
					m_fx[i][w] = x;
					m_fy[i][w] = y;
					++w;
				}
				if (w >= 2u && m_fx[i][0] == m_fx[i][w - 1u] &&
				    m_fy[i][0] == m_fy[i][w - 1u]) {
					--w;
				}
				m_fn[i] = static_cast<u8>(w > 255u ? 255u : w);
				verts += w;
			}
		}
		m_dbg_verts = static_cast<u8>(verts > 255u ? 255u : verts);
		// Caras que llegan al relleno (todas las que sobreviven al clip del FB; el
		// recorte fino por caja/banda ocurre en las etapas de relleno).
		u8 faces = 0u;
		for (u32 i = 0u; i < kFaceCount; ++i) {
			if (m_fn[i] >= 3u) {
				++faces;
			}
		}
		m_dbg_faces = faces;
		ENG_PROF_END(kProfClip);
		prepare_cube_faces(view, camera, clip);
	}

	/// Proyecta las cajas compuestas, recortando cada cara antes de dividir por z. Las caras
	/// visibles se ordenan lejos→cerca; el relleno CPU sobrescribe el índice completo de color.
	void prepare_cube_faces(const eng::math3d::Affine3<>& view,
				const eng::graphics::Camera3D<eng::coord>& camera,
				const eng::retro::Rect& clip) {
		m_cube_face_count = 0u;
		const u16 phase = m_stage_cube_phase & (eng::retro::kAngleSteps - 1u);
		const s32 sin_q12 = eng::retro::kSinQ12[phase];
		const s32 cos_q12 = eng::retro::kSinQ12[
			static_cast<u16>(phase + eng::retro::kAngleSteps / 4u) &
			(eng::retro::kAngleSteps - 1u)];
		const s16 orbit_x = static_cast<s16>((cos_q12 * kCubeOrbitX) >> 12);
		const s16 orbit_z = static_cast<s16>((sin_q12 * kCubeOrbitZ) >> 12);
		const s16 centers[kCubeCount][3] = {
			{K080_CUBE_PROBE ? static_cast<s16>(0) : orbit_x, 0,
			 static_cast<s16>(kCubeOrbitCenterZ + (K080_CUBE_PROBE ? 0 : orbit_z))},
			{static_cast<s16>(-orbit_x), 0, static_cast<s16>(kCubeOrbitCenterZ - orbit_z)},
		};
		using CT = eng::math::scalar_traits<eng::coord>;

		const u8 cube_count = K080_CUBE_PROBE ? 1u : kCubeCount;
		for (u8 cube = 0u; cube < cube_count; ++cube) {
			const u16 rate = (cube == 0u) ? 1u : 2u;
			const u16 spin = static_cast<u16>((m_stage_cube_phase * rate) &
							   (eng::retro::kAngleSteps - 1u));
			eng::math3d::Affine3<> model = eng::math3d::Affine3<>::identity();
			eng::math3d::load_rotate(
				model.m,
				eng::retro::turns(static_cast<u16>((spin * (cube == 0u ? 2u : 3u)) & 4095u)),
				eng::retro::turns(static_cast<u16>((spin * (cube == 0u ? 3u : 5u)) & 4095u)),
				eng::retro::turns(static_cast<u16>((spin * (cube == 0u ? 5u : 7u)) & 4095u)));
			eng::math3d::Vec3 world[8] {};
			eng::math3d::Vec3 cam_vertices[8] {};
			eng::math3d::mesh_transform(
				eng::Span<const eng::math3d::Vec3>(kCubeVertices, 8u), model,
				eng::Span<eng::math3d::Vec3>(world, 8u));
			for (u8 v = 0u; v < 8u; ++v) {
				world[v].v[0] = world[v].v[0] + CT::from_int(centers[cube][0]);
				world[v].v[1] = world[v].v[1] + CT::from_int(centers[cube][1]);
				world[v].v[2] = world[v].v[2] + CT::from_int(centers[cube][2]);
			}
			eng::math3d::mesh_transform(eng::Span<const eng::math3d::Vec3>(world, 8u), view,
						    eng::Span<eng::math3d::Vec3>(cam_vertices, 8u));

			for (u8 f = 0u; f < kCubeFaceCount; ++f) {
				const u8 a = kCubeFaceIndices[f][0];
				const u8 b = kCubeFaceIndices[f][1];
				const u8 c = kCubeFaceIndices[f][2];
				if (!cube_face_front_facing(cam_vertices[a], cam_vertices[b], cam_vertices[c])) {
					continue;
				}
				eng::graphics::CamVertex<eng::coord> input[4] {};
				for (u8 k = 0u; k < 4u; ++k) {
					const eng::math3d::Vec3& v = cam_vertices[kCubeFaceIndices[f][k]];
					input[k] = {v.v[0], v.v[1], v.v[2]};
				}
				eng::graphics::CamVertex<eng::coord> clipped[16] {};
				u32 n = eng::graphics::clip_camera_polygon(input, 4u, camera, clipped, 16u);
				if (n < 3u) continue;
				eng::retro::Vec2 screen[16] {};
				eng::retro::Vec2 tmp[16] {};
				s32 depth = 0;
				const u32 depth_vertices = n;
				bool project_ok = true;
				for (u8 k = 0u; k < n; ++k) {
					s16 sx = 0, sy = 0;
					project_ok = eng::graphics::project_camera_vertex(clipped[k], camera, sx, sy) &&
						     project_ok;
					screen[k] = eng::retro::v2(sx, sy);
					depth += CT::to_int(clipped[k].z);
				}
				if (!project_ok) continue;
				if (!box_inside(screen, n, clip)) {
					if (box_outside(screen, n, clip)) continue;
					n = eng::retro::clip_polygon(
						clip, screen, tmp, n,
						static_cast<u8>(eng::retro::PF_LEFT | eng::retro::PF_TOP |
								eng::retro::PF_RIGHT | eng::retro::PF_BOTTOM));
				}
				if (n < 3u) continue;
				s16 xs[kMaxVerts] {}, ys[kMaxVerts] {};
				for (u8 k = 0u; k < n; ++k) {
					// `clip` conserva su borde máximo geométrico (width/height); el raster CPU
					// recibe índices de píxel, así el borde inclusivo cae en width-1/height-1.
					const s16 x = screen[k].x().v;
					const s16 y = screen[k].y().v;
					xs[k] = (x >= static_cast<s16>(kFbW)) ? static_cast<s16>(kFbW - 1u) : x;
					ys[k] = (y >= static_cast<s16>(kFbH)) ? static_cast<s16>(kFbH - 1u) : y;
				}
				if (eng::graphics::screen_area2(xs, ys, static_cast<u8>(n)) == 0) continue;

				CubeScreenFace candidate {};
				candidate.count = static_cast<u8>(n);
				candidate.color = kCubeFaceColors[cube][f];
				candidate.depth = depth / static_cast<s32>(depth_vertices);
				for (u8 k = 0u; k < candidate.count; ++k) {
					candidate.x[k] = xs[k];
					candidate.y[k] = ys[k];
				}
				u8 insert = m_cube_face_count;
				while (insert > 0u && m_cube_faces[insert - 1u].depth < candidate.depth) {
					m_cube_faces[insert] = m_cube_faces[insert - 1u];
					--insert;
				}
				m_cube_faces[insert] = candidate;
				++m_cube_face_count;
			}
		}
	}

	/// Normal del triángulo en cámara · vector hacia el origen. El cubo es pequeño y cercano
	/// a z<700, así los productos y la suma caben en s32/`mul_wide` (sin `__muldi3` en 68000).
	static bool cube_face_front_facing(const eng::math3d::Vec3& a,
					   const eng::math3d::Vec3& b,
					   const eng::math3d::Vec3& c) {
		const s16 abx = static_cast<s16>(b.v[0].v - a.v[0].v);
		const s16 aby = static_cast<s16>(b.v[1].v - a.v[1].v);
		const s16 abz = static_cast<s16>(b.v[2].v - a.v[2].v);
		const s16 acx = static_cast<s16>(c.v[0].v - a.v[0].v);
		const s16 acy = static_cast<s16>(c.v[1].v - a.v[1].v);
		const s16 acz = static_cast<s16>(c.v[2].v - a.v[2].v);
		const s32 nx = eng::math::mul_wide(aby, acz) - eng::math::mul_wide(abz, acy);
		const s32 ny = eng::math::mul_wide(abz, acx) - eng::math::mul_wide(abx, acz);
		const s32 nz = eng::math::mul_wide(abx, acy) - eng::math::mul_wide(aby, acx);
		const s16 to_camera_x = static_cast<s16>(-a.v[0].v);
		const s16 to_camera_y = static_cast<s16>(-a.v[1].v);
		const s16 to_camera_z = static_cast<s16>(-a.v[2].v);
		const s32 facing = eng::math::mul_wide(static_cast<s16>(nx), to_camera_x) +
				   eng::math::mul_wide(static_cast<s16>(ny), to_camera_y) +
				   eng::math::mul_wide(static_cast<s16>(nz), to_camera_z);
		return facing > 0;
	}

	/// Rellena UN plano de la sala (0 o 1) sobre la caja completa del tubo. El color se
	/// remapea a bit 0 de la llamada (`color >> plane`): el backend prueba el bit local 0
	/// del plano que se le pasa, asi que cada llamada de 1 plano pide su bit como bit 0.
	void stage_room(eng::amiga::AmigaBackend& backend, u8 plane) {
		const eng::PlaneBytes buf = m_scene.buffer(m_draw_buf);
		eng::PlaneBytes dst =
			buf.subspan(static_cast<u32>(plane) * kSceneBytes +
				    static_cast<u32>(kRoomBoxY) * kRowBytes);
#if defined(K080_SKIP_FILL)
		(void)backend.blitter_clear(dst, 1u, kRowBytes, kSceneBytes, kFbW, kRoomBoxH,
					    false);
#else
		// Los vertices ya estan en coords locales de la caja de sala (sin recorte extra:
		// el contorno y la paridad son los mismos que en la llamada de 2 planos). Cada plano
		// se limpia completo de forma independiente para eliminar restos del contorno anterior.
		eng::graphics::PlaneFillBuilder<kFaceCount> fb {};
		for (u32 i = 0u; i < kRoomFaceCount; ++i) {
			if (m_fn[i] < 3u) {
				continue;
			}
			const u8 color = (plane == 0u)
						 ? static_cast<u8>(kFaces[i].color & 0x1u)
						 : static_cast<u8>((kFaces[i].color >> 1u) & 0x1u);
			(void)fb.submit(m_fx[i], m_fy[i], m_fn[i], color);
		}
		const eng::MaskBytes pat =
			kRoomDither
				? (plane == 0u
					   ? m_pattern.view.first(2u * kRowBytes).as_const()
					   : m_pattern.view.subspan(2u * kRowBytes, 2u * kRowBytes)
						     .as_const())
				: eng::MaskBytes {};
		const eng::MaskBytes pats[1] = {pat};
		ENG_PROF_BEGIN(kProfFill);
#if defined(K080_CPU_FILL)
		// Referencia CPU del mismo conjunto de caras (diagnostico DT-042): mismo
		// recorte/paridad, sin Blitter.
		(void)pats;
		(void)backend.blitter_clear(dst, 1u, kRowBytes, kSceneBytes, kFbW, kRoomBoxH,
					    false);
		(void)fb.fill_cpu(dst, kRowBytes, kSceneBytes, 1u, kFbW, kRoomBoxH);
#else
		(void)backend.fill_polygons_by_plane_patterns(
			fb.faces().data(), fb.count(), dst, kRowBytes, kSceneBytes, 1u, kFbW,
			kRoomBoxH, eng::Span<const eng::MaskBytes>(pats, 1u), 2u);
#endif
		ENG_PROF_END(kProfFill);
#endif
	}

	/// Los paneles escriben un índice completo: los colores de la paleta 4/8/12 quedan
	/// estables aunque las entradas vecinas se usen para distinguir las caras de los cubos.
	void stage_panels(eng::amiga::AmigaBackend& backend) {
		const eng::PlaneBytes buf = m_scene.buffer(m_draw_buf);
		// Borra bits de overlay heredados por el buffer alterno; los dos planos base de sala
		// quedan intactos y las cajas/paneles escribirán su índice completo a continuación.
		const eng::PlaneBytes overlay =
			buf.subspan(2u * kSceneBytes, 2u * kSceneBytes);
		(void)backend.blitter_clear(overlay, 2u, kRowBytes, kSceneBytes, kFbW, kFbH,
					    false);
		const s16 wx0 = kPanelBoxX;
		const u16 pw = kPanelBoxW;
		const u16 ph = kPanelBoxH;
		const s16 by0 = kPanelBoxY;
#if defined(K080_SKIP_FILL)
		(void)backend;
		(void)buf;
#else
		const eng::retro::Rect bbox = eng::retro::rect(
			wx0, by0, static_cast<s16>(wx0 + pw), static_cast<s16>(by0 + ph));
		eng::retro::Vec2 in[kMaxVerts];
		eng::retro::Vec2 tmp[kMaxVerts];
		playfield::ContiguousPlayfield panel_pf {};
		if (!panel_pf.bind_raw(buf.data(), static_cast<u32>(buf.size()), kFbW, kFbH,
				       kPlanes, kSceneBytes)) {
			return;
		}
		for (u32 i = kRoomFaceCount; i < kFaceCount; ++i) {
			u32 n = m_fn[i];
			if (n < 3u) {
				continue;
			}
			for (u32 k = 0u; k < n; ++k) {
				in[k] = eng::retro::v2(m_fx[i][k], m_fy[i][k]);
			}
			n = eng::retro::clip_polygon(
				bbox, in, tmp, n,
				static_cast<u8>(eng::retro::PF_LEFT | eng::retro::PF_TOP |
						eng::retro::PF_RIGHT | eng::retro::PF_BOTTOM));
			if (n < 3u) {
				continue;
			}
			for (u32 k = 0u; k < n; ++k) {
				// `clip_polygon` deja la salida en `in`; `tmp` es solo alternancia interna de trabajo.
				m_bx[i][k] = static_cast<s16>(in[k].x().v - wx0);
				m_by[i][k] = static_cast<s16>(in[k].y().v - by0);
			}
			(void)panel_pf.fill_polygon(
				eng::Span<const s16>(m_bx[i], n), eng::Span<const s16>(m_by[i], n),
				kFaces[i].color);
		}
#endif
	}

	/// Las cajas se rasterizan en painter order sobre los cuatro planos: cada cara conserva su
	/// índice RGB propio y la caja cercana reemplaza a la lejana en las zonas de solapamiento.
	void stage_cubes() {
		if (m_cube_face_count == 0u) return;
		const eng::PlaneBytes buf = m_scene.buffer(m_draw_buf);
		playfield::ContiguousPlayfield cube_pf {};
		if (!cube_pf.bind_raw(buf.data(), static_cast<u32>(buf.size()), kFbW, kFbH,
				     kPlanes, kSceneBytes)) {
			return;
		}
		ENG_PROF_BEGIN(kProfFill);
		for (u8 i = 0u; i < m_cube_face_count; ++i) {
			const CubeScreenFace& face = m_cube_faces[i];
			// `prepare_cube_faces` ya recortó a todo el FB; el hook base conserva hasta 16
			// vértices, incluso si near+frustum+viewport llevan una cara a 13.
			(void)cube_pf.fill_polygon(
				eng::Span<const s16>(face.x, face.count),
				eng::Span<const s16>(face.y, face.count),
				face.color);
		}
		ENG_PROF_END(kProfFill);
	}

	/// Pasada completa de una vez (fase rapida / `K080_NO_SLICE`): clip, dos planos de sala,
	/// paneles y cubos se completan en el mismo update.
	void render_full_now(eng::amiga::AmigaBackend& backend, s32 pan_x, s32 pan_y) {
		stage_clip(pan_x, pan_y);
		stage_room(backend, 0u);
		stage_room(backend, 1u);
		stage_panels(backend);
		stage_cubes();
	}

	/// Ejecuta la siguiente etapa del Full rebanado; al terminar los cubos deja el buffer
	/// pendiente de publicación (se muestra en el update siguiente).
	void run_stage(eng::amiga::AmigaBackend& backend) {
#if defined(ENG_AMIGA)
		const u32 c0 = eng::debug::prof_clock();
#endif
		switch (m_stage) {
		case Stage::Clip:
			stage_clip(m_stage_yaw, m_stage_pitch);
			m_stage = Stage::Room0;
			break;
		case Stage::Room0:
			stage_room(backend, 0u);
			m_stage = Stage::Room1;
			break;
		case Stage::Room1:
			stage_room(backend, 1u);
			m_stage = Stage::Panels;
			break;
		case Stage::Panels:
			stage_panels(backend);
			m_stage = Stage::Cubes;
			break;
		case Stage::Cubes:
			stage_cubes();
#if defined(ENG_AMIGA)
			m_stage_cycles += eng::debug::prof_clock() - c0;
			m_present.observe_full(m_stage_cycles);
#endif
			m_pending_buf = m_draw_buf;
			m_present.commit(m_stage_window_x, m_stage_window_y);
			m_stage = Stage::None;
			return;
		case Stage::None:
			return;
		}
#if defined(ENG_AMIGA)
		m_stage_cycles += eng::debug::prof_clock() - c0;
#endif
	}

	// --- HUD --------------------------------------------------------------------------
	void draw_hud_static() {
		playfield::Surface hud {m_hud_pf,
					playfield::SurfaceRect {0, 0, kFbW, kHudLines}};
		hud.fill_rect(0, 0, static_cast<s16>(kFbW), static_cast<s16>(kHudLines), 0u);
		(void)hud.draw_text_words(16, 2, "080 CAMERA PAN WINDOW", 1u);
		// Suelos de la fase F1: base (Full) >= 12.5 Hz con display interpolado >= 25 Hz;
		// el numero de fakes por Full lo decide el coste medido (render_pacing.hpp).
		(void)hud.draw_text_words(16, 12, "FULL 12.5 + FAKE 25", 1u);
	}

	void draw_hud_dynamic(u32 f, const eng::graphics::PresentStep& step) {
		ENG_PROF_BEGIN(kProfHudCtor);
		playfield::Surface hud {m_hud_pf,
					playfield::SurfaceRect {0, 0, kFbW, kHudLines}};
		// Sin `fill_rect` por frame: el relleno de la Surface es CPU-pixel a pixel (caro
		// en Chip bajo DMA) y no hace falta: `draw_text_words` compone glifo + FONDO de
		// golpe (palabra a palabra), asi que repintar la MISMA extension con la cadena
		// rellena a espacios fijos reescribe y limpia la linea anterior.
		char buf[8] {};
		char text[64] {};
		u8 m = 0u;
		m = str_cat(text, m, "FRAME ");
		(void)u16_to_str(static_cast<u16>(f), buf);
		m = str_cat(text, m, buf);
		m = str_cat(text, m, " FULL ");
		(void)u16_to_str(static_cast<u16>(m_fulls), buf);
		m = str_cat(text, m, buf);
		m = str_cat(text, m, " FAKE ");
		(void)u16_to_str(static_cast<u16>(m_fakes), buf);
		m = str_cat(text, m, buf);
		pad_to(text, m, kHudLineChars);
		// Línea 4: modo de cámara y ángulos yaw/pitch reales del recorrido.
		char text2[64] {};
		u8 n = 0u;
		n = str_cat(text2, n, "MODE ");
		n = str_cat(text2, n, motion_mode_name(m_motion_mode));
		n = str_cat(text2, n, " Y ");
		(void)i16_to_str(pan_degrees(m_yaw_pan), buf);
		n = str_cat(text2, n, buf);
		n = str_cat(text2, n, " P ");
		(void)i16_to_str(pan_degrees(m_pitch_pan), buf);
		n = str_cat(text2, n, buf);
		n = str_cat(text2, n, " F ");
		(void)u16_to_str(m_present.pacing().full_fields(), buf);
		n = str_cat(text2, n, buf);
		n = str_cat(text2, n, " K ");
		(void)u16_to_str(static_cast<u16>(m_present.max_consecutive_fakes()), buf);
		n = str_cat(text2, n, buf);
		pad_to(text2, n, kHudLineChars);
		ENG_PROF_END(kProfHudCtor);
		ENG_PROF_BEGIN(kProfHudLine1);
		(void)hud.draw_text_words(16, 22, text, 1u);
		ENG_PROF_END(kProfHudLine1);
		ENG_PROF_BEGIN(kProfHudLine2);
		(void)hud.draw_text_words(16, 32, text2, 1u);
		ENG_PROF_END(kProfHudLine2);
	}

	static const char* motion_mode_name(MotionMode mode) {
		switch (mode) {
		case MotionMode::YawSweep: return "YAW";
		case MotionMode::PitchSweep: return "PITCH";
		case MotionMode::Lissajous: return "LISSAJOUS";
		}
		return "YAW";
	}

	static s16 pan_degrees(s32 pan) {
		return static_cast<s16>((pan * 60) / kSweepPan);
	}

private:
	bool m_ready = false;
	scene::Scene m_scene {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_hud {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_pattern {}; ///< patrón 1-bit de la sala
	playfield::ContiguousPlayfield m_hud_pf {};
	eng::copper::MoveSlot m_bplcon1_slot {};
	eng::copper::MoveSlot m_plane_hi[kPlanes] {};
	eng::copper::MoveSlot m_plane_lo[kPlanes] {};
	eng::graphics::PresentController m_present {};
	CubeScreenFace m_cube_faces[kCubeFaceSlots] {};
	MotionMode m_motion_mode = kForcePitch ? MotionMode::PitchSweep
				      : (kForceLissajous ? MotionMode::Lissajous
							 : MotionMode::YawSweep);
	s32 m_yaw_pan = 0;
	s32 m_pitch_pan = 0;
	s32 m_sweep_pan = 0;
	s32 m_sweep_dir = 1;
	u16 m_lissajous_phase = 0u;
	u16 m_lissajous_frames = 0u;
	u16 m_cube_phase = K080_CUBE_START_PHASE;
	u8 m_cube_face_count = 0u;
	u8 m_show_buf = 0u;
	u8 m_draw_buf = 1u;
	u8 m_pending_buf = 0u; ///< buffer del Full pendiente de publicar (lo decide `m_present`)
	Stage m_stage = Stage::None; ///< etapa en curso del Full rebanado (None = sin pasada)
	s32 m_stage_yaw = 0;         ///< yaw (proxy de grados) con el que se capturó el Full
	s32 m_stage_pitch = 0;       ///< pitch (proxy de grados) con el que se capturó el Full
	u16 m_stage_cube_phase = 0u;  ///< órbita/spin congelados con el Full pendiente
	s32 m_stage_window_x = 0;    ///< offset acumulado de ventana X del Full renderizado
	s32 m_stage_window_y = 0;    ///< offset acumulado de ventana Y del Full renderizado
	u32 m_stage_cycles = 0u;     ///< ciclos acumulados de la pasada (pacing al terminar)
	u32 m_fulls = 0u;
	u32 m_fakes = 0u;
	u8 m_dbg_faces = 0u;
	u8 m_dbg_verts = 0u;
	s16 m_fx[kFaceCount][kMaxVerts] {}; ///< poligonos proyectados (caja de sala = local)
	s16 m_fy[kFaceCount][kMaxVerts] {};
	s16 m_bx[kFaceCount][kMaxVerts] {}; ///< coordenadas locales del lote de relleno en curso
	s16 m_by[kFaceCount][kMaxVerts] {};
	u8 m_fn[kFaceCount] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	PanWindowDemo demo {};
	eng::Engine engine {backend, demo};
	// **Bucle por SONDO** (`run_frames_polling`): bajo un SO vivo (el runner) es el modo
	// estable de la ficha 212 — sondea `VPOSR` en `wait_vblank` sin depender de IRQs ni de
	// `Wait` (el planificador del OS reescribe INTENA y apaga VERTB; con frames largos el
	// clobber es continuo y la espera por IRQ cuelga o arrastra). Mismo modo que la 119.
	engine.run_frames_polling(0xffffu);
	return 0;
}
