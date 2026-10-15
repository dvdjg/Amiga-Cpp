// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/110_ylimited_shooter --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/110_ylimited_shooter --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/110_ylimited_shooter --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/110_ylimited_shooter --keep-running

// ============================================================================
// Demo 110 - Shooter vertical (limite del scroll Y) sobre XYLimited
// ============================================================================
//
// Escenario: mundo 400 px de ancho x 2048 px de alto, tiles 16x16 -> 25x128
// celdas, tileset de 128 tiles. Es un Y-limited: la camara avanza hacia ARRIBA
// (la nave no vuelve a bajar) con el anillo vertical del corkscrew; el X es
// FINITO (recorrido 0..80, sin anillo ni bandas de guarda laterales). Coste de
// Blitter proporcional al salto; el framebuffer esta acotado (el mapa solo
// ocupa indices y el tileset).
//
//   X: AxisPolicy::Finite  (puntero directo; no repinta)
//   Y: y_mode = Ring       (anillo corkscrew) + DirectionPolicy::OneWay
//
// El FG de objetos (nave + balas + torreta) se pinta sobre el lienzo plano PF2 (DPF)
// al principio del frame, antes del scroll, para que el barrido no pille el dirty-rect.

#include <eng/api/api.hpp>
#include <eng/debug/prof.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/tile_demo.hpp>
#include <eng/core/util/broadphase.hpp>
#include <eng/core/util/pathfinding.hpp>
#include <eng/core/math/fixed_math.hpp>
#include <eng/core/math/geometry.hpp>
#include <eng/core/math/interp.hpp>

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

namespace playfield = eng::playfield;

	/// Self-test de las utilidades de rejilla y búsqueda de caminos: se ejecuta en `init`
/// (en el 68000) y el demo NO llega a READY si falla. Verificación por demo de
/// `eng::util::SpatialHash` (broadphase) y `eng::util::bfs`/`reconstruct_path`.
bool util_selftest() {
	eng::util::SpatialHash<8, 8, 8, 16> grid;
	grid.clear();
	if (!grid.insert(1u, 2, 2) || !grid.insert(2u, 10, 10) || !grid.insert(3u, 2, 10)) {
		return false;
	}
	eng::u16 hits[8] = {};
	if (grid.query(eng::util::Aabb {0, 0, 16, 16}, eng::Span<eng::u16> {hits, 8}) != 3u) {
		return false;
	}
	static eng::s16 came[64];
	static eng::u16 queue[64];
	static eng::u16 path[64];
	const auto walk = [](eng::u16) { return true; };
	if (!eng::util::bfs<8, 8>(0u, 63u, walk, eng::Span<eng::s16> {came, 64},
				   eng::Span<eng::u16> {queue, 64})) {
		return false;
	}
	return eng::util::reconstruct_path<8, 8>(eng::Span<const eng::s16> {came, 64}, 0u, 63u,
						 eng::Span<eng::u16> {path, 64}) == 15u;
}

