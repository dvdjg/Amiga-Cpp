// Demo 120 — PROC WORLD SCROLL: el mundo de biomas de `eng::gfx::proc` con SCROLL.
// ----------------------------------------------------------------------------
// Tutorial: el mundo generado EN COMPILACIÓN (`constexpr`, ver la demo 119) se publica
// como **mapa de scroll X-Limited**: el `PatternLayout` de 40×28 celdas de 16 px se
// envuelve como `TileLayerMap` (toroidal) y `XlimitedScene` lo recorre con el corkscrew
// OCS — solo se blittea la columna que entra al cruzar cada límite de 16 px; el resto de
// la pantalla no se toca. La cámara avanza 1 px/frame en X de forma continua.
//
// Piezas del sistema:
//   - **Mundo**: `generate_biome_world` constexpr (91 piezas de 16×16, paleta de 32) —
//     `tools/gen_assets.cpp` de la 119 emite los MISMOS datos como assets offline.
//   - **Banco de tiles**: `indexed_tiles` (1 B/píxel, stride 256) → la escena lo convierte
//     en el banco interleaved X-Limited al arrancar (5 planos estándar, sin EHB).
//   - **Scroll**: `XlimitedScene` con `ScrollConsts` (divisores en compilación, sin
//     `__udivsi3` en el camino caliente) y conductor de presupuesto de Blitter.
//   - **HUD**: franja inferior de 48 px (4 planos, paleta propia) con nombre + versión
//     (norma del repo) y telemetría viva (frames).
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/120_proc_world_scroll --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/120_proc_world_scroll --warp
//
// (El build tarda ~1,5 min de más: el compilador evalúa el atlas + el mundo 40×28.)
#include <eng/api/api.hpp>
#include <eng/core/math/minifloat.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/terrain_biome.hpp>
#include <eng/platform/amiga/backend.hpp>

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

using eng::u8;
using eng::u16;
using eng::u32;
using MF = eng::math::MiniFloat16;
namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
namespace playfield = eng::playfield;

// -----------------------------------------------------------------------------
// Mundo (generado EN COMPILACIÓN: mismas semillas que la 119 y la vía offline)
// -----------------------------------------------------------------------------
constexpr u32 kAtlasSeed = 0x7E5A1u;
constexpr u32 kMapSeed = 0xBEE5u;
constexpr u16 kTile = 16u;
constexpr u16 kMapW = 40u;
constexpr u16 kMapH = 28u;

[[nodiscard]] constexpr proc::TextureAtlas<kTile, kTile, MF> make_world_atlas() {
	proc::TextureAtlas<kTile, kTile, MF> a {};
	a.generate(kAtlasSeed);
	return a;
}
inline constexpr proc::TextureAtlas<kTile, kTile, MF> kWorldAtlas = make_world_atlas();

/// Mundo generado en compilación (layout + set); el banco de tiles se construye desde
/// aquí con `indexed_tiles` (1 B/píxel por tile: stride 256 = 16×16).
struct WorldData {
	terr::TerrainLayout<kMapW, kMapH> layout {};
	terr::BiomeTileSet<kTile, kTile, 400u> set {};
};
[[nodiscard]] constexpr WorldData make_world() {
	WorldData w {};
	(void)terr::generate_biome_world<kMapW, kMapH>(w.layout, w.set, kWorldAtlas, kMapSeed);
	return w;
}
inline constexpr WorldData kWorld = make_world();
static_assert(kWorld.set.count <= 400u, "el set del mundo grande cabe en el banco");

// Vista del tilebank INDEXADO: los `BitmapBuffer<16,16>` del set son contiguos (pixels de
// 256 B por tile) y viven en rodata — la escena los lee tal cual para construir el banco.
[[nodiscard]] constexpr eng::TileBankBytes indexed_tiles_view() {
	return eng::TileBankBytes {kWorld.set.tiles[0].pixels, u32(kWorld.set.count) * 256u};
}

// -----------------------------------------------------------------------------
// Geometría del scroll (constantes a priori: divisores en compilación)
// -----------------------------------------------------------------------------
constexpr u16 kViewportW = 320u;
constexpr u16 kViewportH = 256u;             // total en pantalla (campo + HUD)
constexpr u16 kHudH = 48u;                   // franja inferior (4 planos, paleta propia)
constexpr u16 kMainH = u16(kViewportH - kHudH); // campo visible: 208 (corkscrew holgado)
constexpr u8 kPlanes = 5u;                   // 32 colores estándar (los del mundo)
constexpr u16 kDisplayH = u16(kViewportH + 2u * kTile); // anillo: 288 (invariante 201)
constexpr playfield::ScrollConsts kScrollConsts {
	/*tile_width=*/kTile,
	/*tile_height=*/kTile,
	/*display_height=*/kDisplayH,
	/*display_planelines=*/u16(kDisplayH * kPlanes),
	/*planes=*/kPlanes,
};

