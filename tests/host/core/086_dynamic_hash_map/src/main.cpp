// ============================================================================
// Test HOST-086: DynamicHashMap (tabla hash que crece en un Allocator).
// ============================================================================
//
// Respalda `eng/core/util/dynamic_hash_map.hpp`: crecimiento por rehash sobre una
// arena/bump, estres contra un modelo de referencia (cubre rehash + back-shift) e
// internado de cadenas con clave `StringView`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/086_dynamic_hash_map

#include <cstdio>

#include <eng/core/util/dynamic_hash_map.hpp>

namespace eu = eng::util;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Rng {
	eng::u32 state = 0x9e3779b9u;
	eng::u32 next() {
		state ^= state << 13u;
		state ^= state >> 17u;
		state ^= state << 5u;
		return state;
	}
};

} // namespace

int main() {
	std::printf("== HOST-086 dynamic_hash_map ==\n");

	// --- Casos basicos -------------------------------------------------------
	{
		alignas(16) eng::u8 buf[4096] {};
		eu::BumpAlloc alloc {eng::Span<eng::u8> {buf, sizeof(buf)}};
		eu::DynamicHashMap<eng::u16, int, eu::BumpAlloc> map {alloc};
		check(map.empty() && map.slots() == 0u, "empieza sin tablas");
		check(map.insert(5u, 50) != nullptr, "insert (primera reserva)");
		check(map.insert(5u, 99) == nullptr, "duplicado rechazado");
		check(*map.find(5u) == 50, "el duplicado no sobrescribe");
		check(map.contains(5u) && !map.contains(6u), "contains");
		check(!map.insert_or_assign(5u, 55) && *map.find(5u) == 55, "insert_or_assign actualiza");
		check(map.insert_or_assign(6u, 66) && *map.find(6u) == 66, "insert_or_assign inserta");
		check(map.erase(5u) && !map.contains(5u) && map.size() == 1u, "erase");
		check(map.slots() >= 8u, "las tablas se reservaron");
	}

	// --- Crecimiento + estres contra un modelo de referencia ----------------
	{
		alignas(16) eng::u8 buf[256u * 1024u] {};
		eu::BumpAlloc alloc {eng::Span<eng::u8> {buf, sizeof(buf)}};
		eu::DynamicHashMap<eng::u16, int, eu::BumpAlloc> map {alloc};
		constexpr eng::usize kKeys = 1024;
		static bool ref_present[kKeys] = {};
		int ref_value[kKeys] = {};
		eng::usize ref_count = 0;
		Rng rng;

		for (int step = 0; step < 20000; ++step) {
			const eng::u16 k = static_cast<eng::u16>(rng.next() % kKeys);
			const eng::u32 op = rng.next() % 3u;
			if (op == 0u) {
				const int v = static_cast<int>(rng.next() % 1000u);
				const bool inserted = map.insert_or_assign(k, v);
				check(inserted == !ref_present[k], "insert_or_assign retorno");
				if (!ref_present[k]) {
					ref_present[k] = true;
					++ref_count;
				}
				ref_value[k] = v;
			} else if (op == 1u) {
				check(map.erase(k) == ref_present[k], "erase coincide");
				if (ref_present[k]) {
					ref_present[k] = false;
					--ref_count;
				}
			} else {
				const int* p = map.find(k);
				check((p != nullptr) == ref_present[k], "find coincide");
				if (ref_present[k]) {
					check(*p == ref_value[k], "valor coincide");
				}
			}
			check(map.size() == ref_count, "size coincide");
		}
		check(map.slots() >= 512u, "las tablas crecieron (rehash)");

		eng::usize seen = 0;
		map.for_each([&](eng::u16 key, int value) {
			++seen;
			check(ref_present[key] && ref_value[key] == value, "for_each coincide");
		});
		check(seen == ref_count, "for_each cuenta igual que size");
	}

	// --- Internado de cadenas (`StringView` como clave) ----------------------
	{
		alignas(16) eng::u8 buf[4096] {};
		eu::BumpAlloc alloc {eng::Span<eng::u8> {buf, sizeof(buf)}};
		eu::DynamicHashMap<eu::StringView, eng::u16, eu::BumpAlloc> intern {alloc};
		const char* names[] = {"hero", "enemy", "tile", "hero", "enemy"};
		eng::u16 next_id = 0;
		for (const char* name : names) {
			const eu::StringView sv {name};
			if (!intern.contains(sv)) {
				intern.insert(sv, next_id++);
			}
		}
		check(intern.size() == 3u, "internado deduplica por contenido");
		check(*intern.find(eu::StringView("hero")) == 0u, "id de hero");
		check(*intern.find(eu::StringView("tile")) == 2u, "id de tile");
		check(intern.find(eu::StringView("ghost")) == nullptr, "ausente");
	}

	// --- Sin allocator: no puede reservar ------------------------------------
	{
		eu::DynamicHashMap<eng::u16, int> map;
		check(map.insert(1u, 2) == nullptr, "NullAlloc: insert devuelve null");
		check(map.empty(), "NullAlloc: sigue vacío");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: DynamicHashMap validado.\n");
	return 0;
}
