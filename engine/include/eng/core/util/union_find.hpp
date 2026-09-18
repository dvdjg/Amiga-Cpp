#pragma once

/// \file union_find.hpp
/// `eng::util::UnionFind<MaxElements>`: **conjuntos disjuntos** (union-find / DSU) con
/// compresión de caminos y unión por tamaño. `find`/`unite` son casi O(1) amortizado.
///
/// Sirve para particionar el mundo en **componentes conexas**: regiones de un tilemap,
/// *flood fill*, detección de islas, generación de laberintos (Kruskal) o agrupación de
/// píxeles/paleta. Sin heap y determinista: dos arrays de `u16` (padre y tamaño).
///
/// Los elementos son índices `u16` en `[0, MaxElements)`; al construir, cada uno forma
/// su propio conjunto. `unite` devuelve `false` si ya estaban en el mismo.
///
/// Uso:
///   eng::util::UnionFind<256> uf;         // 256 conjuntos de 1 elemento
///   uf.unite(a, b);
///   if (uf.connected(a, b)) { ... }
///   const eng::u16 n = uf.component_size(a);
///
/// Verificación: HOST-119.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::util {

template <eng::u16 MaxElements>
class UnionFind {
	static_assert(MaxElements > 0u, "UnionFind: MaxElements debe ser mayor que 0");

public:
	/// Deja cada elemento en su propio conjunto.
	constexpr UnionFind() noexcept { reset(); }

	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return MaxElements; }
	[[nodiscard]] constexpr eng::u16 components() const noexcept { return m_components; }

	constexpr void reset() noexcept {
		for (eng::u16 i = 0u; i < MaxElements; ++i) {
			m_parent[i] = i;
			m_size[i] = 1u;
		}
		m_components = MaxElements;
	}

	/// Raíz del conjunto de `i` (con compresión de caminos).
	[[nodiscard]] constexpr eng::u16 find(eng::u16 i) noexcept {
		check(i);
		eng::u16 root = i;
		while (m_parent[root] != root) {
			root = m_parent[root];
		}
		while (m_parent[i] != root) {
			const eng::u16 next = m_parent[i];
			m_parent[i] = root;
			i = next;
		}
		return root;
	}

	/// Versión `const` (recorre sin comprimir).
	[[nodiscard]] constexpr eng::u16 find(eng::u16 i) const noexcept {
		check(i);
		eng::u16 root = i;
		while (m_parent[root] != root) {
			root = m_parent[root];
		}
		return root;
	}

	/// Une los conjuntos de `a` y `b`; `false` si ya estaban unidos.
	constexpr bool unite(eng::u16 a, eng::u16 b) noexcept {
		eng::u16 ra = find(a);
		eng::u16 rb = find(b);
		if (ra == rb) {
			return false;
		}
		if (m_size[ra] < m_size[rb]) {
			const eng::u16 tmp = ra;
			ra = rb;
			rb = tmp;
		}
		m_parent[rb] = ra;
		m_size[ra] = static_cast<eng::u16>(m_size[ra] + m_size[rb]);
		--m_components;
		return true;
	}

	[[nodiscard]] constexpr bool connected(eng::u16 a, eng::u16 b) noexcept {
		return find(a) == find(b);
	}

	/// Tamaño del conjunto al que pertenece `i`.
	[[nodiscard]] constexpr eng::u16 component_size(eng::u16 i) noexcept {
		return m_size[find(i)];
	}

private:
	static constexpr void check(eng::u16 i) noexcept {
		if (i >= MaxElements) {
			eng::detail::span_out_of_bounds();
		}
	}

	eng::u16 m_parent[MaxElements] {};
	eng::u16 m_size[MaxElements] {};
	eng::u16 m_components = MaxElements;
};

} // namespace eng::util