/// Formato de la demo (major.minor; el build lo inyecta como -DENG_BUILD_NUMBER).
constexpr eng::debug::Version kVersion {1u, 0u};

// Paleta del HUD (UN plano = 2 colores): fondo negro, texto blanco. El HUD de
// depuración debe ser lo más sencillo posible: 1 bitplane y texto impreso por palabras.
eng::u16 g_hud_palette[2] { 0x000u, 0xFFFu };
// Paleta del CAMPO (32 colores del atlas; se rellena en `init` desde el constexpr).
eng::u16 g_world_palette[32] {};

/// Número decimal a cadena (para la telemetría del HUD).
void u16_str(u16 v, char* out) {
	char tmp[6] {};
	u8 i = 0u;
	do {
		tmp[i++] = char('0' + (v % 10u));
		v = u16(v / 10u);
	} while (v != 0u && i < 5u);
	u8 n = 0u;
	while (i > 0u) out[n++] = tmp[--i];
	out[n] = '\0';
}

// -----------------------------------------------------------------------------
// Juego
// -----------------------------------------------------------------------------
struct ScrollWorldDemo {
	playfield::XlimitedScene<kScrollConsts> scene {};
	playfield::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	u32 m_frames = 0u;
	char m_frame_text[12] = "F 0";
	bool m_ready = false;
	eng::debug::HostHud m_host_hud {}; ///< HUD del host (nombre/número/versión/frame/fps)
	bool m_hud_dirty = true;
	bool m_hud_cleared = false; // el lienzo del HUD se limpia una vez (ver `draw_hud`)

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Chip: anillo (208+32 filas × 5 planos) + banco de bloques (~16 KB) + copper.
		if (!backend.configure_memory({200u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012001u);
			return;
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.hud.height = kHudH;
		// HUD de depuración **lo más sencillo posible**: UN bitplane con paleta propia de
		// 2 colores (la zona de overlay conmuta BPLCON0/BPLxPT + COLOR00/01 vía
		// `ModeSwitchZone`) y el texto se imprime por **palabras de 16 px** con
		// `Surface::draw_text_words` (dos glifos por palabra; nunca píxel a píxel).
		scene_cfg.hud.planes = 1u;
		scene_cfg.hud.palette = g_hud_palette;
		scene_cfg.tile_width = kTile;
		scene_cfg.tile_height = kTile;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0u;
		scene_cfg.y_mode = playfield::AxisPolicy::Ring;
		scene_cfg.direction = playfield::DirectionPolicy::Bidirectional;
		scene_cfg.linear_display = false; // main=208: el split cabe en el comparador de 8 bits
		scene_cfg.max_step = 2u;
		scene_cfg.map.cells = kWorld.layout.span();
		scene_cfg.map.width = kMapW;
		scene_cfg.map.height = kMapH;
		scene_cfg.map.wrap_x = kMapW; // toroidal en X: scroll continuo sin bordes
		scene_cfg.map.wrap_y = kMapH;
		scene_cfg.map.edge_tile = 0u;
		scene_cfg.map.empty_tile = 0xFFFFu;
		scene_cfg.visible_tile_bias_x = 1u; // offset (0,0) visible = map[0][0]
		scene_cfg.visible_tile_bias_y = 1u;
		scene_cfg.tileset_count = kWorld.set.count;
		scene_cfg.indexed_tiles = indexed_tiles_view();
		scene_cfg.indexed_stride = 256u; // bytes por tile (16×16, 1 B/píxel)
		for (u8 i = 0u; i < 32u; ++i) {
			g_world_palette[i] = kWorldAtlas.palette.color[i];
		}
		scene_cfg.palette = g_world_palette;

		if (!scene.begin(backend.memory_manager(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012002u);
			return;
		}
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012003u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012004u);
			return;
		}
		scene.takeover(backend);
		draw_hud();
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x00000120u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) {
			return;
		}
		// Ablaciones de medición (compiladas, temporales): saltan una etapa para cuantificar
		// su coste con `measure-fps` (bit0 compose, bit1 update/execute, bit2 install).
#if defined(K120_SKIP_COMPOSE)
		const u8 ab = 1u;
#elif defined(K120_SKIP_UPDATE)
		const u8 ab = 2u;
#elif defined(K120_SKIP_INSTALL)
		const u8 ab = 4u;
#else
		const u8 ab = 0u;
#endif
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});
		if (
#if defined(K120_PROBE_ABLATE)
			(ab & 2u) == 0u
#else
			true
#endif
		) {
			// Scroll X continuo de 1 px/frame (Y fija): al cruzar cada límite de 16 px
			// la escena blittea solo la columna entrante.
			scene.update(plan, 1, 0, context.frame.frame_index);
			if (!backend.execute_frame_plan(plan)) {
				m_ready = false;
				eng::debug::mark_failed(g_eng_run_status, 0x00012010u);
				return;
			}
		}
		if (
#if defined(K120_PROBE_ABLATE)
			(ab & 1u) == 0u
#else
			true
#endif
		) {
			if (!scene.compose()) {
				m_ready = false;
				eng::debug::mark_failed(g_eng_run_status, 0x00012011u);
				return;
			}
		}
		++m_frames;
		m_host_hud.draw(backend.debug(), "120 PROC WORLD SCROLL", kVersion, m_frames,
				eng::debug::prof_clock());
