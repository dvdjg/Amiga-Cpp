#pragma once

/// Campo de tiles con scroll 8-way sobre una superficie circular/recentrable.
///
/// La superficie no es un conjunto de tres paginas. En cada eje que scrollea
/// mide viewport + margen_en_bloques * bloque. Con scroll vertical reserva ademas
/// una linea fisica de guardia fuera de la reticula de tiles.
/// La ventana se mueve dentro de ella y, cuando
/// alcanza una frontera, se cambia a la copia preparada del otro lado; solo se
/// modifica metadata y el puntero del display, nunca se copia la pantalla.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

struct TileLayerMap {
	eng::Span<const eng::u16> cells {};
	eng::u16 width = 0, height = 0, wrap_x = 0, wrap_y = 0, edge_tile = 0;
	static eng::s32 wrap_coordinate(eng::s32 value, eng::u16 period) {
		// Los mapas de las demos son potencias de dos (256x128). En el 68000,
		// sustituir modulo por una máscara evita __modsi3 en cada tile. El camino
		// general conserva el contrato para mapas reales de cualquier tamaño.
		if ((period & static_cast<eng::u16>(period - 1u)) == 0u) {
			return value & static_cast<eng::s32>(period - 1u);
		}
		return ((value % static_cast<eng::s32>(period)) + period) % period;
	}
	eng::u16 tile_at(eng::s32 tx, eng::s32 ty) const {
		if (cells.empty() || width == 0 || height == 0) return edge_tile;
		eng::s32 x = tx, y = ty;
		if (wrap_x) x = wrap_coordinate(x, wrap_x);
		else if (x < 0 || x >= width) return edge_tile;
		if (wrap_y) y = wrap_coordinate(y, wrap_y);
		else if (y < 0 || y >= height) return edge_tile;
		return cells.at(static_cast<eng::u32>(x) + static_cast<eng::u32>(y) * width);
	}
};

struct TileScrollOffset { eng::s16 x = 0, y = 0; };

enum class TileSetLayout : eng::u8 {
	TileMajor,       // [tile][plane][row][word], layout de la demo
	RowMajor,        // [row][tile][plane][word], permite fusionar filas
	ColumnMajor,     // [tile][plane][row][word] con contrato de columna contigua
};

struct TileFieldUpdateResult {
	bool accepted = false;
	bool applied = false;
	bool pending = false;
	constexpr explicit operator bool() const { return accepted; }
};

struct TileFieldConfig {
	TileLayerMap map;
	const eng::u16* tileset = nullptr;
	eng::u16 tileset_count = 0;
	eng::u8 tileset_planes = 6;
	eng::u16 tile_width = 16, tile_size = 16;
	eng::u16 viewport_w = 320, viewport_h = 256;
	eng::s16 max_delta_x = 5, max_delta_y = 5;
	eng::u8 max_tiles_per_frame = 4;
	eng::u8 safety_margin_blocks = 2; // bloques totales: uno por lado con el mínimo
	bool scroll_x = true, scroll_y = true;
	TileSetLayout tileset_layout = TileSetLayout::TileMajor;
};

/// Región que se puede consumir en pasos de tiles. Una región nunca cruza la
/// ventana visible. En XY dos regiones que tocan una esquina se recortan para
/// que la esquina pertenezca a una sola banda.
struct TilePendingStrip {
	eng::s32 world_tile_x = 0, world_tile_y = 0;
	eng::u16 fb_tile_x = 0, fb_tile_y = 0, width = 0, height = 0;
	eng::u32 cursor = 0;
	eng::u16 cursor_x = 0, cursor_y = 0;
	bool active = false;
};

struct TileFieldState {
	eng::s32 world_x = 0, world_y = 0;
	eng::s32 surface_origin_x = 0, surface_origin_y = 0;
	eng::u16 surface_w = 0, surface_h = 0;
	eng::u16 window_x = 0, window_y = 0;
	eng::u16 viewport_w = 0, viewport_h = 0;
	eng::u32 recenter_x = 0, recenter_y = 0;
	eng::u32 bands = 0;
	TilePendingStrip pending[64] {};
	eng::u8 pending_count = 0;
	bool initialized = false;
};

