// ============================================================================
// Test HOST-111: blackboard de IA (memoria compartida, capacidad fija)
// ============================================================================
//
// Valida `engine/include/eng/ai/decision/blackboard.hpp`: `Blackboard<Key, Value,
// MaxKeys>` indexa creencias por un enum denso con `find` O(1), sin heap.
//
//   1) set/find/contains/get_or/erase/clear y `size`.
//   2) Sobrescritura de una creencia.
//   3) Valores de tipo struct (no solo escalares).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ai/111_blackboard

#include <cstdio>

#include <eng/ai/decision/blackboard.hpp>

namespace {

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

enum class Belief : eng::u16 { Target, LastSeen, Ammo, Alerted, Count };
using Beliefs = eng::ai::Blackboard<Belief, eng::s32, static_cast<eng::usize>(Belief::Count)>;

void test_scalars() {
	Beliefs bb;
	check(bb.empty() && bb.size() == 0u, "blackboard: arranca vacio");

	bb.set(Belief::Ammo, 30);
	check(bb.contains(Belief::Ammo), "blackboard: contiene la clave escrita");
	check(bb.size() == 1u, "blackboard: una creencia");
	if (const eng::s32* ammo = bb.find(Belief::Ammo)) {
		check(*ammo == 30, "blackboard: find devuelve el valor");
	} else {
		check(false, "blackboard: find no deberia ser null");
	}
	check(bb.find(Belief::Target) == nullptr, "blackboard: clave no puesta -> null");
	check(bb.get_or(Belief::Ammo, -1) == 30, "blackboard: get_or devuelve el valor");
	check(bb.get_or(Belief::Alerted, -1) == -1, "blackboard: get_or usa el fallback");

	bb.set(Belief::Ammo, 10);
	check(bb.get_or(Belief::Ammo, -1) == 10, "blackboard: set sobrescribe");
	check(bb.size() == 1u, "blackboard: sobrescribir no duplica");

	check(bb.erase(Belief::Ammo), "blackboard: erase borra la creencia");
	check(!bb.erase(Belief::Ammo), "blackboard: erase de ausente devuelve false");
	check(!bb.contains(Belief::Ammo) && bb.empty(), "blackboard: tras erase queda vacio");

	bb.set(Belief::Target, 1);
	bb.set(Belief::Alerted, 1);
	check(bb.size() == 2u, "blackboard: dos creencias");
	bb.clear();
	check(bb.empty(), "blackboard: clear vacia");
}

struct Target {
	eng::s16 x;
	eng::s16 y;
	eng::s16 hp;
};

void test_struct_values() {
	using Targets = eng::ai::Blackboard<Belief, Target, static_cast<eng::usize>(Belief::Count)>;
	Targets bb;
	bb.set(Belief::Target, Target {120, 40, 3});
	const Target* t = bb.find(Belief::Target);
	check(t != nullptr && t->x == 120 && t->y == 40 && t->hp == 3,
	      "blackboard: almacena structs del juego");
	check(bb.get_or(Belief::LastSeen, Target {}).x == 0, "blackboard: fallback de struct");
}

} // namespace

int main() {
	std::printf("Blackboard:\n");
	test_scalars();
	test_struct_values();

	if (g_fail == 0u) {
		std::printf("OK: Blackboard (set/find/get_or/erase/clear, structs)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
