#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
#include <amg/graphics/drivers/ehb_tile_scroll.hpp>
#include <amg/graphics/frame_plan.hpp>
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

struct CameraPixels {
	amg::u16 x = 0;
	amg::u16 y = 0;
};

/// Numero de tiles graficos distintos que compone esta demo.
///
/// Aunque el mapa logico sigue siendo una rejilla sencilla, usar 64 patrones evita
/// que una herramienta de analisis confunda scroll real con repeticion visual.
/// En un juego normal estos patrones vendrian del exportador UAF; aqui se generan
/// a mano para que el ejemplo sea autocontenido y facil de depurar.
constexpr amg::u16 tile_pattern_count = 64;
constexpr amg::u16 map_tiles_x = 64;
constexpr amg::u16 map_tiles_y = 32;
constexpr amg::u16 surface_tiles_x = drivers::EhbTileScrollScene::surface_width / drivers::EhbTileScrollScene::tile_size;
constexpr amg::u16 surface_tiles_y = drivers::EhbTileScrollScene::surface_height / drivers::EhbTileScrollScene::tile_size;
constexpr amg::u16 tile_size = drivers::EhbTileScrollScene::tile_size;
constexpr amg::u8 tile_update_budget = 2;
constexpr amg::u16 route_center_pixels = tile_size * 4u;
constexpr amg::u16 route_radius_pixels = tile_size * 4u;
constexpr amg::u16 route_step_pixels = tile_size * 2u;
constexpr amg::u16 axis_segment_frames = 40;
constexpr amg::u16 circle_entry_frames = 40;
constexpr amg::u16 circle_step_frames = 2;
constexpr amg::u16 logical_scroll_columns = map_tiles_x - surface_tiles_x + 1u;
constexpr amg::u16 logical_scroll_rows = map_tiles_y - surface_tiles_y + 1u;

constexpr drivers::EhbPalette sky_palette {{
	0x001, 0x014, 0x06e, 0x0af, 0x7df, 0xfff, 0xfd6, 0xff0,
	0x86b, 0xb9d, 0x263, 0x4a5, 0x6d7, 0xd8f, 0xf5b, 0x222,
	0x002, 0x025, 0x047, 0x069, 0x08b, 0x0ad, 0x4cf, 0x8ef,
	0x210, 0x431, 0x652, 0x873, 0xa94, 0xcb5, 0xed6, 0x333,
}};

constexpr drivers::EhbPalette jungle_palette {{
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xff0,
	0x451, 0x783, 0x0f4, 0x4f8, 0x9fc, 0xfd7, 0xf6a, 0x222,
	0x010, 0x031, 0x052, 0x073, 0x094, 0x0b5, 0x3d7, 0x7f9,
	0x320, 0x541, 0x762, 0x983, 0xba4, 0xdc5, 0xfe6, 0x333,
}};

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

/// Construye el mapa virtual retenido que recorrera la camara.
///
/// En el engine final esta funcion desaparecera de la demo: el mapa vendra de UAF
/// con capas, metadatos y posiblemente reglas de streaming. Mantenerla aqui tiene
/// valor pedagogico porque separa con claridad "mundo logico" de "superficie
/// fisica": el mapa mide 64x32 tiles, mientras que el driver solo muestra y
/// recicla una superficie 480x416.
void build_virtual_map(tilemap::PackedTileCell* cells) {
	for (amg::u16 y = 0; y < map_tiles_y; ++y) {
		for (amg::u16 x = 0; x < map_tiles_x; ++x) {
			// La demo no quiere parecer un tablero de test. Cada banda conserva una
			// lectura estetica clara, pero mezcla x/y para introducir marcas
			// reconocibles: columnas, diagonales y acentos. Esas marcas son las que
			// FrameScope sigue entre frames para verificar que la camara se mueve
			// como declara la telemetria lateral.
			amg::u16 tile = static_cast<amg::u16>((x * 7u + y * 11u + ((x ^ y) & 7u)) & (tile_pattern_count - 1u));
			if (y < 3) {
				tile = static_cast<amg::u16>((tile & 0x30u) | (((x + y * 5u) % 11u) == 0u ? 1u : 0u));
			} else if (y == 3) {
				tile = static_cast<amg::u16>(2u + ((x / 3u) & 1u) + ((tile & 0x18u)));
			} else if (y < 8) {
				tile = static_cast<amg::u16>(4u + ((x + y) % 4u) + (tile & 0x30u));
			} else if (y < 12) {
				tile = static_cast<amg::u16>(8u + (((x / 2u) + y) % 4u) + (tile & 0x30u));
			} else {
				tile = static_cast<amg::u16>(12u + ((x + y * 3u) % 4u) + (tile & 0x30u));
			}
			cells[static_cast<amg::u32>(y) * map_tiles_x + x].set_tile(tile);
		}
	}
}

