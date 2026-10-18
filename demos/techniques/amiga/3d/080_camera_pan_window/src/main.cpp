// ============================================================================
// Demo 080 (3d): fotogramas intermedios por paneo de camara (Full/Fake)
// ============================================================================
//
// Implementa la fase F1 del ROADMAP_3D_PRESENTACION: una pasada completa (render
// 3D poligonal con relleno por plano) a 25 Hz y fotogramas intermedios GRATIS a
// 50 Hz desplazando solo la VENTANA DE DISPLAY dentro de un framebuffer mas
// ancho que el viewport.
//
//   framebuffer de escena (352 px de ancho; viewport de 320 centrado en S=16)
//   +--------------------------------------------------+
//   | margen 16 |      VIEWPORT 320x208      | margen 16|
//   +--------------------------------------------------+
//
// - **Full**: se proyectan la sala (tubo) y los paneles con la camara actual
//   (yaw = pan/focal) y se rellena el FB entero (margenes incluidos) con 1
//   contorno XOR + 1 area fill por plano (`PlaneFillBuilder`,
//   `AmigaBackend::fill_polygons_by_plane`). La ventana se centra: puntero
//   `fine_scroll_coarse(16)` + fino 0.
// - **Fake**: no se toca el FB; solo se parchean los `BPLxPT` (coarse) y
//   `BPLCON1` (fine) con la convencion de `graphics/playfield_scroll.hpp` para
//   desplazar la ventana `offset` px. Como el FB ya tiene el contenido de los
//   margenes, el Fake es EXACTO: el Full anterior desplazado (contrato de pixel
//   que verifica `tools/analyze/verify-080-pan-window.mjs`).
// - Politica Full/Fake: `eng::graphics/pan_window.hpp` (HOST-470) con margen de
//   15 px y racha 1. Fase lenta (1 px/frame): Full/Fake alternos (Full a 25 Hz).
//   Fase rapida (16 px/frame): el offset supera el margen y fuerza Full cada
//   frame (la camara gira mas rapido de lo que el paneo puede aproximar).
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
#include <eng/graphics/render_pacing.hpp>
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
// FB de escena 352x208 (viewport de 320 con margen de 16 px por lado: S=16+offset).
// La conmutacion al HUD debe caer en una linea <=255 del comparador del Copper:
// 44 + 208 = 252.
constexpr u16 kFbW = 352u;
constexpr u16 kFbH = 208u;
constexpr u16 kRowBytes = kFbW / 8u;      // 44 B por fila de plano
constexpr u16 kViewportW = 320u;
constexpr u16 kCenterS = (kFbW - kViewportW) / 2u; // 16
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
constexpr u16 kHudLine = 44u + kFbH;              // 252
constexpr u16 kHudBytes = kRowBytes * kHudLines;
constexpr u16 kHudColor0 = 0x000u;
constexpr u16 kHudColor1 = 0xfffu;

// --- Camara -------------------------------------------------------------------------
// `project_perspective`: sx = cx + x*focal/z, sy = cy + y*focal/z (y positivo = abajo).
// focal 256 -> escala 1 px por unidad de mundo a z=256. yaw = pan/focal rad; en vueltas
// de q12 (65536 por vuelta) = pan * 41 (+-1%).
constexpr s16 kFocal = 256;
constexpr s16 kCx = static_cast<s16>(kCenterS + kViewportW / 2u); // 176
constexpr s16 kCy = kFbH / 2u;

constexpr s16 kRoomX = 160;
constexpr s16 kRoomY = 96;
constexpr s16 kRoomZ0 = 256;
constexpr s16 kRoomZ1 = 1024;

