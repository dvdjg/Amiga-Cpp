#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/field/dpf_composer.hpp>
#include <eng/field/tile_field.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/core/span.hpp>

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

/// Posición de cámara en píxeles (independiente del driver de scroll).
///
/// `s32` porque el scroll infinito recorre rangos negativos y positivos de más de
/// una pantalla: la cámara del primer plano viaja entre -320..320 (X) y
/// -256..256 (Y) para cruzar páginas repetidamente en ambos ejes.
struct ScrollPosition2 {
	eng::s32 x = 0;
	eng::s32 y = 0;
};

/// Globales de depuración (leídos por el host vía GDB).
const eng::u8* g_dbg_bg_fb = nullptr;
const eng::u8* g_dbg_fg_fb = nullptr;

/// Modo dual 3+3: primer plano 3 planos (PF1, delante, color 0 transparente) y
/// fondo 3 planos (PF2). Total 6 bitplanes, 320x256 visible. Cada playfield
/// dispone de sus 8 colores (registros 0-7 para PF1 y 8-15 para PF2).
constexpr eng::u8 kBgPlanes = 3;
constexpr eng::u8 kFgPlanes = 3;
constexpr eng::u8 kPlanes = kBgPlanes + kFgPlanes;
constexpr eng::u8 kBackground = 1; // PF2 (planos 2,4,6): atras
constexpr eng::u8 kForeground = 0; // PF1 (planos 1,3,5): delante

constexpr eng::u16 kTileSize = 16;
constexpr eng::u16 kViewportW = 320;
constexpr eng::u16 kViewportH = 256;
constexpr eng::u16 kMapTilesX = 256;
constexpr eng::u16 kMapTilesY = 128;
constexpr eng::u8 kTileCount = 64;

/// Mapas de mundo infinito: wrap_x/wrap_y hacen el mundo periódico. Cada campo
/// tiene su propio almacenamiento y su propia semilla.
eng::u16 bg_map_cells[kMapTilesX * kMapTilesY] {};
eng::u16 fg_map_cells[kMapTilesX * kMapTilesY] {};

constexpr eng::u16 dual_palette[32] {
	// PF1 (primer plano, 3 planos): registros 0..7. Color 0 = transparente.
	0x000, 0xf0c, 0x0cf, 0xff0, 0xf80, 0x84f, 0xf44, 0xfff,
	// PF2 (fondo, 3 planos): registros 8..15. Color 0 (reg 8) = transparente.
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xfff,
};

/// Hash pseudoaleatorio determinista por celda (x, y, semilla).
constexpr eng::u32 cell_hash(eng::u32 x, eng::u32 y, eng::u32 seed) {
	eng::u32 h = seed ^ (x * 0x9e3779b9u) ^ (y * 0x85ebca6bu);
	h ^= h >> 16u;
	h *= 0x7feb352du;
	h ^= h >> 15u;
	h *= 0x846ca68bu;
	h ^= h >> 16u;
	return h;
}

/// Rellena el mapa (índices u16) con patrones derivados de una semilla.
void build_map(eng::Span<eng::u16> cells, bool is_foreground, eng::u32 seed) {
	const eng::u32 layer_seed = seed ^ (is_foreground ? 0xf0f0f0f0u : 0x0f0f0f0fu);
	for (eng::u32 y = 0; y < kMapTilesY; ++y) {
		for (eng::u32 x = 0; x < kMapTilesX; ++x) {
			const eng::u32 h = cell_hash(x, y, layer_seed);
			eng::u16 tile = static_cast<eng::u16>(h & 63u);
			if (is_foreground && ((h >> 9u) & 1u) != 0u) {
				tile = 63u; // totalmente transparente: el fondo se ve a través
			}
			cells.at(y * kMapTilesX + x) = tile;
		}
	}
}

// --- Tiles de 3 planos por playfield (dual) ----------------------------------

