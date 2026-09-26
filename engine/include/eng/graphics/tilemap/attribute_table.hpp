#pragma once

/// \file attribute_table.hpp
/// **Tabla de atributos** (`eng::graphics::tilemap::AttributeTable`): rejilla mutable de paleta
/// por bloque (estilo *attribute table* de la NES, una entrada cada 16×16 px = 2×2 tiles). Da al
/// juego/consumidor el `set_attribute`/`get` de una capa de tiles sin tocar el framebuffer; el
/// driver de la capa la traduce a parches de paleta/Copper al redibujar.
///
/// Es la tabla **paralela** al `TileEditor`: cambia la paleta de un bloque, no el tile.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::graphics::tilemap {

/// Vista mutable sobre una rejilla de atributos `width`×`height` (valores `u8`).
class AttributeTable {
public:
	constexpr AttributeTable(eng::Span<u8> cells, u16 width, u16 height) noexcept
		: m_cells(cells), m_width(width), m_height(height) {}

	/// `true` si la rejilla cabe en el `Span`.
	[[nodiscard]] constexpr bool valid() const noexcept {
		return m_width != 0u && m_height != 0u &&
		       m_cells.size() >= static_cast<eng::usize>(m_width) * m_height;
	}

	/// Fija el atributo (paleta) del bloque `(ax, ay)`. `false` si queda fuera.
	constexpr bool set(u16 ax, u16 ay, u8 value) noexcept {
		if (!contains(ax, ay)) {
			return false;
		}
		m_cells[idx(ax, ay)] = value;
		return true;
	}
	/// Atributo del bloque `(ax, ay)` (0 si queda fuera).
	[[nodiscard]] constexpr u8 get(u16 ax, u16 ay) const noexcept {
		return contains(ax, ay) ? m_cells[idx(ax, ay)] : 0u;
	}
	/// Rellena toda la tabla con `value`.
	constexpr void fill(u8 value) noexcept {
		const eng::usize n = static_cast<eng::usize>(m_width) * m_height;
		for (eng::usize i = 0; i < n && i < m_cells.size(); ++i) {
			m_cells[i] = value;
		}
	}

	[[nodiscard]] constexpr u16 width() const noexcept { return m_width; }
	[[nodiscard]] constexpr u16 height() const noexcept { return m_height; }

private:
	/// `true` si `(ax, ay)` está dentro de la rejilla.
	[[nodiscard]] constexpr bool contains(u16 ax, u16 ay) const noexcept {
		return ax < m_width && ay < m_height;
	}
	/// Índice lineal de `(ax, ay)`.
	[[nodiscard]] constexpr eng::usize idx(u16 ax, u16 ay) const noexcept {
		return static_cast<eng::usize>(ay) * m_width + ax;
	}

	eng::Span<u8> m_cells {};
	u16 m_width = 0u;
	u16 m_height = 0u;
};

} // namespace eng::graphics::tilemap
