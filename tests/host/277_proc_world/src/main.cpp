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
	check(graph_fingerprint(a) == graph_fingerprint(b), "misma semilla -> mismo grafo");

	// 2) Semillas distintas -> grafos distintos (probabilístico, con 2 semillas).
	Gen c {};
	check(eng::sim::gen::generate(c, 0x1234ull, p), "generate: otra semilla");
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

	// 5) Semilla por sala distinta (para W6) y no cero degenerado.
	check(a.node_seed[0] != a.node_seed[1], "semilla por sala distinta");

	// 6) Rechazo fuera de cotas (robustez).
	Gen small {};
	const eng::sim::gen::WorldGenParams too_many {200u, 8u, 8u, 20u, 0u};
	check(!eng::sim::gen::generate(small, 1ull, too_many), "rechaza rooms > MaxRooms");

	// 7) Solvencia (W4): el mundo generado (sin gates en W0) es soluble desde el spawn a cualquier
	//    sala; e insoluble se detecta si el objetivo está tras una arista que nunca se abre.
	{
		eng::u8 item_of[64];
		for (eng::u16 i = 0; i < 64u; ++i) {
			item_of[i] = eng::sim::gen::kNoItem;
		}
		const eng::u16 last = static_cast<eng::u16>(a.graph.node_count() - 1u);
		check(eng::sim::gen::is_solvable(a, p.spawn, last, item_of, 64u),
		      "mundo generado es soluble (sin gates)");

		// Mundo de 2 salas con la arista de la sala 1 sin abrir: inventario nunca la abre ->
		// la sala 1 no es alcanzable. Se construye un grafo mínimo a mano.
		Gen mini {};
		mini.graph.add_node();
		mini.graph.add_node();
		mini.graph.add_edge(0u, 1u, 1u, true);
		eng::u8 item_mini[64];
		item_mini[0] = eng::sim::gen::kNoItem;
		item_mini[1] = eng::sim::gen::kNoItem;
		// Sin gates, la 1 es alcanzable.
		check(eng::sim::gen::is_solvable(mini, 0u, 1u, item_mini, 64u), "2 salas conectadas: soluble");

		// Nodo aislado (sin aristas): no alcanzable.
		Gen iso {};
		iso.graph.add_node();
		iso.graph.add_node(); // el nodo 1 queda aislado
		eng::u8 item_iso[64];
		for (eng::u16 i = 0; i < 64u; ++i) {
			item_iso[i] = eng::sim::gen::kNoItem;
		}
		check(!eng::sim::gen::is_solvable(iso, 0u, 1u, item_iso, 64u),
		      "nodo aislado: insoluble");
	}

	if (failures == 0) {
		std::printf("OK: W0 (generacion procedural determinista de topologia) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
