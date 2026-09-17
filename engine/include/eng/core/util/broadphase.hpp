#pragma once

/// \file broadphase.hpp
/// **Fase amplia de colisiones** (`eng::util`): rejilla espacial uniforme para no
/// comparar todos contra todos. Cada celda es una lista intrusiva de entradas (del
/// `Pool`), así que insertar es `O(1)` sin heap y consultar solo recorre las celdas que
/// caen en la caja de consulta.
///
/// Se reconstruye por frame (`clear()` + `insert()`), que es el patrón habitual cuando
/// las posiciones cambian a menudo. `CellSize` debe ser potencia de dos (índice por
/// desplazamiento) y las coordenadas, no negativas y dentro de `CellsX×CellsY`.
///
/// Uso:
///   eng::util::SpatialHash<16, 20, 15, 64> grid;
///   grid.clear();
///   grid.insert(id, x, y);
///   eng::u16 hits[8];
///   const eng::usize n = grid.query(box, {hits, 8});

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/bit.hpp>
#include <eng/core/util/collision.hpp>
#include <eng/core/util/intrusive_list.hpp>
#include <eng/core/util/pool.hpp>

namespace eng::util {

template <u16 CellSize, u16 CellsX, u16 CellsY, u16 MaxItems>
class SpatialHash {
	static_assert(CellSize > 0u && has_single_bit(CellSize),
		      "SpatialHash: CellSize potencia de dos");
	static_assert(CellsX > 0u && CellsY > 0u, "SpatialHash: celdas > 0");
	static_assert(MaxItems > 0u, "SpatialHash: MaxItems > 0");

public:
	static constexpr usize cell_count() noexcept {
		return static_cast<usize>(CellsX) * static_cast<usize>(CellsY);
	}

	/// Vacía la rejilla (reutiliza el pool; no libera memoria).
	constexpr void clear() noexcept {
		for (usize i = 0; i < cell_count(); ++i) {
			m_cells[i].clear();
		}
		m_pool.reset();
	}

	/// Inserta `(id, x, y)`. `false` si el punto está fuera de la rejilla o no hay
	/// hueco en el pool.
	constexpr bool insert(u16 id, s16 x, s16 y) noexcept {
		const int cell = cell_of(x, y);
		if (cell < 0) {
			return false;
		}
		const auto handle = m_pool.add();
		if (!handle.valid()) {
			return false;
		}
		Entry* e = m_pool.get(handle);
		e->id = id;
		e->x = x;
		e->y = y;
		m_cells[static_cast<usize>(cell)].push_front(e);
		return true;
	}

	[[nodiscard]] constexpr usize size() const noexcept { return m_pool.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_pool.empty(); }

	/// Recorre las entradas cuya posición cae en `box` (bordes exclusivos).
	template <class Fn>
	constexpr void for_each_in(const Aabb& box, Fn fn) const {
		if (box.max_x <= box.min_x || box.max_y <= box.min_y) {
			return;
		}
		const int cx0 = clamp_cell(box.min_x, CellsX);
		const int cx1 = clamp_cell(static_cast<s16>(box.max_x - 1), CellsX);
		const int cy0 = clamp_cell(box.min_y, CellsY);
		const int cy1 = clamp_cell(static_cast<s16>(box.max_y - 1), CellsY);
		for (int cy = cy0; cy <= cy1; ++cy) {
			for (int cx = cx0; cx <= cx1; ++cx) {
				const usize cell = static_cast<usize>(cy) * CellsX + static_cast<usize>(cx);
				for (const Entry* e = m_cells[cell].front(); e != nullptr; e = e->next) {
					if (point_in_aabb(box, Point2s {e->x, e->y})) {
						fn(e->id, e->x, e->y);
					}
				}
			}
		}
	}

	/// Escribe en `out` los ids que caen en `box` (hasta `out.size()`). Devuelve cuántos.
	constexpr usize query(const Aabb& box, Span<u16> out) const {
		usize n = 0;
		for_each_in(box, [&](u16 id, s16, s16) {
			if (n < out.size()) {
				out[n] = id;
			}
			++n;
		});
		return n;
	}

private:
	struct Entry : IntrusiveSLink<Entry> {
		u16 id = 0;
		s16 x = 0;
		s16 y = 0;
	};

	[[nodiscard]] static constexpr u16 log_cell() noexcept {
		return static_cast<u16>(bit_width(CellSize) - 1);
	}

	/// Índice de celda de `(x,y)`, o `-1` si está fuera.
	[[nodiscard]] constexpr int cell_of(s16 x, s16 y) const noexcept {
		if (x < 0 || y < 0) {
			return -1;
		}
		const u16 cx = static_cast<u16>(static_cast<u16>(x) >> log_cell());
		const u16 cy = static_cast<u16>(static_cast<u16>(y) >> log_cell());
		if (cx >= CellsX || cy >= CellsY) {
			return -1;
		}
		return static_cast<int>(static_cast<u32>(cy) * CellsX + cx);
	}

	[[nodiscard]] static constexpr int clamp_cell(s16 v, u16 limit) noexcept {
		int c = v < 0 ? 0 : static_cast<int>(static_cast<u16>(v) >> log_cell());
		if (c >= static_cast<int>(limit)) {
			c = static_cast<int>(limit) - 1;
		}
		return c;
	}

	Pool<Entry, MaxItems> m_pool {};
	IntrusiveSList<Entry> m_cells[cell_count()] {};
};

} // namespace eng::util
