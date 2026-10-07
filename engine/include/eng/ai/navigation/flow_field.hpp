#pragma once

/// \file flow_field.hpp
/// **Campo de flujo** (`eng::ai::navigation`): un Dijkstra multi-fuente desde los
/// objetivos rellena, para cada celda de una rejilla `W×H`, el coste hasta el objetivo
/// más cercano y la **dirección** hacia el vecino que lo reduce. Cualquier número de
/// agentes sigue el campo con una lectura por celda (sin recalcular camino).
///
/// Pensado para muchos enemigos hacia un mismo destino (base, jugador, punto de
/// reunión): se calcula una vez por `init`/fondo y todos los agentes lo consultan.
/// Reutiliza la misma malla de 4 vecinos que `eng::util::pathfinding`, pero con coste de
/// terreno y multi-fuente. El **tipo del índice de celda** (`Index`, por defecto `u16`)
/// y el **tipo del coste** (`Cost`, por defecto `u16`) son parámetros de plantilla.
///
/// - `compute_flow_field<W,H>`: `terrain_cost(idx) -> Cost` es el coste de **entrar** en
///   la celda (>= 1); una celda con coste `~Cost{0}` (el máximo del tipo) se considera
///   **bloqueada**. Escribe `integration` (`Cost`, máximo = inalcanzable) y `direction`
///   (`u8` = `FlowDir`).
/// - `flow_next<W>`: del campo, da la siguiente celda desde `index`.
///
/// Coste: un `PriorityQueue` de capacidad `W*H` vive inline en `compute_flow_field`; una
/// rejilla grande debe ir en `init`/fondo (o reducirse), no en el camino por frame.
///
/// Verificación: HOST-114.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/bit.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/core/util/priority_queue.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::ai {

/// Dirección de movimiento en el campo (4 vecinos).
enum class FlowDir : eng::u8 { None = 0, North, East, South, West };

namespace detail {

/// Nodo del heap: celda y coste acumulado hasta el objetivo.
template <class Index, class Cost>
struct FlowNode {
	Index idx;
	Cost cost;
};

/// Comparador del heap: menor coste primero; desempata por índice (determinista).
template <class Index, class Cost>
struct FlowCmp {
	[[nodiscard]] constexpr bool operator()(const FlowNode<Index, Cost>& a,
						const FlowNode<Index, Cost>& b) const noexcept {
		if (a.cost != b.cost) {
			return a.cost > b.cost; // min-heap por coste
		}
		return a.idx > b.idx; // desempate determinista
	}
};

/// `log2(W)` para W potencia de dos.
consteval int flow_log2(u16 w) {
	int e = 0;
	while (w > 1u) {
		w = static_cast<u16>(w >> 1u);
		++e;
	}
	return e;
}

} // namespace detail

/// Calcula el campo de flujo hacia `goals`. `terrain_cost(idx) -> Cost` es el coste de
/// entrar en la celda (`~Cost{0}` = bloqueada). Devuelve `false` si los buffers no caben
/// o no hay objetivos.
template <eng::u16 W, eng::u16 H, class Index = eng::u16, class Cost = eng::u16,
	  class TerrainCost>
constexpr bool compute_flow_field(eng::Span<const eng::util::type_identity_t<Index>> goals,
				  TerrainCost terrain_cost,
				  eng::Span<eng::util::type_identity_t<Cost>> integration,
				  eng::Span<eng::u8> direction) noexcept {
	static_assert(eng::util::has_single_bit(W), "compute_flow_field: W potencia de dos");
	constexpr eng::usize N = static_cast<eng::usize>(W) * H;
	static_assert(N <= eng::util::signed_max<Index>,
		      "compute_flow_field: W*H debe caber en Index");
	static_assert(sizeof(Cost) <= 2u, "compute_flow_field: Cost de 16 bits o menos (sat_add)");
	constexpr int kLog = detail::flow_log2(W);
	constexpr Cost kBlocked = ~Cost {0}; // bloqueada = inalcanzable = máximo del tipo
	if (integration.size() < N || direction.size() < N || goals.empty()) {
		return false;
	}
	for (eng::usize i = 0; i < N; ++i) {
		integration[i] = kBlocked;
		direction[i] = static_cast<eng::u8>(FlowDir::None);
	}

	eng::util::PriorityQueue<detail::FlowNode<Index, Cost>, N,
				 detail::FlowCmp<Index, Cost>> open;
	for (eng::usize g = 0; g < goals.size(); ++g) {
		const Index idx = goals[g];
		if (idx >= N) {
			continue;
		}
		integration[idx] = 0u;
		open.push(detail::FlowNode<Index, Cost> {idx, 0u});
	}

	while (!open.empty()) {
		const detail::FlowNode<Index, Cost> cur = open.top();
		open.pop();
		if (cur.cost > integration[cur.idx]) {
			continue; // entrada obsoleta
		}
		const Index x = static_cast<Index>(cur.idx & (W - 1u));
		const Index y = static_cast<Index>(cur.idx >> kLog);

		// `back` es la direccion desde el vecino hacia `cur` (un paso hacia el objetivo).
		auto relax = [&](Index nb, FlowDir back) {
			const Cost step = terrain_cost(nb);
			if (step == kBlocked) {
				return; // bloqueada
			}
			const Cost nc = eng::util::sat_add(cur.cost, step);
			if (nc < integration[nb]) {
				integration[nb] = nc;
				direction[nb] = static_cast<eng::u8>(back);
				open.push(detail::FlowNode<Index, Cost> {nb, nc});
			}
		};
		if (y > 0u) {
			relax(static_cast<Index>(cur.idx - W), FlowDir::South);
		}
		if (x + 1u < W) {
			relax(static_cast<Index>(cur.idx + 1u), FlowDir::West);
		}
		if (y + 1u < H) {
			relax(static_cast<Index>(cur.idx + W), FlowDir::North);
		}
		if (x > 0u) {
			relax(static_cast<Index>(cur.idx - 1u), FlowDir::East);
		}
	}
	return true;
}

/// Siguiente celda desde `index` según el campo; `false` si es objetivo o está fuera.
template <eng::u16 W, class Index = eng::u16>
[[nodiscard]] constexpr bool flow_next(eng::Span<const eng::u8> direction,
				       eng::util::type_identity_t<Index> index,
				       eng::util::type_identity_t<Index>& next) noexcept {
	if (index >= direction.size()) {
		return false;
	}
	switch (static_cast<FlowDir>(direction[index])) {
	case FlowDir::North:
		if (index < W) {
			return false;
		}
		next = static_cast<Index>(index - W);
		return true;
	case FlowDir::East:
		if (static_cast<eng::usize>(index) + 1u >= direction.size()) {
			return false;
		}
		next = static_cast<Index>(index + 1u);
		return true;
	case FlowDir::South:
		if (static_cast<eng::usize>(index) + W >= direction.size()) {
			return false;
		}
		next = static_cast<Index>(index + W);
		return true;
	case FlowDir::West:
		if (index == 0u) {
			return false;
		}
		next = static_cast<Index>(index - 1u);
		return true;
	case FlowDir::None:
		break;
	}
	next = index;
	return false;
}

} // namespace eng::ai
