// ============================================================================
// Test HOST-277: W0 — núcleo determinista de generación procedural de mundos.
// ============================================================================
//
// Verifica `eng::sim::gen::generate` (`eng/sim/gen/world_gen.hpp`): topología determinista de un
// mundo metroidvania a partir de una semilla. Misma semilla -> mismo grafo (byte a byte); semillas
// distintas -> grafos distintos; conectividad garantizada (BFS alcanza todas las salas); hay bucles
// (atajos); bioma y peligro dentro de rango. Sin heap en la generación (todo arrays de capacidad
// fija). Ver docs/guides/roadmap/ROADMAP_PROCEDURAL_WORLDS.md (W0).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/277_proc_world

#include <cstdio>

#include <eng/core/minifloat.hpp>
#include <eng/core/minifloat_math.hpp>
#include <eng/sim/gen/world_gen.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using Gen = eng::sim::gen::WorldGenOutput<64, 256>;

// Firma determinista barata del grafo: nº de nodos/aristas + recorrido de vecinos.
eng::u32 graph_fingerprint(const Gen& w) {
	eng::u32 h = static_cast<eng::u32>(w.graph.node_count()) * 2654435761u;
	h ^= static_cast<eng::u32>(w.graph.edge_count()) * 40503u;
	for (eng::u16 n = 0; n < w.graph.node_count(); ++n) {
		eng::u32 acc = 0;
		w.graph.for_each_neighbor(n, [&](eng::u16 nb, eng::u16 cost) {
			acc += static_cast<eng::u32>(nb) * 31u + cost;
		});
		h = eng::util::hash_u32(h ^ acc ^ n);
	}
	return h;
}

// ¿Es el grafo conexo desde `spawn`? (BFS por el grafo).
bool connected(const Gen& w, eng::u16 spawn) {
	const eng::u16 n = w.graph.node_count();
	bool seen[64] = {};
	eng::u16 queue[64] = {};
	eng::u16 head = 0, tail = 0;
	queue[tail++] = spawn;
	seen[spawn] = true;
	eng::u16 count = 1;
	while (head < tail) {
		const eng::u16 cur = queue[head++];
		w.graph.for_each_neighbor(cur, [&](eng::u16 nb, eng::u16) {
			if (!seen[nb]) {
				seen[nb] = true;
				queue[tail++] = nb;
				++count;
			}
		});
	}
	return count == n;
}

// ¿Hay algún ciclo? (aristas > nodos-1 en un conexo => hay bucle).
bool has_cycles(const Gen& w) {
	return w.graph.edge_count() > 2u * (w.graph.node_count() - 1u); // bidireccional: 2 por arista
}

} // namespace