/// Self-test de las matemáticas `Fixed` (`fixed_math.hpp`) en el 68000, **sin `float`**
/// (compara valores crudos contra márgenes): `sin`/`cos`/`tan` (tabla), `asin`/`acos`/
/// `atan2` (tabla de `atan`), `sincos`/`rotate2` (una pasada), `wrap_angle`, `smooth_damp`
/// (`exp2`), `pow` (`log2`+`exp2`) y `length` (`sqrt`). Si falla, la demo no llega a READY.
bool fixed_math_selftest() {
	using q12 = eng::math::Fixed<eng::s16, 12>;
	const q12 zero {0};
	if (eng::math::scalar_sin<q12>::op(zero).v != 0) {
		return false;
	}
	const eng::s16 s_pi2 = eng::math::scalar_sin<q12>::op(q12 {6434}).v; // sin(pi/2) ≈ 1
	if (!(s_pi2 >= 4000 && s_pi2 <= 4100)) {
		return false;
	}
	const eng::s16 c_0 = eng::math::scalar_cos<q12>::op(zero).v; // cos(0) = 1
	if (!(c_0 >= 4000 && c_0 <= 4100)) {
		return false;
	}
	const q12 half =
		eng::math::smooth_damp<q12>(zero, q12 {4096}, q12 {4096}, q12 {4096}); // 0.5
	if (!(half.v >= 2000 && half.v <= 2100)) {
		return false;
	}
	const q12 pw = eng::math::scalar_pow<q12>::op(q12 {8192}, q12 {8192}); // 2^2 = 4
	if (!(pw.v >= 16200 && pw.v <= 16500)) {
		return false;
	}
	const eng::math::Vec<2, q12> v {q12 {6144}, q12 {8192}}; // |(1.5,2.0)| = 2.5
	const eng::s16 len = eng::math::length(v).v;
	if (!(len >= 10150 && len <= 10350)) {
		return false;
	}
	// Trigonometría inversa/tangente con entrada NO constante (evita el plegado del
	// compilador y ejercita la tabla de `atan` en runtime).
	volatile eng::s16 q_half_raw = 2048; // 0.5 en q12
	const q12 half_q {q_half_raw};
	const eng::s16 asin_half = eng::math::scalar_asin<q12>::op(half_q).v; // asin(0.5) ≈ 0.524
	if (!(asin_half >= 2050 && asin_half <= 2250)) {
		return false;
	}
	const eng::s16 acos_half = eng::math::scalar_acos<q12>::op(half_q).v; // acos(0.5) ≈ 1.047
	if (!(acos_half >= 4200 && acos_half <= 4400)) {
		return false;
	}
	const eng::s16 atan_11 = eng::math::scalar_atan2<q12>::op(half_q, half_q).v; // π/4 ≈ 0.785
	if (!(atan_11 >= 3100 && atan_11 <= 3350)) {
		return false;
	}
	if (eng::math::scalar_tan<q12>::op(zero).v != 0) { // tan(0) = 0
		return false;
	}
	// wrap_angle: pliega 2π+0.5 a 0.5 (sin tabla).
	volatile eng::s16 q_wrap_raw = 27785; // 2π+0.5 en q12
	const eng::s16 wrapped = eng::math::wrap_angle(q12 {q_wrap_raw}).v;
	if (!(wrapped >= 2000 && wrapped <= 2100)) {
		return false;
	}
	// sincos: seno y coseno del mismo ángulo en una sola pasada (π/2 -> (1, 0)).
	volatile eng::s16 q_sincos_raw = 6434;
	q12 sincos_s {0};
	q12 sincos_c {0};
	eng::math::scalar_sincos<q12>::op(q12 {q_sincos_raw}, sincos_s, sincos_c);
	if (!(sincos_s.v >= 4000 && sincos_s.v <= 4100 && sincos_c.v >= -100 &&
	      sincos_c.v <= 100)) {
		return false;
	}
	// rotate2 por ángulo: (1,0) girado π/2 -> (0,1) (sincos en una pasada).
	volatile eng::s16 q_rot_raw = 6434;
	const eng::math::Vec<2, q12> rv = eng::math::rotate2(
		eng::math::Vec<2, q12> {q12 {4096}, q12 {0}},
		eng::math::Angle<q12, eng::math::angle::radians> {q12 {q_rot_raw}});
	if (!(rv.v[0].v >= -100 && rv.v[0].v <= 100 && rv.v[1].v >= 4000 && rv.v[1].v <= 4100)) {
		return false;
	}
	// apuntado: angle_of/from_angle (atan2 + sincos fixed) para un vector hacia arriba.
	volatile eng::s16 q_aim_dy = 4096;
	const auto aim_ang = eng::math::angle_of(
		eng::math::Vec<2, q12> {q12 {0}, q12 {q_aim_dy}}); // π/2
	if (!(aim_ang.value.v >= 6300 && aim_ang.value.v <= 6550)) {
		return false;
	}
	const eng::math::Vec<2, q12> aim_dir = eng::math::from_angle(aim_ang); // ≈ (0,1)
	if (!(aim_dir.v[0].v >= -100 && aim_dir.v[0].v <= 100 && aim_dir.v[1].v >= 4000 &&
	      aim_dir.v[1].v <= 4100)) {
		return false;
	}
	return true;
}

constexpr eng::u16 kTileW = 16;
constexpr eng::u16 kTileH = 16;
constexpr eng::u16 kViewportW = 320;
// LÍMITE OCS: el WAIT del Copper solo compara 8 bits de línea (0..255); con split
// móvil (corkscrew) la línea de corte = DIWSTRT_y(41) + (display_height -
// display_offset) debe ser <= 255, luego el campo visible debe ser <= 214. Se usa
// 208 (13 filas de tile) como canónico (igual que 201/202). NO usar 256.
constexpr eng::u16 kViewportH = 208;
constexpr eng::u8  kPlanes = 3;            // planos POR playfield (DPF 3+3 = 6 HW)
constexpr eng::u16 kDisplayH = kViewportH + 2 * kTileH; // anillo = visible + 2 bloques de staging (240)