/// Convierte tiles procedurales a formato planar 6bpp listo para Blitter.
///
/// Cada tile ocupa 6 planos consecutivos. Dentro de cada plano hay 16 words, una
/// por fila de 16 pixels. Esa disposicion permite que `TileBlockCopy` copie una
/// columna/fila offscreen sin transformar datos en runtime. Para assets reales, el
/// exportador UAF deberia generar exactamente este tipo de cache o una variante
/// compatible con el driver elegido.
void build_tile_word_cache(amg::u16* tile_words) {
	for (amg::u16 tile = 0; tile < tile_pattern_count; ++tile) {
		for (amg::u8 y = 0; y < tile_size; ++y) {
			// `shape` decide la familia visual principal y `variant` introduce
			// variacion de color/mascara sin cambiar el formato planar. La idea es
			// didactica: un tile 16x16 en Amiga es solo una pila de words, uno por
			// plano y fila, y el color final sale de combinar bits de los 6 planos.
			const amg::u8 shape = static_cast<amg::u8>(tile & 15u);
			const amg::u8 variant = static_cast<amg::u8>((tile >> 4u) & 3u);
			amg::u8 color_a = 2;
			amg::u8 color_b = 3;
			amg::u8 color_accent = 5;
			amg::u16 mask_a = 0xffffu;
			amg::u16 mask_accent = 0x0000u;

			if (shape <= 1u) {
				color_a = static_cast<amg::u8>(2u + variant);
				color_b = static_cast<amg::u8>(3u + variant);
				color_accent = static_cast<amg::u8>(5u + variant * 8u);
				mask_accent = (shape == 1u && y >= 5u && y <= 11u) ? static_cast<amg::u16>(0x3ffcu ^ (variant * 0x1111u)) : 0x0000u;
			} else if (shape <= 3u) {
				color_a = static_cast<amg::u8>(8u + variant);
				color_b = static_cast<amg::u8>(9u + variant);
				color_accent = static_cast<amg::u8>(33u + variant * 2u);
				mask_a = y > 8u ? 0xffffu : static_cast<amg::u16>(0xffffu >> (8u - y));
				mask_accent = static_cast<amg::u16>(0x8000u >> ((y + shape * 3u + variant) & 15u));
			} else if (shape <= 7u) {
				color_a = static_cast<amg::u8>(10u + (shape & 1u) + variant);
				color_b = static_cast<amg::u8>(42u + (shape & 1u) + variant);
				color_accent = (shape & 2u) ? static_cast<amg::u8>(13u + variant) : static_cast<amg::u8>(7u + variant);
				mask_a = (y & 4u) ? static_cast<amg::u16>(0x33ccu ^ (variant * 0x1111u)) : static_cast<amg::u16>(0xcc33u ^ (variant * 0x1111u));
				mask_accent = static_cast<amg::u16>(0x1111u << ((y + variant) & 3u));
			} else if (shape <= 11u) {
				color_a = static_cast<amg::u8>(6u + variant);
				color_b = static_cast<amg::u8>(39u + variant);
				color_accent = (shape & 1u) ? static_cast<amg::u8>(14u + variant) : static_cast<amg::u8>(15u + variant);
				mask_a = (y & 4u) ? static_cast<amg::u16>(0xaaaau ^ (variant * 0x1111u)) : static_cast<amg::u16>(0x5555u ^ (variant * 0x1111u));
				mask_accent = (y == 0u || y == 15u) ? 0xffffu : static_cast<amg::u16>(0x8001u >> ((y + variant) & 3u));
			} else {
				color_a = static_cast<amg::u8>(3u + variant);
				color_b = static_cast<amg::u8>(35u + variant * 2u);
				color_accent = (shape & 1u) ? static_cast<amg::u8>(12u + variant) : static_cast<amg::u8>(5u + variant);
				mask_a = (y & 2u) ? static_cast<amg::u16>(0xccccu ^ (variant * 0x1111u)) : static_cast<amg::u16>(0x3333u ^ (variant * 0x1111u));
				mask_accent = static_cast<amg::u16>(0x00f0u << ((y + variant) & 3u));
			}

			const amg::u16 mask_b = static_cast<amg::u16>(~(mask_a | mask_accent));
			for (amg::u8 plane = 0; plane < drivers::EhbTileScrollScene::plane_count; ++plane) {
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
				tile_words[
					static_cast<amg::u32>(tile) * (drivers::EhbTileScrollScene::tile_bytes() / sizeof(amg::u16)) +
					static_cast<amg::u32>(plane) * tile_size +
					y
				] = row;
			}
		}
	}
}

