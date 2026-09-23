// ============================================================================
// Test HOST-084: ArenaAlloc y ChunkedVector.
// ============================================================================
//
// Respalda `eng/core/util/arena_alloc.hpp` (adaptador del LinearArena del engine) y
// `eng/core/util/chunked_vector.hpp` (crecimiento por bloques con direcciones
// estables). Ejercita Vector sobre el arena del engine.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/084_arena_chunked

#include <cstdio>

#include <eng/core/util/arena_alloc.hpp>
#include <eng/core/util/chunked_vector.hpp>
#include <eng/core/util/vector.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-084 arena_alloc + chunked_vector ==\n");

	static_assert(eu::Allocator<eu::ArenaAlloc>, "ArenaAlloc cumple Allocator");

	// --- ArenaAlloc + Vector sobre el LinearArena del engine -----------------
	{
		alignas(16) eng::u8 buf[2048] {};
		eng::LinearArena arena {buf, sizeof(buf), eng::MemoryKind::Any};
		eu::ArenaAlloc alloc {arena};
		check(alloc.valid() && arena.used() == 0u, "ArenaAlloc asociado");

		eu::Vector<eng::u32, eu::ArenaAlloc> v {alloc};
		for (eng::u32 i = 0; i < 40u; ++i) {
			check(v.push_back(i * 2u), "push_back sobre arena");
		}
		check(v.size() == 40u && v[39] == 78u, "contenido");
		check(arena.used() > 0u, "la arena registro las reservas");
		check(arena.overflow_detected() == false, "sin overflow");

		// El mismo arena no puede satisfacer una reserva imposible.
		eu::Vector<eng::u8, eu::ArenaAlloc> big {alloc};
		check(!big.reserve(100000u), "reserva imposible devuelve false");
	}

	// --- ChunkedVector -------------------------------------------------------
	{
		alignas(16) eng::u8 buf[2048] {};
		eng::LinearArena arena {buf, sizeof(buf), eng::MemoryKind::Any};
		eu::ArenaAlloc alloc {arena};
		eu::ChunkedVector<int, 4, 4, eu::ArenaAlloc> cv {alloc};
		static_assert(cv.capacity() == 16u, "capacity = Chunk*MaxChunks");
		check(cv.empty() && cv.chunks_live() == 0u, "ChunkedVector vacío");

		check(cv.push_back(100), "primer elemento (reserva bloque 0)");
		int* stable = &cv[0];
		check(cv.chunks_live() == 1u, "un bloque vivo");
		for (int i = 1; i < 16; ++i) {
			check(cv.push_back(100 + i), "push_back dentro de capacidad");
		}
		check(cv.full() && cv.size() == 16u, "lleno");
		check(&cv[0] == stable, "la direccion del primer elemento es estable");
		check(cv[5] == 105 && cv[15] == 115, "mapeo por bloques");
		check(cv.chunks_live() == 4u, "4 bloques de 4");
		check(!cv.push_back(999), "no cabe un elemento 17");

		cv.clear();
		check(cv.empty() && cv.chunks_live() == 4u, "clear conserva los bloques");
		for (int i = 0; i < 16; ++i) {
			check(cv.push_back(i), "relleno tras clear");
		}
		check(cv.chunks_live() == 4u, "reutiliza los bloques reservados");
		check(cv[3] == 3 && cv[4] == 4, "contenido tras reutilizar");

		eu::ChunkedVector<int, 4, 4, eu::ArenaAlloc> moved =
			static_cast<eu::ChunkedVector<int, 4, 4, eu::ArenaAlloc>&&>(cv);
		check(moved.size() == 16u && moved[0] == 0 && moved[15] == 15, "move conserva");
		check(cv.empty() && cv.chunks_live() == 0u, "move deja el origen vacío");
	}

	// --- ChunkedVector sin allocator: no puede crear bloques -----------------
	{
		eu::ChunkedVector<int, 4, 4> cv;
		check(!cv.push_back(1), "NullAlloc: no puede reservar el primer bloque");
		check(cv.empty() && cv.chunks_live() == 0u, "estado intacto");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: ArenaAlloc y ChunkedVector validados.\n");
	return 0;
}
