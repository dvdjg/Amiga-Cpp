#pragma once

/// \file tile_scroll.hpp
/// Modelo retenido minimo para tilemaps con scroll.
///
/// Este archivo nace a partir del analisis de `demoscene-repo/effects/tiles16`,
/// pero no porta su codigo. Extrae la idea reusable:
///
/// - un mapa de tiles puede guardar dirty flags por buffer visible/oculto;
/// - el scroll se separa en parte gruesa de tile/word y parte fina de pixels;
/// - la logica de juego no debe saber si el Amiga usara `BPLxPT`, `BPLCON1`,
///   blits interleaved o cualquier otra tecnica;
/// - una fase posterior compilara este plan a `FramePlan` y `CopperScheduler`.
///
/// La clase es deliberadamente pequena y sin heap. El juego aporta memoria externa
/// para el mapa, normalmente desde un recurso UAF-R o desde una arena del engine.

#include <amg/core/types.hpp>

namespace amg::graphics::tilemap {

/// Celda de tile con flags embebidos.
///
/// Los bits altos guardan el indice de tile. Los bits bajos guardan dirty flags por
/// buffer. Esta tecnica permite que un doble buffer recuerde que cada pagina
/// necesita actualizar sus tiles ocultos sin forzar un refresco completo en cada
/// frame.
struct PackedTileCell {
	u16 value = 0;

	static constexpr u16 dirty_buffer_0 = 0x0001;
	static constexpr u16 dirty_buffer_1 = 0x0002;
	static constexpr u16 dirty_all = dirty_buffer_0 | dirty_buffer_1;
	static constexpr u16 tile_shift = 2;

	constexpr u16 tile_index() const {
		return static_cast<u16>(value >> tile_shift);
	}

	constexpr bool dirty_for(u8 buffer_index) const {
		const u16 flag = static_cast<u16>(1u << (buffer_index & 1u));
		return (value & flag) != 0;
	}

	constexpr void set_tile(u16 index) {
		value = static_cast<u16>((index << tile_shift) | dirty_all);
	}

	constexpr void mark_dirty() {
		value = static_cast<u16>(value | dirty_all);
	}

	constexpr void clear_dirty_for(u8 buffer_index) {
		const u16 flag = static_cast<u16>(1u << (buffer_index & 1u));
		value = static_cast<u16>(value & ~flag);
	}
};

/// Rectangulo de tiles visible o actualizado.
struct TileRect {
	u16 left = 0;
	u16 top = 0;
	u16 width = 0;
	u16 height = 0;

	constexpr bool valid() const {
		return width != 0 && height != 0;
	}
};

/// Posicion de scroll expresada en pixels logicos.
struct ScrollPosition {
	u16 x = 0;
	u16 y = 0;
};

/// Descomposicion del scroll para un driver Amiga.
///
/// `tile_x/tile_y` seleccionan la celda superior izquierda del mapa. `coarse_x`
/// es el desplazamiento alineado a word que puede materializarse cambiando punteros
/// de bitplane. `fine_x` es el desplazamiento 0..15 que en OCS/ECS normalmente
/// acabara en `BPLCON1`, pero esta cabecera no menciona ese registro.
struct ScrollDecomposition {
	u16 tile_x = 0;
	u16 tile_y = 0;
	u16 coarse_x_pixels = 0;
	u8 fine_x_pixels = 0;
	u8 fine_y_pixels = 0;
};

/// Resultado retenido que el render compiler usara para crear trabajos reales.
struct TileScrollFrame {
	TileRect visible_tiles {};
	ScrollDecomposition scroll {};
	u8 target_buffer = 0;
	u16 dirty_tiles = 0;
	bool overflow = false;
};

/// Vista mutable de un tilemap 16x16.
///
/// No posee memoria. Esto permite usar datos cocinados desde UAF-R, buffers de
/// streaming o mapas generados durante la carga sin imponer una politica de heap.
class TileMap16 {
public:
	static constexpr u8 tile_size = 16;

	constexpr TileMap16() = default;