/// Devuelve el inicio planar de un tile dentro de la cache Chip RAM.
///
/// El indice se enmascara contra `tile_pattern_count - 1` porque esta demo usa una
/// biblioteca de patrones potencia de dos. En un engine de produccion lo normal
/// sera validar el indice al cargar la escena y no pagar comprobaciones extra en
/// cada upload de Blitter.
const amg::u16* tile_source(const amg::MemoryBlock& block, amg::u16 tile_index) {
	return reinterpret_cast<const amg::u16*>(
		static_cast<const amg::u8*>(block.data) +
		static_cast<amg::u32>(tile_index & (tile_pattern_count - 1u)) * drivers::EhbTileScrollScene::tile_bytes()
	);
}

/// Estampa un tile por CPU durante la inicializacion.
///
/// Esta ruta no es la que queremos para streaming en juego; se usa solo para
/// poblar la superficie inicial antes de arrancar la animacion. Los cambios
/// incrementales posteriores pasan por `FramePlan` y Blitter, que es el contrato
/// relevante para el engine.
void stamp_tile_cpu(
	drivers::EhbTileScrollScene& scene,
	const amg::u16* tile,
	amg::u16 surface_tile_x,
	amg::u16 surface_tile_y
) {
	for (amg::u8 plane = 0; plane < drivers::EhbTileScrollScene::plane_count; ++plane) {
		for (amg::u16 y = 0; y < tile_size; ++y) {
			const amg::u32 destination_offset =
				static_cast<amg::u32>(plane) * drivers::EhbTileScrollScene::plane_bytes +
				static_cast<amg::u32>(surface_tile_y * tile_size + y) * drivers::EhbTileScrollScene::surface_bytes_per_row +
				static_cast<amg::u32>(surface_tile_x) * sizeof(amg::u16);
			reinterpret_cast<amg::u16*>(scene.bitplanes() + destination_offset)[0] =
				tile[static_cast<amg::u32>(plane) * tile_size + y];
		}
	}
}

