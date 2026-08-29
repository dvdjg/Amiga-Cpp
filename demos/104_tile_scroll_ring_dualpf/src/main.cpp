#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/graphics/drivers/tile_scroll.hpp>
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

namespace drivers = eng::graphics::drivers;

/// Dual 3+3 con superficie ring 352x288 (viewport 320x256 + 2 tiles de margen).
///
/// 3+3 significa 6 bitplanes: PF1 (planos 1,3,5) y PF2 (planos 2,4,6). Cada
/// playfield usa 8 colores (PF1: registros 0-7, PF2: 8-15). En DPF el color 0
/// de cada playfield es transparente: donde PF1 (delante) es transparente se ve
/// PF2, asi el buffer compartido muestra dos capas.
///
/// El anillo: el buffer fisico solo mide 352x288 (22x18 tiles) y la camara
/// apunta siempre dentro de los primeros 16 px de cada fila (px en [1,16]); el
/// fine scroll lo hace BPLCON1. Al cruzar el borde, el contenido se desplaza
/// 16 px al lado contrario (shift) y se recargan los tiles que entran (estilo
/// Lionheart). Es la variante ahorradora de RAM frente al buffer lineal grande
/// de 101/102. Ver README_104.md.
constexpr auto kMode = drivers::TileScrollMode::dual(3, 3);
using Scene = drivers::TileScrollScene<kMode, 2, 2>;

// --- Geometria de la superficie (todo constexpr, auditado en compilacion) ----
//
//   superficie: 352 px x 288 px  =  44 bytes/fila  =  22 tiles x 18 tiles
//   visible:    320 px x 256 px  =  window + margen de fetch (42 bytes/fila)
//   plane_bytes = 44 * 288 = 12.672 bytes  (6 planos -> 76 KB en total)
constexpr eng::u16 tile_size = Scene::tile_size;
constexpr eng::u16 surface_tiles_x = Scene::surface_width / tile_size;   // 22
constexpr eng::u16 surface_tiles_y = Scene::surface_height / tile_size;  // 18
constexpr eng::u16 surface_bytes_per_row = Scene::surface_bytes_per_row; // 44
constexpr eng::u32 plane_bytes = Scene::plane_bytes;

constexpr eng::u16 map_tiles_x = 256;
constexpr eng::u16 map_tiles_y = 128;
constexpr eng::u8 bg_pattern_count = 64;
constexpr eng::u8 fg_pattern_count = 64;

// Playfield fisico de cada capa. pf_foreground = 0 (PF1) queda delante
// (BPLCON2=0); pf_background = 1 (PF2) detras.
constexpr eng::u8 pf_background = 1; // PF2 (detras, colores 8..15)
constexpr eng::u8 pf_foreground = 0; // PF1 (delante, colores 0..7, transparencia)

// Paleta OCS (RGB444). PF1 usa registros 0-7 (vivos, para el primer plano);
// PF2 usa 8-15 (verdes, para el fondo). El color 0 de cada banco es
// transparente en DPF y se deja a 0x000 (se vera el otro playfield / borde).
constexpr drivers::EhbPalette dual_palette {{
	// PF1 (primer plano): registros 0..7. Color 0 = transparente.
	0x000, 0xf0c, 0x0cf, 0xff0, 0xf80, 0x84f, 0xf44, 0xfff,
	// PF2 (fondo): registros 8..15. Color 8 = transparente.
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xfff,
}};

constexpr drivers::EhbPaletteZone palette_zones[] {};

// --- Glifos y tiles simbolicos (3 planos por playfield, como la demo 102) -----

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

constexpr eng::u16 row_mask_range(eng::u8 left, eng::u8 width) {
	eng::u16 mask = 0;
	for (eng::u8 i = 0; i < width; ++i) {
		mask |= static_cast<eng::u16>(0x8000u >> (left + i));
	}
	return mask;
}

constexpr eng::u16 glyph_row_mask(eng::u8 glyph, eng::u8 y) {
	if (y < 1u || y >= 15u) {
		return 0;
	}
	const eng::u8 glyph_y = static_cast<eng::u8>((y - 1u) / 2u);
	const eng::u8 bits = hex_glyph_row(glyph, glyph_y);
	eng::u16 mask = 0;
	for (eng::u8 glyph_x = 0; glyph_x < 5u; ++glyph_x) {
		if ((bits & (1u << (4u - glyph_x))) != 0u) {
			mask |= row_mask_range(static_cast<eng::u8>(3u + glyph_x * 2u), 2);
		}
	}
	return mask;
}