// Paleta (4 planos, 16 colores). La asignacion de BITS importa: el relleno por plano
// compone por PARIDAD y los planos son independientes, asi que la region de un objeto que
// tapa a otro conserva los bits del fondo + los del objeto. Diseno por RANGOS:
//  - Suelo/techo/paredes usan SOLO los bits 0-1 (indices 1..3).
//  - Los paneles usan SOLO los bits 2-3; y las 4 entradas de cada combinacion de esos
//    bits (4..7, 8..11, 12..15) llevan el MISMO color, de modo que el panel se ve de su
//    color sea cual sea la pared que tiene detras (4 bits del fondo no lo tinen).
constexpr eng::Palette32 kPalette {{
	0x124, 0x764, 0x474, 0x246, 0xfd0, 0xfd0, 0xfd0, 0xfd0,
	0x0ff, 0x0ff, 0x0ff, 0x0ff, 0xf80, 0xf80, 0xf80, 0xf80,
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

// Patrón de relleno de la SALA (plano de patrón 1-bit en Chip, construido UNA vez): el
// relleno por plano lo aplica con un blit por plano (`D = A & B`, B = D) sobre la misma
// caja del fill — ver `eng/graphics/fill_pattern.hpp`. Los paneles quedan SÓLIDOS (su
// llamada no lleva patrón): el marcador naranja conserva su color único.
// `K080_NO_PATTERN`: sala sólida (A/B de medida: el patrón añade ~50-80k al Full, el blit
// de aplicación por plano sobre la caja de la sala).
#if defined(K080_NO_PATTERN)
constexpr eng::graphics::FillPattern kRoomPattern = eng::graphics::FillPattern::Solid;
#else
constexpr eng::graphics::FillPattern kRoomPattern = eng::graphics::FillPattern::Checker4;
#endif

// Margen 15 px por lado (ventana centrada en S=16; el FB admite S en [1..31]) y racha
// ADAPTATIVA: el numero de Fakes entre Fulls lo fija `RenderPacing` a partir del coste
// medido de la pasada completa (HOST-471), con tope `kFakeCap` (tolerancia visual: en la
// fase lenta cada Fake acumula 1 px de paneo no resuelto). Sin medida aun, racha 1.
constexpr eng::u8 kFakeCap = 3u;
constexpr eng::graphics::PanWindowPolicy kPanPolicyBase {15, 0, 1};

// --- Escena: sala (5 caras) + 4 paneles en fila (mismo z: sin solapes entre ellos) ---
constexpr u32 kFaceCount = 9u;
constexpr u8 kMaxVerts = 10u; // un cuadrilatero recortado por 4 planos da <= 8

struct FaceSpec {
	eng::math3d::Vec3 v[4];
	u8 color;
};

constexpr eng::math3d::Vec3 p3(s16 x, s16 y, s16 z) {
	return eng::math3d::vec3(x, y, z);
}

// Orden de pintado: sala y paneles despues (estan por delante). Los 4 paneles viven a
// z=640 (mismo plano; dos quads coplanares separados en x no pueden solaparse en
// proyeccion) y usan bits 2-3; el naranja 12 (x=+100) es el marcador unico.
// Los paneles llevan un sesgo de z (+-5 px entre su borde izquierdo y el derecho):
// asi sus aristas superior/inferior NO son exactamente horizontales y el contorno EOR
// las dibuja (el trazador del Blitter DESCARTA las horizontales; sin ellas el area fill
// no cierra la region del quad y el panel sale hueco).
constexpr FaceSpec kFaces[kFaceCount] = {
	{{p3(-kRoomX, kRoomY, kRoomZ0), p3(-kRoomX, kRoomY, kRoomZ1),
	  p3(kRoomX, kRoomY, kRoomZ1), p3(kRoomX, kRoomY, kRoomZ0)}, 1u},
	{{p3(-kRoomX, -kRoomY, kRoomZ0), p3(kRoomX, -kRoomY, kRoomZ0),
	  p3(kRoomX, -kRoomY, kRoomZ1), p3(-kRoomX, -kRoomY, kRoomZ1)}, 3u},
	{{p3(-kRoomX, -kRoomY, kRoomZ0), p3(-kRoomX, -kRoomY, kRoomZ1),
	  p3(-kRoomX, kRoomY, kRoomZ1), p3(-kRoomX, kRoomY, kRoomZ0)}, 2u},
	{{p3(kRoomX, -kRoomY, kRoomZ0), p3(kRoomX, kRoomY, kRoomZ0),
	  p3(kRoomX, kRoomY, kRoomZ1), p3(kRoomX, -kRoomY, kRoomZ1)}, 1u},
	{{p3(-kRoomX, -kRoomY, kRoomZ1), p3(kRoomX, -kRoomY, kRoomZ1),
	  p3(kRoomX, kRoomY, kRoomZ1), p3(-kRoomX, kRoomY, kRoomZ1)}, 3u},
	{{p3(-112, -80, 635), p3(-88, -80, 645), p3(-88, 80, 645), p3(-112, 80, 635)}, 8u},
	{{p3(-72, -80, 635), p3(-48, -80, 645), p3(-48, 80, 645), p3(-72, 80, 635)}, 4u},
	{{p3(48, -80, 635), p3(72, -80, 645), p3(72, 80, 645), p3(48, 80, 635)}, 8u},
	{{p3(88, -80, 635), p3(112, -80, 645), p3(112, 80, 645), p3(88, 80, 635)}, 12u},
};

// Caja FIJA de la sala (planos 0-1): la proyeccion del tubo cabe en las filas 8..200
// para TODO el rango de paneo (en yaw 0 el borde cercano de suelo/techo toca 8 y 200;
// con yaw los bordes se meten hacia dentro). El barrido de clear+area fill se acota a
// esta caja (~7% menos palabras por plano); cubre tambien la posicion de los renders
// anteriores del mismo buffer (el contenido varia poco entre rerenders).
constexpr s16 kRoomBoxY = 8;
constexpr u16 kRoomBoxH = 193u; // filas 8..200 inclusive

// Caja FIJA de los planos de panel (2-3): cubre la posicion de los paneles para todo el
// rango de paneo (+-29 grados). Debe cubrir tambien la posicion en el render ANTERIOR del
// mismo buffer (hasta 64 px de paneo entre rerenders en la fase rapida), o quedarian
// paneles fantasma al limpiar solo la caja del frame actual. Filas 44..163.
constexpr s16 kPanelBoxX = 0;
constexpr u16 kPanelBoxW = kFbW;      // 352 px (22 palabras)
constexpr s16 kPanelBoxY = 52;
constexpr u16 kPanelBoxH = 104u;

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

struct PanWindowDemo {
	// --- Setup -----------------------------------------------------------------------
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
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

		// Plano de patrón de la sala (1 bit, caja de la sala): lo lee el Blitter por el
		// canal A del blit de aplicación; se construye AQUÍ una sola vez (CPU).
		m_pattern = backend.memory_manager().chip().reserve<eng::MaskTag>(
			static_cast<eng::u32>(kRowBytes) * kRoomBoxH, 16);
		if (!m_pattern.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008005u);
			return;
		}
		eng::graphics::fill_pattern_plane_cpu(m_pattern.view, kRowBytes, kRoomBoxH,
						      kRoomPattern);

		scene::SceneResources res {};
		res.width = kFbW;
		res.height = kFbH + kHudLines; // 256 lineas visibles (208 escena + 48 HUD)
		res.rows = kFbH;               // el bitmap de escena tiene 208 filas fisicas
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
		ENG_PROF_INIT(10u);
		eng::debug::mark_ready(g_eng_run_status, 0u);
	}

	// --- Frame: politica Full/Fake + render o paneo ----------------------------------
	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready || !m_scene.ok()) {
			return;
		}
		const u32 f = context.frame.frame_index;