// Mundo: 400 x 2048 px -> 25 x 128 tiles.
constexpr eng::u16 kMapCols = 25;
constexpr eng::u16 kMapRows = 128;

// FG por hojas pre-renderizadas: 16 variantes (offset de palabra de `x`) por objeto; el
// Blitter copia palabras ya alineadas. Las cajas cubren TODA la forma (borrado + dibujo).
// Coste por frame: 4 jobs CopyRect (~3-5k cada uno) en vez de ~30 `fill_rect` (~6,5k/llamada).
constexpr eng::u16 kShipBoxW = 20u;   // bbox de la nave (x-2 .. x+17)
constexpr eng::u16 kShipBoxH = 20u;
constexpr eng::u16 kTurretBoxW = 28u; // bbox de la torreta (x-6 .. x+21)
constexpr eng::u16 kTurretBoxH = 18u;
constexpr eng::u16 kShipSheetWords = 4u;   // 64 bits: contenido en [16, 58] → cubre prev ∪ nueva
constexpr eng::u16 kTurretSheetWords = 4u; // (la copia empieza una palabra a la izquierda de `x0`)
constexpr eng::u16 kShipSheetPlaneWords = kShipSheetWords * kShipBoxH;
constexpr eng::u16 kTurretSheetPlaneWords = kTurretSheetWords * kTurretBoxH;
constexpr eng::u32 kShipSheetBytes = 16u * 3u * kShipSheetPlaneWords * 2u;
constexpr eng::u32 kTurretSheetBytes = 16u * 3u * kTurretSheetPlaneWords * 2u;
constexpr eng::u16 kTilesetCount = 128;

constexpr playfield::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileW,
	/*tile_height=*/       kTileH,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/static_cast<eng::u32>(kDisplayH) * kPlanes,
	/*planes=*/            kPlanes,
};

// Generador de filas del tileset (128 tiles = 16 glifos x 8 variantes).
eng::u16 shooter_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	return playfield::demo::pf_plane_row(glyph, static_cast<eng::u8>(variant & 3u), row, plane, 0, false);
}

// Paleta DPF de 16 registros: BG (PF1, regs 0..7) + objetos (PF2, regs 8..15).
constexpr eng::u16 kPalette[16] {
	// BG (fondo azul).
	0x000, 0x013, 0x025, 0x037, 0x049, 0x15b, 0x26d, 0x37f,
	// Objetos (naves/disparos: cian/blanco/rojo/verde).
	0x0cf, 0x7ef, 0xfff, 0xf40, 0x0f0, 0xfd0, 0xf0f, 0xaaa,
};

eng::u16 g_map[kMapCols * kMapRows] {};

/// Escribe un rect de color en una hoja de 16 variantes (layout planar: variante, plano,
/// filas × `words`). `lx`/`ly` van en coordenadas LOCALES a la caja del objeto y la variante
/// `v` desplaza el contenido `v` bits: la copia del Blitter cae en la palabra de `x`.
/// Solo se usa en el setup (el coste de init no importa).
void sheet_rect(eng::u8* base, eng::u16 v, eng::u16 words, eng::u16 rows, eng::u8 planes,
		eng::u16 lx, eng::u16 ly, eng::u16 w, eng::u16 h, eng::u8 color) {
	const eng::u16 b0 = static_cast<eng::u16>(lx + v);
	const eng::u16 b1 = static_cast<eng::u16>(lx + v + w - 1u);
	const eng::u16 first = static_cast<eng::u16>(0xffffu >> (b0 & 15u));
	const eng::u16 last = static_cast<eng::u16>(0xffffu << (15u - (b1 & 15u)));
	for (eng::u8 p = 0; p < planes; ++p) {
		for (eng::u16 r = 0; r < h; ++r) {
			eng::u16* row = reinterpret_cast<eng::u16*>(
				base + (static_cast<eng::u32>(v) * planes + p) *
					       static_cast<eng::u32>(words) * rows * 2u +
				static_cast<eng::u32>(ly + r) * words * 2u);
			for (eng::u16 wd = b0 >> 4u; wd <= (b1 >> 4u); ++wd) {
				eng::u16 mask = 0xffffu;
				if (wd == (b0 >> 4u)) mask = static_cast<eng::u16>(mask & first);
				if (wd == (b1 >> 4u)) mask = static_cast<eng::u16>(mask & last);
				const eng::u16 val = ((color >> p) & 1u) != 0u ? mask : 0u;
				row[wd] = static_cast<eng::u16>((row[wd] & ~mask) | val);
			}
		}
	}
}