constexpr eng::u16 variant_marker_mask(eng::u8 variant, eng::u8 y) {
	const eng::u8 marker_size = static_cast<eng::u8>(2u + (variant & 1u));
	eng::u16 mask = 0;
	if (y < marker_size) {
		mask |= row_mask_range(1, marker_size);
	}
	if (variant >= 2u && y >= static_cast<eng::u8>(15u - marker_size)) {
		mask |= row_mask_range(static_cast<eng::u8>(15u - marker_size), marker_size);
	}
	return mask;
}

/// Compone una fila planar de un tile simbolico de 3 planos para un playfield.
constexpr eng::u16 pf_plane_row(
	eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane, eng::u8 base, bool transparent_bg
) {
	constexpr eng::u8 bg_colors[] {1, 2, 3, 4};
	constexpr eng::u8 ink_colors[] {7, 6, 5, 7};
	constexpr eng::u8 border_colors[] {5, 3, 1, 6};
	const eng::u8 bg = static_cast<eng::u8>(base + bg_colors[variant & 3u]);
	const eng::u8 ink = static_cast<eng::u8>(base + ink_colors[variant & 3u]);
	const eng::u8 border = static_cast<eng::u8>(base + border_colors[variant & 3u]);

	const eng::u16 border_mask = (y == 0u || y == 15u) ? 0xffffu : 0x8001u;
	const eng::u16 marker_mask = static_cast<eng::u16>(variant_marker_mask(variant, y) & ~border_mask);
	const eng::u16 glyph_mask = static_cast<eng::u16>(glyph_row_mask(glyph, y) & ~(border_mask | marker_mask));
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

constexpr eng::u16 bg_plane_row(eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane) {
	return pf_plane_row(glyph, variant, y, plane, 8, false);
}

constexpr eng::u16 fg_plane_row(eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane) {
	return pf_plane_row(glyph, variant, y, plane, 0, true);
}

/// Tile del mundo para una capa: hash determinista de (col, row, capa).
///
/// El mundo es infinito sin gastar RAM en un mapa: cualquier coordenada de mundo
/// produce un indice de tile reproducible. Es un hash entero de 32 bits
/// (multiplicadores primos + xorshift) cortado en glifo (4 bits) y variante
/// (2 bits). La semilla de capa hace que fg y bg tengan patrones distintos.
constexpr eng::u16 world_tile(eng::u32 col, eng::u32 row, eng::u32 layer_seed) {
	const eng::u32 x = col % map_tiles_x;
	const eng::u32 y = row % map_tiles_y;
	eng::u32 h = x * 0x9e3779b9u ^ y * 0x85ebca6bu ^ layer_seed;
	h ^= h >> 16u;
	h *= 0x7feb352du;
	h ^= h >> 15u;
	return static_cast<eng::u16>((h & 0x0fu) | (((h >> 4u) & 3u) << 4u));
}

struct TileCache {
	eng::MemoryBlock block {};
	eng::u16 pattern_count = 0;
	eng::u8 planes = 0;
	eng::u32 layer_seed = 0;

	void build(eng::amiga::MinimalBackend& backend, eng::u8 playfield, eng::u16 count, eng::u32 seed, bool fg) {
		pattern_count = count;
		planes = Scene::playfield_planes(playfield);
		layer_seed = seed ^ (fg ? 0xf0f0f0f0u : 0x0f0f0f0fu);
		block = backend.memory().chip.allocate(
			Scene::playfield_tile_bytes(playfield) * count,
			16
		);
		if (!block.valid()) {
			return;
		}
		const eng::u32 words_per_tile = (Scene::tile_plane_bytes() / sizeof(eng::u16)) * planes;
		eng::Span<eng::u16> words = eng::Span<eng::u16>::from_raw(
			static_cast<eng::u16*>(block.data),
			block.size / sizeof(eng::u16)
		);
		for (eng::u16 tile = 0; tile < count; ++tile) {
			const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
			const eng::u8 variant = static_cast<eng::u8>((tile >> 4u) & 3u);
			for (eng::u8 y = 0; y < tile_size; ++y) {
				for (eng::u8 plane = 0; plane < planes; ++plane) {
					words.at(static_cast<eng::u32>(tile) * words_per_tile + static_cast<eng::u32>(plane) * tile_size + y) =
						fg ? fg_plane_row(glyph, variant, y, plane)
						   : bg_plane_row(glyph, variant, y, plane);
				}
			}
		}
	}

	const eng::u16* tile_data(eng::u16 tile_index) const {
		const eng::u32 words_per_tile = (Scene::tile_plane_bytes() / sizeof(eng::u16)) * planes;
		return reinterpret_cast<const eng::u16*>(static_cast<const eng::u8*>(block.data)) +
			static_cast<eng::u32>(tile_index & (pattern_count - 1u)) * words_per_tile;
	}
};

// --- Camara de anillo ---------------------------------------------------------
//
// Separa la posicion de mundo (px infinitos) del puntero fisico del buffer:
//   wx/wy       posicion de mundo en pixeles (crece sin limite)
//   view_x/y    columna/fila de mundo en el borde del buffer = wx / 16
//   px/py       puntero fino 1..16 = (wx % 16) + 1  -> lo que recibe el chipset
//
// El buffer solo tiene 22 tiles de ancho; el display apunta siempre a px en
// [1,16] y el fine scroll lo hace BPLCON1. Cuando view_x cambia (cruza un tile),
// vdx/vdy != 0 avisan al update de que hay que hacer el wrap (shift + recarga).
struct RingCamera {
	eng::u32 wx = 1;
	eng::u32 wy = 1;
	eng::u32 view_x = 0;
	eng::u32 view_y = 0;
	eng::u16 px = 1;
	eng::u16 py = 1;

	eng::s8 vdx = 0;
	eng::s8 vdy = 0;

	// Fases de la ruta: 30 frames de pausa + duraciones. En cada fase la camara
	// se recalcula desde el frame_index (determinista, sin deriva).
	static constexpr eng::u32 pause_frames = 30;
	static constexpr eng::u32 phase_dur[7] {400, 400, 320, 320, 480, 480, 480};
	static constexpr eng::u32 jump_start = 30 * 7u + 400 + 400 + 320 + 320 + 480 + 480 + 480;
	static constexpr eng::u32 repattern_frames = 500;

	eng::u8 m_phase = 0xffu;
	eng::u32 m_phase_wx = 1;
	eng::u32 m_phase_wy = 1;
	eng::s8 m_h_dir = 1;
	eng::s8 m_v_dir = 1;
	eng::u32 m_jump_epoch = 0xffffffffu;
	eng::u32 m_rng = 0x13579bdu;

	void advance(eng::u32 frame_index) {
		if (frame_index >= jump_start) {
			advance_jump(frame_index);
		} else {
			advance_phases(frame_index);
		}
		derive();
	}

private:
	static constexpr eng::s16 circle_offset_x(eng::u8 index) {
		constexpr eng::s16 offsets[] {
			64, 64, 63, 61, 59, 56, 53, 49,
			45, 41, 36, 31, 24, 18, 12, 6,
			0, -6, -12, -18, -24, -31, -36, -41,
			-45, -49, -53, -56, -59, -61, -63, -64,
			-64, -64, -63, -61, -59, -56, -53, -49,
			-45, -41, -36, -31, -24, -18, -12, -6,
			0, 6, 12, 18, 24, 31, 36, 41,
			45, 49, 53, 56, 59, 61, 63, 64,
		};
		return offsets[index & 63u];
	}

	static constexpr eng::s16 circle_offset_y(eng::u8 index) {
		constexpr eng::s16 offsets[] {
			0, 6, 12, 18, 24, 31, 36, 41,
			45, 49, 53, 56, 59, 61, 63, 64,
			64, 64, 63, 61, 59, 56, 53, 49,
			45, 41, 36, 31, 24, 18, 12, 6,
			0, -6, -12, -18, -24, -31, -36, -41,
			-45, -49, -53, -56, -59, -61, -63, -64,
			-64, -64, -63, -61, -59, -56, -53, -49,
			-45, -41, -36, -31, -24, -18, -12, -6,
		};
		return offsets[index & 63u];
	}

	static constexpr eng::u32 sub_u32(eng::u32 a, eng::u32 b) {
		return a >= b ? a - b : 0;
	}

	static constexpr eng::s16 radius_signed(eng::s16 offset) {
		return static_cast<eng::s16>(96 * offset / 64);
	}

	void advance_phases(eng::u32 frame_index) {
		eng::u32 t = frame_index;
		for (eng::u8 p = 0; p < 7; ++p) {
			const eng::u32 block = pause_frames + phase_dur[p];
			if (t < pause_frames) {
				return;
			}
			t -= pause_frames;
			if (t < phase_dur[p]) {
				if (p != m_phase) {
					m_phase = p;
					m_phase_wx = wx;
					m_phase_wy = wy;
				}
				switch (p) {
				case 0: wx = m_phase_wx + t; wy = m_phase_wy; break;
				case 1: wx = sub_u32(m_phase_wx, t); wy = m_phase_wy; break;
				case 2: wx = m_phase_wx; wy = m_phase_wy + t; break;
				case 3: wx = m_phase_wx; wy = sub_u32(m_phase_wy, t); break;
				case 4: wx = m_phase_wx + t; wy = m_phase_wy + t; break;
				case 5: {
					const eng::u8 a = static_cast<eng::u8>((t * 64u) / phase_dur[5]);
					wx = m_phase_wx + radius_signed(circle_offset_x(a));
					wy = m_phase_wy + radius_signed(circle_offset_y(a));
					break;
				}
				case 6: {
					const eng::u8 a = static_cast<eng::u8>((t * 128u) / phase_dur[6]);
					wx = m_phase_wx + t;
					wy = m_phase_wy + radius_signed(circle_offset_y(a));
					break;
				}
				}
				return;
			}
			t -= phase_dur[p];
		}
	}

	void advance_jump(eng::u32 frame_index) {
		const eng::u32 epoch = (frame_index - jump_start) / repattern_frames;
		if (epoch != m_jump_epoch) {
			m_jump_epoch = epoch;
			m_h_dir = (rng_next() & 1u) != 0u ? 1 : -1;
			m_v_dir = (rng_next() & 1u) != 0u ? 1 : -1;
		}
		const eng::u32 step = 2 + (rng_next() % 14u);
		wx += static_cast<eng::u32>(static_cast<eng::s32>(step) * m_h_dir);
		wy += static_cast<eng::u32>(static_cast<eng::s32>(step) * m_v_dir);
	}

	// Convierte la posicion de mundo (wx,wy) en el puntero fisico del buffer:
	//   view_x = wx / 16   -> cuantas veces hemos envuelto el anillo
	//   px     = wx % 16 + 1 -> posicion fina dentro del tile (1..16)
	// Si view_x cambio respecto al frame anterior, vdx/vdy (en {-1,0,+1}) marcan
	// el wrap necesario. Como la camara avanza como mucho ~1 tile por frame en
	// las fases, nunca hace falta mas de un wrap por eje en un frame.
	void derive() {
		const eng::u32 nvx = wx / 16u;
		const eng::u32 nvy = wy / 16u;
		vdx = 0;
		vdy = 0;
		if (nvx != view_x) {
			vdx = nvx > view_x ? 1 : -1;
		}
		if (nvy != view_y) {
			vdy = nvy > view_y ? 1 : -1;
		}
		view_x = nvx;
		view_y = nvy;
		px = static_cast<eng::u16>((wx % 16u) + 1u);
		py = static_cast<eng::u16>((wy % 16u) + 1u);
	}

	eng::u32 rng_next() {
		eng::u32 z = m_rng;
		z ^= z << 13u;
		z ^= z >> 17u;
		z ^= z << 5u;
		m_rng = z;
		return z;
	}
};

// --- Demo ----------------------------------------------------------------------

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// 120 KB de Chip RAM: superficie (76 KB) + tiles (12 KB) + scratch del
		// shift (12 KB) + copperlist (1.5 KB). Menos de la mitad que 101/102.
		if (!backend.configure_memory({120u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000410u);
			return;
		}

		const drivers::TileScrollConfig config {&dual_palette, palette_zones, 0, 1536};
		if (!m_scene.init(backend.memory(), config)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000411u);
			return;
		}

		// Caches de tiles procedurales (64 patrones por playfield, 3 planos cada
		// uno = 96 bytes/tile). fg usa la paleta 0-7 con transparencia; bg la 8-15.
		m_bg.build(backend, pf_background, bg_pattern_count, 0x13579bdu, false);
		m_fg.build(backend, pf_foreground, fg_pattern_count, 0x2468aceu, true);
		if (!m_bg.block.valid() || !m_fg.block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000412u);
			return;
		}
		// Scratch para el shift no-solapado (una fila de un plano: 42x288 bytes).
		// El shift se hace en dos blits (superficie->scratch, scratch->superficie)
		// porque el CopyRect solapado no mueve el buffer en WinUAE-DBG; el scratch
		// se reutiliza plano a plano.
		m_scratch = backend.memory().chip.allocate(
			static_cast<eng::u32>(surface_bytes_per_row - (tile_size / 8u)) * Scene::surface_height,
			16
		);
		if (!m_scratch.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000413u);
			return;
		}

		// Limpia y puebla la superficie completa (22x18 tiles por playfield)
		// directamente por CPU: es la unica vez que se tocan píxeles, en init.
		m_scene.bitplane_span().clear();
		stamp_layer(m_bg, pf_background);
		stamp_layer(m_fg, pf_foreground);

		// Primera copperlist: camara inicial (1,1), ambos playfields.
		drivers::TileScrollInput input {};
		input.playfield[pf_background] = {m_cam.px, m_cam.py};
		input.playfield[pf_foreground] = {m_cam.px, m_cam.py};
		if (!m_scene.rebuild_copper(input)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000414u);
			return;
		}
		m_scene.install(backend);
		publish_status(0);
		m_ready = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) {
			return;
		}

		const eng::u32 frame = context.frame.frame_index;

		// Cada 10s (500 frames) se cambia la semilla del hash del mundo: los
		// tiles que la camara revele se recargaran con el patron nuevo, de modo
		// que el mundo "muta" de aspecto progresivamente sin tocar RAM extra.
		const eng::u32 epoch = frame / 500u;
		if (epoch != m_last_epoch) {
			m_last_epoch = epoch;
			rebuild_patterns();
		}

		// 1) Avanza la camara y detecta si hay wrap (vdx/vdy != 0).
		m_cam.advance(frame);

		// 2) Si la camara cruzo un borde de tile, el anillo necesita:
		//      - el SHIFT: mover el contenido 16 px al lado contrario,
		//      - los EDGES: recargar la columna/fila de tiles que entra.
		//    Todo se encola en un FramePlan y se ejecuta por Blitter.
		if (m_cam.vdx != 0 || m_cam.vdy != 0) {
			m_frame_plan.clear();
			configure_budget(m_frame_plan);
			add_shift(m_frame_plan, m_cam.vdx, m_cam.vdy);
			add_edges(m_frame_plan, m_bg, pf_background, m_cam.vdx, m_cam.vdy);
			add_edges(m_frame_plan, m_fg, pf_foreground, m_cam.vdx, m_cam.vdy);
			if (m_frame_plan.blit_budget_report().status == eng::graphics::BlitBudgetStatus::Exceeded) {
				eng::debug::mark_failed(g_eng_run_status, 0x00000417u);
				return;
			}
			if (!backend.execute_frame_plan(m_frame_plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00000416u);
				return;
			}
		}

		// 3) Recompila la copperlist con el puntero fisico actual (px,py): solo
		//    cambian BPLCON1 y los 12 punteros BPLxPT; el Copper los aplica en el
		//    VBlank de forma sincronizada con el raster.
		drivers::TileScrollInput input {};
		input.playfield[pf_background] = {m_cam.px, m_cam.py};
		input.playfield[pf_foreground] = {m_cam.px, m_cam.py};
		if (!m_scene.rebuild_copper(input)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000415u);
			return;
		}
		publish_status(m_cam.vdx != 0 || m_cam.vdy != 0 ? 1u : 0u);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_ready) {
			m_scene.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static void configure_budget(eng::graphics::FramePlan& plan) {
		// Un cruce de borde ejecuta el shift en dos blits por plano via scratch
		// (2 x 21x288x6 = 72K words) mas los tiles de borde (~3.8K words).
		plan.set_blit_budget_limits({16384, 131072, 96, 256});
	}

	/// Shift del ring: desplaza el contenido del buffer 16 px al lado contrario.
	///
	/// Cuando la camara cruza un tile (px pasa 16->1), el contenido debe moverse
	/// 16 px para que Denise siga viendo la imagen continua. Geometricamente se
	/// copia [16px, 352px) -> [0px, 336px) en cada fila:
	///   ancho = 352-16 = 336 px = 42 bytes = 21 words
	///   modulo = 44 - 42 = 2   (una fila avanza 44 bytes, el copy cubre 42)
	///   alto  = 288 filas (272 si tambien hay wrap vertical, se descuenta 16)
	///
	/// Se hace en DOS blits no-solapados por plano via un scratch (superficie ->
	/// scratch -> superficie): el CopyRect solapado no mueve el buffer en
	/// WinUAE-DBG. El scratch (12 KB) se reutiliza plano a plano.
	void add_shift(eng::graphics::FramePlan& plan, eng::s8 vdx, eng::s8 vdy) {
		const eng::u8* base = m_scene.bitplanes();
		// Desplazamiento de 16 px = 2 bytes. Si avanzamos a la derecha (vdx>0) el
		// contenido se mueve a la izquierda (origen +2, destino +0); si vamos a la
		// izquierda, al reves.
		const eng::u32 src_dx = vdx > 0 ? 2u : 0u;
		const eng::u32 dst_dx = vdx < 0 ? 2u : 0u;
		const eng::u32 src_dy = vdy > 0 ? surface_bytes_per_row * 16u : 0u;
		const eng::u32 dst_dy = vdy < 0 ? surface_bytes_per_row * 16u : 0u;
		// OJO: 336 son PIXELES = 42 bytes = 21 words. Tratar 336 como bytes daria
		// 168 words (8x de mas) y corromperia el buffer.
		const eng::u16 width_bytes = vdx != 0
			? static_cast<eng::u16>(surface_bytes_per_row - (tile_size / 8u))
			: surface_bytes_per_row;
		const eng::u16 height = vdy != 0 ? static_cast<eng::u16>(Scene::surface_height - tile_size) : Scene::surface_height;
		const eng::u16 words = static_cast<eng::u16>(width_bytes / 2u);
		const eng::s16 src_mod = static_cast<eng::s16>(surface_bytes_per_row - width_bytes);
		eng::u8* scratch = static_cast<eng::u8*>(m_scratch.data);
		for (eng::u8 pl = 0; pl < Scene::plane_count; ++pl) {
			// superficie -> scratch (scratch contiguo por fila: modulo 0)
			plan.add_tile_block_copy({
				eng::graphics::BlitJobKind::CopyRect,
				nullptr,
				reinterpret_cast<const eng::u16*>(base + pl * plane_bytes + src_dx + src_dy),
				reinterpret_cast<eng::u16*>(scratch),
				words,
				height,
				src_mod,   // filas de la superficie separadas 44 bytes
				0,         // scratch contiguo (42 bytes por fila)
				1,
				0,
				plane_bytes,
				width_bytes,
				false,
			});
			// scratch -> superficie (con el desplazamiento inverso)
			plan.add_tile_block_copy({
				eng::graphics::BlitJobKind::CopyRect,
				nullptr,
				reinterpret_cast<const eng::u16*>(scratch),
				reinterpret_cast<eng::u16*>(m_scene.bitplanes() + pl * plane_bytes + dst_dx + dst_dy),
				words,
				height,
				0,
				src_mod,
				1,
				0,
				width_bytes,
				plane_bytes,
				false,
			});
		}
	}

	/// Recarga los tiles del mundo que entran por el borde tras el wrap.
	///
	/// Tras el shift, la columna/fila que queda al descubierto ya no es una copia
	/// del buffer: es contenido NUEVO del mundo. Para avance a la derecha (vdx>0)
	/// se dibuja la columna de mundo (view_x + 21) en el slot 21 (el margen
	/// derecho); para avance a la izquierda, la columna view_x en el slot 0.
	/// Simetrico en vertical. Cada playfield recarga su propia capa.
	///
	/// Usa `make_playfield_upload_jobs`, que emite UN job por tile con stride de
	/// destino 2*plane_bytes (los planos de un playfield estan intercalados:
	/// PF1=1,3,5 / PF2=2,4,6). Fusionar los planos reduce el overhead por blit
	/// (wait_blitter + programacion de registros) de 3x a 1x.
	void add_edges(eng::graphics::FramePlan& plan, const TileCache& cache, eng::u8 playfield, eng::s8 vdx, eng::s8 vdy) {
		if (vdx != 0) {
			const eng::u16 slot_col = vdx > 0 ? static_cast<eng::u16>(surface_tiles_x - 1u) : 0u;
			const eng::u32 world_col = vdx > 0 ? m_cam.view_x + surface_tiles_x - 1u : m_cam.view_x;
			for (eng::u16 ty = 0; ty < surface_tiles_y; ++ty) {
				const eng::u32 world_row = m_cam.view_y + ty;
				eng::graphics::BlitJob blits[3] {};
				const eng::u8 n = m_scene.make_playfield_upload_jobs(
					playfield,
					cache.tile_data(world_tile(world_col, world_row, cache.layer_seed)),
					slot_col,
					ty,
					blits
				);
				for (eng::u8 b = 0; b < n; ++b) {
					plan.add_tile_block_copy(blits[b]);
				}
			}
		}
		if (vdy != 0) {
			const eng::u16 slot_row = vdy > 0 ? static_cast<eng::u16>(surface_tiles_y - 1u) : 0u;
			const eng::u32 world_row = vdy > 0 ? m_cam.view_y + surface_tiles_y - 1u : m_cam.view_y;
			for (eng::u16 tx = 0; tx < surface_tiles_x; ++tx) {
				const eng::u32 world_col = m_cam.view_x + tx;
				eng::graphics::BlitJob blits[3] {};
				const eng::u8 n = m_scene.make_playfield_upload_jobs(
					playfield,
					cache.tile_data(world_tile(world_col, world_row, cache.layer_seed)),
					tx,
					slot_row,
					blits
				);
				for (eng::u8 b = 0; b < n; ++b) {
					plan.add_tile_block_copy(blits[b]);
				}
			}
		}
	}

	void stamp_layer(const TileCache& cache, eng::u8 playfield) {
		for (eng::u16 ty = 0; ty < surface_tiles_y; ++ty) {
			for (eng::u16 tx = 0; tx < surface_tiles_x; ++tx) {
				const eng::u16 tile = world_tile(tx, ty, cache.layer_seed);
				for (eng::u8 plane_in_pf = 0; plane_in_pf < cache.planes; ++plane_in_pf) {
					const eng::u8 hw = Scene::hardware_plane_of(playfield, plane_in_pf);
					for (eng::u16 row = 0; row < tile_size; ++row) {
						const eng::u32 index =
							static_cast<eng::u32>(ty * tile_size + row) *
							(surface_bytes_per_row / sizeof(eng::u16)) +
							tx;
						m_scene.plane_words(hw).at(index) =
							cache.tile_data(tile)[static_cast<eng::u32>(plane_in_pf) * tile_size + row];
					}
				}
			}
		}
	}

	void rebuild_patterns() {
		const eng::u32 seed = rng_next();
		m_bg.layer_seed = seed ^ 0x0f0f0f0fu;
		m_fg.layer_seed = seed ^ 0xf0f0f0f0u;
	}

	eng::u32 rng_next() {
		eng::u32 z = m_rng;
		z ^= z << 13u;
		z ^= z >> 17u;
		z ^= z << 5u;
		m_rng = z;
		return z;
	}

	void publish_status(eng::u32 wrap) {
		eng::debug::mark_ready(
			g_eng_run_status,
			0x14000000u |
				(static_cast<eng::u32>(m_cam.px & 0xffu) << 16u) |
				(static_cast<eng::u32>(m_cam.py & 0xffu) << 8u) |
				(static_cast<eng::u32>(wrap & 0x0fu) << 4u)
		);
	}

	bool m_ready = false;
		Scene m_scene {};
		eng::graphics::FramePlan m_frame_plan {};
		eng::MemoryBlock m_scratch {};
		TileCache m_bg {};
	TileCache m_fg {};
	RingCamera m_cam {};
	eng::u32 m_last_epoch = 0;
	eng::u32 m_rng = 0x29a7c0deu;
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
