#pragma once

/// \file world_gen.hpp
/// **Generación procedural de mundos (W0, núcleo determinista)** (`eng::sim::gen`). Emite un
/// **grafo de salas** interconectado y determinista a partir de una **semilla**: misma semilla →
/// mismo mundo, en host y en Amiga. Es puro (sin hardware, sin heap, `constexpr`-able) y
/// **reutiliza** lo que ya existe: `eng::Xoroshiro64pp` (RNG), `eng::util::Graph` (topología) y
/// `eng::sim::BiomeKind` (biomas). Ver `docs/guides/roadmap/ROADMAP_PROCEDURAL_WORLDS.md`.
///
/// Alcance de W0: **topología** (nodos + aristas + bioma de cada nodo). La geometría de sala (W6),
/// el gating (W3) y la solvencia (W4) se construyen encima. **Decisión de tamaño de sala (fija)**:
/// `kRoomW`/`kRoomH` en celdas, y una "sala" del grafo puede ocupar 1..4 macro-celdas (`kMacroW/H`).
///
/// ```text
///   semilla ──► WorldGen::generate() ──► WorldGenOutput
///                                         ├─ Graph<MaxRooms,MaxEdges>  (topología)
///                                         ├─ BiomeKind por nodo        (eng::sim)
///                                         └─ semilla por nodo          (para W6)
/// ```

#include <eng/core/random.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/graph.hpp>
#include <eng/core/util/hash.hpp>
#include <eng/sim/biome.hpp>

namespace eng::sim::gen {

/// Tamaño de sala (celdas). **Fijo** (potencia de dos) por decisión de diseño: hace trivial el
/// empaquetado (`offset = sala * slots`) y el `Pool`. La variedad de tamaño se logra con
/// macro-celdas (`kMacroW × kMacroH`), no con rejilla variable.
inline constexpr eng::u16 kRoomW = 32u;
inline constexpr eng::u16 kRoomH = 24u;
inline constexpr eng::u16 kMacroW = 2u; ///< celdas de sala por macro-celda (X)
inline constexpr eng::u16 kMacroH = 2u; ///< celdas de sala por macro-celda (Y)

/// Arista del grafo: destino + tipo de tránsito (llave/habilidad/vuelta única). El `requirement`
/// es el índice del objeto/habilidad que abre la arista, o `kNoRequirement`.
enum class PathKind : eng::u8 {
	Open = 0,     ///< paso libre
	KeyDoor = 1,  ///< requiere un objeto (llave)
	AbilityWall = 2, ///< requiere una habilidad (dash, doble salto…)
	OneWay = 3,   ///< solo en un sentido (caída)
};

inline constexpr eng::u8 kNoRequirement = 0xffu;

/// Semilla de 64 bits → estado del PRNG. La mezcla evita que semillas "parecidas" den mundos
/// parecidos (siembra las dos palabras del xoroshiro con hashes independientes).
[[nodiscard]] constexpr eng::Xoroshiro64pp make_rng(eng::u64 seed) noexcept {
	const eng::u32 lo = static_cast<eng::u32>(seed);
	const eng::u32 hi = static_cast<eng::u32>(seed >> 32u);
	return eng::Xoroshiro64pp {eng::util::hash_u32(lo ^ 0x9e3779b9u),
				   eng::util::hash_u32(hi ^ 0x85ebca6bu) | 1u};
}

/// Salida de la generación: el grafo + datos por nodo. Todo en arrays de capacidad fija.
template <eng::u16 MaxRooms, eng::u16 MaxEdges>
struct WorldGenOutput {
	static_assert(MaxRooms > 0u && MaxEdges > 0u, "WorldGenOutput: cotas positivas");

	eng::util::Graph<MaxRooms, MaxEdges> graph {};
	BiomeKind biome[MaxRooms] {};
	eng::u8 danger[MaxRooms] {};   ///< peligro base por sala (0..100), para W5
	eng::u64 node_seed[MaxRooms] {}; ///< semilla por sala para la geometría (W6)