#if defined(K080_OFFSET_TEST)
		// PRUEBA DE AISLAMIENTO (VALIDATION §3): camara congelada en pan=-128 y ciclo de
		// offsets 0..16 (8 frames cada uno) sobre la ventana. Con el contenido fijo,
		// cada desplazamiento pedido debe producir EXACTAMENTE ese desplazamiento.
		m_pan = -128;
#endif
#if defined(K080_PAN_FREEZE)
		// Vista estática para verificación de geometría (captura sin tearing): camara
		// congelada en el paneo dado (p. ej. 0 = vista simétrica del túnel).
		m_pan = static_cast<s16>(K080_PAN_FREEZE);
#endif
		ENG_PROF_FRAME();
		ENG_PROF_BEGIN(kProfFrame);
		eng::graphics::PanWindowStep step =
			eng::graphics::pan_window_step(m_pan_state, m_pan, 0, m_policy);
#if defined(K080_OFFSET_TEST)
		{
			static constexpr s16 kCycle[17] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
							   12, 13, 14, 15, 16};
			step.full = false;
			step.offset_x = kCycle[(f / 8u) % 17u];
		}
#endif

		u16 s = static_cast<u16>(kCenterS + step.offset_x);
		if (step.full) {
			++m_fulls;
			ENG_PROF_BEGIN(kProfFull);
#if defined(ENG_AMIGA)
			const eng::u32 full_c0 = eng::debug::prof_clock();
#endif
			render_full(backend, m_pan);
#if defined(ENG_AMIGA)
			// Ratio de render: coste medido del Full -> racha de Fakes para la base
			// (>=12,5 Hz) y la presentacion con interpolados (>=25 Hz), HOST-471.
			m_pacing.observe(eng::debug::prof_clock() - full_c0);
			m_policy.max_consecutive_fakes = m_pacing.affordable_fakes(kFakeCap);
#endif
			ENG_PROF_END(kProfFull);
			m_show_buf = m_draw_buf;
			m_draw_buf = static_cast<u8>(m_draw_buf ^ 1u);
			s = kCenterS;
		} else {
			++m_fakes;
		}
		// El HUD se repinta cada 16 frames (~3 Hz): suficiente para telemetria de
		// depuracion y deja el camino del frame en 1 campo (una linea de texto por
		// palabras cuesta ~190k ciclos medidos cuando el frame desborda el VBlank y
		// escribe con el display activo; medido con `profile.mjs`).
