// ============================================================================
// Demo 112 - RoboCod: single playfield de 5 planos (4 FG + 1 BG por blitter)
// ============================================================================
//
// Porte del truco del RoboCod ORIGINAL (sin DPF): **un solo playfield de 5
// planos**. Los 4 primeros (16 colores) son el FG de juego (plataformas); el 5.º
// plano es el FONDO, dibujado por Blitter. El blit del tilemap FG es interleaved
// y cubre los 5 planos (pisa el plano de fondo con 0); DESPUES, cada frame, se
// re-copia por Blitter la ventana del patron al 5.º plano (tecnica "soft DPF").
//
// El fondo se mantiene **FIJO** en pantalla: se compensa TODO el scroll X del
// playfield (grueso+grueso) desplazando el contenido (`src = -videoposx`) y se
// COMPENSA EL COPPER SPLIT emitiendo dos rects (arriba/abajo del corte) con las
// filas del patron contiguas -> sin costura en el wrap vertical. La copia usa el
// barrel shifter para el sub-pixel y deja una guarda enmascarada (ver
// `make_bg_plane_copy_rect_job` y kGuardX). Ver robocod-layered-scroll.md §3.
//
// LÍMITE OCS: campo visible 208 (el WAIT del Copper es de 8 bits; ver kViewportH).
//
// Paleta de 32 indices MAPEADA a 16: `palette[c]==palette[c+16]` para c=1..15, de
// modo que el plano de fondo NO tinta el FG; solo el indice 0/16 pasa de negro
// (fondo off) al color del patron (fondo on). Asi el fondo, al moverse bajo el FG,
// no se nota en el FG ("duplica la paleta").
//
//   X: Finite (mundo X cabe en el bitmap)   Y: usada (corkscrew)

#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#ifdef K_DIAG_CYCLES
#include <eng/debug/peripheral.hpp>
#endif
#if defined(K_DIAG_BG) || defined(K_DIAG_TOTAL)
#include <eng/debug/peripheral.hpp>
#endif
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/tile_demo.hpp>

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

namespace field = eng::field;

constexpr eng::u16 kTileW = 16;
constexpr eng::u16 kTileH = 16;
constexpr eng::u16 kViewportW = 320;
// LÍMITE OCS: el WAIT del Copper solo compara 8 bits de línea (0..255). Con un
// split MÓVIL (corkscrew), la línea de corte = DIWSTRT_y(41) + (display_height -
// display_offset) debe quedar <= 255 => el campo visible debe ser <= 214. Se usa
// 208 (13 filas de tile) como valor canónico (igual que 201/202). NO usar 256.
constexpr eng::u16 kViewportH = 208;
constexpr eng::u16 kDisplayH = 288;        // anillo = 256 + 2*16 (invariante §7 201)
constexpr eng::u8  kPlanes = 5;            // 4 FG (16 colores) + 1 BG (RoboCod)
constexpr eng::u8  kParallaxPlane = 4;
constexpr eng::u8  kParallaxDiv = 2;

constexpr eng::u16 kMapCols = 25;          // 400 px (X Finite)
constexpr eng::u16 kMapRows = 32;          // 512 px (mayor que la ventana)
constexpr eng::u16 kTilesetCount = 32;
// La copia con shift deja una guarda de hasta 15 px al principio del bitmap del
// fondo (shift-in enmascarado a cero). La cámara nunca baja de 16 px para que esa
// guarda quede fuera de la ventana visible.
constexpr eng::s32 kGuardX = 16;
// Timing: la ventana visible ocupa las líneas raster [DIWSTRT_y, DIWSTRT_y+viewport).
// Las escrituras al fondo VISIBLE deben caer en el blanking (fin de visible -> inicio
// del siguiente). DIWSTRT_y = 41 (kDiwStrt 0x2981), así que el blank empieza en 249.
constexpr eng::u16 kDiwStrtY = 41;
constexpr eng::u16 kBlankStart = static_cast<eng::u16>(kDiwStrtY + kViewportH);
#ifndef K_INIT_CAMX
#define K_INIT_CAMX (kViewportW / 4)
#endif

constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileW,
	/*tile_height=*/       kTileH,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/static_cast<eng::u32>(kDisplayH) * kPlanes,
	/*planes=*/            kPlanes,
};

// Tiles del FG (plataformas). El plano 4 (fondo) lo pinta el engine.
eng::u16 fg_row(eng::u8 glyph, eng::u8, eng::u8 row, eng::u8 plane) {
	if (glyph == 0u) return 0u;                 // transparente
	const eng::u8 c = (row == 0u) ? 6u : 5u;    // borde claro / cuerpo
	return (c & (1u << plane)) != 0u ? 0xffffu : 0u;
}

