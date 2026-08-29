#pragma once

/// \file tile_field.hpp
/// API de campos de tiles con scroll infinito por doble pagina.
///
/// Diseño documentado en `docs/architecture/TILE_FIELD_API.md`. Resumen:
///
/// - Cada playfield (campo) es un controlador independiente con su propio mapa
///   de tiles (indices u16), su framebuffer y su camara. No conoce a los demas.
/// - El scroll infinito en un eje se consigue con un framebuffer del DOBLE del
///   viewport en ese eje: dos paginas del tamaño del viewport. La camara scrollea
///   dentro del framebuffer; la pagina offscreen se va rellenando con los tiles
///   que se revelaran (repartidos segun el presupuesto), y al cruzar el limite de
///   pagina el display "salta" a la pagina ya preparada (invisible).
/// - `tile_field_begin(offset_absoluto)` pinta el framebuffer inicial;
///   `tile_field_update(delta)` se llama cada frame con el desplazamiento
///   respecto al frame anterior (tope `max_delta_x/y`, configurable y a priori).
/// - El `DpfDisplayComposer` es la UNICA capa que toca los registros compartidos
///   (BPLxPT, BPLCON1, modulos, BPLCON2, DPF). Un campo bitmap opcional
///   (`BitmapFieldConfig`) sirve para naves/HUD/BOBs.
///
/// La API no impone DPF: un solo `TileFieldController` con 5 bitplanes es un
/// campo de 32 colores normal.

#include <eng/core/types.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

/// Indices de tile del mundo (u16 -> tilesets de miles de patrones).
///
/// `wrap_x`/`wrap_y` > 0 hacen que la coordenada de mundo repita cada N tiles
/// (mundo periodico); 0 significa borde del mundo (no se puede scrollar mas
/// alla; fuera del mapa se devuelve el tile `edge_tile`).
struct TileLayerMap {
	const eng::u16* cells = nullptr;
	eng::u16 width = 0;
	eng::u16 height = 0;
	eng::u16 wrap_x = 0;
	eng::u16 wrap_y = 0;
	eng::u16 edge_tile = 0;

	eng::u16 tile_at(eng::s32 tx, eng::s32 ty) const {
		eng::u32 x, y;
		if (wrap_x != 0) {
			x = static_cast<eng::u32>(((tx % static_cast<eng::s32>(wrap_x)) + wrap_x) % wrap_x);
		} else {
			if (tx < 0 || tx >= static_cast<eng::s32>(width)) return edge_tile;
			x = static_cast<eng::u32>(tx);
		}
		if (wrap_y != 0) {
			y = static_cast<eng::u32>(((ty % static_cast<eng::s32>(wrap_y)) + wrap_y) % wrap_y);
		} else {
			if (ty < 0 || ty >= static_cast<eng::s32>(height)) return edge_tile;
			y = static_cast<eng::u32>(ty);
		}
		return cells[y * width + x];
	}
};

/// Desplazamiento (x, y) en pixeles. En `begin` es el offset absoluto desde la
/// esquina superior izquierda; en `update` es el DELTA respecto al frame previo.
struct TileScrollOffset {
	eng::s16 x = 0;
	eng::s16 y = 0;
};

/// Configuracion estatica de un campo de tiles.
struct TileFieldConfig {
	TileLayerMap map;
	const eng::u16* tileset = nullptr;   // patrones en Chip RAM (indice u16)
	eng::u16 tileset_count = 0;
	eng::u8 tileset_planes = 6;          // planos del campo (5 => 32 colores)
	eng::u16 tile_size = 16;
	eng::u16 viewport_w = 320;
	eng::u16 viewport_h = 256;
	/// Contrato de rendimiento (a priori): tope del salto por frame por eje.
	/// max_delta pequeno (p. ej. 2) da muchos frames para repartir el redibujo
	/// de la pagina offscreen; grande (15) exige mas tiles por frame.
	eng::s16 max_delta_x = 15;
	eng::s16 max_delta_y = 15;
	eng::u8 max_tiles_per_frame = 4;     // presupuesto de dibujo por frame
	bool scroll_x = true;                // scroll infinito horizontal (2 paginas)
	bool scroll_y = true;                // scroll infinito vertical (2 paginas)
};