#if !defined(K080_SKIP_HUD)
		if ((f & 31u) == 0u) {
			ENG_PROF_BEGIN(kProfHud);
			draw_hud_dynamic(f, step);
			ENG_PROF_END(kProfHud);
		}
#endif		ENG_PROF_BEGIN(kProfPatch);
#if !defined(K080_SKIP_PATCH)
		patch_window(s);
#endif
		ENG_PROF_END(kProfPatch);

		// Telemetria por frame (la leen el canal lateral y el verificador):
		// [31:24] modo (0xA1 Full / 0xA2 Fake), [23:16] offset+128, [15:0] pan.
		g_eng_run_status.detail =
			(static_cast<u32>(step.full ? 0xa1u : 0xa2u) << 24u) |
			(static_cast<u32>(static_cast<u8>(step.offset_x + 128)) << 16u) |
			(static_cast<u32>(static_cast<u16>(m_pan)) & 0xffffu);

		// Paneo triangular determinista: lento (1 px/frame) |pan|<96 y rapido (16)
		// a partir de ahi; rebote en +-128.
		const s32 v = (m_pan < 96 && m_pan > -96) ? 1 : 16;
		m_pan += m_dir * v;
		if (m_pan > 128) {
			m_pan = 128;
			m_dir = -1;
		} else if (m_pan < -128) {
			m_pan = -128;
			m_dir = 1;
		}
		ENG_PROF_END(kProfFrame);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
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
	void patch_window(u16 s) {
		const eng::PlaneBytes planes = m_scene.buffer(m_show_buf);
		const u32 coarse_bytes = static_cast<u32>(eng::graphics::fine_scroll_coarse(s)) >> 3u;
#if !defined(K080_PATCH_NO_PLANES)
		eng::Span<u16> words = m_scene.plan().active_span();
		for (u8 p = 0u; p < kPlanes; ++p) {
			const u8* pc = planes.data() + static_cast<u32>(p) * kSceneBytes + coarse_bytes;
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

	// --- Render de la pasada completa -------------------------------------------------
	void render_full(eng::amiga::AmigaBackend& backend, s32 pan) {
		ENG_PROF_BEGIN(kProfWait);
		backend.wait_blitter();
		ENG_PROF_END(kProfWait);
		// Vista mundo->camara con yaw = pan/focal: Ry(-yaw) aplica xc = x*cos - z*sin
		// (girar a la derecha mueve los puntos +x hacia la izquierda de la pantalla).
		// `retro::turns` usa el indice de 4096 pasos/vuelta (kAngleSteps): 1 px de imagen
		// = 1/focal rad = 2.5465 pasos -> (pan*10430)>>12 (= pan/focal * 4096/2pi).
		eng::math3d::Affine3<> view = eng::math3d::Affine3<>::identity();
		const s16 yaw_steps = static_cast<s16>(-((static_cast<s32>(pan) * 10430) >> 12));
		eng::math3d::load_rotate(view.m, eng::retro::turns(0u),
					 eng::retro::turns(static_cast<u16>(yaw_steps)),
					 eng::retro::turns(0u));

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

		// Dos pasadas de relleno por plano: la sala en los planos 0-1 sobre el area
		// completa, y los paneles en los planos 2-3 sobre su bbox (el barrido del area
		// fill es lo caro; acotarlo a la caja de los paneles ahorra ~85k ciclos/frame).
		eng::graphics::PlaneFillBuilder<kFaceCount> walls_fb;
		eng::graphics::PlaneFillBuilder<kFaceCount> panels_fb;
		u32 verts = 0u;
		// Fast path del recorte: dentro = sin pasadas; fuera = descartar; si no, S-H.
		const auto inside_box = [](const eng::retro::Vec2* v, const eng::retro::Rect& r) {
			for (u32 k = 0u; k < 4u; ++k) {
				if (!(v[k].x().v >= r.minX.v && v[k].x().v < r.maxX.v &&
				      v[k].y().v >= r.minY.v && v[k].y().v < r.maxY.v)) {
					return false;
				}
			}
			return true;
		};
		const auto outside_box = [](const eng::retro::Vec2* v, const eng::retro::Rect& r) {
			bool left = true;
			bool right = true;
			bool top = true;
			bool bottom = true;
			for (u32 k = 0u; k < 4u; ++k) {
				left = left && (v[k].x().v < r.minX.v);
				right = right && (v[k].x().v >= r.maxX.v);
				top = top && (v[k].y().v < r.minY.v);
				bottom = bottom && (v[k].y().v >= r.maxY.v);
			}
			return left || right || top || bottom;
		};

		ENG_PROF_BEGIN(kProfClip);
		for (u32 i = 0u; i < kFaceCount; ++i) {
			eng::math3d::Vec3 cam[4];
			eng::math3d::mesh_transform(eng::Span<const eng::math3d::Vec3>(kFaces[i].v, 4),
						    view, eng::Span<eng::math3d::Vec3>(cam, 4));
			for (u32 k = 0u; k < 4u; ++k) {
				s16 sx = 0;
				s16 sy = 0;
				eng::graphics::project_perspective(cam[k], kFocal, kCx, kCy, sx, sy);
				in[k] = eng::retro::v2(sx, sy);
			}
			u32 n = 0u;
			if (inside_box(in, clip)) {
				n = 4u; // fast path: todo dentro del framebuffer
			} else if (!outside_box(in, clip)) {
				n = eng::retro::clip_polygon(
					clip, in, tmp, 4u,
					static_cast<u8>(eng::retro::PF_LEFT | eng::retro::PF_TOP |
							eng::retro::PF_RIGHT | eng::retro::PF_BOTTOM));
			}
			if (n < 3u) {
				m_fn[i] = 0u;
				continue;
			}
			if (i < 5u) {
				// Sala: acotar a la caja de filas del tubo; coords locales a la caja.
				if (!inside_box(in, room_box)) {
					n = outside_box(in, room_box)
						    ? 0u
						    : eng::retro::clip_polygon(
							      room_box, in, tmp, n,
							      static_cast<u8>(eng::retro::PF_LEFT |
									      eng::retro::PF_TOP |
									      eng::retro::PF_RIGHT |
									      eng::retro::PF_BOTTOM));
				}
				m_fn[i] = static_cast<u8>(n);
				if (n < 3u) {
					continue;
				}
				for (u32 k = 0u; k < n; ++k) {
					m_fx[i][k] = in[k].x().v;
					m_fy[i][k] = static_cast<s16>(in[k].y().v - kRoomBoxY);
				}
				verts += n;
				(void)walls_fb.submit(m_fx[i], m_fy[i], static_cast<u8>(n),
						      kFaces[i].color);
			} else {
				// Paneles: se guardan en coords globales; el bloque de paneles los
				// recorta a su caja fija y los desplaza.
				m_fn[i] = static_cast<u8>(n);
				for (u32 k = 0u; k < n; ++k) {
					m_fx[i][k] = in[k].x().v;
					m_fy[i][k] = in[k].y().v;
				}
				verts += n;
			}
		}
		m_dbg_verts = static_cast<u8>(verts > 255u ? 255u : verts);
		ENG_PROF_END(kProfClip);

		const eng::PlaneBytes buf = m_scene.buffer(m_draw_buf);
#if defined(K080_SKIP_FILL)
		(void)backend.blitter_clear(buf, kPlanes, kRowBytes, kSceneBytes, kFbW, kFbH, true);
		(void)walls_fb;
		(void)panels_fb;
#else
		if (walls_fb.count() != 0u) {
			// Con patrón `Solid` no se pasa plano: el fill queda igual que sin patrón
			// (sin el blit de aplicación por plano).
			const eng::MaskBytes room_pattern =
				kRoomPattern == eng::graphics::FillPattern::Solid
					? eng::MaskBytes {}
					: m_pattern.view.as_const();
			ENG_PROF_BEGIN(kProfFill);
			(void)backend.fill_polygons_by_plane(
				walls_fb.faces().data(), walls_fb.count(),
				buf.subspan(static_cast<u32>(kRoomBoxY) * kRowBytes), kRowBytes,
				kSceneBytes, 2u, kFbW, kRoomBoxH, room_pattern,
				eng::graphics::fill_pattern_period(kRoomPattern));
			ENG_PROF_END(kProfFill);
		} else {
			(void)backend.blitter_clear(buf.subspan(static_cast<u32>(kRoomBoxY) * kRowBytes),
						    2u, kRowBytes, kSceneBytes, kFbW, kRoomBoxH, false);
		}

		// Paneles: bbox con margen de 8 px (el contenido del mismo buffer cambia como
		// mucho ~4 px entre Fulls ya que solo varia la camara) y coords locales a la caja;
		// los colores se remapean a los bits 0-1 de la ventana de 2 planos.
		// Paneles: coords locales a la caja FIJA de los planos 2-3 y colores remapeados
		// a los bits 0-1 de esa ventana de 2 planos.
		{
			const s16 wx0 = kPanelBoxX;
			const u16 pw = kPanelBoxW;
			const u16 ph = kPanelBoxH;
			const s16 by0 = kPanelBoxY;
			const eng::retro::Rect bbox =
				eng::retro::rect(wx0, by0, static_cast<s16>(wx0 + pw), static_cast<s16>(by0 + ph));
			for (u32 i = 5u; i < kFaceCount; ++i) {
				const u8 n = m_fn[i];
				if (n < 3u) {
					continue;
				}
				for (u32 k = 0u; k < n; ++k) {
					in[k] = eng::retro::v2(m_fx[i][k], m_fy[i][k]);
				}
				const u32 m = eng::retro::clip_polygon(
					bbox, in, tmp, n,
					static_cast<u8>(eng::retro::PF_LEFT | eng::retro::PF_TOP |
							eng::retro::PF_RIGHT | eng::retro::PF_BOTTOM));
				if (m < 3u) {
					continue;
				}
				for (u32 k = 0u; k < m; ++k) {
					m_fx[i][k] = static_cast<s16>(in[k].x().v - wx0);
					m_fy[i][k] = static_cast<s16>(in[k].y().v - by0);
				}
				// Color remapeado: 4->1, 8->2, 12->3 (bits 0-1 de los planos 2-3).
				(void)panels_fb.submit(m_fx[i], m_fy[i], static_cast<u8>(m),
						       static_cast<u8>(kFaces[i].color >> 2u));
			}
			if (panels_fb.count() != 0u) {
				const u32 dest_off = 2u * kSceneBytes +
						     static_cast<u32>(by0) * kRowBytes +
						     static_cast<u32>(wx0 >> 3);
				ENG_PROF_BEGIN(kProfFill);
				(void)backend.fill_polygons_by_plane(
					panels_fb.faces().data(), panels_fb.count(),
					buf.subspan(dest_off), kRowBytes, kSceneBytes, 2u, pw, ph);
				ENG_PROF_END(kProfFill);
			}
		}
#endif
		m_dbg_faces = static_cast<u8>(walls_fb.count() + panels_fb.count());
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

	void draw_hud_dynamic(u32 f, const eng::graphics::PanWindowStep& step) {
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
		// Linea 4: diagnostico del frame (modo, offset, caras/vertices) y paneo.
		char text2[64] {};
		u8 n = 0u;
		n = str_cat(text2, n, step.full ? "FULL OFF " : "FAKE OFF ");
		(void)i16_to_str(static_cast<s16>(step.offset_x), buf);
		n = str_cat(text2, n, buf);
		n = str_cat(text2, n, " FAC ");
		(void)u16_to_str(static_cast<u16>(m_dbg_faces), buf);
		n = str_cat(text2, n, buf);
		n = str_cat(text2, n, " VER ");
		(void)u16_to_str(static_cast<u16>(m_dbg_verts), buf);
		n = str_cat(text2, n, buf);
		n = str_cat(text2, n, " PAN ");
		(void)i16_to_str(static_cast<s16>(m_pan), buf);
		n = str_cat(text2, n, buf);
		n = str_cat(text2, n, " K ");
		(void)u16_to_str(static_cast<u16>(m_policy.max_consecutive_fakes), buf);
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

private:
	bool m_ready = false;
	scene::Scene m_scene {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_hud {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_pattern {}; ///< patrón 1-bit de la sala
	playfield::ContiguousPlayfield m_hud_pf {};
	eng::copper::MoveSlot m_bplcon1_slot {};
	eng::copper::MoveSlot m_plane_hi[kPlanes] {};
	eng::copper::MoveSlot m_plane_lo[kPlanes] {};
	eng::graphics::PanWindowState m_pan_state {};
	eng::graphics::PanWindowPolicy m_policy = kPanPolicyBase;
	eng::graphics::RenderPacing m_pacing {};
	s32 m_pan = -128; ///< paneo en px de imagen (inicio en la fase rapida)
	s32 m_dir = 1;
	u8 m_show_buf = 0u;
	u8 m_draw_buf = 1u;
	u32 m_fulls = 0u;
	u32 m_fakes = 0u;
	u8 m_dbg_faces = 0u;
	u8 m_dbg_verts = 0u;
	s16 m_fx[kFaceCount][kMaxVerts] {};
	s16 m_fy[kFaceCount][kMaxVerts] {};
	u8 m_fn[kFaceCount] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	PanWindowDemo game {};
	eng::Engine engine {backend, game};
	engine.set_blit_service_enabled(false);
	engine.run_frames(0xffffu);
	return 0;
}