struct DemoGame {
	playfield::XlimitedScene<kScrollConsts> scene {};
	playfield::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	/// Blits del FG (nave/torreta): se encolan aquí y se ejecutan en ráfaga al principio
	/// del frame (el lienzo FG es single-buffer; el haz no debe alcanzar los objetos).
	eng::graphics::FramePlan fg_plan {};
	eng::u8 patrol = 0;          // fase del vaivén X
	eng::u8 patrol_acc = 0;
	bool ready = false;

	// Objetos del juego (capa FG lienzo, regs 8..15). Nave fija + balas.
	struct Bullet { eng::s16 x = 0, y = 0, py = 0; bool live = false; };
	Bullet m_bullets[6] {};
	eng::s16 m_ship_x = 152;
	eng::s16 m_ship_px = 152;
	eng::u8 m_ship_dir = 0;
	eng::u8 m_fire = 0;
	eng::s16 m_enemy_x = 40; // torreta enemiga (patrulla + apunta al jugador)
	eng::s16 m_enemy_y = 36;
	eng::s16 m_enemy_px = 40; // posición previa (para borrar el área al moverse)
	eng::s16 m_enemy_dir = 1; // sentido del vaivén de la torreta
	eng::s16 m_cannon_px[3] {}; // puntos previos del cañón (se borran uno a uno)
	eng::s16 m_cannon_py[3] {};
	bool m_cannon_valid = false;

