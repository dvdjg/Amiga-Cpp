#pragma once

/// \\file field_controller.hpp
/// Control independiente de un campo de juego.
///
/// Un `TileFieldController` no es el compositor: es el propietario logico de un
/// campo. Conserva su mapa, tileset, camara, paginas fisicas y presupuesto de
/// precarga. Varios controladores pueden emitir trabajos al mismo `FramePlan` y
/// una escena dual puede combinar sus entradas de hardware al final.
///
/// No hay heap ni copias de mapas. `TileFieldMap` y el tileset son vistas a datos
/// que normalmente provienen de assets estaticos. Las celdas son `u16`, para que
/// una herramienta de edicion no tenga que truncar indices; un asset puede usar
/// almacenamiento u8 externo y convertirlo al formato u16 al cocinarlo.

#include <eng/core/types.hpp>
#include <eng/graphics/drivers/tile_scroll.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::graphics {

enum class TileFieldSource : u8 {
	TileMap,
	Bitmap,
	Canvas,
};

struct TileFieldMap {
	const u16* cells = nullptr;
	u16 width = 0;
	u16 height = 0;

	constexpr bool valid() const { return cells != nullptr && width != 0 && height != 0; }
	constexpr u16 at(u16 x, u16 y) const {
		return cells[static_cast<u32>(y % height) * width + (x % width)];
	}
};

/// Vista no propietaria para un campo que no usa tilemap. El driver de bitmap o
/// canvas decide como instalar estos words; el controlador de tiles no los copia.
struct TileFieldBitmap {
	const u16* words = nullptr;
	u16 width = 0;
	u16 height = 0;
	u8 planes = 0;
};

struct TileFieldConfig {
	TileFieldSource source = TileFieldSource::TileMap;
	TileFieldMap map {};
	TileFieldBitmap bitmap {};
	const u16* tile_words = nullptr;
	u16 tile_count = 0;
	u8 tile_planes = 0;
	u8 playfield = 0;
	u8 upload_budget = 4;
	bool scroll = true;
};

struct TileFieldPageLoad {
	u16 page_x = 0;
	u16 page_y = 0;
	u16 cursor = 0;
	bool active = false;
};

struct TileFieldLoadedPage {
	u16 page_x = 0xffffu;
	u16 page_y = 0xffffu;
	bool valid = false;
};

struct TileFieldState {
	u16 x = 1;
	u16 y = 1;
	s16 dx = 1;
	s16 dy = 1;
	u16 world_tile_x = 0;
	u16 world_tile_y = 0;
	TileFieldPageLoad pending[4] {};
	TileFieldLoadedPage loaded[4] {};
	u32 uploads = 0;
	bool ready = false;

	constexpr u8 page(u16 page_x, u16 page_y) const {
		return static_cast<u8>((page_x & 1u) | ((page_y & 1u) << 1u));
	}
	constexpr bool page_ready(u16 page_x, u16 page_y) const {
		const u8 slot = page(page_x, page_y);
		return loaded[slot].valid && loaded[slot].page_x == page_x && loaded[slot].page_y == page_y;
	}
};

template <typename Scene>
class TileFieldController {
public:
	static constexpr u16 tile_size = Scene::tile_size;
	// Logical pages define camera/page selection; physical slots include a two-tile
	// right/bottom overlap for the extra word fetched by the chipset.
	static constexpr u16 page_tiles_x = 20;
	static constexpr u16 page_tiles_y = 16;
	static constexpr u16 physical_page_tiles_x = 22;
	static constexpr u16 physical_page_tiles_y = 18;
	static constexpr u8 page_count = 4;

	constexpr TileFieldController(u8 playfield, const TileFieldConfig& config)
		: m_config(config) { m_config.playfield = playfield; }

	constexpr TileFieldController() = default;

	void configure(const TileFieldConfig& config) { m_config = config; }
	constexpr const TileFieldConfig& config() const { return m_config; }
	constexpr TileFieldState& state() { return m_state; }
	constexpr const TileFieldState& state() const { return m_state; }