	constexpr TileMap16(PackedTileCell* cells, u16 width, u16 height)
		: m_cells(cells), m_width(width), m_height(height) {}

	constexpr bool reset(PackedTileCell* cells, u16 width, u16 height) {
		m_cells = cells;
		m_width = width;
		m_height = height;
		return ok();
	}

	constexpr bool ok() const {
		return m_cells != nullptr && m_width != 0 && m_height != 0;
	}

	constexpr u16 width() const { return m_width; }
	constexpr u16 height() const { return m_height; }

	constexpr PackedTileCell& cell(u16 x, u16 y) {
		return m_cells[static_cast<u32>(y) * m_width + x];
	}

	constexpr const PackedTileCell& cell(u16 x, u16 y) const {
		return m_cells[static_cast<u32>(y) * m_width + x];
	}

	/// Marca una zona de tiles como pendiente para ambos buffers.
	///
	/// La funcion recorta el rectangulo al mapa. Si la zona queda fuera por completo
	/// devuelve `false`, lo cual permite a una demo detectar errores de camara/mapa.
	constexpr bool mark_dirty(TileRect rect) {
		if (!ok() || !rect.valid() || rect.left >= m_width || rect.top >= m_height) {
			return false;
		}

		const u16 right = min_u16(m_width, static_cast<u16>(rect.left + rect.width));
		const u16 bottom = min_u16(m_height, static_cast<u16>(rect.top + rect.height));
		for (u16 y = rect.top; y < bottom; ++y) {
			for (u16 x = rect.left; x < right; ++x) {
				cell(x, y).mark_dirty();
			}
		}
		return true;
	}

	/// Construye el estado logico de un frame de scroll.
	///
	/// `visible_tiles_x/y` suelen incluir una columna/fila extra oculta para permitir
	/// scroll fino. La funcion cuenta cuantos tiles visibles estan sucios para el
	/// buffer destino y limpia sus flags, simulando lo que hara despues el blitter al
	/// publicar esos tiles en la pagina oculta.
	constexpr TileScrollFrame prepare_frame(
		ScrollPosition position,
		u16 visible_tiles_x,
		u16 visible_tiles_y,
		u8 target_buffer
	) {
		TileScrollFrame frame {};
		frame.target_buffer = static_cast<u8>(target_buffer & 1u);
		frame.scroll = decompose(position);
		frame.visible_tiles = {
			frame.scroll.tile_x,
			frame.scroll.tile_y,
			visible_tiles_x,
			visible_tiles_y,
		};

		if (!ok()) {
			frame.overflow = true;
			return frame;
		}

		const u16 right = min_u16(m_width, static_cast<u16>(frame.visible_tiles.left + visible_tiles_x));
		const u16 bottom = min_u16(m_height, static_cast<u16>(frame.visible_tiles.top + visible_tiles_y));
		if (right == frame.visible_tiles.left || bottom == frame.visible_tiles.top) {
			frame.overflow = true;
			return frame;
		}

		for (u16 y = frame.visible_tiles.top; y < bottom; ++y) {
			for (u16 x = frame.visible_tiles.left; x < right; ++x) {
				PackedTileCell& c = cell(x, y);
				if (c.dirty_for(frame.target_buffer)) {
					++frame.dirty_tiles;
					c.clear_dirty_for(frame.target_buffer);
				}
			}
		}

		return frame;
	}

	static constexpr ScrollDecomposition decompose(ScrollPosition position) {
		const u16 tile_x = static_cast<u16>(position.x / tile_size);
		const u16 tile_y = static_cast<u16>(position.y / tile_size);
		const u8 fine_x = static_cast<u8>(position.x & 15u);
		const u8 fine_y = static_cast<u8>(position.y & 15u);
		const u16 coarse_x = static_cast<u16>(position.x & 0xfff0u);
		return {tile_x, tile_y, coarse_x, fine_x, fine_y};
	}

private:
	static constexpr u16 min_u16(u16 a, u16 b) {
		return a < b ? a : b;
	}

	PackedTileCell* m_cells = nullptr;
	u16 m_width = 0;
	u16 m_height = 0;
};

} // namespace amg::graphics::tilemap