struct DemoGame {
	/// Reserva memoria, genera assets procedurales y deja instalada la primera lista Copper.
	///
	/// La secuencia de inicializacion refleja el orden que necesitara una room real:
	/// configurar arenas, reservar bitplanes/copperlist en Chip RAM, cargar tiles,
	/// preparar la superficie fisica y publicar un primer estado lateral para que
	/// las pruebas sepan que la demo esta viva.
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		if (!backend.configure_memory({180u * 1024u, 16u * 1024u, 8u * 1024u})) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000110u);
			return;
		}

		const drivers::EhbTileScrollConfig config {
			&sky_palette,
			palette_zones,
			static_cast<amg::u8>(sizeof(palette_zones) / sizeof(palette_zones[0])),
			1536,
		};
		if (!m_scene.init(backend.memory(), config)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000111u);
			return;
		}

		m_tiles = backend.memory().chip.allocate(drivers::EhbTileScrollScene::tile_bytes() * tile_pattern_count, 16);
		if (!m_tiles.valid()) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000112u);
			return;
		}

		clear_bytes(m_scene.bitplanes(), drivers::EhbTileScrollScene::bitplane_bytes);
		build_virtual_map(m_cells);
		build_tile_word_cache(static_cast<amg::u16*>(m_tiles.data));
		m_map.reset(m_cells, map_tiles_x, map_tiles_y);

		for (amg::u16 y = 0; y < surface_tiles_y; ++y) {
			for (amg::u16 x = 0; x < surface_tiles_x; ++x) {
				const amg::u16 tile = m_cells[static_cast<amg::u32>(y) * map_tiles_x + x].tile_index();
				stamp_tile_cpu(m_scene, tile_source(m_tiles, tile), x, y);
			}
		}

		// La superficie lineal arranca completamente poblada. El objeto `m_ring`
		// sigue llevando la contabilidad de que columnas/filas de mundo estan listas
		// en cada slot fisico porque ese contrato sera el mismo cuando pasemos a una
		// superficie circular real. En este MVP todavia limitamos la ruta al margen
		// seguro de 480x416 para poder razonar el Copper y el Blitter por separado.
		m_scheduler.reset();
		m_ring.reset(0, 0);

		const CameraPixels initial_camera = scripted_camera(0);
		m_active_camera_tile_x = camera_tile(initial_camera.x);
		m_active_camera_tile_y = camera_tile(initial_camera.y);
		m_previous_logical_column = m_active_camera_tile_x;
		m_previous_logical_row = m_active_camera_tile_y;

		if (!m_scene.rebuild_copper(initial_camera.x, initial_camera.y)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000114u);
			return;
		}

		m_scene.install(backend);
		publish_status(initial_camera.x, initial_camera.y, 0);
		m_ready = true;
	}

	/// Avanza la camara retenida, prepara tiles offscreen y recompila el display.
	///
	/// El juego solo decide una posicion de camara. El resto se reparte en capas:
	/// el scheduler decide que tiles faltan, `FramePlan` convierte esos tiles en
	/// trabajos de Blitter y `EhbTileScrollScene` traduce la camara a punteros de
	/// bitplane + `BPLCON1`. Esta separacion es la base para soportar otros drivers
	/// y, mas adelante, otras maquinas.
	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		if (m_ready) {
			const CameraPixels camera = scripted_camera(context.frame.frame_index);
			m_active_camera_tile_x = camera_tile(camera.x);
			m_active_camera_tile_y = camera_tile(camera.y);
			schedule_next_visible_margin(m_active_camera_tile_x, m_active_camera_tile_y);
			const amg::u8 tile_jobs = upload_prefetch_tiles(backend);
			if (!m_scene.rebuild_copper(camera.x, camera.y)) {
				amg::debug::mark_failed(g_amg_run_status, 0x00000115u);
				return;
			}
			m_scene.install(backend);
			publish_status(camera.x, camera.y, tile_jobs);
		}
	}

	/// Reinstala la copperlist vigente y mantiene vivo el probe lateral.
	///
	/// En esta demo la lista se recompila en `update`, pero reinstalarla en render
	/// deja claro el punto de compromiso del frame: lo que llega aqui es lo que el
	/// Amiga debe mostrar durante el siguiente refresco.
	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		if (m_ready) {
			m_scene.install(backend);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	bool m_ready = false;
	drivers::EhbTileScrollScene m_scene {};
	tilemap::PackedTileCell m_cells[map_tiles_x * map_tiles_y] {};
	tilemap::TileMap16 m_map {};
	tilemap::ProgressiveTileScheduler m_scheduler {};
	drivers::EhbBidirectionalRingPrefetch m_ring {};
	amg::graphics::FramePlan m_frame_plan {};
	amg::MemoryBlock m_tiles {};
	amg::u16 m_previous_logical_column = 0;
	amg::u16 m_previous_logical_row = 0;
	amg::u16 m_active_camera_tile_x = 0;
	amg::u16 m_active_camera_tile_y = 0;
	amg::u16 m_pending_world_column = drivers::EhbBidirectionalRingPrefetch::unknown_index;
	amg::u16 m_pending_world_row = drivers::EhbBidirectionalRingPrefetch::unknown_index;
	amg::u16 m_pending_column_slot = 0;
	amg::u16 m_pending_row_slot = 0;
	amg::u8 m_pending_column_tiles = 0;
	amg::u8 m_pending_row_tiles = 0;
	amg::u8 m_recycled_columns = 0;
	amg::u8 m_recycled_rows = 0;

	static constexpr amg::u16 camera_tile(amg::u16 pixels) {
		return static_cast<amg::u16>(pixels / tile_size);
	}

	static constexpr amg::u16 lerp_u16(amg::u16 from, amg::u16 to, amg::u16 step, amg::u16 steps) {
		return static_cast<amg::u16>(from + ((static_cast<amg::u32>(to - from) * step) / steps));
	}

	static constexpr amg::u16 inv_lerp_u16(amg::u16 from, amg::u16 to, amg::u16 step, amg::u16 steps) {
		return static_cast<amg::u16>(from - ((static_cast<amg::u32>(from - to) * step) / steps));
	}

	/// Componente X de una circunferencia de 64 pasos y 64 pixels de radio.
	///
	/// La tabla evita depender de `sin/cos` o de libm en m68k, y deja una ruta muy
	/// visible para FrameScope: cada paso es pequeno, continuo y repetible. Los
	/// valores estan redondeados a pixels enteros, suficiente para una demo de
	/// scroll de bitplanes.
	static constexpr amg::s16 circle_offset_x(amg::u8 index) {
		constexpr amg::s16 offsets[] {
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

	/// Componente Y de la misma circunferencia de 64 pasos.
	///
	/// Y positivo significa camara mas baja dentro de la superficie. En pantalla el
	/// contenido se movera hacia arriba, que es justo lo que FrameScope contrasta
	/// contra la telemetria de camara.
	static constexpr amg::s16 circle_offset_y(amg::u8 index) {
		constexpr amg::s16 offsets[] {
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

	/// Ruta visible de validacion.
	///
	/// La camara empieza en el centro del margen oculto. Primero se mueve dos tiles
	/// a la derecha, vuelve, sube dos tiles, vuelve, y despues recorre una orbita de
	/// cuatro tiles de radio. Hay un tramo de entrada desde el centro al borde
	/// derecho de la circunferencia para evitar un salto visual justo antes de la
	/// fase mas importante de la prueba.
	///
	/// Toda la ruta queda dentro de la superficie lineal, asi que los uploads de
	/// prefetch pueden escribirse fuera de pantalla sin asomar tiles a mitad del
	/// viewport.
	static constexpr CameraPixels scripted_camera(amg::u32 frame_index) {
		const amg::u16 base = route_center_pixels;
		const amg::u16 right = static_cast<amg::u16>(route_center_pixels + route_step_pixels);
		const amg::u16 up = static_cast<amg::u16>(route_center_pixels - route_step_pixels);
		if (frame_index < axis_segment_frames) {
			return {lerp_u16(base, right, static_cast<amg::u16>(frame_index), axis_segment_frames), base};
		}
		if (frame_index < axis_segment_frames * 2u) {
			return {inv_lerp_u16(right, base, static_cast<amg::u16>(frame_index - axis_segment_frames), axis_segment_frames), base};
		}
		if (frame_index < axis_segment_frames * 3u) {
			return {base, inv_lerp_u16(base, up, static_cast<amg::u16>(frame_index - axis_segment_frames * 2u), axis_segment_frames)};
		}
		if (frame_index < axis_segment_frames * 4u) {
			return {base, lerp_u16(up, base, static_cast<amg::u16>(frame_index - axis_segment_frames * 3u), axis_segment_frames)};
		}

		const amg::u32 circle_entry_start = axis_segment_frames * 4u;
		if (frame_index < circle_entry_start + circle_entry_frames) {
			return {
				lerp_u16(base, static_cast<amg::u16>(route_center_pixels + route_radius_pixels), static_cast<amg::u16>(frame_index - circle_entry_start), circle_entry_frames),
				base,
			};
		}

		const amg::u8 circle_index = static_cast<amg::u8>(((frame_index - circle_entry_start - circle_entry_frames) / circle_step_frames) & 63u);
		return {
			static_cast<amg::u16>(route_center_pixels + circle_offset_x(circle_index)),
			static_cast<amg::u16>(route_center_pixels + circle_offset_y(circle_index)),
		};
	}

	static constexpr amg::u16 visible_safe_right_column(amg::u16 camera_tile_x) {
		return static_cast<amg::u16>(camera_tile_x + drivers::EhbBidirectionalRingPrefetch::visible_columns + 1u);
	}

	static constexpr amg::u16 visible_safe_bottom_row(amg::u16 camera_tile_y) {
		return static_cast<amg::u16>(camera_tile_y + drivers::EhbBidirectionalRingPrefetch::visible_rows + 1u);
	}

	/// Encola como mucho una franja horizontal y una vertical cuando la camara cruza tiles.
	///
	/// Esta version es deliberadamente conservadora: solo solicita trabajo si no
	/// hay nada pendiente. Asi se ve con claridad como un presupuesto pequeno de
	/// Blitter reparte una franja en varios frames, que es la tecnica que queremos
	/// explotar para scrolls suaves estilo plataformas.
	void schedule_next_visible_margin(amg::u16 camera_world_column, amg::u16 camera_world_row) {
		if (m_scheduler.queued_count() != 0 || m_pending_column_tiles != 0 || m_pending_row_tiles != 0) {
			return;
		}

		if (camera_world_column > m_previous_logical_column) {
			enqueue_margin_strip(
				{visible_safe_right_column(camera_world_column), camera_world_row, 1, drivers::EhbBidirectionalRingPrefetch::visible_rows},
				tilemap::TileUpdateEdge::Right
			);
		} else if (camera_world_column < m_previous_logical_column && camera_world_column != 0) {
			enqueue_margin_strip(
				{static_cast<amg::u16>(camera_world_column - 1u), camera_world_row, 1, drivers::EhbBidirectionalRingPrefetch::visible_rows},
				tilemap::TileUpdateEdge::Left
			);
		}

		if (camera_world_row > m_previous_logical_row) {
			enqueue_margin_strip(
				{camera_world_column, visible_safe_bottom_row(camera_world_row), drivers::EhbBidirectionalRingPrefetch::visible_columns, 1},
				tilemap::TileUpdateEdge::Bottom
			);
		} else if (camera_world_row < m_previous_logical_row && camera_world_row != 0) {
			enqueue_margin_strip(
				{camera_world_column, static_cast<amg::u16>(camera_world_row - 1u), drivers::EhbBidirectionalRingPrefetch::visible_columns, 1},
				tilemap::TileUpdateEdge::Top
			);
		}

		m_previous_logical_column = camera_world_column;
		m_previous_logical_row = camera_world_row;
	}

	/// Traduce una franja de mundo a jobs elementales de tile.
	///
	/// `ProgressiveTileScheduler` descompone una columna o fila en unidades 16x16.
	/// La demo guarda contadores pendientes para saber cuando toda la franja ha
	/// quedado lista y entonces marca el slot como reciclado en el anillo 2D.
	void enqueue_margin_strip(tilemap::TileRect rect, tilemap::TileUpdateEdge edge) {
		if (rect.left >= surface_tiles_x || rect.top >= surface_tiles_y) {
			return;
		}

		const amg::u8 enqueued = m_scheduler.enqueue_strip(m_map, rect, edge, 0, 12, 1);
		if (enqueued == 0) {
			return;
		}
		if (edge == tilemap::TileUpdateEdge::Left || edge == tilemap::TileUpdateEdge::Right) {
			m_pending_world_column = rect.left;
			m_pending_column_slot = rect.left;
			m_pending_column_tiles = enqueued;
		} else {
			m_pending_world_row = rect.top;
			m_pending_row_slot = rect.top;
			m_pending_row_tiles = enqueued;
		}
	}

	/// Consume un presupuesto pequeno de tiles y lo ejecuta por Blitter.
	///
	/// Esta funcion es el embrion del futuro `TileScrollDriver::compile_frame`: la
	/// escena retenida dice que tiles urgen, el driver decide presupuesto, y el
	/// backend ejecuta trabajos concretos de Blitter sin que la logica de juego vea
	/// registros custom.
	amg::u8 upload_prefetch_tiles(amg::amiga::MinimalBackend& backend) {
		const tilemap::ProgressiveTileUpdatePlan plan = m_scheduler.take_budget(tile_update_budget);
		m_frame_plan.clear();
		for (amg::u8 i = 0; i < plan.count; ++i) {
			const tilemap::TileUpdateJob& job = plan.jobs[i];
			if (!m_frame_plan.add_tile_block_copy(m_scene.make_tile_upload_job(
				tile_source(m_tiles, job.tile_index),
				job.x,
				job.y
			))) {
				amg::debug::mark_failed(g_amg_run_status, 0x00000113u);
				return 0;
			}
		}

		if (plan.count != 0 && !backend.execute_frame_plan(m_frame_plan)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000116u);
			return 0;
		}
		for (amg::u8 i = 0; i < plan.count; ++i) {
			const tilemap::TileUpdateJob& job = plan.jobs[i];
			if ((job.edge == tilemap::TileUpdateEdge::Left || job.edge == tilemap::TileUpdateEdge::Right) &&
				m_pending_column_tiles != 0 && job.x == m_pending_world_column) {
				--m_pending_column_tiles;
				if (m_pending_column_tiles == 0) {
					m_ring.mark_column_ready(m_pending_column_slot, m_pending_world_column);
					m_pending_world_column = drivers::EhbBidirectionalRingPrefetch::unknown_index;
					++m_recycled_columns;
				}
			}
			if ((job.edge == tilemap::TileUpdateEdge::Top || job.edge == tilemap::TileUpdateEdge::Bottom) &&
				m_pending_row_tiles != 0 && job.y == m_pending_world_row) {
				--m_pending_row_tiles;
				if (m_pending_row_tiles == 0) {
					m_ring.mark_row_ready(m_pending_row_slot, m_pending_world_row);
					m_pending_world_row = drivers::EhbBidirectionalRingPrefetch::unknown_index;
					++m_recycled_rows;
				}
			}
		}
		return plan.count;
	}

	/// Publica un estado compacto para herramientas externas.
	///
	/// `runStatus.detail` es nuestro canal barato de observabilidad: no sustituye a
	/// GDB ni al canal lateral avanzado, pero permite que scripts y FrameScope sepan
	/// en que frame/camara/prefetch estaba la demo cuando se tomo cada captura.
	void publish_status(amg::u16 camera_x, amg::u16 camera_y, amg::u8 tile_jobs) {
		const amg::u8 prefetch_flags = static_cast<amg::u8>(
			(m_recycled_columns != 0 ? 0x1u : 0u) |
			(m_recycled_rows != 0 ? 0x2u : 0u)
		);
		amg::debug::mark_ready(
			g_amg_run_status,
			0x11000000u |
				(static_cast<amg::u32>(camera_x & 0xffu) << 16u) |
				(static_cast<amg::u32>(camera_y & 0xffu) << 8u) |
				(static_cast<amg::u32>(tile_jobs & 0x0fu) << 4u) |
				static_cast<amg::u32>(prefetch_flags)
		);
	}
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