// Paleta 32 mapeada a 16: 0..15 FG (0 negro); 16 = color del fondo; 17..31 = copia
// de 1..15 (duplicado -> el plano de fondo no tinta el FG).
constexpr eng::u16 kPalette[32] {
	0x000, 0x210, 0x320, 0x430, 0x540, 0xf80, 0xff0, 0xfff,
	0x8af, 0x9cf, 0x0cf, 0x0f9, 0xf40, 0xf00, 0xf0f, 0xaaa,
	0x037, 0x210, 0x320, 0x430, 0x540, 0xf80, 0xff0, 0xfff,
	0x8af, 0x9cf, 0x0cf, 0x0f9, 0xf40, 0xf00, 0xf0f, 0xaaa,
};

// Raster colors (Copper) del fondo: el color del plano 4 (COLOR16) cambia por
// líneas de raster -> gradiente pastel sobre las bandas. Requiere display lineal
// (sin split de Copper) para no desordenar el orden de los WAIT. Se conserva la
// API `RasterColorZone` en el engine, pero la demo 112 usa el SPLIT (corkscrew)
// para poder llevar el fondo fijo a través del corte, así que no las usa.

eng::u16 g_map[kMapCols * kMapRows] {};

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::graphics::FramePlan bg_plan {};   // blit de fondo (filas VISIBLES) -> en blanking
	eng::MemoryBlock m_bg_pattern {};
	eng::s16 m_dx = 1, m_dy = 1;
	eng::s32 m_bgscroll = 0;                 // cámara PROPIA del fondo (soft DPF)
	eng::s8  m_bgdx = 1;
	bool ready = false;

	static constexpr eng::u16 kPatPeriodPx = 512;  // periodo horizontal del motivo
	static constexpr eng::u16 kPatRowBytes = 128;  // 1024 px = 2 periodos (margen para la ventana)
	static constexpr eng::u16 kPatRows = 512;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// El doble buffer del fondo (soft DPF) usa 2 buffers con el stride interleaved
		// (2 * display_height * planes * row), por eso se pide más Chip RAM.
		if (!backend.configure_memory({400u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011201u);
			return;
		}
		// Mapa tipo plataformas: suelo + plataformas flotantes; resto transparente.
		for (eng::u16 y = 0; y < kMapRows; ++y) {
			for (eng::u16 x = 0; x < kMapCols; ++x) {
				eng::u16 t = 0u;
				if (y >= static_cast<eng::u16>(kMapRows - 3u)) t = 1u;
				else if ((y == 8u || y == 16u || y == 22u) && ((x % 10u) < 4u)) t = 1u;
				else if ((y == 12u || y == 26u) && ((x % 14u) < 3u)) t = 2u;
				g_map[static_cast<eng::u32>(y) * kMapCols + x] = t;
			}
		}

		// Patrón de fondo (1 bit) en Chip RAM (fuente del Blitter): bandas
		// diagonales + puntos (motivo de tiles). Dos periodos de 512 px (1024 px =
		// 128 bytes) para que la ventana copiada nunca invada la fila siguiente al
		// desplazarse horizontalmente.
		m_bg_pattern = backend.memory().chip.allocate(
			static_cast<eng::u32>(kPatRowBytes) * kPatRows, 16);
		if (m_bg_pattern.valid()) {
			auto* p = static_cast<eng::u8*>(m_bg_pattern.data);
			for (eng::u16 y = 0; y < kPatRows; ++y) {
				for (eng::u16 bx = 0; bx < kPatRowBytes; ++bx) {
					eng::u8 v = 0u;
					for (eng::u8 bit = 0; bit < 8u; ++bit) {
						const eng::u32 x = static_cast<eng::u32>(bx) * 8u + bit;
						const bool dot = ((x & 7u) == 0u) && ((y & 7u) == 0u);
						const bool band = ((x + y) & 63u) < 32u;
						if (dot || band) v |= static_cast<eng::u8>(0x80u >> bit);
					}
					p[static_cast<eng::u32>(y) * kPatRowBytes + bx] = v;
				}
			}
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTileW;
		scene_cfg.tile_height = kTileH;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = true;                                  // corkscrew (Y)
		scene_cfg.x_mode = eng::field::AxisMode::Finite;            // X lineal acotado
		scene_cfg.scroll_mode = eng::field::ScrollMode::EightWay;
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 4;
		scene_cfg.parallax_plane = kParallaxPlane;                  // plano de fondo RoboCod
		scene_cfg.parallax_div = kParallaxDiv;
		scene_cfg.linear_display = false;                          // SPLIT de Copper (corkscrew)

		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_map, kMapCols * kMapRows);
		scene_cfg.map.width = kMapCols;
		scene_cfg.map.height = kMapRows;
		scene_cfg.map.wrap_x = 0;
		scene_cfg.map.wrap_y = 0;
		scene_cfg.map.edge_tile = 0;

		scene_cfg.tileset_count = kTilesetCount;
		scene_cfg.fg_row_fn = &fg_row;
		scene_cfg.bg_row_fn = &fg_row;
		scene_cfg.palette = kPalette;

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011202u);
			return;
		}
		scene.bg().set_camera(static_cast<eng::s32>(K_INIT_CAMX), 0);
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011203u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011204u);
			return;
		}
		scene.takeover(backend);
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x11200000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 200});
#ifdef K_DIAG_CYCLES
		const eng::u32 t0 = eng::debug::DebugPeripheral::cycle_counter();
