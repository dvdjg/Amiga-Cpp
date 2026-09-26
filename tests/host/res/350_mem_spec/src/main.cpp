// ============================================================================
// Test HOST-350: peticion de memoria en 2 ejes (eng::MemSpec) + resolve_bank.
// ============================================================================
//
// Respalda `eng/memory/mem_spec.hpp`: `MemSpec { MemReq, MemHint }` (requisito duro +
// preferencia blanda) resuelto a un banco fisico por `resolve_bank` segun disponibilidad. Cubre
// las combinaciones (Chip solo Chip; NonChip nunca Chip; Any preserva Chip) y los fallbacks.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/350_mem_spec

#include <cstdio>

#include <eng/memory/memory_manager.hpp>

namespace {

int g_fail = 0;
using A = eng::MemAvail;
using R = eng::MemReq;
using H = eng::MemHint;
using K = eng::MemoryKind;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

constexpr eng::MemoryKind rb(R req, H hint, A a) {
	return eng::resolve_bank(eng::MemSpec {req, hint}, a);
}

} // namespace

int main() {
	std::printf("== HOST-350 mem_spec ==\n");

	const A full {true, true, true};
	check(rb(R::Chip, H::None, full) == K::Chip, "Chip+None -> Chip");
	check(rb(R::Chip, H::Fast, full) == K::Chip, "Chip+Fast -> Chip");
	check(rb(R::Chip, H::Slow, full) == K::Chip, "Chip+Slow -> Chip");
	check(rb(R::NonChip, H::None, full) == K::Fast, "NonChip+None -> Fast");
	check(rb(R::NonChip, H::Fast, full) == K::Fast, "NonChip+Fast -> Fast");
	check(rb(R::NonChip, H::Slow, full) == K::Slow, "NonChip+Slow -> Slow");
	check(rb(R::Any, H::None, full) == K::Fast, "Any+None -> Fast (preserva Chip)");
	check(rb(R::Any, H::Fast, full) == K::Fast, "Any+Fast -> Fast");
	check(rb(R::Any, H::Slow, full) == K::Slow, "Any+Slow -> Slow");

	// Solo Chip (A500): NonChip no tiene a donde ir; Any cae a Chip.
	const A only_chip {true, false, false};
	check(rb(R::Chip, H::None, only_chip) == K::Chip, "Chip con chip");
	check(rb(R::NonChip, H::Fast, only_chip) == K::Any, "NonChip sin Fast/Slow -> ninguno");
	check(rb(R::Any, H::Fast, only_chip) == K::Chip, "Any -> Chip (unica)");

	// Sin Fast: los hints caen a Slow.
	const A chip_slow {true, true, false};
	check(rb(R::NonChip, H::Fast, chip_slow) == K::Slow, "NonChip+Fast sin Fast -> Slow");
	check(rb(R::Any, H::Fast, chip_slow) == K::Slow, "Any+Fast sin Fast -> Slow");
	check(rb(R::Any, H::Slow, chip_slow) == K::Slow, "Any+Slow -> Slow");

	// Chip ausente: el requisito Chip no se cumple.
	const A no_chip {false, true, true};
	check(rb(R::Chip, H::None, no_chip) == K::Any, "Chip sin chip -> ninguno");

	// Conexion con MemoryManager (via runtime, no-DMA).
	eng::u8 chip_buf[256] {};
	eng::u8 slow_buf[128] {};
	eng::MemoryManager mm {};
	(void)mm.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), nullptr, 0u);
	const auto b = mm.allocate(eng::MemSpec {R::Any, H::Fast}, 64u);
	check(b.valid() && b.kind == K::Slow, "MemoryManager: Any+Fast sin Fast -> Slow");
	const auto nc = mm.allocate(eng::MemSpec {R::NonChip, H::None}, 32u);
	check(nc.valid() && nc.kind != K::Chip, "MemoryManager: NonChip nunca Chip");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: MemSpec/resolve_bank (2 ejes + fallbacks) validado.\n");
	return 0;
}