/// Franja de tiles pendiente de dibujar (repartida entre frames).
struct TilePendingStrip {
	eng::s32 world_tile_x = 0;   // coordenada de mundo (tiles) de la franja
	eng::s32 world_tile_y = 0;
	eng::u16 length = 0;         // tiles de la franja
	eng::u16 slot = 0;           // pagina/slot del framebuffer destino
	bool is_column = false;
	bool active = false;
};

/// Estado mutable de un campo (lo mantiene el controlador).
struct TileFieldState {
	eng::s32 world_x = 0;        // posicion de mundo (pixeles), origen = TL
	eng::s32 world_y = 0;
	eng::s32 page_base_x = 0;    // multiplo de viewport_w: pagina en el borde
	eng::s32 page_base_y = 0;
	eng::u8 active_page_x = 0;   // 0/1: que pagina tiene el borde izquierdo
	eng::u8 active_page_y = 0;
	TilePendingStrip pending[4] {};
	eng::u8 pending_count = 0;
	bool initialized = false;
};

/// Vista hardware de un campo: lo unico que el compositor necesita.
struct FieldHardwareView {
	const eng::u8* bitplanes = nullptr;
	eng::u32 plane_stride = 0;
	eng::s16 scroll_x = 0;       // posicion de scroll (BPLxPT + BPLCON1)
	eng::s16 scroll_y = 0;
	eng::u8 plane_count = 0;
	eng::u8 first_hardware_plane = 0; // en DPF: 0 (PF1) o 1 (PF2)
	eng::u16 bpl1mod = 0;
};

/// Lienzo bitmap opcional (naves, HUD, BOBs): no conoce tiles.
struct BitmapFieldConfig {
	const eng::u8* bitmap = nullptr;
	eng::u16 width = 0;
	eng::u16 height = 0;
	eng::u8 planes = 0;
	bool scrolling = false;
};

/// Controlador de un campo de tiles.
///
/// No conoce DPF ni a otros campos. Gestiona el framebuffer (doble pagina por
/// eje infinito), la camara y el reparto de franjas. Los blits de tiles van por
/// el `FramePlan` (el backend decide como programar el Blitter).
///
/// El framebuffer se reserva en `begin` desde el MemorySystem. El mapa y el
/// tileset (patrones planares contiguos, indice u16) los proporciona la demo.
class TileFieldController {
public:
	TileFieldController() = default;

	/// Pinta el framebuffer completo (pagina visible + paginas offscreen) para
	/// un offset absoluto inicial y deja el estado listo.
	bool begin(
		eng::MemorySystem& memory,
		const TileFieldConfig& config,
		TileScrollOffset initial,
		eng::graphics::FramePlan& plan
	) {
		m_config = config;
		// Tamaño del framebuffer: 2x viewport en cada eje con scroll infinito.
		m_fb_w = config.viewport_w * (config.scroll_x ? 2u : 1u);
		m_fb_h = config.viewport_h * (config.scroll_y ? 2u : 1u);
		m_row_bytes = m_fb_w / 8u;
		m_plane_bytes = static_cast<eng::u32>(m_row_bytes) * m_fb_h;

		m_framebuffer = memory.chip.allocate(
			m_plane_bytes * config.tileset_planes, 16
		);
		if (!m_framebuffer.valid()) {
			return false;
		}

		// Estado inicial: el borde izquierdo en la pagina 0.
		m_state.world_x = initial.x;
		m_state.world_y = initial.y;
		m_state.page_base_x = (initial.x / static_cast<eng::s32>(config.viewport_w)) *
			static_cast<eng::s32>(config.viewport_w);
		m_state.page_base_y = (initial.y / static_cast<eng::s32>(config.viewport_h)) *
			static_cast<eng::s32>(config.viewport_h);
		m_state.active_page_x = 0;
		m_state.active_page_y = 0;
		m_state.pending_count = 0;

		// Estampa las 2 paginas (o 1 si no hay scroll) con el mundo inicial.
		stamp_all_pages(plan);
		m_state.initialized = true;
		return true;
	}