struct FieldHardwareView {
	const eng::u8* bitplanes = nullptr;
	eng::u32 plane_stride = 0, display_byte_offset = 0;
	eng::u8 fine_x = 0, plane_count = 0, first_hardware_plane = 0;
	eng::u16 fetch_bytes = 0;
	eng::u16 bpl1mod = 0;
	eng::u16 row_bytes = 0, surface_w = 0, surface_h = 0, visible_h = 0, guard_line = 0xffffu;
	eng::u32 plane_bytes = 0;
	bool split_active = false;
	eng::u16 split_line = 0;
	eng::u32 split_display_byte_offset = 0;
};

struct BitmapFieldConfig {
	const eng::u8* bitmap = nullptr;
	eng::u16 width = 0, height = 0;
	eng::u8 planes = 0;
	bool scrolling = false;
};

class TileFieldController {
public:
	TileFieldController() = default;

	bool begin(eng::MemorySystem& memory, const TileFieldConfig& config, TileScrollOffset initial) {
		m_config = config;
		if (!valid_config()) return false;
		const eng::u16 margin_x = config.scroll_x ? static_cast<eng::u16>(config.safety_margin_blocks * config.tile_width) : 0;
		const eng::u16 margin_y = config.scroll_y ? static_cast<eng::u16>(config.safety_margin_blocks * config.tile_size) : 0;
		m_fb_w = config.scroll_x ? static_cast<eng::u16>(config.viewport_w + margin_x) : config.viewport_w;
		const eng::u16 tile_rows = config.scroll_y
			? static_cast<eng::u16>((config.viewport_h + margin_y) / config.tile_size) : 0;
		m_fb_h = config.scroll_y
			? static_cast<eng::u16>(tile_rows * config.tile_size + 1u) : config.viewport_h;
		m_row_bytes = static_cast<eng::u16>(m_fb_w / 8u);
		m_plane_bytes = static_cast<eng::u32>(m_row_bytes) * m_fb_h;
		m_framebuffer = memory.chip.allocate(m_plane_bytes * config.tileset_planes, 16);
		if (!m_framebuffer.valid()) return false;
		if (config.scroll_y) {
			for (eng::u8 plane = 0; plane < config.tileset_planes; ++plane) {
				auto* guard = static_cast<eng::u8*>(m_framebuffer.data) +
					static_cast<eng::u32>(plane) * m_plane_bytes +
					static_cast<eng::u32>(m_fb_h - 1u) * m_row_bytes;
				for (eng::u16 byte = 0; byte < m_row_bytes; ++byte) guard[byte] = 0;
			}
		}
		m_state = {};
		m_pending_state = false;
		m_state.world_x = initial.x; m_state.world_y = initial.y;
		m_state.surface_w = m_fb_w; m_state.surface_h = m_fb_h;
		m_state.viewport_w = config.viewport_w; m_state.viewport_h = config.viewport_h;
		m_state.window_x = config.scroll_x ? left_margin(true) : 0;
		m_state.window_y = config.scroll_y ? left_margin(false) : 0;
		m_state.surface_origin_x = initial.x - static_cast<eng::s32>(m_state.window_x);
		m_state.surface_origin_y = initial.y - static_cast<eng::s32>(m_state.window_y);
		m_queued_dx = m_queued_dy = 0;
		enqueue_initial();
		m_state.initialized = true;
		return true;
	}

	TileFieldUpdateResult pump(eng::graphics::FramePlan& plan, eng::u8 budget) {
		if (!m_state.initialized) return {};
		draw_pending(plan, budget);
		commit_pending_state();
		return {true, !has_pending(), has_pending()};
	}
	bool busy() const { return m_state.initialized && has_pending(); }

