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
// El FG de objetos (DPF) se anade en una fase posterior; esta demo es el BG.

#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/tile_demo.hpp>
#include <eng/core/util/broadphase.hpp>
#include <eng/core/util/pathfinding.hpp>
#include <eng/core/fixed_math.hpp>
#include <eng/core/geometry.hpp>
#include <eng/core/interp.hpp>

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
/// (compara valores crudos contra márgenes): `sin`/`cos` (tabla), `smooth_damp`
/// (`exp2`), `pow` (`log2`+`exp2`) y `length` (`sqrt`). Si falla, la demo no llega a
/// READY.
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
	return len >= 10150 && len <= 10350;
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
constexpr eng::u16 kDisplayH = 288;        // anillo = 256 + 2*16

// Mundo: 400 x 2048 px -> 25 x 128 tiles.
constexpr eng::u16 kMapCols = 25;
constexpr eng::u16 kMapRows = 128;
constexpr eng::u16 kTilesetCount = 128;

constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileW,
	/*tile_height=*/       kTileH,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/static_cast<eng::u32>(kDisplayH) * kPlanes,
	/*planes=*/            kPlanes,
};

// Generador de filas del tileset (128 tiles = 16 glifos x 8 variantes).
eng::u16 shooter_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	return field::demo::pf_plane_row(glyph, static_cast<eng::u8>(variant & 3u), row, plane, 0, false);
}

// Paleta DPF de 16 registros: BG (PF1, regs 0..7) + objetos (PF2, regs 8..15).
constexpr eng::u16 kPalette[16] {
	// BG (fondo azul).
	0x000, 0x013, 0x025, 0x037, 0x049, 0x15b, 0x26d, 0x37f,
	// Objetos (naves/disparos: cian/blanco/rojo/verde).
	0x0cf, 0x7ef, 0xfff, 0xf40, 0x0f0, 0xfd0, 0xf0f, 0xaaa,
};

eng::u16 g_map[kMapCols * kMapRows] {};

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
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

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011001u);
			return;
		}
		// Mapa: rejilla de tiles determinista (128 tiles).
		for (eng::u16 y = 0; y < kMapRows; ++y) {
			for (eng::u16 x = 0; x < kMapCols; ++x) {
				g_map[static_cast<eng::u32>(y) * kMapCols + x] =
					static_cast<eng::u16>(field::demo::cell_hash(x, y, 0x5eedu) & (kTilesetCount - 1u));
			}
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTileW;
		scene_cfg.tile_height = kTileH;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.y_mode = eng::field::AxisPolicy::Ring;                                  // anillo corkscrew (Y)
		scene_cfg.x_mode = eng::field::AxisPolicy::Finite;                 // X lineal acotado
		scene_cfg.direction = eng::field::DirectionPolicy::OneWay;    // solo fila entrante
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 4;

		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_map, kMapCols * kMapRows);
		scene_cfg.map.width = kMapCols;
		scene_cfg.map.height = kMapRows;
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

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011002u);
			return;
		}
		// Arranca abajo del mundo, a media anchura (la nave sube).
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

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;

		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 160});

		// Vaivén X lento (0..80) y avance Y hacia arriba (-2 px/frame).
		if (++patrol_acc >= 4) { patrol_acc = 0; ++patrol; }
		const eng::s32 target_x = (patrol & 128u) ? 80 : 0;
		const eng::s32 cur_x = scene.bg().mapposx();
		const eng::s32 dx = (target_x > cur_x) ? (target_x - cur_x > 2 ? 2 : target_x - cur_x)
		                                        : (cur_x - target_x > 2 ? -2 : target_x - cur_x);
		const eng::s32 dy = -2;

		if (!scene.bg().update_scroll(plan, dx, dy)) {
			// Tope del mundo (arriba): reinicia abajo (demo infinita).
			scene.bg().set_camera(kViewportW / 4, static_cast<eng::s32>(kMapRows * kTileH) - kViewportH);
		}
		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00011010u);
			return;
		}
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00011011u);
			return;
		}

		// --- FG: objetos (nave + balas) en el lienzo plano (PF2). --------------
		// Dirty-rect: se borra lo del frame anterior y se pinta lo nuevo. El color
		// 0 de PF2 es transparente (deja ver el BG).
		const eng::s16 ship_y = 208;
		{
			auto fg = scene.canvas_fg_surface();
			// Borra nave (posición previa) y balas (posición previa).
			fg.fill_rect(m_ship_px, ship_y, 16, 14, 0);
			for (auto& b : m_bullets) if (b.live) fg.fill_rect(b.x, b.py, 2, 6, 0);

			// Avanza la nave en X (vaivén 40..200) y dispara.
			if (m_ship_dir == 0) { ++m_ship_x; if (m_ship_x >= 200) m_ship_dir = 1; }
			else { --m_ship_x; if (m_ship_x <= 40) m_ship_dir = 0; }
			if ((++m_fire & 7u) == 0u) {
				for (auto& b : m_bullets) {
					if (!b.live) { b.live = true; b.x = static_cast<eng::s16>(m_ship_x + 7); b.y = static_cast<eng::s16>(ship_y - 8); break; }
				}
			}
			for (auto& b : m_bullets) {
				if (!b.live) continue;
				b.y = static_cast<eng::s16>(b.y - 6);
				if (b.y < 2) b.live = false;
			}

			// Pinta nave (silueta) y balas. Color 0 = transparente; 1..7 = regs 9..15.
			fg.fill_rect(static_cast<eng::s16>(m_ship_x + 7), static_cast<eng::s16>(ship_y - 4), 2, 4, 2);  // morro
			fg.fill_rect(static_cast<eng::s16>(m_ship_x + 5), ship_y, 6, 4, 1);
			fg.fill_rect(static_cast<eng::s16>(m_ship_x + 3), static_cast<eng::s16>(ship_y + 4), 10, 4, 1);
			fg.fill_rect(m_ship_x, static_cast<eng::s16>(ship_y + 8), 16, 4, 3);                          // base
			for (auto& b : m_bullets) if (b.live) { fg.fill_rect(b.x, b.y, 2, 5, 2); b.py = b.y; }
			m_ship_px = m_ship_x;
		}

		// Telemetría: cámara X/Y para el assert de movimiento en regresión.
		g_eng_run_status.detail = 0x11000000u |
			((static_cast<eng::u32>(scene.bg().mapposx()) & 0x3ffu) << 10) |
			(static_cast<eng::u32>(scene.bg().mapposy()) & 0x3ffu);
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