	/// Inicializa el campo. Bitmap y Canvas son validos aunque no tengan mapa:
	/// otro driver puede pintar su superficie y este controlador no emite scroll.
	bool begin(Scene& scene, u16 initial_x = 1, u16 initial_y = 1) {
		m_state.x = initial_x;
		m_state.y = initial_y;
		m_state.world_tile_x = static_cast<u16>(initial_x / tile_size);
		m_state.world_tile_y = static_cast<u16>(initial_y / tile_size);
		if (m_config.source != TileFieldSource::TileMap) {
			m_state.ready = true;
			return true;
		}
		if (!m_config.map.valid() || m_config.tile_words == nullptr ||
			m_config.tile_count == 0 || m_config.tile_planes != Scene::playfield_planes(m_config.playfield) ||
			m_config.playfield >= Scene::playfield_count()) return false;
		load_page(scene, static_cast<u16>(initial_x / tile_size), static_cast<u16>(initial_y / tile_size));
		m_state.ready = true;
		return true;
	}

	/// Avanza una camara y carga como maximo el presupuesto de este campo.
	/// El plan puede ser compartido por otros campos; sus paginas nunca se mezclan.
	bool update(Scene& scene, FramePlan& plan, u16 frame) {
		if (!m_state.ready || m_config.source != TileFieldSource::TileMap) return true;
		prefetch();
		step(frame);
		for (u8 i = 0; i < m_config.upload_budget; ++i) {
			if (!upload_one(scene, plan)) break;
		}
		return plan.ok();
	}

	constexpr drivers::ScrollPosition2 scroll_position() const {
		return {
			static_cast<u16>(m_state.x % (page_tiles_x * tile_size)),
			static_cast<u16>(m_state.y % (page_tiles_y * tile_size)),
		};
	}
	constexpr drivers::TilePageOrigin page_origin() const {
		const u8 slot = m_state.page(static_cast<u16>(m_state.x / tile_size / page_tiles_x),
			static_cast<u16>(m_state.y / tile_size / page_tiles_y));
		return {static_cast<u16>((slot & 1u) * physical_page_tiles_x),
			static_cast<u16>(((slot >> 1u) & 1u) * physical_page_tiles_y)};
	}

private:
	void step(u16 frame) {
		if (!m_config.scroll || (frame & 1u) != 0u) return;
		u16 nx = static_cast<u16>(m_state.x + m_state.dx);
		u16 ny = static_cast<u16>(m_state.y + m_state.dy);
		const u16 max_x = static_cast<u16>(m_config.map.width * tile_size - Scene::visible_width);
		const u16 max_y = static_cast<u16>(m_config.map.height * tile_size - Scene::visible_height);
		if (nx <= 1u || nx >= max_x) { m_state.dx = static_cast<s16>(-m_state.dx); nx = static_cast<u16>(m_state.x + m_state.dx); }
		if (ny <= 1u || ny >= max_y) { m_state.dy = static_cast<s16>(-m_state.dy); ny = static_cast<u16>(m_state.y + m_state.dy); }
		const u16 px = static_cast<u16>(nx / tile_size / page_tiles_x);
		const u16 py = static_cast<u16>(ny / tile_size / page_tiles_y);
		if (!m_state.page_ready(px, py)) return;
		m_state.x = nx; m_state.y = ny;
		m_state.world_tile_x = static_cast<u16>(nx / tile_size);
		m_state.world_tile_y = static_cast<u16>(ny / tile_size);
	}

	void prefetch() {
		const s16 px = static_cast<s16>(m_state.x / tile_size / page_tiles_x);
		const s16 py = static_cast<s16>(m_state.y / tile_size / page_tiles_y);
		const s16 sx = m_state.dx < 0 ? -1 : 1;
		const s16 sy = m_state.dy < 0 ? -1 : 1;
		queue_page(px + sx, py); queue_page(px, py + sy); queue_page(px + sx, py + sy);
		queue_page(px + sx * 2, py); queue_page(px, py + sy * 2);
	}