	TileFieldUpdateResult update(const TileFieldConfig& config, TileScrollOffset delta, eng::graphics::FramePlan& plan) {
		if (!m_state.initialized) return {};
		m_config = config;
		if (!valid_config()) return {};
		// Never expose a band whose Blitter jobs are still pending. It is legal to
		// spend this frame draining the queue while the logical camera waits.
		if (has_pending()) {
			if (config.scroll_x) m_queued_dx += clamp(delta.x, config.max_delta_x);
			if (config.scroll_y) m_queued_dy += clamp(delta.y, config.max_delta_y);
			draw_pending(plan, config.max_tiles_per_frame);
			commit_pending_state();
			return {true, false, has_pending()};
		}
		const eng::s32 requested_x = m_queued_dx + (config.scroll_x ? clamp(delta.x, config.max_delta_x) : 0);
		const eng::s32 requested_y = m_queued_dy + (config.scroll_y ? clamp(delta.y, config.max_delta_y) : 0);
		const eng::s16 bounded_x = static_cast<eng::s16>(requested_x > 5 ? 5 : (requested_x < -5 ? -5 : requested_x));
		const eng::s16 bounded_y = static_cast<eng::s16>(requested_y > 5 ? 5 : (requested_y < -5 ? -5 : requested_y));
		const eng::s16 dx = config.scroll_x ? clamp(bounded_x, config.max_delta_x) : 0;
		const eng::s16 dy = config.scroll_y ? clamp(bounded_y, config.max_delta_y) : 0;
		m_queued_dx = requested_x - dx;
		m_queued_dy = requested_y - dy;
		const eng::s32 old_x = m_state.world_x, old_y = m_state.world_y;
		const eng::u16 old_window_x = m_state.window_x, old_window_y = m_state.window_y;
		const eng::s32 old_origin_x = m_state.surface_origin_x, old_origin_y = m_state.surface_origin_y;
		m_state.world_x += dx; m_state.world_y += dy;
		m_state.window_x = static_cast<eng::u16>(m_state.window_x + dx);
		m_state.window_y = static_cast<eng::u16>(m_state.window_y + dy);
		// The window is allowed to run across the surface. At an end, select the
		// already prepared copy on the other side; no pixel move is necessary.
		if (config.scroll_x) recenter_axis(true);
		if (config.scroll_y) recenter_axis(false);
		const eng::s32 new_tx = floor_div(m_state.world_x, config.tile_width);
		const eng::s32 new_ty = floor_div(m_state.world_y, config.tile_size);
		const bool enter_x = config.scroll_x && floor_div(old_x, config.tile_width) != new_tx;
		const bool enter_y = config.scroll_y && floor_div(old_y, config.tile_size) != new_ty;
		if (enter_x) enqueue_x_band(dx > 0 ? 1 : -1, enter_y);
		if (enter_y) enqueue_y_band(dy > 0 ? 1 : -1, enter_x, dx > 0 ? 1 : -1);
		m_pending_world_x = m_state.world_x; m_pending_world_y = m_state.world_y;
		m_pending_window_x = m_state.window_x; m_pending_window_y = m_state.window_y;
		m_pending_origin_x = m_state.surface_origin_x; m_pending_origin_y = m_state.surface_origin_y;
		m_pending_state = has_pending();
		draw_pending(plan, config.max_tiles_per_frame);
		if (has_pending()) {
			// The newly exposed band is not visible yet. Keep the previous display
			// position until its last TileBlockCopy has completed.
			m_state.world_x = old_x; m_state.world_y = old_y;
			m_state.window_x = old_window_x; m_state.window_y = old_window_y;
			m_state.surface_origin_x = old_origin_x; m_state.surface_origin_y = old_origin_y;
		}
		commit_pending_state();
		return {true, !has_pending() && !m_pending_state, has_pending() || m_pending_state};
	}

