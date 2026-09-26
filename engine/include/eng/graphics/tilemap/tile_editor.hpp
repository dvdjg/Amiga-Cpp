#pragma once

/// \file tile_editor.hpp
/// **Editor de tiles de juego** (`eng::graphics::tilemap::TileEditor`): vista mutable sobre una
/// rejilla de `PackedTileCell` (índice de tile + dirty por buffer). Da al juego/consumidor las
/// operaciones de alto nivel de una capa de tiles —`set_tile`, `tile`, `mark_all_dirty`,
/// `dirty_rect` (caja envolvente de celdas sucias), `flush`— sin tocar framebuffers ni Blitter.
/// El redibujado (jobs de `FramePlan`/Copper) lo hace el driver de la capa al consumir el
/// `dirty_rect` del buffer que toca.
///
/// ```cpp
/// eng::graphics::tilemap::TileEditor ed {cells, tiles_x, tiles_y};
/// ed.set_tile(3, 2, /*index=*/17);        // marca la celda sucia en ambos buffers
/// eng::graphics::tilemap::TileRect r = ed.dirty_rect(back);
/// // ... el driver blittea r ...
/// ed.flush(back);                         // limpia el dirty de ese buffer
/// ```

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/tilemap/tile_scroll.hpp>

namespace eng::graphics::tilemap {

/// Vista mutable de alto nivel sobre una rejilla de `PackedTileCell`.
class TileEditor {
public:
	constexpr TileEditor(eng::Span<PackedTileCell> cells, u16 width, u16 height) noexcept
		: m_cells(cells), m_width(width), m_height(height) {}

	/// `true` si la rejilla cabe en el `Span` de celdas.
	[[nodiscard]] constexpr bool valid() const noexcept {
		return m_width != 0u && m_height != 0u &&
		       m_cells.size() >= static_cast<eng::usize>(m_width) * m_height;
	}

	/// Fija el tile de `(tx, ty)` (índice) y lo marca sucio en ambos buffers. `false` si la
	/// coordenada queda fuera (la capa decide wrap/clamp antes).
	constexpr bool set_tile(u16 tx, u16 ty, u16 index) noexcept {
		if (!contains(tx, ty)) {
			return false;
		}
		m_cells[idx(tx, ty)].set_tile(index);
		return true;
	}
	/// Índice de tile de `(tx, ty)` (0 si fuera).
	[[nodiscard]] constexpr u16 tile(u16 tx, u16 ty) const noexcept {
		return contains(tx, ty) ? m_cells[idx(tx, ty)].tile_index() : 0u;
	}
	/// Marca sucia toda la rejilla (p. ej. al cambiar el tileset o la paleta).
	constexpr void mark_all_dirty() noexcept {
		const eng::usize n = static_cast<eng::usize>(m_width) * m_height;
		for (eng::usize i = 0; i < n && i < m_cells.size(); ++i) {
			m_cells[i].mark_dirty();
		}
	}

	/// **Caja envolvente** de las celdas sucias del `buffer` (vacía si no hay ninguna). Es el
	/// rectángulo mínimo a redibujar (`flush`).
	[[nodiscard]] constexpr TileRect dirty_rect(u8 buffer) const noexcept {
		s32 l = m_width, t = m_height, r = -1, b = -1;
		for (u16 y = 0; y < m_height; ++y) {
			for (u16 x = 0; x < m_width; ++x) {
				if (idx(x, y) < m_cells.size() && m_cells[idx(x, y)].dirty_for(buffer)) {
					if (x < l) l = x;
					if (x > r) r = x;
					if (y < t) t = y;
					if (y > b) b = y;
				}
			}
		}
		if (r < 0) {
			return TileRect {};
		}
		return TileRect {static_cast<u16>(l), static_cast<u16>(t),
				 static_cast<u16>(r - l + 1), static_cast<u16>(b - t + 1)};
	}

	/// Limpia el dirty del `buffer` para toda la rejilla (tras blitear `dirty_rect`).
	constexpr void flush(u8 buffer) noexcept {
		const eng::usize n = static_cast<eng::usize>(m_width) * m_height;
		for (eng::usize i = 0; i < n && i < m_cells.size(); ++i) {
			m_cells[i].clear_dirty_for(buffer);
		}
	}

	[[nodiscard]] constexpr u16 width() const noexcept { return m_width; }
	[[nodiscard]] constexpr u16 height() const noexcept { return m_height; }

private:
	/// `true` si `(tx, ty)` está dentro de la rejilla.
	[[nodiscard]] constexpr bool contains(u16 tx, u16 ty) const noexcept {
		return tx < m_width && ty < m_height;
	}
	/// Índice lineal de la celda `(tx, ty)`.
	[[nodiscard]] constexpr eng::usize idx(u16 tx, u16 ty) const noexcept {
		return static_cast<eng::usize>(ty) * m_width + tx;
	}

	eng::Span<PackedTileCell> m_cells {};
	u16 m_width = 0u;
	u16 m_height = 0u;
};

} // namespace eng::graphics::tilemap