constexpr eng::u8 hex_glyph_row(eng::u8 glyph, eng::u8 row) {
	constexpr eng::u8 rows[] {
		0x0eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0eu, // 0
		0x04u, 0x0cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0eu, // 1
		0x0eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1fu, // 2
		0x1eu, 0x01u, 0x01u, 0x0eu, 0x01u, 0x01u, 0x1eu, // 3
		0x02u, 0x06u, 0x0au, 0x12u, 0x1fu, 0x02u, 0x02u, // 4
		0x1fu, 0x10u, 0x10u, 0x1eu, 0x01u, 0x01u, 0x1eu, // 5
		0x0eu, 0x10u, 0x10u, 0x1eu, 0x11u, 0x11u, 0x0eu, // 6
		0x1fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u, // 7
		0x0eu, 0x11u, 0x11u, 0x0eu, 0x11u, 0x11u, 0x0eu, // 8
		0x0eu, 0x11u, 0x11u, 0x0fu, 0x01u, 0x01u, 0x0eu, // 9
		0x0eu, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u, // A
		0x1eu, 0x11u, 0x11u, 0x1eu, 0x11u, 0x11u, 0x1eu, // B
		0x0eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0eu, // C
		0x1eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1eu, // D
		0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x1fu, // E
		0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x10u, // F
	};
	return rows[static_cast<eng::u16>(glyph & 0x0fu) * 7u + (row % 7u)];
}

/// Fila planar de un tile simbólico de 3 planos para un playfield.
///
/// `base` es el índice del primer color del playfield (0 para PF1, 8 para PF2).
/// Cada tile combina fondo tramado al 50% (si `transparent_bg`), borde, glifo
/// hexadecimal grande y marcador de variante en las esquinas.
constexpr eng::u16 pf_plane_row(eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane, eng::u8 base, bool transparent_bg) {
	constexpr eng::u8 bg_colors[] {1, 2, 3, 4};
	constexpr eng::u8 ink_colors[] {7, 6, 5, 7};
	constexpr eng::u8 border_colors[] {5, 3, 1, 6};
	const eng::u8 bg = static_cast<eng::u8>(base + bg_colors[variant & 3u]);
	const eng::u8 ink = static_cast<eng::u8>(base + ink_colors[variant & 3u]);
	const eng::u8 border = static_cast<eng::u8>(base + border_colors[variant & 3u]);

	const eng::u16 border_mask = (y == 0u || y == 15u) ? 0xffffu : 0x8001u;
	const eng::u16 marker_size = static_cast<eng::u16>(2u + (variant & 1u));
	eng::u16 marker_mask = 0;
	if (y < marker_size) {
		eng::u16 m = 0;
		for (eng::u8 i = 0; i < marker_size; ++i) {
			m |= static_cast<eng::u16>(0x8000u >> (1u + i));
		}
		marker_mask = static_cast<eng::u16>(m & ~border_mask);
	}
	eng::u16 glyph_mask = 0;
	if (y >= 1u && y <= 14u) {
		const eng::u8 bits = hex_glyph_row(glyph, static_cast<eng::u8>((y - 1u) / 2u));
		for (eng::u8 x = 0; x < 5u; ++x) {
			if ((bits & (1u << (4u - x))) != 0u) {
				eng::u16 m = 0;
				for (eng::u8 i = 0; i < 2u; ++i) {
					m |= static_cast<eng::u16>(0x8000u >> (3u + x * 2u + i));
				}
				glyph_mask |= m;
			}
		}
	}
	glyph_mask = static_cast<eng::u16>(glyph_mask & ~(border_mask | marker_mask));
	const eng::u16 bg_mask = static_cast<eng::u16>(~(border_mask | marker_mask | glyph_mask));

	eng::u16 row = 0;
	if (transparent_bg) {
		const eng::u16 checker = (y & 1u) == 0u ? 0xaaaa : 0x5555;
		if ((bg & (1u << plane)) != 0u) {
			row |= static_cast<eng::u16>(bg_mask & checker);
		}
	} else if ((bg & (1u << plane)) != 0u) {
		row |= bg_mask;
	}
	if ((border & (1u << plane)) != 0u) {
		row |= border_mask;
	}
	if ((ink & (1u << plane)) != 0u) {
		row |= static_cast<eng::u16>(marker_mask | glyph_mask);
	}
	return row;
}