#if !defined(K120_SKIP_HUD)
		if ((m_frames & 63u) == 0u) {
			// Contador del HUD ~1 vez/segundo; el redibujado completo es ~100 escrituras
			// de palabra (se imprime por palabras, ver `draw_hud`), coste despreciable.
			char num[8];
			u16_str(u16(m_frames & 0xFFFFu), num);
			u8 n = 0u;
			m_frame_text[n++] = 'F';
			m_frame_text[n++] = ' ';
			for (u8 i = 0u; num[i] != '\0' && n < 11u; ++i) m_frame_text[n++] = num[i];
			m_frame_text[n] = '\0';
			m_hud_dirty = true;
		}
#endif
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (m_ready) {
#if defined(K120_SKIP_INSTALL)
			const u8 ab = 4u;
#else
			const u8 ab = 0u;
#endif
#if !defined(K120_SKIP_HUD)
			if (m_hud_dirty) {
				draw_hud();
				m_hud_dirty = false;
			}
#endif
			if ((ab & 4u) == 0u) {
				scene.install(backend);
			}
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Pinta la franja HUD (1 bitplane): nombre + versión (norma del repo) + contador de
	/// frames. El lienzo NO nace limpio (memoria Chip): se borra entero UNA vez, en el
	/// primer pintado. El texto se imprime con `Surface::draw_text_words` (genérico en
	/// planos/color; aquí 1 plano y tinta 1), que compone **dos glifos por palabra de
	/// 16 px** y escribe palabras consecutivas del plano (ocho escrituras por par de
	/// glifos, patrón + fondo de golpe): el redibujado completo de la línea son ~100
	/// escrituras de palabra, sin bucle por píxel ni por bit.
	///
	/// La celda inicial (x=16, múltiplo de 16 para la alineación a palabra) queda tras la
	/// guarda de fetch del corkscrew, que no se ve (como en la 201).
	void draw_hud() {
		constexpr eng::s32 kHudTextX = 16;
		auto hud = scene.hud_surface();
		if (!m_hud_cleared) {
			hud.fill_rect(0, 0, 512, kHudH, 0u);
			m_hud_cleared = true;
		}
		char label[24];
		const u8 vlen = eng::debug::version_label(kVersion, label, sizeof(label));
		char line1[40] = "PROC WORLD SCROLL ";
		u8 n = 18u;
		for (u8 i = 0u; i < vlen && n + 1u < sizeof(line1); ++i) line1[n++] = label[i];
		line1[n] = '\0';
		// La ventana útil del lienzo del HUD en esta geometría muestra una sola línea de
		// 8 px: nombre + versión (norma del repo) + contador de frames.
		const char* sep = "  ";
		for (const char* c = sep; *c != '\0' && n + 1u < sizeof(line1); ++c) line1[n++] = *c;
		for (const char* c = m_frame_text; *c != '\0' && n + 1u < sizeof(line1); ++c) line1[n++] = *c;
		line1[n] = '\0';
		(void)hud.draw_text_words(kHudTextX, 1, line1, 1u);
	}
};

static_assert(eng::DisplayDriver<playfield::XlimitedScene<kScrollConsts>, eng::amiga::AmigaBackend>);

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	ScrollWorldDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
