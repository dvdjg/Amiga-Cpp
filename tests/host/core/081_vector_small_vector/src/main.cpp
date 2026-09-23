// ============================================================================
// Test HOST-081: Vector y SmallVector (crecimiento sin heap).
// ============================================================================
//
// Respalda `eng/core/util/vector.hpp` y `eng/core/util/small_vector.hpp`: crecimiento
// sobre un asignador (BumpAlloc), capacidad inline (SmallVector) y el caso sin
// asignador (NullAlloc) que no crece.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/081_vector_small_vector

#include <cstdio>

#include <eng/core/util/small_vector.hpp>
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
	std::printf("== HOST-081 vector + small_vector ==\n");

	// --- Vector con asignador (crece) ---------------------------------------
	{
		alignas(16) eng::u8 buf[1024] {};
		eu::BumpAlloc alloc {eng::Span<eng::u8> {buf, sizeof(buf)}};
		eu::Vector<int, eu::BumpAlloc> v {alloc};
		check(v.empty() && v.capacity() == 0u, "Vector vacío");

		for (int i = 0; i < 20; ++i) {
			check(v.push_back(i), "push_back dentro de capacidad");
		}
		check(v.size() == 20u && v[5] == 5 && v.back() == 19, "acceso tras push_back");
		check(v.reserve(100u) && v.capacity() >= 100u, "reserve crece");
		check(v[0] == 0 && v.front() == 0, "front");
		check(v.insert(0u, 999) && v[0] == 999 && v[1] == 0 && v.size() == 21u, "insert");
		v.erase(0u);
		check(v.size() == 20u && v[0] == 0, "erase");
		check(v.resize(25u, -1) && v.size() == 25u && v[24] == -1, "resize rellena");
		v.pop_back();
		check(v.size() == 24u, "pop_back");
		check(v.span().size() == 24u, "span");

		eu::Vector<int, eu::BumpAlloc> moved = static_cast<eu::Vector<int, eu::BumpAlloc>&&>(v);
		check(moved.size() == 24u && moved[19] == 19 && moved[23] == -1,
		      "move conserva el contenido");
		check(v.empty() && v.capacity() == 0u, "move deja el origen vacío");

		// Desbordamiento controlado: un buffer diminuto solo admite unos pocos.
		alignas(16) eng::u8 tiny[40] {};
		eu::BumpAlloc alloc2 {eng::Span<eng::u8> {tiny, sizeof(tiny)}};
		eu::Vector<int, eu::BumpAlloc> small {alloc2};
		int accepted = 0;
		while (small.push_back(accepted) && accepted < 1000) {
			++accepted;
		}
		check(accepted >= 4 && accepted < 1000, "push_back falla al agotar el buffer");
		check(small.size() == static_cast<eng::usize>(accepted), "size coherente tras el fallo");
	}

	// --- Vector sin asignador (NullAlloc: no crece) -------------------------
	{
		eu::Vector<int> v;
		check(!v.push_back(1), "Vector con NullAlloc no puede crecer");
		check(v.empty(), "Vector con NullAlloc sigue vacío");
	}

	// --- SmallVector: inline hasta N, luego al asignador ---------------------
	{
		alignas(16) eng::u8 buf[1024] {};
		eu::BumpAlloc alloc {eng::Span<eng::u8> {buf, sizeof(buf)}};
		eu::SmallVector<int, 4, eu::BumpAlloc> sv {alloc};
		check(sv.is_inline() && sv.capacity() == 4u && sv.inline_capacity() == 4u,
		      "SmallVector arranca inline");
		for (int i = 1; i <= 4; ++i) {
			check(sv.push_back(i), "push_back inline");
		}
		check(sv.is_inline() && sv.size() == 4u, "sigue inline con 4");
		check(sv.push_back(5), "push_back desborda al asignador");
		check(!sv.is_inline() && sv.size() == 5u && sv.capacity() >= 5u, "creció fuera");
		check(sv[0] == 1 && sv[4] == 5, "contenido tras desbordar");

		eu::SmallVector<int, 4, eu::BumpAlloc> moved = static_cast<eu::SmallVector<int, 4, eu::BumpAlloc>&&>(sv);
		check(moved.size() == 5u && moved[4] == 5, "move conserva el contenido");
		check(sv.empty() && sv.is_inline(), "move deja el origen inline y vacío");
	}

	// --- SmallVector con NullAlloc: no desborda -----------------------------
	{
		eu::SmallVector<int, 3> sv;
		check(sv.push_back(1) && sv.push_back(2) && sv.push_back(3), "3 inline");
		check(!sv.push_back(4), "NullAlloc: no puede desbordar");
		check(sv.is_inline() && sv.size() == 3u, "estado intacto tras el fallo");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Vector y SmallVector validados.\n");
	return 0;
}
