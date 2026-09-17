// ============================================================================
// Test HOST-092: ScopeGuard y StaticString.
// ============================================================================
//
// Respalda `eng/core/util/scope_guard.hpp` y `eng/core/util/static_string.hpp`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/092_scope_guard_static_string

#include <cstdio>

#include <eng/core/util/scope_guard.hpp>
#include <eng/core/util/static_string.hpp>

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
	std::printf("== HOST-092 scope_guard + static_string ==\n");

	// --- ScopeGuard ----------------------------------------------------------
	{
		int fired = 0;
		{
			auto guard = eu::make_scope_guard([&fired] { ++fired; });
			check(guard.active() && fired == 0, "ScopeGuard armado");
		}
		check(fired == 1, "ScopeGuard ejecuta al salir");

		{
			auto guard = eu::make_scope_guard([&fired] { ++fired; });
			guard.release();
			check(!guard.active(), "release desactiva");
		}
		check(fired == 1, "release no ejecuta");

		{
			auto guard = eu::make_scope_guard([&fired] { ++fired; });
			auto moved = static_cast<decltype(guard)&&>(guard);
			check(!guard.active() && moved.active(), "move transfiere la acción");
		}
		check(fired == 2, "move ejecuta una sola vez");
	}

	// --- StaticString --------------------------------------------------------
	{
		eu::StaticString<8> s; // capacidad 7
		static_assert(s.capacity() == 7u, "capacity = N-1");
		check(s.empty() && s.size() == 0u && s.c_str()[0] == '\0', "vacía y terminada");

		s.assign(eu::StringView("abc"));
		check(s.size() == 3u && s[1] == 'b', "assign");
		check(eu::StringView(s.c_str()) == eu::StringView("abc"), "c_str");
		check(s.c_str()[3] == '\0', "terminador tras assign");

		check(s.append('d') && s.size() == 4u, "append(char)");
		check(s.append(eu::StringView("efg")) && s.size() == 7u && s.full(), "append llena");
		check(!s.append('x'), "append rechaza si está llena");
		check(!s.append(eu::StringView("z")), "append StringView rechaza si está llena");
		check(s.view() == eu::StringView("abcdefg"), "view");

		s.clear();
		check(s.empty() && s.c_str()[0] == '\0', "clear");

		// assign recorta a la capacidad.
		eu::StaticString<4> t; // capacidad 3
		t.assign(eu::StringView("abcdef"));
		check(t.size() == 3u && t.view() == eu::StringView("abc"), "assign recorta");

		// Constructor desde StringView.
		const eu::StaticString<16> u {eu::StringView("hola")};
		check(u.size() == 4u && u.view() == eu::StringView("hola"), "ctor desde StringView");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: ScopeGuard y StaticString validados.\n");
	return 0;
}