	// Hojas del FG (Chip, fuente del Blitter) y geometría del lienzo (interleaved).
	eng::Block<eng::BobTag> m_ship_sheet {};
	eng::Block<eng::BobTag> m_turret_sheet {};
	eng::u8* m_fg_base = nullptr;
	eng::u16 m_fg_row_bytes = 0;  // bytes por fila (plano a plano)
	eng::u32 m_fg_row_stride = 0; // salto de fila (planes × row_bytes)
	eng::u16 m_fg_guard = 0;      // guarda izquierda del lienzo (x pantalla → x bitmap)

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		// Perfilado por secciones (opt-in): scroll+blits, compose y FG. ~2 lecturas del
		// contador por sección y frame; atribuye los fields por elemento (F1).
		ENG_PROF_INIT(7);
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011001u);
			return;
		}
		// Mapa: rejilla de tiles determinista (128 tiles).
		for (eng::u16 y = 0; y < kMapRows; ++y) {
			for (eng::u16 x = 0; x < kMapCols; ++x) {
				g_map[static_cast<eng::u32>(y) * kMapCols + x] =
					static_cast<eng::u16>(playfield::demo::cell_hash(x, y, 0x5eedu) & (kTilesetCount - 1u));
			}
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTileW;
		scene_cfg.tile_height = kTileH;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.y_mode = eng::playfield::AxisPolicy::Ring;                                  // anillo corkscrew (Y)
		scene_cfg.x_mode = eng::playfield::AxisPolicy::Finite;                 // X lineal acotado
		scene_cfg.direction = eng::playfield::DirectionPolicy::OneWay;    // solo fila entrante
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 4;

		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_map, kMapCols * kMapRows);
		scene_cfg.map.width = kMapCols;
		scene_cfg.map.height = kMapRows;
		// Toroide: el mapa se enrolla en Y (scroll infinito real; la fila -1 es la
		// 127) → sin teleport de reinicio. El motor envuelve la fila pintada.
		scene_cfg.map.wrap_y = kMapRows;
		scene_cfg.map.wrap_x = 0;    // X finito: sin wrap
		scene_cfg.map.wrap_y = 0;    // Y acotado (se recorre de abajo arriba)
		scene_cfg.map.edge_tile = 0;

		scene_cfg.tileset_count = kTilesetCount;
		scene_cfg.fg_row_fn = &shooter_row;
		scene_cfg.bg_row_fn = &shooter_row;
		scene_cfg.palette = kPalette;
		// DPF: BG = corkscrew XYLimited (PF1, regs 0..7); FG = lienzo plano de
		// objetos (PF2, regs 8..15) que el juego dibuja cada frame.
		scene_cfg.dpf.enabled = true;
		scene_cfg.dpf.fg_canvas = true;
		scene_cfg.dpf.foreground_is_pf2 = true; // objetos (PF2) DELANTE del BG

		if (!scene.begin(backend.memory_manager(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011002u);
			return;
		}
		// FG por hojas + Blitter: el lienzo (interleaved, con guarda izquierda) se toca con
		// 4 jobs `CopyRect` por frame + escrituras CPU diminutas (balas/cañón). Sin
		// `fill_rect`: cada llamada costaba ~6,5k ciclos (medido; ATRIBUCION_DE_CICLOS.md).
		{
			const eng::playfield::PlayfieldHardwareView hv = scene.canvas_fg().hardware_view();
			m_fg_base = hv.bitplanes.ptr();
			m_fg_row_bytes = hv.bitmap_bytes_per_row;
			m_fg_row_stride = static_cast<eng::u32>(hv.planes) * hv.bitmap_bytes_per_row;
			m_fg_guard = static_cast<eng::u16>(hv.bitmap_bytes_per_row * 8u - hv.viewport_w);
		}
		m_ship_sheet = backend.memory_manager().chip().reserve<eng::BobTag>(kShipSheetBytes, 16u);
		m_turret_sheet =
			backend.memory_manager().chip().reserve<eng::BobTag>(kTurretSheetBytes, 16u);
		if (!m_ship_sheet.valid() || !m_turret_sheet.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011007u);
			return;
		}
		build_fg_sheets();
		build_fg_sheets();
		// Arranca abajo del mundo, a media anchura (la nave sube). Con `wrap_y` el
		// scroll es infinito: al pasar la fila 0 continúa por la 127 (sin teleport).
		scene.bg().set_camera(kViewportW / 4, static_cast<eng::s32>(kMapRows * kTileH) - kViewportH);
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011003u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011004u);
			return;
		}
		scene.takeover(backend);
		if (!util_selftest()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011005u);
			return;
		}
		if (!fixed_math_selftest()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011006u);
			return;
		}
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x11000000u);
	}

	// --- FG por hojas + Blitter -------------------------------------------------

	/// Encola una copia de caja del FG (`CopyRect` en `fg_plan`): destino **interleaved**
	/// del lienzo (plano p a `row_bytes`, filas a `planes*row_bytes`), en palabras ya
	/// alineadas (`bx >> 4`). `src_mod` repite la fila fuente (borrado: `-(words*2)`).
	void blit_box(eng::s16 bx, eng::s16 by, const eng::u16* src, eng::s16 src_mod,
		      eng::u32 src_plane_stride, eng::u16 words, eng::u16 rows) {
		if (bx < 0 || by < 0) return; // la trayectoria acotada lo garantiza
		eng::graphics::BlitJob job {};
		job.kind = eng::graphics::BlitJobKind::CopyRect;
		job.source = eng::graphics::BlitPtr::from_storage(src);
		job.destination = eng::graphics::BlitPtr::from_storage(
			reinterpret_cast<eng::u16*>(m_fg_base +
				static_cast<eng::u32>(by) * m_fg_row_stride) +
			(static_cast<eng::u16>(bx) >> 4u));
		job.words_per_row = words;
		job.height = rows;
		job.source_modulo_bytes = src_mod;
		job.destination_modulo_bytes = eng::graphics::mod16(
			static_cast<eng::s32>(m_fg_row_stride) - static_cast<eng::s32>(words) * 2);
		job.bitplane_count = 3u;
		job.source_plane_stride_bytes = src_plane_stride;
		job.destination_plane_stride_bytes = m_fg_row_bytes;
		(void)fg_plan.add_copy_rect(job);
	}

	/// Relleno CPU directo sobre el lienzo FG (balas y puntos del cañón: demasiado pequeños
	/// para un job). Escribe SOLO los planos con bit a 1 en `color`: el lienzo FG solo
	/// contiene objetos, así que un plano sin bits del objeto ya está a 0 en su caja
	/// (`fg_clear` limpia los planos del objeto anterior). Sin multiplicaciones en el bucle.
	void fg_fill(eng::s16 x, eng::s16 y, eng::u16 w, eng::u16 h, eng::u8 color) {
		if (x < 0 || y < 0 || w == 0u || h == 0u) return;
		const eng::u16 bx0 = static_cast<eng::u16>(x + static_cast<eng::s16>(m_fg_guard));
		const eng::u16 bx1 = static_cast<eng::u16>(bx0 + w - 1u);
		const eng::u16 w0 = bx0 >> 4u;
		const eng::u16 w1 = bx1 >> 4u;
		const eng::u16 first = static_cast<eng::u16>(0xffffu >> (bx0 & 15u));
		const eng::u16 last = static_cast<eng::u16>(0xffffu << (15u - (bx1 & 15u)));
		const eng::u32 row_step = m_fg_row_stride >> 1u; // en palabras
		const eng::u32 plane_step = m_fg_row_bytes >> 1u;
		eng::u16* row = reinterpret_cast<eng::u16*>(
				       m_fg_base + static_cast<eng::u32>(y) * m_fg_row_stride) +
			       w0;
		for (eng::u8 p = 0; p < 3u; ++p) {
			if (((color >> p) & 1u) == 0u) continue;
			eng::u16* r = row + static_cast<eng::u32>(p) * plane_step;
			for (eng::u16 rr = 0; rr < h; ++rr) {
				eng::u16* cell = r;
				for (eng::u16 wd = w0; wd <= w1; ++wd, ++cell) {
					eng::u16 mask = 0xffffu;
					if (wd == w0) mask = static_cast<eng::u16>(mask & first);
					if (wd == w1) mask = static_cast<eng::u16>(mask & last);
					*cell = static_cast<eng::u16>(*cell | mask);
				}
				r += row_step;
			}
		}
	}

	/// Borrado CPU directo: limpia los planos de `plane_mask` en la caja (los que ocupaba
	/// el objeto anterior; p. ej. bala = plano 1, cañón = planos 0+2).
	void fg_clear(eng::s16 x, eng::s16 y, eng::u16 w, eng::u16 h, eng::u8 plane_mask) {
		if (x < 0 || y < 0 || w == 0u || h == 0u) return;
		const eng::u16 bx0 = static_cast<eng::u16>(x + static_cast<eng::s16>(m_fg_guard));
		const eng::u16 bx1 = static_cast<eng::u16>(bx0 + w - 1u);
		const eng::u16 w0 = bx0 >> 4u;
		const eng::u16 w1 = bx1 >> 4u;
		const eng::u16 first = static_cast<eng::u16>(0xffffu >> (bx0 & 15u));
		const eng::u16 last = static_cast<eng::u16>(0xffffu << (15u - (bx1 & 15u)));
		const eng::u32 row_step = m_fg_row_stride >> 1u;
		const eng::u32 plane_step = m_fg_row_bytes >> 1u;
		eng::u16* row = reinterpret_cast<eng::u16*>(
				       m_fg_base + static_cast<eng::u32>(y) * m_fg_row_stride) +
			       w0;
		for (eng::u8 p = 0; p < 3u; ++p) {
			if (((plane_mask >> p) & 1u) == 0u) continue;
			eng::u16* r = row + static_cast<eng::u32>(p) * plane_step;
			for (eng::u16 rr = 0; rr < h; ++rr) {
				eng::u16* cell = r;
				for (eng::u16 wd = w0; wd <= w1; ++wd, ++cell) {
					eng::u16 mask = 0xffffu;
					if (wd == w0) mask = static_cast<eng::u16>(mask & first);
					if (wd == w1) mask = static_cast<eng::u16>(mask & last);
					*cell = static_cast<eng::u16>(*cell & ~mask);
				}
				r += row_step;
			}
		}
	}

	/// Prerenderiza las hojas del FG (setup): nave y torreta con sus detalles en 16
	/// variantes de offset de palabra. Cero coste por frame.
	void build_fg_sheets() {
		eng::u8* const ship = m_ship_sheet.view.data();
		eng::u8* const turret = m_turret_sheet.view.data();
		for (eng::u16 v = 0; v < 16u; ++v) {
			// Nave (origen local = (x-2, y-8), contenido en el bit 16+v): morro, cabina,
			// cuerpo, base, motor.
			sheet_rect(ship, v, kShipSheetWords, kShipBoxH, 3u, 25u, 2u, 2u, 4u, 2u);
			sheet_rect(ship, v, kShipSheetWords, kShipBoxH, 3u, 24u, 6u, 4u, 3u, 6u);
			sheet_rect(ship, v, kShipSheetWords, kShipBoxH, 3u, 21u, 9u, 10u, 4u, 1u);
			sheet_rect(ship, v, kShipSheetWords, kShipBoxH, 3u, 18u, 13u, 16u, 4u, 3u);
			sheet_rect(ship, v, kShipSheetWords, kShipBoxH, 3u, 23u, 17u, 6u, 3u, 4u);
			// Torreta (origen local = (x-6, y-1), contenido en el bit 16+v): base, cuerpo,
			// cúpula, ojo.
			sheet_rect(turret, v, kTurretSheetWords, kTurretBoxH, 3u, 22u, 13u, 16u, 4u, 3u);
			sheet_rect(turret, v, kTurretSheetWords, kTurretBoxH, 3u, 24u, 7u, 12u, 6u, 4u);
			sheet_rect(turret, v, kTurretSheetWords, kTurretBoxH, 3u, 27u, 3u, 6u, 4u, 6u);
			sheet_rect(turret, v, kTurretSheetWords, kTurretBoxH, 3u, 29u, 4u, 2u, 2u, 7u);
		}
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!ready) return;
		ENG_PROF_FRAME();

		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 160});
		fg_plan.clear();
		fg_plan.set_blit_budget_limits({8192, 16384, 4, 160});

		// --- FG: objetos (nave + balas + torreta) -----------------------------
		// Se pinta NADA MÁS arrancar el frame, antes del scroll/compose: el lienzo FG es
		// single-buffer y el haz ya barre la torreta (y≈36) ~3 ms tras el VBlank. Si el FG
		// se pinta después del scroll, el borrado+repintado coincide con el barrido y la
		// torreta se ve a medias o desaparece (flicker). El color 0 de PF2 es transparente.
		ENG_PROF_BEGIN(2); // sección 2: FG (objetos)
		const eng::s16 ship_y = static_cast<eng::s16>(kViewportH - 16); // fila visible más baja
		{
			ENG_PROF_BEGIN(4); // sección 4 (diagnóstico): estado + puntería (trig)

			// 1) Estado nuevo (sin tocar el bitmap): X de la nave, disparo y balas.
			if (m_ship_dir == 0) { ++m_ship_x; if (m_ship_x >= 200) m_ship_dir = 1; }
			else { --m_ship_x; if (m_ship_x <= 40) m_ship_dir = 0; }
			if ((++m_fire & 7u) == 0u) {
				for (auto& b : m_bullets) {
					if (!b.live) { b.live = true; b.x = static_cast<eng::s16>(m_ship_x + 7); b.y = static_cast<eng::s16>(ship_y - 8); break; }
				}
			}

			// 2) Cañón de la torreta (trigonometría fixed con tablas, sin float) con la
			//    posición nueva de la nave; se calcula ANTES de borrar para no dejar hueco.
			const eng::s16 ship_cx = static_cast<eng::s16>(m_ship_x + 7);
			const eng::s16 ship_cy = static_cast<eng::s16>(ship_y + 4);
			using qa = eng::math::Fixed<eng::s16, 6>; // rango ±511 px: cabe el delta de pantalla
			const eng::s16 ex = static_cast<eng::s16>(m_enemy_x + 8);
			const eng::s16 ey = static_cast<eng::s16>(m_enemy_y + 8);
			const qa ddx = eng::math::scalar_traits<qa>::from_int(ship_cx - ex);
			const qa ddy = eng::math::scalar_traits<qa>::from_int(ship_cy - ey);
			const auto aim = eng::math::angle_of(eng::math::Vec<2, qa> {{ddx, ddy}});
			const eng::math::Vec<2, qa> dir = eng::math::from_angle(aim);
			ENG_PROF_END(4);

			ENG_PROF_BEGIN(5); // sección 5: nave + balas
			// 3) Nave: UN blit — la copia empieza una palabra a la izquierda de la caja y mide
			//    4 palabras: el contenido (bit 16+v) cubre la posición NUEVA y la anterior
			//    (movimiento ≤ 16 px/frame), así que no hace falta borrado aparte. La copia
			//    va alineada a palabra (16 variantes en la hoja).
			{
				const eng::s16 bx = static_cast<eng::s16>(m_ship_x - 2 + m_fg_guard - 16);
				const eng::u16 v = static_cast<eng::u16>((bx + 16) & 15);
				blit_box(bx, static_cast<eng::s16>(ship_y - 8),
					 reinterpret_cast<const eng::u16*>(m_ship_sheet.view.data()) +
						 static_cast<eng::u32>(v) * 3u * kShipSheetPlaneWords,
					 0, kShipSheetPlaneWords * 2u, kShipSheetWords, kShipBoxH);
				m_ship_px = m_ship_x;
			}

			// 4) Balas: CPU directo (2×6 px): borrado de la posición previa + avance +
			//    repintado pegados (mismo par de operaciones, sin trabajo intermedio).
			for (auto& b : m_bullets) {
				if (!b.live) continue;
				fg_clear(b.x, b.py, 2u, 6u, 2u); // la bala vive en el plano 1 (color 2)
				b.y = static_cast<eng::s16>(b.y - 6);
				if (b.y < 2) { b.live = false; continue; }
				fg_fill(b.x, b.y, 2u, 5u, 2u);
				b.py = b.y;
			}
			ENG_PROF_END(5);

			ENG_PROF_BEGIN(6); // sección 6: torreta + cañón
			// 5) Torreta enemiga: vaivén lento (además de apuntar al jugador); 2 blits
			//    (borrado + copia de variante). El cañón son 3 puntos que siguen a la nave.
			m_enemy_x += m_enemy_dir;
			if (m_enemy_x >= 120) m_enemy_dir = -1;
			else if (m_enemy_x <= 24) m_enemy_dir = 1;
			if (m_cannon_valid) {
				for (eng::u8 i = 0; i < 3u; ++i) {
					fg_clear(m_cannon_px[i], m_cannon_py[i], 2u, 2u, 5u); // puntos: planos 0+2
				}
			}
			{
				const eng::s16 bx = static_cast<eng::s16>(m_enemy_x - 6 + m_fg_guard - 16);
				const eng::u16 v = static_cast<eng::u16>((bx + 16) & 15);
				blit_box(bx, static_cast<eng::s16>(m_enemy_y - 1),
					 reinterpret_cast<const eng::u16*>(m_turret_sheet.view.data()) +
						 static_cast<eng::u32>(v) * 3u * kTurretSheetPlaneWords,
					 0, kTurretSheetPlaneWords * 2u, kTurretSheetWords, kTurretBoxH);
				m_enemy_px = m_enemy_x;
			}
			for (eng::u8 i = 0; i < 3u; ++i) {
				const qa li = eng::math::scalar_traits<qa>::from_int(
					static_cast<int>(2u + i * 2u));
				m_cannon_px[i] = static_cast<eng::s16>(
					ex + (eng::math::mul_norm(dir.v[0], li).v >> 6));
				m_cannon_py[i] = static_cast<eng::s16>(
					ey + (eng::math::mul_norm(dir.v[1], li).v >> 6));
				fg_fill(m_cannon_px[i], m_cannon_py[i], 2u, 2u, 5u);
			}
			m_cannon_valid = true;
			ENG_PROF_END(6);
		}
		// Ejecuta la ráfaga del FG AQUÍ (temprano): el lienzo FG es single-buffer y el haz
		// barre la torreta (y≈36) ~5.6 ms tras el VBlank; los 4 jobs terminan antes.
		if (!backend.execute_frame_plan(fg_plan)) {
			ready = false;
			return;
		}
		ENG_PROF_END(2);

		// --- BG: scroll + composición (tras el FG: el blitter tarda más y su contenido no
		//     tiene el hueco de borrado del dirty-rect) --------------------------
		// Vaivén X lento (0..80) y avance Y hacia arriba (-2 px/frame).
		if (++patrol_acc >= 4) { patrol_acc = 0; ++patrol; }
		const eng::s32 target_x = (patrol & 128u) ? 80 : 0;
		const eng::s32 cur_x = scene.bg().mapposx();
		const eng::s32 dx = (target_x > cur_x) ? (target_x - cur_x > 2 ? 2 : target_x - cur_x)
		                                        : (cur_x - target_x > 2 ? -2 : target_x - cur_x);
		const eng::s32 dy = -2;

		ENG_PROF_BEGIN(0); // sección 0: cálculo de scroll (update_scroll)
		if (!scene.bg().update_scroll(plan, dx, dy)) {
			// Tope del mundo (arriba): reinicia abajo (demo infinita).
			scene.bg().set_camera(kViewportW / 4, static_cast<eng::s32>(kMapRows * kTileH) - kViewportH);
		}
		ENG_PROF_END(0);
		ENG_PROF_BEGIN(3); // sección 3: ejecución del plan (blits)
		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00011010u);
			return;
		}
		ENG_PROF_END(3);
		ENG_PROF_BEGIN(1); // sección 1: compose (copperlist)
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00011011u);
			return;
		}
		ENG_PROF_END(1);

		// Telemetría: cámara X/Y para el assert de movimiento en regresión.
		g_eng_run_status.detail = 0x11000000u |
			((static_cast<eng::u32>(scene.bg().mapposx()) & 0x3ffu) << 10) |
			(static_cast<eng::u32>(scene.bg().mapposy()) & 0x3ffu);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (ready) scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