int main() {
	const eng::sim::gen::WorldGenParams p {32u, 8u, 8u, 40u, 0u};

	// 1) Determinismo: misma semilla -> misma firma (varias veces).
	Gen a {};
	Gen b {};
	check(eng::sim::gen::generate(a, 0xACE123456789ull, p), "generate: cupo en cotas");
	check(eng::sim::gen::generate(b, 0xACE123456789ull, p), "generate: cupo (2)");
	check(a.graph.node_count() == 32u, "32 salas");
	// W1: bioma por ruido `fbm2`/`worley2` (escalar del llamador; host -> MiniFloat16).
	eng::sim::gen::assign_biomes_fbm<64u, 256u, eng::math::MiniFloat16>(a, 0xACE123456789ull, a.cell_of_room,
								     32u, p.grid_w, 3u, 3u);
	eng::sim::gen::assign_biomes_fbm<64u, 256u, eng::math::MiniFloat16>(b, 0xACE123456789ull, b.cell_of_room,
								     32u, p.grid_w, 3u, 3u);
	check(graph_fingerprint(a) == graph_fingerprint(b), "misma semilla -> mismo grafo");

	// 2) Semillas distintas -> grafos distintos (probabilístico, con 2 semillas).
	Gen c {};
	check(eng::sim::gen::generate(c, 0x1234ull, p), "generate: otra semilla");
	eng::sim::gen::assign_biomes_fbm<64u, 256u, eng::math::MiniFloat16>(c, 0x1234ull, c.cell_of_room, 32u,
								     p.grid_w, 3u, 3u);
	check(graph_fingerprint(a) != graph_fingerprint(c), "semillas distintas -> grafos distintos");

	// 3) Conectividad y bucles.
	check(connected(a, p.spawn), "grafo conexo desde el spawn");
	check(has_cycles(a), "hay bucles (atajos)");

	// 4) Bioma y peligro en rango; spawn con peligro 0.
	bool biomes_ok = true, danger_ok = true;
	for (eng::u16 i = 0; i < a.graph.node_count(); ++i) {
		if (static_cast<eng::u8>(a.biome[i]) >= static_cast<eng::u8>(eng::sim::BiomeKind::Count)) {
			biomes_ok = false;
		}
		if (a.danger[i] > 100u) {
			danger_ok = false;
		}
	}
	check(biomes_ok, "bioma valido por sala");
	check(danger_ok, "peligro 0..100");
	check(a.danger[p.spawn] == 0u, "spawn con peligro 0");

	// 4b) W1: el bioma es **espacialmente coherente** (no franjas por índice). Se cuenta cuántas
	//     salas comparten bioma con algún vecino del grafo: en un mapa por ruido la mayoría sí
	//     (contigüidad); en uno por `índice/total` (el documento original) sería casi ninguna.
	{
		eng::u16 same_as_neighbor = 0u;
		for (eng::u16 i = 0u; i < a.graph.node_count(); ++i) {
			bool shares = false;
			a.graph.for_each_neighbor(i, [&](eng::u16 nb, eng::u16) {
				if (a.biome[nb] == a.biome[i]) {
					shares = true;
				}
			});
			if (shares) {
				++same_as_neighbor;
			}
		}
		check(same_as_neighbor * 2u >= a.graph.node_count(),
		      "W1: biomas contiguos (>= 50% salas comparten bioma con un vecino)");
	}

	// 5) Semilla por sala distinta (para W6) y no cero degenerado.
	check(a.node_seed[0] != a.node_seed[1], "semilla por sala distinta");

	// 6) Rechazo fuera de cotas (robustez).
	Gen small {};
	const eng::sim::gen::WorldGenParams too_many {200u, 8u, 8u, 20u, 0u};
	check(!eng::sim::gen::generate(small, 1ull, too_many), "rechaza rooms > MaxRooms");

	// 7) Gating por construcción (W3) + solvencia (W4): tras colocar llaves/puertas, TODAS las
	//    salas son alcanzables desde el spawn (gating por construcción -> soluble siempre), y hay
	//    puertas con llave colocadas.
	{
		eng::sim::gen::place_gates(a, 0xACE123456789ull, p.spawn, 3u);

		eng::u8 gates = 0u, keys = 0u;
		for (eng::u16 i = 0u; i < a.graph.node_count(); ++i) {
			if (a.gate_obj[i] != eng::sim::gen::kNoItem) {
				++gates;
			}
			if (a.item_of[i] != eng::sim::gen::kNoItem) {
				++keys;
			}
		}
		check(gates > 0u, "W3: hay puertas con llave");
		check(keys >= gates, "W3: hay al menos tantas llaves como puertas");

		bool all_solvable = true;
		for (eng::u16 t = 0u; t < a.graph.node_count(); ++t) {
			if (!eng::sim::gen::is_solvable(a, p.spawn, t)) {
				all_solvable = false;
			}
		}
		check(all_solvable, "W3/W4: todas las salas solubles con gating por construccion");

		// Determinismo del gating.
		Gen a2 {};
		eng::sim::gen::generate(a2, 0xACE123456789ull, p);
		eng::sim::gen::place_gates(a2, 0xACE123456789ull, p.spawn, 3u);
		bool gate_same = true;
		for (eng::u16 i = 0u; i < a.graph.node_count(); ++i) {
			if (a.gate_obj[i] != a2.gate_obj[i] || a.item_of[i] != a2.item_of[i]) {
				gate_same = false;
			}
		}
		check(gate_same, "W3: gating determinista");

		// Nodo aislado (sin aristas): no alcanzable (red de seguridad de W4).
		Gen iso {};
		iso.graph.add_node();
		iso.graph.add_node();
		check(!eng::sim::gen::is_solvable(iso, 0u, 1u), "W4: nodo aislado insoluble");
	}

	// 8) W2: topología fina — el grafo cabe en las cotas (pool), y el grado por sala es razonable
	//    (no hay un "hub" que desborde el array de vecinos de una sala en runtime).
	{
		check(a.graph.node_count() <= 64u && a.graph.edge_count() <= 256u, "W2: grafo dentro de cotas");
		eng::u16 max_degree = 0u;
		for (eng::u16 i = 0u; i < a.graph.node_count(); ++i) {
			eng::u16 deg = 0u;
			a.graph.for_each_neighbor(i, [&](eng::u16, eng::u16) { ++deg; });
			if (deg > max_degree) {
				max_degree = deg;
			}
		}
		check(max_degree <= 12u, "W2: grado maximo por sala <= 12 (cabe en el pool de vecinos)");
		// Tipos de tránsito disponibles (PathKind) — el gating usa llaves; los tipos existen.
		check(static_cast<eng::u8>(eng::sim::gen::PathKind::KeyDoor) == 1u &&
			      static_cast<eng::u8>(eng::sim::gen::PathKind::OneWay) == 3u,
		      "W2: PathKind (Open/KeyDoor/AbilityWall/OneWay)");
	}

	if (failures == 0) {
		std::printf("OK: W0 (generacion procedural determinista de topologia) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