/// Construye el tileset del campo (planar contiguo por playfield).
///
/// Layout: [tile][plano][filas]. Cada tile ocupa `tile_planes * tile_size`
/// words; el controlador lo copia con un único blit `TileBlockCopy`.
///
/// El tile 63 del PRIMER PLANO es COMPLETAMENTE transparente (todas las filas a
/// 0 en todos los planos): en DPF su color 0 deja ver el fondo PF2. El mapa del
/// fg usa este tile en ~50% de sus celdas (ver `build_map`), así el fondo se ve
/// a través de tiles enteros.
void build_tile_cache(eng::Span<eng::u16> words, eng::u8 tile_planes, bool is_foreground) {
	const eng::u32 words_per_tile = words.size() / kTileCount;
	for (eng::u16 tile = 0; tile < kTileCount; ++tile) {
		const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
		const eng::u8 variant = static_cast<eng::u8>((tile >> 4u) & 3u);
		const bool fully_transparent = is_foreground && tile == 63u;
		for (eng::u8 y = 0; y < kTileSize; ++y) {
			for (eng::u8 plane = 0; plane < tile_planes; ++plane) {
				words.at(static_cast<eng::u32>(tile) * words_per_tile + static_cast<eng::u32>(plane) * kTileSize + y) =
					fully_transparent
						? 0
						: (is_foreground
							? pf_plane_row(glyph, variant, y, plane, 0, true)
							: pf_plane_row(glyph, variant, y, plane, 8, false));
			}
		}
	}
}

/// Seno de 64 pasos exactos (amplitud 64). Tabla manual precisa usada como base
/// de la interpolación de alta resolución.
constexpr eng::s16 sin64(eng::u8 index) {
	constexpr eng::s16 t[] {
		0, 6, 12, 18, 24, 31, 36, 41,
		45, 49, 53, 56, 59, 61, 63, 64,
		64, 64, 63, 61, 59, 56, 53, 49,
		45, 41, 36, 31, 24, 18, 12, 6,
		0, -6, -12, -18, -24, -31, -36, -41,
		-45, -49, -53, -56, -59, -61, -63, -64,
		-64, -64, -63, -61, -59, -56, -53, -49,
		-45, -41, -36, -31, -24, -18, -12, -6,
	};
	return t[index & 63u];
}

/// Seno interpolado con precisión Q16 (escala 65536 = 1.0).
///
/// La onda recorre más de una pantalla (radio 320x256). Para que el movimiento no
/// vaya a trompicones hay que conservar la FRACCIÓN del seno: si se redondea a
/// entero, un paso del seno (64/64) son 320/64 = 5 px en X y el movimiento salta
/// 0,0,5,0,5 px en vez de avanzar suave. Aquí se devuelve el seno escalado a Q16
/// ([-64*65536, 64*65536]) con interpolación lineal de 64 sub-pasos por entrada
/// de la tabla; el acumulador de resto de `update` convierte a píxeles enteros
/// sin perder la fracción.
constexpr eng::s32 sin_smooth(eng::u32 frame_index, eng::u32 period) {
	const eng::u32 ph = (frame_index * 4096u) / period;
	const eng::u8 i = static_cast<eng::u8>((ph >> 6) & 63u);
	const eng::u8 frac = static_cast<eng::u8>(ph & 63u);
	const eng::s32 s0 = sin64(i);
	const eng::s32 s1 = sin64(static_cast<eng::u8>(i + 1u) & 63u);
	// Interpolación lineal en Q16: (s0*(64-frac)+s1*frac)/64, *65536.
	return (s0 * static_cast<eng::s32>(64 - frac) + s1 * static_cast<eng::s32>(frac)) * 1024;
}

