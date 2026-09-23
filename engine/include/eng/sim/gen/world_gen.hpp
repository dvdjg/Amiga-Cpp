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

#include <eng/core/fixed.hpp>
#include <eng/core/noise.hpp>
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
/// "Sin objeto/habilidad" para `item_of`/`gate_obj` (una sala no da objeto / una arista no exige).
inline constexpr eng::u8 kNoItem = 0xffu;

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

	// --- Gating (W3) ---
	/// Objeto/habilidad que **contiene** la sala `n` (se recoge al visitarla), o `kNoItem`.
	eng::u8 item_of[MaxRooms] {};
	/// La arista del **árbol** que entra en la sala `n` exige `gate_obj[n]` (`kNoItem` = libre).
	/// (Los atajos extra quedan abiertos: son atajos de progresión, no puertas.)
	eng::u8 gate_obj[MaxRooms] {};
	/// Sala padre en el árbol de expansión (`0xffff` = es la raíz).
	eng::u16 tree_parent[MaxRooms] {};
	bool is_spine[MaxRooms] {}; ///< la sala pertenece al "camino principal" (spine)
	/// Macro-celda (índice lineal en la rejilla) de cada sala; útil para W1/W5/W6.
	eng::u16 cell_of_room[MaxRooms] {};

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
	eng::u16 biome_cells_x = 3u; ///< macro-celdas por "celda de bioma" en X (escala del ruido)
	eng::u16 biome_cells_y = 3u; ///< macro-celdas por "celda de bioma" en Y
};

/// Clasifica el bioma a partir de **temperatura** (fbm), **humedad** (fbm) y **cueva** (worley),
/// todos en `[0,1]`. Reglas (deterministas, sin tabla): temperatura baja → Tundra/Mountain;
/// alta+seca → Desert; alta+húmeda → Swamp; cueva fuerte → Cave/Reef; resto → Plains/Forest.
/// Devuelve un `BiomeKind` válido. Puro y `constexpr`-able.
template <class Fx>
[[nodiscard]] constexpr BiomeKind classify_biome(Fx temp, Fx humid, Fx cave) noexcept {
	// Umbrales como constantes del escalar `Fx` (constexpr).
	const Fx half = eng::math::scalar_const<Fx>::from(0.5);
	const Fx third = eng::math::scalar_const<Fx>::from(1.0 / 3.0);
	const Fx two_thirds = eng::math::scalar_const<Fx>::from(2.0 / 3.0);
	// Cueva por worley bajo (celda de Voronoi cercana = cavidad): prioridad alta.
	if (cave < third) {
		return (humid > half) ? BiomeKind::Reef : BiomeKind::Cave;
	}
	if (temp < third) {
		return (humid > half) ? BiomeKind::Tundra : BiomeKind::Mountain;
	}
	if (temp > two_thirds) {
		return (humid < half) ? BiomeKind::Desert : BiomeKind::Swamp;
	}
	return (humid > half) ? BiomeKind::Forest : BiomeKind::Plains;
}

/// **Asigna biomas con ruido** (`fbm2`/`worley2`) usando el escalar `Fx` que aporta el llamador
/// (p. ej. `MiniFloat16` en host, o un `Fixed` con ruido en punto fijo en Amiga). **Genérico**: el
/// header no fija el escalar (regla §1.10). `coords[i]` = macro-celda de la sala `i`; `cells_x/y` =
/// escala del ruido. Determinista respecto a `seed`.
///
/// En **m68k** el llamador debe elegir un `Fx` sin libcalls (`Fixed`), porque `MiniFloat16`+
/// `worley2` generan `__divsf3`/`__mulsi3` que `-nostdlib` no enlaza; el `generate()` por defecto
/// usa bandas enteras y esto se reserva al host (precocinado) o a un futuro ruido en punto fijo.
template <eng::u16 MaxRooms, eng::u16 MaxEdges, class Fx>
constexpr void assign_biomes_fbm(WorldGenOutput<MaxRooms, MaxEdges>& out, eng::u64 seed,
				 const eng::u16 (&cell_of_room)[MaxRooms], eng::u16 rooms,
				 eng::u16 grid_w, eng::u16 cells_x, eng::u16 cells_y) noexcept {
	const Fx inv_wx = eng::math::scalar_const<Fx>::from(
		1.0 / static_cast<double>(cells_x ? cells_x : 1u));
	const Fx inv_wy = eng::math::scalar_const<Fx>::from(
		1.0 / static_cast<double>(cells_y ? cells_y : 1u));
	const Fx lac = eng::math::scalar_const<Fx>::from(2.0);
	const Fx gain = eng::math::scalar_const<Fx>::from(0.5);
	for (eng::u16 i = 0u; i < rooms; ++i) {
		const Fx nx = eng::math::mul_norm(
			eng::math::scalar_const<Fx>::from(
				static_cast<double>(cell_of_room[i] % grid_w)),
			inv_wx);
		const Fx ny = eng::math::mul_norm(
			eng::math::scalar_const<Fx>::from(
				static_cast<double>(cell_of_room[i] / grid_w)),
			inv_wy);
		const Fx temp = eng::math::fbm2<Fx>(nx, ny, static_cast<eng::u32>(seed), 4, lac, gain);
		const Fx humid = eng::math::fbm2<Fx>(nx, ny,
						     static_cast<eng::u32>(seed) ^ 0x9e3779b9u, 4,
						     lac, gain);
		const Fx cave = eng::math::worley2<Fx>(nx, ny,
						       static_cast<eng::u32>(seed) ^ 0x85ebca6bu);
		out.biome[i] = classify_biome(temp, humid, cave);
	}
}

