// ============================================================================
// Test HOST-099: búsqueda de caminos (eng::util::pathfinding).
// ============================================================================
//
// Respalda `eng/core/util/pathfinding.hpp`: BFS y A* en rejilla 8x8 con obstáculos y
// reconstrucción de camino.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/099_pathfinding

#include <cstdio>

#include <eng/core/util/pathfinding.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

constexpr eng::u16 W = 8u;
constexpr eng::u16 H = 8u;
constexpr eng::usize N = W * H;

/// ¿Camino con pasos de a uno (4-vecinos)?
bool path_is_adjacent(const eng::u16* path, eng::usize n) {
	for (eng::usize i = 1; i < n; ++i) {
		const int ax = path[i - 1] % W;
		const int ay = path[i - 1] / W;
		const int bx = path[i] % W;
		const int by = path[i] / W;
		if ((ax - bx) * (ax - bx) + (ay - by) * (ay - by) != 1) {
			return false;
		}
	}
	return true;
}

} // namespace

int main() {
	std::printf("== HOST-099 pathfinding ==\n");

	// Muro vertical en x=4 con hueco en y>=6.
	static bool wall[N] = {};
	for (int y = 0; y <= 5; ++y) {
		wall[y * W + 4] = true;
	}
	const auto walkable = [](eng::u16 i) { return !wall[i]; };

	// --- BFS -----------------------------------------------------------------
	{
		static eng::s16 came_from[N] = {};
		static eng::u16 queue[N] = {};
		const eng::u16 start = 0u;        // (0,0)
		const eng::u16 goal = W * H - 1u; // (7,7)
		const bool found = eu::bfs<W, H>(start, goal, walkable,
						 eng::Span<eng::s16> {came_from, N},
						 eng::Span<eng::u16> {queue, N});
		check(found, "BFS encuentra camino rodeando el muro");

		static eng::u16 path[N] = {};
		const eng::usize len = eu::reconstruct_path<W, H>(
			eng::Span<const eng::s16> {came_from, N}, start, goal, eng::Span<eng::u16> {path, N});
		check(len >= 15u, "longitud >= Manhattan (hay rodeo)");
		check(path[0] == start && path[len - 1u] == goal, "extremos del camino");
		check(path_is_adjacent(path, len), "el camino es adyacente (4-vecinos)");
		std::printf("  BFS camino de %u nodos\n", static_cast<unsigned>(len));
	}

	// --- A* (coste unitario) = BFS ------------------------------------------
	{
		static eng::s16 came_from[N] = {};
		static eng::u16 g_score[N] = {};
		static eng::u8 closed[N] = {};
		const eng::u16 start = 0u;
		const eng::u16 goal = W * H - 1u;
		const bool found = eu::astar<W, H>(
			start, goal, walkable, [](eng::u16, eng::u16) { return static_cast<eng::u16>(1u); },
			eng::Span<eng::s16> {came_from, N}, eng::Span<eng::u16> {g_score, N},
			eng::Span<eng::u8> {closed, N});
		check(found, "A* encuentra camino");
		static eng::u16 path[N] = {};
		const eng::usize len = eu::reconstruct_path<W, H>(
			eng::Span<const eng::s16> {came_from, N}, start, goal, eng::Span<eng::u16> {path, N});
		check(path[0] == start && path[len - 1u] == goal && path_is_adjacent(path, len),
		      "A* camino válido");
	}

	// --- Bloqueado -----------------------------------------------------------
	{
		static bool walls2[N] = {};
		for (int y = 0; y < H; ++y) {
			walls2[y * W + 4] = true; // muro completo en x=4
		}
		const auto blocked = [](eng::u16 i) { return !walls2[i]; };
		static eng::s16 came_from[N] = {};
		static eng::u16 queue[N] = {};
		const bool found = eu::bfs<W, H>(0u, 7u, blocked, eng::Span<eng::s16> {came_from, N},
						 eng::Span<eng::u16> {queue, N});
		check(!found, "BFS no cruza un muro completo");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: pathfinding validado.\n");
	return 0;
}