/// Camara del primer plano: onda de Lissajous en SUB-PÍXELES (Q16).
///
/// Recorre más de una pantalla en cada eje (0..640 en X, 0..512 en Y) cruzando
/// páginas en ambas direcciones. Radio 320x256 (2 pantallas), periodos 640/480
/// frames (velocidad pico ~3.8px/frame, suave con Q16 y dentro de max_delta=8).
/// Con fase inicial en 3/4 de ciclo la cámara arranca en (0,0) para continuidad
/// con `begin`. Los valores devueltos son px * 65536.
constexpr ScrollPosition2 fg_wave_camera(eng::u32 frame_index) {
	const eng::s32 sx = sin_smooth(frame_index + 3u * 640u / 4u, 640) * 320 / 64;
	const eng::s32 sy = sin_smooth(frame_index + 3u * 480u / 4u, 480) * 256 / 64;
	return {
		320 * 65536 + sx,
		256 * 65536 + sy,
	};
}

/// Camara del fondo: scroll infinito diagonal constante en ambos ejes.
///
/// 2 px/frame en X y 1 px/frame en Y: cruza una pantalla horizontal cada 160
/// frames y una vertical cada 256. Es el caso más simple de scroll infinito y
/// demuestra ambos ejes con parallax opuesto al primer plano.
constexpr ScrollPosition2 bg_scroll(eng::u32 frame_index) {
	return {
		static_cast<eng::s32>(frame_index) * 2,
		static_cast<eng::s32>(frame_index),
	};
}

struct DemoGame {
	eng::MemoryBlock tiles_bg {};
	eng::MemoryBlock tiles_fg {};
	field::TileFieldController bg {};
	field::TileFieldController fg {};
	field::DpfDisplayComposer composer {};
	eng::graphics::FramePlan plan {};
	field::TileFieldConfig bg_config {};
	field::TileFieldConfig fg_config {};
	eng::u32 tiles_uploaded = 0;
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({360u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010601u);
			return;
		}