	/// Avanza el campo un frame. `delta` es el desplazamiento respecto al frame
	/// anterior (se clampa a max_delta_x/y y a 0 si scroll_x/y es false).
	/// Encola las franjas de tiles que la camara revelara y consume el
	/// presupuesto (max_tiles_per_frame) dibujando parte de las pendientes.
	bool update(const TileFieldConfig& config, TileScrollOffset delta, eng::graphics::FramePlan& plan) {
		if (!m_state.initialized) {
			return false;
		}
		m_config = config;

		// 1) Clamp del delta al contrato y a la posibilidad de scroll.
		eng::s16 dx = config.scroll_x ? clamp16(delta.x, config.max_delta_x) : 0;
		eng::s16 dy = config.scroll_y ? clamp16(delta.y, config.max_delta_y) : 0;
		if (config.scroll_x && !can_scroll_x(dx)) dx = 0;
		if (config.scroll_y && !can_scroll_y(dy)) dy = 0;

		// 2) Avanza la camara y detecta cruce de pagina.
		m_state.world_x += dx;
		m_state.world_y += dy;
		const eng::s32 half_w = static_cast<eng::s32>(config.viewport_w);
		const eng::s32 half_h = static_cast<eng::s32>(config.viewport_h);
		const eng::s32 mod_x = m_state.world_x - m_state.page_base_x;
		if (config.scroll_x && mod_x >= half_w) {
			// La camara entro en la pagina derecha: la izquierda queda vacante.
			m_state.page_base_x += half_w;
			m_state.active_page_x = static_cast<eng::u8>(m_state.active_page_x ^ 1u);
			enqueue_vacated_page_x();
		} else if (config.scroll_x && mod_x < 0) {
			m_state.page_base_x -= half_w;
			m_state.active_page_x = static_cast<eng::u8>(m_state.active_page_x ^ 1u);
			enqueue_vacated_page_x();
		}
		const eng::s32 mod_y = m_state.world_y - m_state.page_base_y;
		if (config.scroll_y && mod_y >= half_h) {
			m_state.page_base_y += half_h;
			m_state.active_page_y = static_cast<eng::u8>(m_state.active_page_y ^ 1u);
			enqueue_vacated_page_y();
		} else if (config.scroll_y && mod_y < 0) {
			m_state.page_base_y -= half_h;
			m_state.active_page_y = static_cast<eng::u8>(m_state.active_page_y ^ 1u);
			enqueue_vacated_page_y();
		}

		// 3) Consume el presupuesto: dibuja hasta max_tiles_per_frame.
		draw_pending(plan, config.max_tiles_per_frame);
		return true;
	}

	/// Devuelve la vista hardware (punteros + scroll) para el compositor.
	FieldHardwareView hardware_view(eng::u8 first_hardware_plane) const {
		FieldHardwareView v;
		v.bitplanes = static_cast<const eng::u8*>(m_framebuffer.data);
		v.plane_stride = m_plane_bytes;
		v.scroll_x = static_cast<eng::s16>(m_state.world_x & 15);
		v.scroll_y = static_cast<eng::s16>(m_state.world_y & 15);
		v.plane_count = m_config.tileset_planes;
		v.first_hardware_plane = first_hardware_plane;
		v.bpl1mod = 0;
		return v;
	}

	const eng::u8* bitplanes() const { return static_cast<const eng::u8*>(m_framebuffer.data); }
	const TileFieldState& state() const { return m_state; }

private:
	static eng::s16 clamp16(eng::s16 v, eng::s16 max) {
		if (v > max) return max;
		if (v < -max) return static_cast<eng::s16>(-max);
		return v;
	}

	bool can_scroll_x(eng::s16 dx) const {
		// Tope: si la pagina activa esta en el borde del mapa (sin wrap),
		// no permitir avanzar hacia el vacio.
		if (m_config.map.wrap_x != 0) return true;
		const eng::s32 page_cols = static_cast<eng::s32>(m_config.map.width) -
			m_state.page_base_x / static_cast<eng::s32>(m_config.tile_size);
		return dx < 0 || page_cols > static_cast<eng::s32>(m_config.viewport_w / m_config.tile_size);
	}