#endif

		// Rebote diagonal sobre el area extra (X: 80 px; Y: 512-256).
		const eng::s32 camx = scene.bg().mapposx();
		const eng::s32 camy = scene.bg().mapposy();
		if (camx >= static_cast<eng::s32>(kMapCols * kTileW) - kViewportW) m_dx = -1;
		if (camx <= kGuardX) m_dx = 1;
		if (camy >= static_cast<eng::s32>(kMapRows * kTileH) - kViewportH) m_dy = -1;
		if (camy <= 0) m_dy = 1;
		eng::s32 dx = (m_dx > 0) ? 2 : -2;
		eng::s32 dy = (m_dy > 0) ? 1 : -1;
#ifdef K_DIAG_XONLY
		dy = 0;   // diagnóstico: solo scroll horizontal
#endif
#ifdef K_DIAG_YONLY
		dx = 0;   // diagnóstico: solo scroll vertical
#endif
#ifndef K_DIAG_SKIP_SCROLL
		if (!scene.bg().update_scroll(plan, dx, dy)) {
			m_dx = -m_dx; m_dy = -m_dy;
		}
#endif
		// 1) FG (tiles del anillo/staging): NO toca filas visibles -> puede ejecutarse
		//    en cualquier momento del frame.
		if (!backend.execute_frame_plan(plan)) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011210u); return;
		}

		// 2) Fondo FIJO: se copia SOLO la ventana visible (21 words/fila) y se ejecuta
		//    en el BLANKING vertical (fin de visible -> inicio del siguiente), de modo
		//    que el haz nunca lea una fila visible a medio reescribir (evita el
		//    tearing). El display fetcha desde planeaddx = ceil(camx/16)*2; se copia
		//    [planeaddx-2, planeaddx+40) = 1 word de guarda + 20 visibles. El barrel
		//    shifter deja la guarda (<=15 px) justo antes de la cámara (no visible).
		//    Ver robocod-layered-scroll.md §3.1/§3.3.
#ifndef K_DIAG_SKIP_BGCOPY
		if (m_bg_pattern.valid()) {
			const eng::u8* pat = static_cast<const eng::u8*>(m_bg_pattern.data);
			const eng::s32 camx = scene.bg().videoposx();
			// Ventana horizontal del blit (helper puro y testeado): [planeaddx-2,
			// planeaddx+fetch) = guarda + visible, y src_x para que quede FIJA.
			// fetch = viewport/8 + 1 word: el DDFSTRT=0x30 ya incluye la word extra
			// que el scroll fino coloca a la izquierda, así que el display lee 21
			// words (42 B) desde planeaddx -> la ventana necesita 22 words.
			field::BgWindow win = field::bg_window_for(
				camx, kPatPeriodPx, static_cast<eng::u16>(kViewportW / 8u + 2u));
			// Soft DPF: el fondo tiene su PROPIA cámara (`m_bgscroll`) independiente
			// del FG. La posición aparente del fondo es `m_bgscroll + x`, así que el
			// offset de contenido es `src_x = m_bgscroll - camx (+dest*8)`.
			m_bgscroll += m_bgdx;
			if (m_bgscroll >= static_cast<eng::s32>(kPatPeriodPx)) { m_bgscroll = 0; }
			win.src_x = static_cast<eng::u16>(
				(static_cast<eng::s32>(win.src_x) + m_bgscroll) %
				static_cast<eng::s32>(kPatPeriodPx));
			const field::BgSplitRects rects = field::bg_split_rects(
				scene.bg().display_offset(), kDisplayH, kViewportH, /*bg_y=*/0u);
			bg_plan.clear();
			bg_plan.set_blit_budget_limits({8192, 16384, 4, 200});
			for (eng::u8 i = 0; i < rects.count; ++i) {
				if (!bg_plan.add_tile_block_copy(scene.bg().make_bg_plane_copy_rect_job(
					pat, kPatRowBytes, win.src_x,
					rects.src_y[i], rects.dest_row[i], rects.rows[i],
					win.dest_byte_off, win.words))) {
					ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011212u); return;
				}
			}
			// Espera al inicio del blanking vertical (línea kBlankStart). Se entra
			// lo antes posible: si el primer valor ya está muy avanzado, se espera
			// al siguiente frame (mejor eso que empezar tarde y derramar al visible).
			for (;;) {
				const eng::u16 ln = backend.current_raster_line();
				if (ln == kBlankStart) break;
			}