	void queue_page(s16 page_x, s16 page_y) {
		const u16 max_x = static_cast<u16>((m_config.map.width + page_tiles_x - 1u) / page_tiles_x);
		const u16 max_y = static_cast<u16>((m_config.map.height + page_tiles_y - 1u) / page_tiles_y);
		if (page_x < 0 || page_y < 0 || page_x >= max_x || page_y >= max_y) return;
		const u8 slot = m_state.page(static_cast<u16>(page_x), static_cast<u16>(page_y));
		if (m_state.page_ready(static_cast<u16>(page_x), static_cast<u16>(page_y))) return;
		for (const TileFieldPageLoad& load : m_state.pending)
			if (load.active && (m_state.page(load.page_x, load.page_y) == slot ||
				(load.page_x == page_x && load.page_y == page_y))) return;
		for (TileFieldPageLoad& load : m_state.pending) if (!load.active) {
			load = {static_cast<u16>(page_x), static_cast<u16>(page_y), 0, true}; return;
		}
	}

	void load_page(Scene& scene, u16 world_x, u16 world_y) {
		const u8 slot = m_state.page(static_cast<u16>(world_x / page_tiles_x), static_cast<u16>(world_y / page_tiles_y));
		const u16 px = static_cast<u16>((slot & 1u) * physical_page_tiles_x);
		const u16 py = static_cast<u16>(((slot >> 1u) & 1u) * physical_page_tiles_y);
		const u16 base_x = static_cast<u16>(world_x / page_tiles_x * page_tiles_x);
		const u16 base_y = static_cast<u16>(world_y / page_tiles_y * page_tiles_y);
		for (u16 y = 0; y < physical_page_tiles_y; ++y) for (u16 x = 0; x < physical_page_tiles_x; ++x) {
			const u16* source = tile(m_config.map.at(static_cast<u16>(base_x + x), static_cast<u16>(base_y + y)));
			for (u8 plane = 0; plane < m_config.tile_planes; ++plane) {
				Span<u16> destination = scene.plane_words(Scene::hardware_plane_of(m_config.playfield, plane));
				for (u16 row = 0; row < tile_size; ++row)
					destination.at(static_cast<u32>(py + y) * tile_size * (Scene::surface_bytes_per_row / sizeof(u16)) +
						static_cast<u32>(row) * (Scene::surface_bytes_per_row / sizeof(u16)) + px + x) =
						source[static_cast<u32>(plane) * tile_size + row];
			}
		}
		m_state.loaded[slot] = {static_cast<u16>(world_x / page_tiles_x), static_cast<u16>(world_y / page_tiles_y), true};
	}

	const u16* tile(u16 index) const {
		return m_config.tile_words + static_cast<u32>((index < m_config.tile_count ? index : 0u) * m_config.tile_planes) * tile_size;
	}

	bool upload_one(Scene& scene, FramePlan& plan) {
		const u16 displayed_x = static_cast<u16>(m_state.x / tile_size);
		const u16 displayed_y = static_cast<u16>(m_state.y / tile_size);
		const u8 displayed_slot = m_state.page(static_cast<u16>(displayed_x / page_tiles_x), static_cast<u16>(displayed_y / page_tiles_y));
		TileFieldPageLoad* selected = nullptr;
		for (TileFieldPageLoad& candidate : m_state.pending) {
			if (candidate.active && m_state.page(candidate.page_x, candidate.page_y) != displayed_slot) { selected = &candidate; break; }
		}
		if (selected == nullptr) return false;
		const u16 tile_x = static_cast<u16>(selected->cursor % physical_page_tiles_x);
		const u16 tile_y = static_cast<u16>(selected->cursor / physical_page_tiles_x);
		const u8 slot = m_state.page(selected->page_x, selected->page_y);
		const u16 wx = static_cast<u16>(selected->page_x * page_tiles_x + tile_x);
		const u16 wy = static_cast<u16>(selected->page_y * page_tiles_y + tile_y);
		BlitJob jobs[3] {};
		scene.make_playfield_upload_jobs(m_config.playfield, tile(m_config.map.at(wx, wy)), tile_x, tile_y,
			static_cast<u16>((slot & 1u) * physical_page_tiles_x),
			static_cast<u16>(((slot >> 1u) & 1u) * physical_page_tiles_y), jobs);
		if (!plan.add_tile_block_copy(jobs[0])) return false;
		++m_state.uploads; ++selected->cursor;
		if (selected->cursor == physical_page_tiles_x * physical_page_tiles_y) {
			m_state.loaded[slot] = {selected->page_x, selected->page_y, true}; selected->active = false;
		}
		return true;
	}

	TileFieldConfig m_config {};
	TileFieldState m_state {};
};

} // namespace eng::graphics
