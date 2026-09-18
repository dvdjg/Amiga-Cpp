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
/// terreno y multi-fuente.
///
/// - `compute_flow_field<W,H>`: `terrain_cost(idx)` es el coste de **entrar** en la celda
///   (>= 1); una celda con coste `0xffff` se considera **bloqueada**. Escribe
///   `integration` (`u16`, `0xffff` = inalcanzable) y `direction` (`u8` = `FlowDir`).
/// - `flow_next<W>`: del campo, da la siguiente celda desde `index`.
///
/// Coste: un `PriorityQueue` de capacidad `W*H` vive inline en `compute_flow_field`; una
/// rejilla grande debe ir en `init`/fondo (o reducirse), no en el camino por frame.
///
/// Verificación: HOST-114.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/bit.hpp>
#include <eng/core/util/priority_queue.hpp>

namespace eng::ai {

/// Dirección de movimiento en el campo (4 vecinos).
enum class FlowDir : eng::u8 { None = 0, North, East, South, West };

namespace detail {

struct FlowNode {
	eng::u16 idx;
	eng::u16 cost;
};

struct FlowCmp {
	[[nodiscard]] constexpr bool operator()(const FlowNode& a,
						const FlowNode& b) const noexcept {
		if (a.cost != b.cost) {
			return a.cost > b.cost; // min-heap por coste
		}
		return a.idx > b.idx; // desempate determinista
	}
};

consteval int flow_log2(u16 w) {
	int e = 0;
	while (w > 1u) {
		w = static_cast<u16>(w >> 1u);
		++e;
	}
	return e;
}

} // namespace detail

/// Calcula el campo de flujo hacia `goals`. `terrain_cost(idx) -> u16` es el coste de
/// entrar en la celda (`0xffff` = bloqueada). Devuelve `false` si los buffers no caben o
/// no hay objetivos.
template <eng::u16 W, eng::u16 H, class TerrainCost>
constexpr bool compute_flow_field(eng::Span<const eng::u16> goals, TerrainCost terrain_cost,
				  eng::Span<eng::u16> integration,
				  eng::Span<eng::u8> direction) noexcept {
	static_assert(eng::util::has_single_bit(W), "compute_flow_field: W potencia de dos");
	constexpr eng::usize N = static_cast<eng::usize>(W) * H;
	static_assert(N <= 65535u, "compute_flow_field: W*H debe caber en u16");
	constexpr int kLog = detail::flow_log2(W);
	if (integration.size() < N || direction.size() < N || goals.empty()) {
		return false;
	}
	for (eng::usize i = 0; i < N; ++i) {
		integration[i] = 0xffffu;
		direction[i] = static_cast<eng::u8>(FlowDir::None);
	}

	eng::util::PriorityQueue<detail::FlowNode, N, detail::FlowCmp> open;
	for (eng::usize g = 0; g < goals.size(); ++g) {
		const eng::u16 idx = goals[g];
		if (idx >= N) {
			continue;
		}
		integration[idx] = 0u;
		open.push(detail::FlowNode {idx, 0u});
	}

	while (!open.empty()) {
		const detail::FlowNode cur = open.top();
		open.pop();
		if (cur.cost > integration[cur.idx]) {
			continue; // entrada obsoleta
		}
		const eng::u16 x = static_cast<eng::u16>(cur.idx & (W - 1u));
		const eng::u16 y = static_cast<eng::u16>(cur.idx >> kLog);

		// `back` es la direccion desde el vecino hacia `cur` (un paso hacia el objetivo).
		auto relax = [&](eng::u16 nb, FlowDir back) {
			const eng::u16 step = terrain_cost(nb);
			if (step == 0xffffu) {
				return; // bloqueada
			}
			const eng::u32 nc = static_cast<eng::u32>(cur.cost) + step;
			if (nc < integration[nb]) {
				integration[nb] = static_cast<eng::u16>(nc > 0xffffu ? 0xffffu : nc);
				direction[nb] = static_cast<eng::u8>(back);
				open.push(detail::FlowNode {nb, integration[nb]});
			}
		};
		if (y > 0u) {
			relax(static_cast<eng::u16>(cur.idx - W), FlowDir::South);
		}
		if (x + 1u < W) {
			relax(static_cast<eng::u16>(cur.idx + 1u), FlowDir::West);
		}
		if (y + 1u < H) {
			relax(static_cast<eng::u16>(cur.idx + W), FlowDir::North);
		}
		if (x > 0u) {
			relax(static_cast<eng::u16>(cur.idx - 1u), FlowDir::East);
		}
	}
	return true;
}

/// Siguiente celda desde `index` según el campo; `false` si es objetivo o está fuera.
template <eng::u16 W>
[[nodiscard]] constexpr bool flow_next(eng::Span<const eng::u8> direction, eng::u16 index,
				       eng::u16& next) noexcept {
	if (index >= direction.size()) {
		return false;
	}
	switch (static_cast<FlowDir>(direction[index])) {
	case FlowDir::North:
		if (index < W) {
			return false;
		}
		next = static_cast<eng::u16>(index - W);
		return true;
	case FlowDir::East:
		if (static_cast<eng::usize>(index) + 1u >= direction.size()) {
			return false;
		}
		next = static_cast<eng::u16>(index + 1u);
		return true;
	case FlowDir::South:
		if (static_cast<eng::usize>(index) + W >= direction.size()) {
			return false;
		}
		next = static_cast<eng::u16>(index + W);
		return true;
	case FlowDir::West:
		if (index == 0u) {
			return false;
		}
		next = static_cast<eng::u16>(index - 1u);
		return true;
	case FlowDir::None:
		break;
	}
	next = index;
	return false;
}

} // namespace eng::ai