		// Tilesets: cada campo guarda su propio banco de patrones en Chip RAM.
		tiles_bg = backend.memory().chip.allocate(kTileCount * kBgPlanes * kTileSize * 2u, 16);
		tiles_fg = backend.memory().chip.allocate(kTileCount * kFgPlanes * kTileSize * 2u, 16);
		if (!tiles_bg.valid() || !tiles_fg.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010602u);
			return;
		}
		build_tile_cache(tile_span(tiles_bg), kBgPlanes, false);
		build_tile_cache(tile_span(tiles_fg), kFgPlanes, true);

		build_map(eng::Span<eng::u16>::from_raw(bg_map_cells, kMapTilesX * kMapTilesY), false, 0x13579bdu);
		build_map(eng::Span<eng::u16>::from_raw(fg_map_cells, kMapTilesX * kMapTilesY), true, 0x2468aceu);

		bg_config = make_config(tiles_bg, kBgPlanes, false);
		fg_config = make_config(tiles_fg, kFgPlanes, true);

		// Estampado inicial por Blitter, en lotes hasta terminar (sin límite real).
		const eng::u8 budget = 120;
		if (!bg.begin(backend.memory(), bg_config, {0, 0})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010603u);
			return;
		}
		while (bg.busy()) {
			plan.clear();
			bg.pump(plan, budget);
			if (!backend.execute_frame_plan(plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00010604u);
				return;
			}
		}
		plan.clear();
		if (!fg.begin(backend.memory(), fg_config, {0, 0})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010605u);
			return;
		}
		while (fg.busy()) {
			plan.clear();
			fg.pump(plan, budget);
			if (!backend.execute_frame_plan(plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00010606u);
				return;
			}
		}

		if (!composer.init(backend.memory(), {
			dual_palette, 1536, true, false, kPlanes,
		})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010607u);
			return;
		}

		// Primer compose: emite la lista completa con los punteros iniciales.
		plan.clear();
		if (!composer.compose(fg.hardware_view(kForeground), bg.hardware_view(kBackground))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010608u);
			return;
		}
		composer.install(backend);
		g_dbg_bg_fb = bg.bitplanes();
		g_dbg_fg_fb = fg.bitplanes();

		eng::debug::DebugPeripheral::counter_name(0, reinterpret_cast<eng::u32>("tiles_uploaded"));
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x10600000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) {
			return;
		}

		const eng::u32 frame = context.frame.frame_index;
		const ScrollPosition2 fg_q = fg_wave_camera(frame); // Q16
		const ScrollPosition2 bg_pos = bg_scroll(frame);    // px enteros

		plan.clear();
		// Presupuesto de Blitter: 2 campos x hasta 32 tiles/frame = 64 jobs. Al
		// cruzar en diagonal se encolan hasta 1280 tiles y el presupuesto debe
		// cubrir ~20 tiles/frame por campo para no mostrar la página activa a
		// medio dibujar (negro residual en el cruce de página vertical).
		plan.set_blit_budget_limits({8192, 16384, 4, 80});

		const field::TileScrollOffset bg_delta {
			static_cast<eng::s16>(bg_pos.x - bg_last_x),
			static_cast<eng::s16>(bg_pos.y - bg_last_y),
		};
		bg_last_x = bg_pos.x;
		bg_last_y = bg_pos.y;

		// Delta del fg en sub-píxeles con acumulador de resto: la cámara es Q16
		// y solo se pasa al controlador la parte entera de la diferencia, pero el
		// resto se acumula para que el movimiento sea suave (no 0,0,5,0,5 px).
		fg_rest_x += fg_q.x - fg_last_x;
		fg_rest_y += fg_q.y - fg_last_y;
		fg_last_x = fg_q.x;
		fg_last_y = fg_q.y;
		const eng::s16 fg_dx = static_cast<eng::s16>(fg_rest_x / 65536);
		const eng::s16 fg_dy = static_cast<eng::s16>(fg_rest_y / 65536);
		fg_rest_x -= static_cast<eng::s32>(fg_dx) * 65536;
		fg_rest_y -= static_cast<eng::s32>(fg_dy) * 65536;
		const field::TileScrollOffset fg_delta {fg_dx, fg_dy};

		if (!bg.update(bg_config, bg_delta, plan) || !fg.update(fg_config, fg_delta, plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00010609u);
			return;
		}
		tiles_uploaded += plan.blit_budget().tile_jobs;
		eng::debug::DebugPeripheral::counter_value(0, tiles_uploaded);

		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0001060au);
			return;
		}

		// El compositor parchea BPLCON1 + punteros de ambas vistas.
		if (!composer.compose(fg.hardware_view(kForeground), bg.hardware_view(kBackground))) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0001060bu);
			return;
		}

		// Telemetría: latch de cruce de página (bits 16-19, se mantiene una vez el
		// campo cruza una página en ese eje, independiente del instante de lectura)
		// + página activa actual (bits 12-15) + mundo del fg en tiles (X bits 8-15,
		// Y bits 0-7).
		const eng::u8 bg_px = bg.state().active_page_x & 1u;
		const eng::u8 bg_py = bg.state().active_page_y & 1u;
		const eng::u8 fg_px = fg.state().active_page_x & 1u;
		const eng::u8 fg_py = fg.state().active_page_y & 1u;
		m_crossed_bg_x |= bg_px != m_last_bg_px;
		m_crossed_bg_y |= bg_py != m_last_bg_py;
		m_crossed_fg_x |= fg_px != m_last_fg_px;
		m_crossed_fg_y |= fg_py != m_last_fg_py;
		m_last_bg_px = bg_px; m_last_bg_py = bg_py;
		m_last_fg_px = fg_px; m_last_fg_py = fg_py;
		const eng::u32 marker = 0x10600000u |
			(static_cast<eng::u32>(m_crossed_bg_x) << 16u) |
			(static_cast<eng::u32>(m_crossed_fg_x) << 17u) |
			(static_cast<eng::u32>(m_crossed_bg_y) << 18u) |
			(static_cast<eng::u32>(m_crossed_fg_y) << 19u) |
			(static_cast<eng::u32>(bg_px) << 12u) |
			(static_cast<eng::u32>(fg_px) << 13u) |
			(static_cast<eng::u32>(bg_py) << 14u) |
			(static_cast<eng::u32>(fg_py) << 15u) |
			(static_cast<eng::u32>((fg.state().world_x >> 4) & 0xffu) << 8u) |
			static_cast<eng::u32>((fg.state().world_y >> 4) & 0xffu);
		eng::debug::mark_ready(g_eng_run_status, marker);	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (ready) {
			composer.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static eng::Span<eng::u16> tile_span(const eng::MemoryBlock& block) {
		return eng::Span<eng::u16>::from_raw(
			static_cast<eng::u16*>(block.data),
			block.size / sizeof(eng::u16)
		);
	}

	field::TileFieldConfig make_config(const eng::MemoryBlock& tiles, eng::u8 planes, bool is_foreground) {
		field::TileFieldConfig config {};
		config.map.cells = is_foreground
			? eng::Span<const eng::u16>::from_raw(fg_map_cells, kMapTilesX * kMapTilesY)
			: eng::Span<const eng::u16>::from_raw(bg_map_cells, kMapTilesX * kMapTilesY);
		config.map.width = kMapTilesX;
		config.map.height = kMapTilesY;
		config.map.wrap_x = kMapTilesX;
		config.map.wrap_y = kMapTilesY;
		config.map.edge_tile = 63;
		config.tileset = static_cast<const eng::u16*>(tiles.data);
		config.tileset_count = kTileCount;
		config.tileset_planes = planes;
		config.tile_width = 16;
		config.tile_size = kTileSize;
		config.viewport_w = kViewportW;
		config.viewport_h = kViewportH;
		// max_delta >= pico de velocidad de la cámara (fg: 5px/f X, 4px/f Y) para
		// que el clamp nunca recorte la trayectoria de la Lissajous.
		//
		// Presupuesto: al cruzar en diagonal se encolan a la vez la página X
		// vacante (20x32=640 tiles) y la Y (40x16=640) = 1280 tiles; hay ~64
		// frames hasta el siguiente cruce -> ~20 tiles/frame. Con menos, la página
		// activa se mostraba a medio dibujar (negro residual al cruzar la página
		// vertical). 32/frame da margen real incluso si ambos campos cruzan juntos.
		config.max_delta_x = 8;
		config.max_delta_y = 8;
		config.max_tiles_per_frame = 32;
		config.scroll_x = true;
		config.scroll_y = true;
		return config;
	}

	eng::s32 bg_last_x = 0;
	eng::s32 bg_last_y = 0;
	eng::s32 fg_last_x = 0;
	eng::s32 fg_last_y = 0;
	eng::s32 fg_rest_x = 0;   // resto sub-píxel (Q16) para movimiento suave
	eng::s32 fg_rest_y = 0;
	eng::u8 m_crossed_bg_x = 0;
	eng::u8 m_crossed_bg_y = 0;
	eng::u8 m_crossed_fg_x = 0;
	eng::u8 m_crossed_fg_y = 0;
	eng::u8 m_last_bg_px = 0;
	eng::u8 m_last_bg_py = 0;
	eng::u8 m_last_fg_px = 0;
	eng::u8 m_last_fg_py = 0;
};

DemoGame g_game {};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	eng::Engine engine { backend, g_game };
	engine.run_frames(0xffffffffu);

	return 0;
}