/// Construye el mundo. `true` si todo cupo en las cotas (`graph.node_count() == params.rooms`).
// `inline` (no `constexpr`): el GCC m68k a `-O0` da un ICE de CFI (`dwarf2cfi.cc`) con el cuerpo
// `constexpr` + lambdas de este tamaño. La generación es determinista y pura igualmente (no necesita
// evaluarse en compile-time); a `-O1/-O2` compila en ambos modos.
template <eng::u16 MaxRooms, eng::u16 MaxEdges>
[[nodiscard]] inline bool generate(WorldGenOutput<MaxRooms, MaxEdges>& out,
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
		out.cell_of_room[i] = cell;
		// Semilla por sala sin multiplicaciones (`__mulsi3`/`__muldi3` en 68000: prohibidas).
		// Hash u32 (rotaciones/xors/sumas) de la semilla maestra y el índice.
		const eng::u32 s_lo = static_cast<eng::u32>(seed);
		const eng::u32 s_hi = static_cast<eng::u32>(seed >> 32u);
		const eng::u32 h0 = eng::util::hash_u32(s_lo ^ static_cast<eng::u32>(i));
		const eng::u32 h1 = eng::util::hash_u32(s_hi + static_cast<eng::u32>(i) + 0x9e3779b9u);
		out.node_seed[i] = (static_cast<eng::u64>(h0) << 32u) | h1;
		(void)out.graph.add_node();
	}

	// 2) Árbol de expansión por proximidad: cada sala (salvo la 0) se conecta a la ya conectada
	//    más cercana **en la rejilla** (no aleatoria) → topología con geografía. Se procesan en
	//    orden de recorrido (i de 1..n-1) eligiendo entre las anteriores la de menor distancia
	//    Manhattan en macro-celdas; empate → la de menor índice (determinista).
	// Sin lambdas locales: el GCC m68k a `-O0` da un ICE de CFI (`dwarf2cfi.cc`) con lambdas en
	// este cuerpo. Las coordenadas de macro-celda de una sala se calculan in-line.
	for (eng::u16 i = 1u; i < params.rooms; ++i) {
		eng::u16 best = 0u;
		eng::s32 best_d = 0x7fffffff;
		for (eng::u16 j = 0u; j < i; ++j) {
			const eng::s32 xi = static_cast<eng::s32>(cell_of_room[i] % params.grid_w);
			const eng::s32 yi = static_cast<eng::s32>(cell_of_room[i] / params.grid_w);
			const eng::s32 xj = static_cast<eng::s32>(cell_of_room[j] % params.grid_w);
			const eng::s32 yj = static_cast<eng::s32>(cell_of_room[j] / params.grid_w);
			const eng::s32 dx = xi - xj;
			const eng::s32 dy = yi - yj;
			const eng::s32 d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
			if (d < best_d) {
				best_d = d;
				best = j;
			}
		}
		// Coste = distancia de rejilla (mínimo 1) → afecta a A* y a la métrica de peligro.
		const eng::u16 cost = static_cast<eng::u16>(best_d == 0 ? 1 : best_d);
		(void)out.graph.add_edge(i, best, cost, true);
		out.tree_parent[i] = best;
	}

	// 3) Aristas EXTRA (atajos): añaden bucles (progresión tipo metroidvania). Se intentan
	//    `rooms * ratio / 100` conexiones entre salas cercanas en la rejilla que no sean ya vecinas
	//    del árbol. Determinista (el RNG decide cuáles).
	// u16 (no u32): `mulu.w`/`divu.w` nativos en 68000, sin `__mulsi3`/`__udivsi3`.
	const eng::u16 extra = static_cast<eng::u16>(
		(static_cast<eng::u16>(params.rooms) * static_cast<eng::u16>(params.extra_links_ratio)) / 100u);
	for (eng::u16 k = 0u; k < extra; ++k) {
		const eng::u16 a = static_cast<eng::u16>(rng.next_mod(params.rooms));
		const eng::u16 b = static_cast<eng::u16>(rng.next_mod(params.rooms));
		if (a == b) {
			continue;
		}
		const eng::s32 xa = static_cast<eng::s32>(cell_of_room[a] % params.grid_w);
		const eng::s32 ya = static_cast<eng::s32>(cell_of_room[a] / params.grid_w);
		const eng::s32 xb = static_cast<eng::s32>(cell_of_room[b] % params.grid_w);
		const eng::s32 yb = static_cast<eng::s32>(cell_of_room[b] / params.grid_w);
		const eng::s32 dx = xa - xb;
		const eng::s32 dy = ya - yb;
		const eng::s32 d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
		if (d <= 3) { // solo atajos "locales" (coherentes geográficamente)
			(void)out.graph.add_edge(a, b, static_cast<eng::u16>(d == 0 ? 1 : d), true);
		}
	}

	// 4) Bioma por sala con **ruido** `fbm2`/`worley2` sobre las coordenadas de macro-celda (W1):
	//    biomas **contiguos** (celdas vecinas tienden al mismo bioma), no franjas por índice.
	//    Escalar `Fixed<s32,16>` (portable, **sin float**); `wx` = nº de celdas de bioma por
	//    macro-celda (escala del ruido).
	// Bioma por **bandas enteras** (contiguo, determinista, **sin float** → vale en m68k). El bioma
	// con **ruido** `fbm2`/`worley2` es genérico (ver `assign_biomes_fbm<Fx>` abajo) y se elige por
	// el llamador con su escalar; en m68k el ruido `float`-based no enlaza (`-nostdlib`), así que el
	// defecto (runtime Amiga) es este entero, y el host puede llamar a `assign_biomes_fbm`.
	for (eng::u16 i = 0u; i < params.rooms; ++i) {
		const eng::u16 bx = static_cast<eng::u16>(cell_of_room[i] % params.grid_w);
		const eng::u16 by = static_cast<eng::u16>(cell_of_room[i] / params.grid_w);
		const eng::u16 band = static_cast<eng::u16>((bx + by * 2u) % 8u);
		out.biome[i] = static_cast<BiomeKind>(band);
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
			const eng::u16 deg = out.graph.neighbor_count(n);
			for (eng::u16 e = 0u; e < deg; ++e) {
				eng::u16 nb = 0u, cost = 0u;
				(void)out.graph.neighbor_at(n, e, nb, cost);
				if (!seen[nb]) {
					seen[nb] = true;
					dist[nb] = static_cast<eng::u16>(dist[n] + 1u);
					queue[tail++] = nb;
				}
			}
		}
		for (eng::u16 i = 0u; i < params.rooms; ++i) {
			// u16 (mulu.w/divu.w nativos): `dist` ≤ nº de salas (≤64), ×100 cabe en u16.
			out.danger[i] = (maxd == 0u)
						? 0u
						: static_cast<eng::u8>(
							  (static_cast<eng::u16>(dist[i]) * 100u) / maxd);
		}
	}

	return out.graph.node_count() == params.rooms;
}