	FieldHardwareView hardware_view(eng::u8 first_hardware_plane) const {
		FieldHardwareView v;
		v.bitplanes = static_cast<const eng::u8*>(m_framebuffer.data);
		v.plane_stride = m_plane_bytes;
		const eng::s32 px = m_state.world_x - m_state.surface_origin_x;
		const eng::s32 py = m_state.world_y - m_state.surface_origin_y;
		const eng::s32 fetch = px > 0 ? ((px - 1) & ~15) : 0;
		v.display_byte_offset = static_cast<eng::u32>(fetch / 8) + static_cast<eng::u32>(py) * m_row_bytes;
		v.fine_x = static_cast<eng::u8>((16 - (px & 15)) & 15);
		v.fetch_bytes = static_cast<eng::u16>(m_config.scroll_x ? m_config.viewport_w / 8u + 2u : m_config.viewport_w / 8u);
		v.bpl1mod = static_cast<eng::u16>(m_row_bytes - v.fetch_bytes);
		v.row_bytes = m_row_bytes; v.surface_w = m_fb_w; v.surface_h = m_fb_h; v.visible_h = m_config.viewport_h;
		v.plane_bytes = m_plane_bytes;
		if (m_config.scroll_y) {
			v.guard_line = static_cast<eng::u16>(m_fb_h - 1u);
			v.split_active = true;
			v.split_line = static_cast<eng::u16>(m_config.viewport_h / 2u);
			v.split_display_byte_offset = v.display_byte_offset +
				static_cast<eng::u32>(v.split_line) * m_row_bytes;
		}
		v.plane_count = m_config.tileset_planes; v.first_hardware_plane = first_hardware_plane;
		return v;
	}
	const eng::u8* bitplanes() const { return static_cast<const eng::u8*>(m_framebuffer.data); }
	const TileFieldState& state() const { return m_state; }

private:
	static eng::s16 clamp(eng::s16 v, eng::s16 max) { return v > max ? max : (v < -max ? static_cast<eng::s16>(-max) : v); }
	static eng::s32 floor_div(eng::s32 v, eng::s32 d) { return v >= 0 ? v / d : -static_cast<eng::s32>((-v + d - 1) / d); }
	bool valid_config() const {
		const eng::u16 margin_x = m_config.scroll_x ? static_cast<eng::u16>(m_config.safety_margin_blocks * m_config.tile_width) : 0;
		const eng::u16 margin_y = m_config.scroll_y ? static_cast<eng::u16>(m_config.safety_margin_blocks * m_config.tile_size) : 0;
		const eng::u16 fb_w = m_config.scroll_x ? static_cast<eng::u16>(m_config.viewport_w + margin_x) : m_config.viewport_w;
		const eng::u16 tile_rows = m_config.scroll_y
			? static_cast<eng::u16>((m_config.viewport_h + margin_y) / m_config.tile_size) : 0;
		const eng::u16 fb_h = m_config.scroll_y
			? static_cast<eng::u16>(tile_rows * m_config.tile_size + 1u) : m_config.viewport_h;
		return m_config.tileset && m_config.tileset_count && m_config.tileset_planes &&
			m_config.safety_margin_blocks >= 2u && m_config.safety_margin_blocks <= 3u &&
			m_config.max_delta_x >= 0 && m_config.max_delta_x <= 5 && m_config.max_delta_y >= 0 && m_config.max_delta_y <= 5 &&
			m_config.tile_width && m_config.tile_size && !(m_config.tile_width & 15u) &&
			!(m_config.viewport_w % m_config.tile_width) && !(m_config.viewport_h % m_config.tile_size) &&
			(!m_config.scroll_x || (m_config.viewport_w / 8u + 2u <= fb_w / 8u)) &&
			(!m_config.scroll_y || (fb_h > m_config.viewport_h && fb_h >= tile_rows * m_config.tile_size + 1u));
	}
	eng::u16 left_margin(bool x) const {
		const eng::u16 block = x ? m_config.tile_width : m_config.tile_size;
		return static_cast<eng::u16>((m_config.safety_margin_blocks / 2u) * block);
	}
	void enqueue_initial() {
		const eng::u16 cols = static_cast<eng::u16>(m_fb_w / m_config.tile_width);
		const eng::u16 rows = static_cast<eng::u16>(m_fb_h / m_config.tile_size);
		enqueue(floor_div(m_state.surface_origin_x, m_config.tile_width), floor_div(m_state.surface_origin_y, m_config.tile_size), 0, 0, cols, rows);
	}
	void enqueue_x_band(eng::s16 direction, bool diagonal) {
		const eng::u16 cols = static_cast<eng::u16>(m_fb_w / m_config.tile_width);
		const eng::u16 rows = static_cast<eng::u16>(m_fb_h / m_config.tile_size);
		const eng::u16 entering = direction > 0
			? static_cast<eng::u16>((m_state.window_x + m_config.viewport_w) / m_config.tile_width)
			: 0;
		const eng::u16 opposite = direction > 0 ? 0 : static_cast<eng::u16>(cols - 1u);
		enqueue_physical(entering, 0, 1, rows);
		enqueue_physical(opposite, 0, 1, rows);
	}
	void enqueue_y_band(eng::s16 direction, bool diagonal, eng::s16 xdirection) {
		const eng::u16 cols = static_cast<eng::u16>(m_fb_w / m_config.tile_width);
		const eng::u16 rows = static_cast<eng::u16>(m_fb_h / m_config.tile_size);
		const eng::u16 entering = direction > 0
			? static_cast<eng::u16>((m_state.window_y + m_config.viewport_h) / m_config.tile_size)
			: 0;
		const eng::u16 opposite = direction > 0 ? 0 : static_cast<eng::u16>(rows - 1u);
		if (!diagonal) {
			enqueue_physical(0, entering, cols, 1);
			enqueue_physical(0, opposite, cols, 1);
			return;
		}
		const eng::u16 x_entering = xdirection > 0
			? static_cast<eng::u16>((m_state.window_x + m_config.viewport_w) / m_config.tile_width)
			: 0;
		const eng::u16 x_opposite = xdirection > 0 ? 0 : static_cast<eng::u16>(cols - 1u);
		enqueue_y_row(entering, x_entering, x_opposite);
		enqueue_y_row(opposite, x_entering, x_opposite);
	}
	void enqueue_y_row(eng::u16 row, eng::u16 corner_a, eng::u16 corner_b) {
		if (corner_a > corner_b) { const eng::u16 t = corner_a; corner_a = corner_b; corner_b = t; }
		if (corner_a) enqueue_physical(0, row, corner_a, 1);
		if (corner_b > corner_a + 1u) enqueue_physical(static_cast<eng::u16>(corner_a + 1u), row, static_cast<eng::u16>(corner_b - corner_a - 1u), 1);
		if (corner_b + 1u < static_cast<eng::u16>(m_fb_w / m_config.tile_width)) enqueue_physical(static_cast<eng::u16>(corner_b + 1u), row, static_cast<eng::u16>(m_fb_w / m_config.tile_width - corner_b - 1u), 1);
	}
	void enqueue_physical(eng::u16 fx, eng::u16 fy, eng::u16 w, eng::u16 h, eng::u16 x_offset = 0) {
		const eng::u16 physical_x = static_cast<eng::u16>(fx + x_offset);
		const eng::s32 wx = floor_div(m_state.surface_origin_x + static_cast<eng::s32>(physical_x) * m_config.tile_width, m_config.tile_width);
		const eng::s32 wy = floor_div(m_state.surface_origin_y + static_cast<eng::s32>(fy) * m_config.tile_size, m_config.tile_size);
		enqueue(wx, wy, physical_x, fy, w, h);
	}
	void enqueue(eng::s32 wx, eng::s32 wy, eng::u16 fx, eng::u16 fy, eng::u16 w, eng::u16 h) {
		if (!w || !h || m_state.pending_count >= 64) return;
		for (eng::u8 i = 0; i < m_state.pending_count; ++i) {
			const TilePendingStrip& p = m_state.pending[i];
			if (p.active && p.fb_tile_x == fx && p.fb_tile_y == fy && p.width == w && p.height == h && p.world_tile_x == wx && p.world_tile_y == wy) return;
		}
		m_state.pending[m_state.pending_count++] = {wx, wy, fx, fy, w, h, 0, 0, 0, true};
		++m_state.bands;
	}
	void recenter_axis(bool x) {
		const eng::u16 size = x ? m_fb_w : m_fb_h, view = x ? m_config.viewport_w : m_config.viewport_h;
		eng::u16& pos = x ? m_state.window_x : m_state.window_y;
		const eng::u16 left = left_margin(x);
		const eng::u16 right = static_cast<eng::u16>((m_config.safety_margin_blocks - m_config.safety_margin_blocks / 2u) * (x ? m_config.tile_width : m_config.tile_size));
		if (pos < left || static_cast<eng::u32>(pos) + view + right > size) {
			// Opposite-side copy is valid because every crossing prepared one block.
			const eng::u16 old_pos = pos;
			pos = left;
			if (x) m_state.surface_origin_x += static_cast<eng::s32>(old_pos) - pos;
			else m_state.surface_origin_y += static_cast<eng::s32>(old_pos) - pos;
			if (x) ++m_state.recenter_x; else ++m_state.recenter_y;
		}
	}
	eng::graphics::BlitJob tile_job(eng::u16 tile, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h) const {
		const eng::u16 words = static_cast<eng::u16>(w / 16u);
		const eng::u32 plane_words = static_cast<eng::u32>(m_config.tile_size) * words;
		const eng::u16* src = m_config.tileset + static_cast<eng::u32>(tile % m_config.tileset_count) * m_config.tileset_planes * plane_words;
		eng::u16* dst = reinterpret_cast<eng::u16*>(static_cast<eng::u8*>(m_framebuffer.data) + static_cast<eng::u32>(y) * m_row_bytes + static_cast<eng::u32>(x) / 8u);
		return {eng::graphics::BlitJobKind::TileBlockCopy, nullptr, src, dst, words, h, 0,
			static_cast<eng::s16>(m_row_bytes - words * 2u), m_config.tileset_planes, 0,
			plane_words * sizeof(eng::u16), m_plane_bytes, false};
	}
	eng::graphics::BlitJob horizontal_job(eng::u16 tile, eng::s32 x, eng::s32 y, eng::u16 count) const {
		const eng::u16 words = static_cast<eng::u16>(m_config.tile_width / 16u);
		const eng::u32 plane_words = static_cast<eng::u32>(m_config.tile_size) * words;
		const eng::u32 row_words = static_cast<eng::u32>(m_config.tileset_count) * words;
		const eng::u16* src = m_config.tileset + static_cast<eng::u32>(tile) * words;
		eng::u16* dst = reinterpret_cast<eng::u16*>(static_cast<eng::u8*>(m_framebuffer.data) + static_cast<eng::u32>(y) * m_row_bytes + static_cast<eng::u32>(x) / 8u);
		return {eng::graphics::BlitJobKind::TileBlockCopy, nullptr, src, dst, static_cast<eng::u16>(count * words), m_config.tile_size,
			static_cast<eng::s16>((row_words - count * words) * sizeof(eng::u16)),
			static_cast<eng::s16>(m_row_bytes - count * words * 2u), m_config.tileset_planes, 0,
			row_words * m_config.tile_size * sizeof(eng::u16), m_plane_bytes, false};
	}
	eng::graphics::BlitJob vertical_job(eng::u16 tile, eng::s32 x, eng::s32 y, eng::u16 count) const {
		const eng::u16 words = static_cast<eng::u16>(m_config.tile_width / 16u);
		const eng::u32 plane_words = static_cast<eng::u32>(m_config.tile_size) * words;
		const eng::u32 tile_stride = static_cast<eng::u32>(m_config.tileset_count) * plane_words;
		const eng::u16* src = m_config.tileset + static_cast<eng::u32>(tile) * plane_words;
		eng::u16* dst = reinterpret_cast<eng::u16*>(static_cast<eng::u8*>(m_framebuffer.data) + static_cast<eng::u32>(y) * m_row_bytes + static_cast<eng::u32>(x) / 8u);
		return {eng::graphics::BlitJobKind::TileBlockCopy, nullptr, src, dst, words, static_cast<eng::u16>(count * m_config.tile_size),
			static_cast<eng::s16>(tile_stride * sizeof(eng::u16) - words * sizeof(eng::u16)),
			static_cast<eng::s16>(m_row_bytes - words * 2u), m_config.tileset_planes, 0,
			plane_words * sizeof(eng::u16), m_plane_bytes, false};
	}
	void draw_pending(eng::graphics::FramePlan& plan, eng::u8 budget) {
		for (eng::u8 i = 0; i < m_state.pending_count && budget; ++i) {
			TilePendingStrip& p = m_state.pending[i]; if (!p.active) continue;
			const eng::u32 total = static_cast<eng::u32>(p.width) * p.height;
			while (p.cursor < total && budget) {
				// cursor_x/cursor_y recorren la franja sin dividir por cada tile.
				// En una CPU 68000 las divisiones enteras son mucho más caras que dos
				// incrementos y una comparación.
				const eng::u16 x = p.cursor_x, y = p.cursor_y;
				const eng::u16 tile = m_config.map.tile_at(p.world_tile_x + x, p.world_tile_y + y);
				eng::u16 run = 1;
				while (run < budget && ((p.width > 1 && x + run < p.width) || (p.width == 1 && y + run < p.height)) &&
					((p.height == 1 && m_config.tileset_layout == TileSetLayout::RowMajor) ||
					 (p.width == 1 && m_config.tileset_layout == TileSetLayout::ColumnMajor)) &&
					m_config.map.tile_at(p.world_tile_x + x + (p.width == 1 ? 0 : run), p.world_tile_y + y + (p.width == 1 ? run : 0)) == static_cast<eng::u16>(tile + run)) ++run;
				const bool horizontal = run > 1 && p.height == 1;
				const bool vertical = run > 1 && p.width == 1;
				const eng::s32 px = (p.fb_tile_x + x) * m_config.tile_width;
				const eng::s32 py = (p.fb_tile_y + y) * m_config.tile_size;
				const eng::graphics::BlitJob job = horizontal ? horizontal_job(tile, px, py, run) :
					vertical ? vertical_job(tile, px, py, run) : tile_job(tile, px, py, m_config.tile_width, m_config.tile_size);
				if (!plan.add_tile_block_copy(job)) return;
				p.cursor += run; budget = static_cast<eng::u8>(budget - run);
				p.cursor_x = static_cast<eng::u16>(p.cursor_x + run);
				while (p.cursor_x >= p.width) {
					p.cursor_x = static_cast<eng::u16>(p.cursor_x - p.width);
					++p.cursor_y;
				}
			}
			if (p.cursor == total) p.active = false;
		}
	}
	void commit_pending_state() {
		if (m_pending_state && !has_pending()) {
			m_state.world_x = m_pending_world_x; m_state.world_y = m_pending_world_y;
			m_state.window_x = m_pending_window_x; m_state.window_y = m_pending_window_y;
			m_state.surface_origin_x = m_pending_origin_x; m_state.surface_origin_y = m_pending_origin_y;
			m_pending_state = false;
		}
	}
	bool has_pending() const { for (eng::u8 i = 0; i < m_state.pending_count; ++i) if (m_state.pending[i].active) return true; return false; }
	TileFieldConfig m_config {};
	TileFieldState m_state {};
	eng::MemoryBlock m_framebuffer {};
	eng::u16 m_fb_w = 0, m_fb_h = 0, m_row_bytes = 0;
	eng::u32 m_plane_bytes = 0;
	bool m_pending_state = false;
	eng::s32 m_pending_world_x = 0, m_pending_world_y = 0;
	eng::s32 m_pending_origin_x = 0, m_pending_origin_y = 0;
	eng::u16 m_pending_window_x = 0, m_pending_window_y = 0;
	eng::s32 m_queued_dx = 0, m_queued_dy = 0;
};

} // namespace eng::field