	bool can_scroll_y(eng::s16 dy) const {
		if (m_config.map.wrap_y != 0) return true;
		const eng::s32 page_rows = static_cast<eng::s32>(m_config.map.height) -
			m_state.page_base_y / static_cast<eng::s32>(m_config.tile_size);
		return dy < 0 || page_rows > static_cast<eng::s32>(m_config.viewport_h / m_config.tile_size);
	}

	// Cuando una pagina queda vacante (la camara la dejo atras), se encola su
	// redibujado completo: los tiles del mundo que esa zona mostrara cuando la
	// camara vuelva a ella (una pagina mas adelante).
	void enqueue_vacated_page_x() {
		// La pagina vacante se redibuja con las columnas que seran visibles
		// cuando la camara avance una pagina mas: page_base + 2*viewport.
		const eng::s32 page_px = static_cast<eng::s32>(m_config.viewport_w);
		const eng::s32 col = (m_state.page_base_x + page_px) / static_cast<eng::s32>(m_config.tile_size);
		for (eng::u8 i = 0; i < m_state.pending_count; ++i) {
			if (m_state.pending[i].active && m_state.pending[i].is_column &&
				m_state.pending[i].world_tile_x == col) {
				return;
			}
		}
		if (m_state.pending_count < 4) {
			m_state.pending[m_state.pending_count++] = {
				col,
				m_state.page_base_y / static_cast<eng::s32>(m_config.tile_size),
				static_cast<eng::u16>(m_fb_h / m_config.tile_size),
				static_cast<eng::u16>(m_state.active_page_x), // slot = pagina
				0,
				true,
			};
		}
	}

	void enqueue_vacated_page_y() {
		const eng::s32 page_px = static_cast<eng::s32>(m_config.viewport_h);
		const eng::s32 row = (m_state.page_base_y + page_px) / static_cast<eng::s32>(m_config.tile_size);
		for (eng::u8 i = 0; i < m_state.pending_count; ++i) {
			if (m_state.pending[i].active && !m_state.pending[i].is_column &&
				m_state.pending[i].world_tile_y == row) {
				return;
			}
		}
		if (m_state.pending_count < 4) {
			m_state.pending[m_state.pending_count++] = {
				m_state.page_base_x / static_cast<eng::s32>(m_config.tile_size),
				row,
				static_cast<eng::u16>(m_fb_w / m_config.tile_size),
				static_cast<eng::u16>(m_state.active_page_y),
				0,
				false,
			};
		}
	}

	// Dibuja hasta `budget` tiles de las franjas pendientes (reparto entre
	// frames). Por ahora estampa por CPU sobre el framebuffer (los blits via
	// FramePlan se anadiran al integrarlo con el backend de blits).
	void draw_pending(eng::graphics::FramePlan&, eng::u8 budget) {
		for (eng::u8 i = 0; i < m_state.pending_count && budget > 0; ++i) {
			TilePendingStrip& strip = m_state.pending[i];
			if (!strip.active) continue;
			const eng::s32 tile_step = strip.is_column ? 1 : 1;
			const eng::u16 tile_px = m_config.tile_size;
			// Dibuja un tile de la franja por iteracion hasta agotar el presupuesto.
			while (strip.length > 0 && budget > 0) {
				const eng::s32 wx = strip.world_tile_x;
				const eng::s32 wy = strip.world_tile_y;
				const eng::u16 tile = m_config.map.tile_at(wx, wy);
				stamp_tile_cpu(wx, wy, tile, strip.slot, strip.is_column);
				if (strip.is_column) ++strip.world_tile_y; else ++strip.world_tile_x;
				--strip.length;
				--budget;
			}
			if (strip.length == 0) {
				strip.active = false;
			}
		}
	}