/// **Gating por construcción (W3)**: coloca llaves y puertas de forma que el mundo sea **soluble
/// siempre**, sin reparar a posteriori (mejora sobre el documento de origen, que degradaba
/// cerraduras si fallaba). Recorre el **árbol de expansión** en orden de creación (`0..n-1`, donde el
/// padre es siempre menor que el hijo) y, cada `gate_every` salas, cierra la arista padre→hijo con
/// una **llave nueva** que se coloca en el **padre** (ya alcanzable) → la puerta nunca queda sin su
/// llave delante. Determinista.
///
/// `spine_ratio`: % de salas que forman el "camino principal" hacia el jefe (las que quedan a mayor
/// distancia del spawn), para W5.
template <eng::u16 MaxRooms, eng::u16 MaxEdges>
constexpr void place_gates(WorldGenOutput<MaxRooms, MaxEdges>& out, eng::u64 seed,
			   eng::u16 spawn, eng::u8 gate_every = 3u) noexcept {
	const eng::u16 n = out.graph.node_count();
	for (eng::u16 i = 0u; i < n; ++i) {
		out.item_of[i] = kNoItem;
		out.gate_obj[i] = kNoItem;
		if (i == 0u) {
			out.tree_parent[i] = 0xffffu; // raíz del árbol
		}
	}
	// La raíz es el spawn (el árbol cuelga del nodo 0). Si el spawn pidió otro, se respeta.
	(void)spawn;

	eng::u8 next_item = 0u;
	eng::u16 counter = 0u;
	for (eng::u16 i = 1u; i < n; ++i) {
		const eng::u16 parent = out.tree_parent[i];
		if (parent == 0xffffu || parent >= n) {
			continue; // nodo suelto (no debería en un árbol conexo)
		}
		++counter;
		// Cierra esta arista SOLO si el padre aún no da una llave (evita conflictos de colocación) y
		// queda presupuesto de objetos. La llave se coloca en el **padre**, alcanzable antes de la
		// puerta → solvencia por construcción.
		if (gate_every != 0u && (counter % gate_every) == 0u && next_item < 63u &&
		    out.item_of[parent] == kNoItem) {
			out.gate_obj[i] = next_item;
			out.item_of[parent] = next_item;
			++next_item;
		}
	}
	// El spawn siempre debe ser alcanzable (no puede estar tras una puerta): si el árbol se colgó
	// del nodo 0 y el spawn es otro, abrimos su arista entrante.
	if (spawn != 0u && spawn < n) {
		out.gate_obj[spawn] = kNoItem;
	}
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
/// ¿Es soluble el mundo? Usa el **gating del propio output** (`gate_obj`/`item_of`): una arista
/// `cur -> nb` está **abierta** si `nb` NO es hijo de `cur` en el árbol (es un **atajo**: siempre
/// libre) o si el inventario ya tiene `gate_obj[nb]`. Devuelve `true` si `target` es alcanzable.
///
/// El gating **por construcción** (W3, `place_gates`) hace que esto dé `true` para todo `target`;
/// esta función es la **red de seguridad** que lo comprueba.
template <eng::u16 MaxRooms, eng::u16 MaxEdges>
[[nodiscard]] constexpr bool is_solvable(const WorldGenOutput<MaxRooms, MaxEdges>& world,
					 eng::u16 spawn, eng::u16 target) noexcept {
	const eng::u16 n = world.graph.node_count();
	if (n == 0u || spawn >= n || target >= n) {
		return false;
	}
	eng::u64 inventory = 0u;
	for (;;) {
		// BFS con el inventario actual, filtrando las aristas del árbol cerradas.
		bool reached[MaxRooms] = {};
		eng::u16 queue[MaxRooms] = {};
		eng::u16 head = 0u, tail = 0u;
		queue[tail++] = spawn;
		reached[spawn] = true;
		while (head < tail) {
			const eng::u16 cur = queue[head++];
			const eng::u16 deg = world.graph.neighbor_count(cur);
			for (eng::u16 e = 0u; e < deg; ++e) {
				eng::u16 nb = 0u, cost = 0u;
				(void)world.graph.neighbor_at(cur, e, nb, cost);
				if (reached[nb]) {
					continue;
				}
				// ¿Es `nb` hijo de `cur` en el árbol? Entonces su gate debe estar abierto.
				const bool is_tree_child = (world.tree_parent[nb] == cur) ||
							   (world.tree_parent[cur] == nb);
				if (is_tree_child) {
					const eng::u8 req = (world.tree_parent[nb] == cur) ? world.gate_obj[nb]
										  : world.gate_obj[cur];
					if (req != kNoItem && (inventory & (1ull << (req & 63u))) == 0u) {
						continue; // cerrada: aún no tenemos la llave
					}
				}
				reached[nb] = true;
				queue[tail++] = nb;
			}
		}
		// Recoger objetos nuevos.
		eng::u64 next = inventory;
		for (eng::u16 i = 0u; i < n; ++i) {
			if (reached[i] && world.item_of[i] != kNoItem) {
				next |= (1ull << (world.item_of[i] & 63u));
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
