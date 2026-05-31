#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
#include <amg/graphics/drivers/ehb_scene.hpp>
#include <amg/graphics/tilemap/tile_scroll.hpp>
#include <amg/platform/amiga_minimal.hpp>
#include <amg/scene/virtual_scene.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile amg::debug::RunStatus g_amg_run_status {
	amg::debug::run_status_magic,
	amg::debug::run_status_version,
	static_cast<amg::u16>(amg::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

namespace drivers = amg::graphics::drivers;
namespace scene = amg::scene;
namespace tilemap = amg::graphics::tilemap;

constexpr amg::u16 screen_width = drivers::StaticEhbScene::width;
constexpr amg::u16 screen_height = drivers::StaticEhbScene::height;
constexpr amg::u16 bytes_per_row = drivers::StaticEhbScene::bytes_per_row;
constexpr amg::u8 plane_count = drivers::StaticEhbScene::plane_count;
constexpr amg::u32 plane_bytes = drivers::StaticEhbScene::plane_bytes;
constexpr amg::u16 map_tiles_x = 64;
constexpr amg::u16 map_tiles_y = 16;
constexpr amg::u16 tile_size = 16;
constexpr amg::u16 final_camera_x = 57;
constexpr amg::u16 screen_words_per_row = bytes_per_row / sizeof(amg::u16);

/// Paleta base: cielo limpio y colores de lectura alta.
///
/// La demo usa indices pequenos para que las zonas Copper cambien el ambiente sin
/// tocar los bitplanes. Es el mismo truco conceptual que usaremos en aventuras EHB:
/// los assets son estables, pero el Copper reinterpreta franjas de la escena.
constexpr drivers::EhbPalette sky_palette {{
	0x001, 0x014, 0x06e, 0x0af, 0x7df, 0xfff, 0xfd6, 0xff0,
	0x86b, 0xb9d, 0x263, 0x4a5, 0x6d7, 0xd8f, 0xf5b, 0x222,
	0x002, 0x025, 0x047, 0x069, 0x08b, 0x0ad, 0x4cf, 0x8ef,
	0x210, 0x431, 0x652, 0x873, 0xa94, 0xcb5, 0xed6, 0x333,
}};

/// Zona media: jungla saturada y ruinas claras.
constexpr drivers::EhbPalette jungle_palette {{
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xff0,
	0x451, 0x783, 0x0f4, 0x4f8, 0x9fc, 0xfd7, 0xf6a, 0x222,
	0x010, 0x031, 0x052, 0x073, 0x094, 0x0b5, 0x3d7, 0x7f9,
	0x320, 0x541, 0x762, 0x983, 0xba4, 0xdc5, 0xfe6, 0x333,
}};

/// Zona inferior: piedra calida, agua y media intensidad EHB para sombras.
constexpr drivers::EhbPalette under_palette {{
	0x000, 0x112, 0x246, 0x48a, 0x7bd, 0xfff, 0xfc8, 0xf90,
	0x421, 0x742, 0x085, 0x0aa, 0x4dd, 0xf6c, 0xf3a, 0x221,
	0x100, 0x211, 0x322, 0x533, 0x744, 0x955, 0xb76, 0xd98,
	0x012, 0x124, 0x236, 0x348, 0x45a, 0x66c, 0x88e, 0x333,
}};

constexpr drivers::EhbPaletteZone palette_zones[] {
	{0x74, &jungle_palette},
	{0xb8, &under_palette},
};

void clear_bytes(amg::u8* bytes, amg::u32 count) {
	for (amg::u32 i = 0; i < count; ++i) {
		bytes[i] = 0;
	}
}

/// Construye un mapa virtual procedimental.
///
/// En una demo exportada por herramientas, esta informacion vendria de UAF:
/// indices de tile, flips, metadatos de colision y atributos por zona. Aqui lo
/// generamos en codigo para que el MVP sea autocontenido y facil de compilar.
void build_virtual_map(tilemap::PackedTileCell* cells) {
	for (amg::u16 y = 0; y < map_tiles_y; ++y) {
		for (amg::u16 x = 0; x < map_tiles_x; ++x) {
			amg::u16 tile = 0;
			if (y < 3) {
				tile = static_cast<amg::u16>(((x + y * 5u) % 11u) == 0u ? 1u : 0u);
			} else if (y == 3) {
				tile = static_cast<amg::u16>(2u + ((x / 3u) & 1u));
			} else if (y < 8) {
				tile = static_cast<amg::u16>(4u + ((x + y) % 4u));
			} else if (y < 12) {
				tile = static_cast<amg::u16>(8u + (((x / 2u) + y) % 4u));
			} else {
				tile = static_cast<amg::u16>(12u + ((x + y * 3u) % 4u));
			}
			cells[static_cast<amg::u32>(y) * map_tiles_x + x].set_tile(tile);
		}
	}
}

/// Devuelve el color de un pixel dentro de un tile artistico.
///
/// Los patrones mezclan diagonales, dithering y ondas para que la demo tenga una
/// lectura humana agradable. No robamos codigo de `demoscene-repo/effects/tiles16`,
/// pero si adoptamos su idea clave: tiles 16x16, mapa virtual y scroll con dirty
/// flags por buffer.
amg::u8 tile_pixel(amg::u16 tile, amg::u8 lx, amg::u8 ly) {
	const bool checker = (((lx >> 2) + (ly >> 2) + tile) & 1u) != 0;
	const bool diagonal = ((lx + ly + tile * 3u) & 15u) < 3u;
	const bool sparkle = (((lx * 5u + ly * 3u + tile * 17u) & 63u) == 0u);

	if (tile <= 1u) {
		if (tile == 1u && ly > 5u && lx > 2u && lx < 14u) {
			return checker ? 5u : 38u;
		}
		return static_cast<amg::u8>(2u + ((tile + (ly >> 3)) & 1u));
	}

	if (tile <= 3u) {
		const bool ridge = ly >= ((lx + tile * 5u) & 15u);
		return ridge ? static_cast<amg::u8>(8u + (tile & 1u)) : 33u;
	}

	if (tile <= 7u) {
		if (sparkle) {
			return 7u;
		}
		if (diagonal) {
			return 13u;
		}
		return checker ? static_cast<amg::u8>(10u + (tile & 1u)) : static_cast<amg::u8>(42u + (tile & 1u));
	}

	if (tile <= 11u) {
		const bool brick_line = (ly == 0u) || (ly == 15u) || (((lx + (ly & 8u)) & 15u) == 0u);
		if (brick_line) {
			return 15u;
		}
		if (diagonal) {
			return 14u;
		}
		return checker ? 6u : 39u;
	}

	const bool wave = (((lx + (ly << 1)) + tile * 9u) & 15u) < 5u;
	if (wave) {
		return 12u;
	}
	if (sparkle) {
		return 5u;
	}
	return checker ? 3u : 35u;
}

/// Precalcula tiles planares.
///
/// UAF-R acabara entregando este tipo de datos ya cocinados. Mientras tanto, la
/// demo genera 16 tiles artisticas y las guarda como 6 planos x 16 filas x 1 word.
/// Esto hace que el render sea lo bastante rapido para que el canal lateral pueda
/// confirmar READY en pocos segundos.
void build_tile_word_cache(amg::u16 words[16][plane_count][tile_size]) {
	for (amg::u16 tile = 0; tile < 16; ++tile) {
		for (amg::u8 plane = 0; plane < plane_count; ++plane) {
			for (amg::u8 y = 0; y < tile_size; ++y) {
				words[tile][plane][y] = 0;
			}
		}

		for (amg::u8 y = 0; y < tile_size; ++y) {
			amg::u8 color_a = 2;
			amg::u8 color_b = 3;
			amg::u8 color_accent = 5;
			amg::u16 mask_a = (y & 4u) ? 0x0f0fu : 0xf0f0u;
			amg::u16 mask_accent = 0x0000u;

			if (tile <= 1u) {
				color_a = 2;
				color_b = 3;
				color_accent = 5;
				mask_a = 0xffffu;
				mask_accent = (tile == 1u && y >= 5u && y <= 11u) ? 0x3ffcu : 0x0000u;
			} else if (tile <= 3u) {
				color_a = 8;
				color_b = 9;
				color_accent = 33;
				mask_a = y > 8u ? 0xffffu : static_cast<amg::u16>(0xffffu >> (8u - y));
				mask_accent = static_cast<amg::u16>(0x8000u >> ((y + tile * 3u) & 15u));
			} else if (tile <= 7u) {
				color_a = static_cast<amg::u8>(10u + (tile & 1u));
				color_b = static_cast<amg::u8>(42u + (tile & 1u));
				color_accent = (tile & 2u) ? 13u : 7u;
				mask_a = (y & 4u) ? 0x33ccu : 0xcc33u;
				mask_accent = static_cast<amg::u16>(0x1111u << (y & 3u));
			} else if (tile <= 11u) {
				color_a = 6;
				color_b = 39;
				color_accent = (tile & 1u) ? 14u : 15u;
				mask_a = (y & 4u) ? 0xaaaau : 0x5555u;
				mask_accent = (y == 0u || y == 15u) ? 0xffffu : static_cast<amg::u16>(0x8001u >> (y & 3u));
			} else {
				color_a = 3;
				color_b = 35;
				color_accent = (tile & 1u) ? 12u : 5u;
				mask_a = (y & 2u) ? 0xccccu : 0x3333u;
				mask_accent = static_cast<amg::u16>(0x00f0u << (y & 3u));
			}

			const amg::u16 mask_b = static_cast<amg::u16>(~(mask_a | mask_accent));
			for (amg::u8 plane = 0; plane < plane_count; ++plane) {
				amg::u16 row = 0;
				if (color_a & (1u << plane)) {
					row |= static_cast<amg::u16>(mask_a & ~mask_accent);
				}
				if (color_b & (1u << plane)) {
					row |= mask_b;
				}
				if (color_accent & (1u << plane)) {
					row |= mask_accent;
				}
				words[tile][plane][y] = row;
			}
		}
	}
}

/// Renderiza el viewport de la escena virtual con tiles planares.
///
/// Sigue siendo una ruta tutorial, pero ya respeta la forma mental del Amiga:
/// procesa words de 16 pixels y compone el fine scroll desplazando words. El driver
/// final no redibujara todo el viewport; solo actualizara margenes ocultos y
/// cambiara punteros/fine scroll sincronizados con VBlank.
void draw_viewport(
	amg::u8* planes,
	const tilemap::PackedTileCell* cells,
	const amg::u16 tile_words[16][plane_count][tile_size],
	const scene::Camera2D& camera
) {
	clear_bytes(planes, drivers::StaticEhbScene::bitplane_bytes);

	const tilemap::ScrollPosition scroll = camera.scroll_position();
	const amg::u8 fine_x = static_cast<amg::u8>(scroll.x & 15u);
	const amg::u16 start_tile_x = static_cast<amg::u16>(scroll.x / tile_size);
	for (amg::u16 sy = 0; sy < screen_height; ++sy) {
		const amg::u16 wy = static_cast<amg::u16>(scroll.y + sy);
		const amg::u16 tile_y = static_cast<amg::u16>((wy / tile_size) % map_tiles_y);
		const amg::u8 local_y = static_cast<amg::u8>(wy & 15u);
		for (amg::u16 source_col = 0; source_col <= screen_words_per_row; ++source_col) {
			const amg::u16 tile_x = static_cast<amg::u16>((start_tile_x + source_col) % map_tiles_x);
			const amg::u16 tile = cells[static_cast<amg::u32>(tile_y) * map_tiles_x + tile_x].tile_index();
			for (amg::u8 plane = 0; plane < plane_count; ++plane) {
				amg::u16* row = reinterpret_cast<amg::u16*>(
					planes + static_cast<amg::u32>(plane) * plane_bytes + static_cast<amg::u32>(sy) * bytes_per_row
				);
				const amg::u16 source_word = tile_words[tile & 15u][plane][local_y];
				if (fine_x == 0u) {
					if (source_col < screen_words_per_row) {
						row[source_col] |= source_word;
					}
				} else {
					if (source_col < screen_words_per_row) {
						row[source_col] |= static_cast<amg::u16>(source_word << fine_x);
					}
					if (source_col > 0u && source_col - 1u < screen_words_per_row) {
						row[source_col - 1u] |= static_cast<amg::u16>(source_word >> (16u - fine_x));
					}
				}
			}
		}
	}
}

struct DemoGame {
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		if (!backend.configure_memory({80u * 1024u, 16u * 1024u, 8u * 1024u})) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000100u);
			return;
		}

		const drivers::StaticEhbSceneConfig scene_config {
			&sky_palette,
			palette_zones,
			static_cast<amg::u8>(sizeof(palette_zones) / sizeof(palette_zones[0])),
			1536,
		};
		if (!m_scene.init(backend.memory(), scene_config)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000101u);
			return;
		}

		build_virtual_map(m_cells);
		m_map.reset(m_cells, map_tiles_x, map_tiles_y);

		scene::Camera2D camera {};
		camera.reset({0, 0, map_tiles_x * tile_size, map_tiles_y * tile_size}, {screen_width, screen_height});
		m_layer = {
			"painted_world",
			&m_map,
			{
				scene::TileFramebufferStrategy::DoubleBufferedHiddenMargins,
				tile_size,
				1,
				1,
				true,
				true,
			},
			0,
			0,
		};
		if (!m_virtual_scene.reset(camera, &m_layer, 1)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000102u);
			return;
		}

		build_tile_word_cache(m_tile_words);
		draw_frame(backend, final_camera_x);
		m_scene.install(backend);
		amg::debug::mark_ready(
			g_amg_run_status,
			0x10000000u |
				(static_cast<amg::u32>(m_camera_x & 0xffu) << 16u) |
				(static_cast<amg::u32>(m_last_fine_x & 0x0fu) << 8u) |
				static_cast<amg::u32>(m_last_layer_count)
		);
		m_ready_to_run = true;
	}

	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		if (!m_ready_to_run) {
			return;
		}
		m_scene.install(backend);
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	void draw_frame(amg::amiga::MinimalBackend& backend, amg::u16 camera_x) {
		scene::Camera2D& camera = m_virtual_scene.camera();
		camera.begin_frame();
		m_camera_x = camera_x;
		camera.set_position(m_camera_x, 0);

		scene::VirtualSceneFrame frame = m_virtual_scene.prepare_frame();
		m_last_layer_count = frame.tile_layer_count;
		m_last_fine_x = frame.tile_layer_count != 0u ? frame.tile_layers[0].scroll.scroll.fine_x_pixels : 0;

		draw_viewport(m_scene.bitplanes(), m_cells, m_tile_words, camera);
		m_scene.install(backend);
	}

	bool m_ready_to_run = false;
	drivers::StaticEhbScene m_scene {};
	tilemap::PackedTileCell m_cells[map_tiles_x * map_tiles_y] {};
	amg::u16 m_tile_words[16][plane_count][tile_size] {};
	tilemap::TileMap16 m_map {};
	scene::TileLayer m_layer {};
	scene::VirtualScene m_virtual_scene {};
	amg::u16 m_camera_x = 0;
	amg::u8 m_last_fine_x = 0;
	amg::u8 m_last_layer_count = 0;
};

DemoGame g_game {};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	amg::debug::reset(g_amg_run_status);

	amg::amiga::MinimalBackend backend {};
	amg::Engine engine { backend, g_game };
	engine.run_frames(0xffff);

	return 0;
}