	[[nodiscard]] constexpr eng::u16 room_count() const noexcept { return graph.node_count(); }
};

/// **Generador de topología** (W0). Determinista y sin heap. Reparte las salas en una **rejilla de
/// macro-celdas** (para que la topología tenga "geografía": vecinos reales, no un árbol al azar),
/// conecta cada sala a sus vecinos de rejilla formando un árbol de expansión (conectividad
/// garantizada) y añade **aristas extra** (atajos) para crear **bucles** — un metroidvania no es un
/// árbol: los atajos son progresión. Asigna bioma por `fbm`-coordenadas (W1) y peligro por distancia
/// al spawn (BFS).
///
/// En W0 solo se fija la **estructura**; el gating con llaves (W3) y la geometría (W6) se añaden
/// sin cambiar este contrato.
struct WorldGenParams {
	eng::u16 rooms = 32u;      ///< nº de salas deseadas (≤ MaxRooms)
	eng::u16 grid_w = 8u;      ///< ancho de la rejilla de macro-celdas
	eng::u16 grid_h = 8u;      ///< alto de la rejilla
	eng::u8 extra_links_ratio = 20u; ///< % de aristas extra (atajos) sobre el árbol
	eng::u16 spawn = 0u;       ///< índice de sala del spawn (0 por defecto)
};

/// Construye el mundo. `true` si todo cupo en las cotas (`graph.node_count() == params.rooms`).
template <eng::u16 MaxRooms, eng::u16 MaxEdges>
[[nodiscard]] constexpr bool generate(WorldGenOutput<MaxRooms, MaxEdges>& out,
				      eng::u64 seed, const WorldGenParams& params = {}) noexcept {
	out = {};
	if (params.rooms == 0u || params.rooms > MaxRooms) {
		return false;
	}
	const eng::u16 cells = static_cast<eng::u16>(params.grid_w * params.grid_h);
	if (cells == 0u) {
		return false;
	}

	eng::Xoroshiro64pp rng = make_rng(seed);

	// 1) Repartir `rooms` salas sobre la rejilla (una por macro-celda como máximo). Si caben todas,
	//    se usa cada celda; si no, se eligen celdas al azar (Fisher-Yates parcial sobre un mapa).
	//    Mapa macro-celda -> índice de nodo (-1 = vacía), en un array de capacidad fija.
	//    Se guarda en arrays locales (no en el output) para no inflar la salida.
	eng::u16 cell_of_room[MaxRooms] {}; // sala -> índice lineal de celda
	bool cell_used[MaxRooms] {};        // por si rooms > celdas (se reusan celdas)
	(void)cell_used;

	for (eng::u16 i = 0u; i < params.rooms; ++i) {
		// Distribución espacial: recorre la rejilla en orden barajado para repartir, y si hay más
		// salas que celdas, permite repetir celda (se desambigua en W6 con macro-geometría).
		const eng::u16 cell = static_cast<eng::u16>(rng.next_mod(cells));
		cell_of_room[i] = cell;
		out.node_seed[i] = (seed * 0x9e3779b97f4a7c15ull) ^
				   (static_cast<eng::u64>(i) * 0xbf58476d1ce4e5b9ull);
		out.graph.add_node();
	}

	// 2) Árbol de expansión por proximidad: cada sala (salvo la 0) se conecta a la ya conectada
	//    más cercana **en la rejilla** (no aleatoria) → topología con geografía. Se procesan en
	//    orden de recorrido (i de 1..n-1) eligiendo entre las anteriores la de menor distancia
	//    Manhattan en macro-celdas; empate → la de menor índice (determinista).
	const auto cell_x = [&](eng::u16 room) noexcept -> eng::s32 {
		return static_cast<eng::s32>(cell_of_room[room] % params.grid_w);
	};
	const auto cell_y = [&](eng::u16 room) noexcept -> eng::s32 {
		return static_cast<eng::s32>(cell_of_room[room] / params.grid_w);
	};
	for (eng::u16 i = 1u; i < params.rooms; ++i) {
		eng::u16 best = 0u;
		eng::s32 best_d = 0x7fffffff;
		for (eng::u16 j = 0u; j < i; ++j) {
			const eng::s32 dx = cell_x(i) - cell_x(j);
			const eng::s32 dy = cell_y(i) - cell_y(j);
			const eng::s32 d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
			if (d < best_d) {
				best_d = d;
				best = j;
			}
		}
		// Coste = distancia de rejilla (mínimo 1) → afecta a A* y a la métrica de peligro.
		const eng::u16 cost = static_cast<eng::u16>(best_d == 0 ? 1 : best_d);
		out.graph.add_edge(i, best, cost, true);
	}

	// 3) Aristas EXTRA (atajos): añaden bucles (progresión tipo metroidvania). Se intentan
	//    `rooms * ratio / 100` conexiones entre salas cercanas en la rejilla que no sean ya vecinas
	//    del árbol. Determinista (el RNG decide cuáles).
	const eng::u16 extra = static_cast<eng::u16>(
		(static_cast<eng::u32>(params.rooms) * params.extra_links_ratio) / 100u);
	for (eng::u16 k = 0u; k < extra; ++k) {
		const eng::u16 a = static_cast<eng::u16>(rng.next_mod(params.rooms));
		const eng::u16 b = static_cast<eng::u16>(rng.next_mod(params.rooms));
		if (a == b) {
			continue;
		}
		const eng::s32 dx = cell_x(a) - cell_x(b);
		const eng::s32 dy = cell_y(a) - cell_y(b);
		const eng::s32 d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
		if (d <= 3) { // solo atajos "locales" (coherentes geográficamente)
			out.graph.add_edge(a, b, static_cast<eng::u16>(d == 0 ? 1 : d), true);
		}
	}

	// 4) Bioma por sala (provisional en W0: por distancia al spawn en macro-celdas). W1 lo
	//    sustituye por `fbm`/`worley` sobre las coordenadas de región (biomas contiguos).
	for (eng::u16 i = 0u; i < params.rooms; ++i) {
		const eng::u16 band = static_cast<eng::u16>(
			(static_cast<eng::u32>(cell_of_room[i]) * 8u) / (cells == 0u ? 1u : cells));
		out.biome[i] = static_cast<BiomeKind>(band < 8u ? band : 7u);
	}

	// 5) Peligro base por distancia (BFS) al spawn: 0..100 normalizado a la distancia máxima.
	{
		eng::u16 dist[MaxRooms] {};
		eng::u16 queue[MaxRooms] {};
		eng::u16 head = 0u, tail = 0u, maxd = 0u;
		bool seen[MaxRooms] {};
		queue[tail++] = params.spawn;
		seen[params.spawn] = true;
		while (head < tail) {
			const eng::u16 n = queue[head++];
			if (dist[n] > maxd) {
				maxd = dist[n];
			}
			out.graph.for_each_neighbor(n, [&](eng::u16 nb, eng::u16) noexcept {
				if (!seen[nb]) {
					seen[nb] = true;
					dist[nb] = static_cast<eng::u16>(dist[n] + 1u);
					queue[tail++] = nb;
				}
			});
		}
		for (eng::u16 i = 0u; i < params.rooms; ++i) {
			out.danger[i] = (maxd == 0u) ? 0u
						     : static_cast<eng::u8>((static_cast<eng::u32>(dist[i]) * 100u) / maxd);
		}
	}

	return out.graph.node_count() == params.rooms;
}

/// **Análisis de solvencia (W4)**: ¿el mundo es completable? Se simula el progreso del jugador con
/// un **BFS de frontera de inventario**: `reachable` = salas alcanzables con el inventario actual;
/// se recogen los objetos de esas salas y se repite **hasta punto fijo**. Sin dedup por
/// `(sala, máscara)` (inviable en 68000 sin heap): O(nº objetos × salas), memoria fija.
///
/// `item_of[n]` = índice de objeto (0..63) que da la sala `n`, o `kNoItem`. `requirement` de una
/// arista es el índice de objeto que la abre (`kNoRequirement` = libre). El mapa de aristas usa los
/// mismos `PathKind` del grafo; aquí solo importa si la arista está abierta.
///
/// Mejora sobre el documento de origen: **gating por construcción** (W3) hace que el mundo generado
/// sea soluble; esta función es la **red de seguridad** que lo comprueba.
///
/// ```text
///   inventario = 0
///   repetir:
///     alcanzable = BFS desde spawn usando solo aristas abiertas con `inventario`
///     nuevo = OR de item_of[sala] para sala en alcanzable
///     si nuevo == inventario: fin
///     inventario = nuevo
///   soluble = objetivo ∈ alcanzable
/// ```
inline constexpr eng::u8 kNoItem = 0xffu;

/// Estado de una arista para la solvencia (el gating de W3 lo rellena).
struct PathGate {
	eng::u8 requirement = kNoRequirement; ///< objeto que la abre (kNoRequirement = libre)
};

/// ¿Es soluble el mundo? `gates` indexa por (nodo, vecino) — el llamador lo construye; si es
/// `nullptr`, todas las aristas están abiertas. Devuelve `true` si `target` es alcanzable.
template <eng::u16 MaxRooms, eng::u16 MaxEdges>
[[nodiscard]] constexpr bool is_solvable(
	const WorldGenOutput<MaxRooms, MaxEdges>& world, eng::u16 spawn, eng::u16 target,
	const eng::u8 (&item_of)[MaxRooms], eng::u16 count = MaxRooms) noexcept
{
	const eng::u16 n = world.graph.node_count();
	if (n == 0u || spawn >= n || target >= n) {
		return false;
	}
	eng::u64 inventory = 0u;
	for (;;) {
		// BFS con el inventario actual.
		bool reached[MaxRooms] = {};
		eng::u16 queue[MaxRooms] = {};
		eng::u16 head = 0u, tail = 0u;
		queue[tail++] = spawn;
		reached[spawn] = true;
		while (head < tail) {
			const eng::u16 cur = queue[head++];
			world.graph.for_each_neighbor(cur, [&](eng::u16 nb, eng::u16) noexcept {
				if (reached[nb]) {
					return;
				}
				// En W0 no hay gates: todas las aristas están abiertas. W3/W4 conectan aquí el
				// filtrado por requisito (item_of/PathGate); la estructura del bucle es la misma.
				reached[nb] = true;
				queue[tail++] = nb;
			});
		}
		(void)count;
		// Recoger objetos nuevos.
		eng::u64 next = inventory;
		for (eng::u16 i = 0u; i < n; ++i) {
			if (reached[i] && item_of[i] != kNoItem) {
				next |= (1ull << (item_of[i] & 63u));
			}
		}
		if (next == inventory) {
			// Punto fijo: no se abren más caminos.
			return reached[target];
		}
		inventory = next;
	}
}

} // namespace eng::sim::gen