	// Estampa un tile por CPU en el framebuffer. `slot` es la pagina (0/1) y
	// `is_column` indica si la franja es vertical (pagina X) u horizontal (Y).
	void stamp_tile_cpu(eng::s32 world_tile_x, eng::s32 world_tile_y, eng::u16 tile_index,
		eng::u16 slot, bool is_column) {
		eng::u8* base = static_cast<eng::u8*>(m_framebuffer.data);
		const eng::u16 tile_px = m_config.tile_size;
		// Pagina destino: si es columna, la pagina X activa (base * half_w);
		// la fila de mundo no cambia la pagina. Si es fila, pagina Y.
		const eng::u32 page_px_x = static_cast<eng::u32>(m_config.viewport_w);
		const eng::u32 page_px_y = static_cast<eng::u32>(m_config.viewport_h);
		eng::s32 fb_x = is_column
			? static_cast<eng::s32>(slot * page_px_x) + (world_tile_x - m_state.page_base_x / static_cast<eng::s32>(tile_px)) * tile_px
			: static_cast<eng::s32>(world_tile_x - m_state.page_base_x / static_cast<eng::s32>(tile_px)) * tile_px;
		eng::s32 fb_y = is_column
			? static_cast<eng::s32>(world_tile_y - m_state.page_base_y / static_cast<eng::s32>(tile_px)) * tile_px
			: static_cast<eng::s32>(slot * page_px_y) + (world_tile_y - m_state.page_base_y / static_cast<eng::s32>(tile_px)) * tile_px;
		if (fb_x < 0 || fb_y < 0) return;

		const eng::u16* src = m_config.tileset +
			static_cast<eng::u32>(tile_index & (m_config.tileset_count - 1u)) * (m_config.tileset_planes * tile_px);
		for (eng::u8 plane = 0; plane < m_config.tileset_planes; ++plane) {
			eng::u8* dst = base + static_cast<eng::u32>(plane) * m_plane_bytes;
			for (eng::u16 r = 0; r < tile_px; ++r) {
				const eng::u16 word = src[static_cast<eng::u32>(plane) * tile_px + r];
				// Un tile de 16 px = 1 word (2 bytes) por fila.
				eng::u32 off = static_cast<eng::u32>(fb_y + r) * m_row_bytes + static_cast<eng::u32>(fb_x / 8u);
				dst[off] = static_cast<eng::u8>(word >> 8);
				dst[off + 1u] = static_cast<eng::u8>(word & 0xffu);
			}
		}
	}

	// Estampa todas las paginas del mundo inicial (llamado solo en begin).
	void stamp_all_pages(eng::graphics::FramePlan&) {
		const eng::s32 tile_px = static_cast<eng::s32>(m_config.tile_size);
		const eng::u16 cols = static_cast<eng::u16>(m_fb_w / m_config.tile_size);
		const eng::u16 rows = static_cast<eng::u16>(m_fb_h / m_config.tile_size);
		const eng::s32 base_x = m_state.page_base_x / tile_px;
		const eng::s32 base_y = m_state.page_base_y / tile_px;
		for (eng::u16 ty = 0; ty < rows; ++ty) {
			for (eng::u16 tx = 0; tx < cols; ++tx) {
				const eng::s32 wx = base_x + tx;
				const eng::s32 wy = base_y + ty;
				const eng::u16 tile = m_config.map.tile_at(wx, wy);
				// Determina la pagina (0/1) por la posicion en el framebuffer.
				const eng::u16 page_x = tx >= static_cast<eng::u16>(m_config.viewport_w / m_config.tile_size) ? 1u : 0u;
				const eng::u16 page_y = ty >= static_cast<eng::u16>(m_config.viewport_h / m_config.tile_size) ? 1u : 0u;
				stamp_tile_cpu(wx, wy, tile, page_x, true);
				// Nota: stamp_tile_cpu con is_column=true usa la pagina X; la fila
				// se deriva del world_tile_y (pagina Y no aplica por columna).
			}
		}
	}

	TileFieldConfig m_config {};
	TileFieldState m_state {};
	eng::MemoryBlock m_framebuffer {};
	eng::u16 m_fb_w = 0;
	eng::u16 m_fb_h = 0;
	eng::u16 m_row_bytes = 0;
	eng::u32 m_plane_bytes = 0;
};

} // namespace eng::field