#ifdef K_DIAG_BG
			const eng::u32 tb0 = eng::debug::DebugPeripheral::cycle_counter();
#endif
			if (!backend.execute_frame_plan(bg_plan)) {
				ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011213u); return;
			}
#ifdef K_DIAG_BG
			const eng::u32 tb1 = eng::debug::DebugPeripheral::cycle_counter();
			const eng::u16 ln1 = backend.current_raster_line();
			g_eng_run_status.detail = (static_cast<eng::u32>(kBlankStart) << 24) |
				(static_cast<eng::u32>(ln1) << 16) | ((tb1 - tb0) & 0xffffu);
			return;
#endif
		}
#endif
		// 3) Conmuta el doble buffer del fondo (el blit fue al buffer trasero) y compone
		//    la copperlist con el nuevo delantero -> sin tearing en el plano de fondo.
		scene.bg().bg_flip();
		if (!scene.compose()) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011211u); return;
		}
#ifdef K_EARLY_INSTALL
		// Instala la copperlist YA, dentro del blanking y ANTES del VBlank, de modo
		// que el swap (COP1LC) sea determinista respecto al bitmap (ya escrito): la
		// copperlist mostrada siempre es la de la cámara del bitmap.
		scene.install(backend);
#endif
#ifdef K_DIAG_FBCHECK
		// Auto-comprobación del FRAMEBUFFER (no de capturas): lee el plano 4 en las
		// posiciones que el display mostrará (fila (d+ay), píxel (camx+x)) y lo compara
		// con el patrón esperado de un fondo FIJO. Si `mism==0`, el dibujo es correcto.
		{
			const auto hw = scene.bg().hardware_view();
			// El plano de fondo puede vivir en su propio buffer (soft DPF doble buffer).
			const eng::u8* fb = hw.bg_plane_base ? hw.bg_plane_base
			                   : (hw.real_base ? hw.real_base : hw.bitplanes);
			const eng::u16 row = hw.bitmap_bytes_per_row;
			const eng::u16 npl = hw.planes;
			const eng::u16 d = hw.display_offset;
			const eng::s32 cx = hw.videoposx;
			int mism = 0, total = 0;
			for (eng::s32 ay = 0; ay < static_cast<eng::s32>(kViewportH); ay += 5) {
				const eng::u16 R = static_cast<eng::u16>((d + ay) % kDisplayH);
				for (eng::s32 x = 0; x < 320; x += 17) {
					const eng::s32 P = cx + x;
					if (P < 0) continue;
					const eng::u32 off = (static_cast<eng::u32>(R) * npl + 4u) * row +
					                     static_cast<eng::u32>(P >> 3);
					const int bit = (fb[off] >> (7 - (P & 7))) & 1;
					const bool dot = ((x & 7) == 0) && ((ay & 7) == 0);
					const bool band = (((x + ay) & 63) < 32);
					const int pat = (dot || band) ? 1 : 0;
					if (bit != pat) ++mism;
					++total;
				}
			}
			g_eng_run_status.detail = 0xFBFBu | (static_cast<eng::u32>(mism) << 16);
			return;
		}
#endif
#ifdef K_DIAG_CYCLES
		const eng::u32 t2 = eng::debug::DebugPeripheral::cycle_counter();
		g_eng_run_status.detail = t2 - t0; // ciclos del update completo
		return;
#endif
		g_eng_run_status.detail = 0x11200000u |
			((static_cast<eng::u32>(scene.bg().mapposx()) & 0xffffu) << 8) |
			(static_cast<eng::u32>(scene.bg().mapposy()) & 0xffu);
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
	engine.run_frames_polling(0xffff);
	return 0;
}
