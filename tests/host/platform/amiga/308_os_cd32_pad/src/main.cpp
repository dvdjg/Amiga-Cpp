// ============================================================================
// Test HOST-308: decodificador del pad CD32 (eng/os/input.hpp, M2).
// ============================================================================
//
// Valida `cd32_mask_from_shift`: los 8 bits serie del registro de desplazamiento (activo a 0;
// bit0 = primer bit = Blue) se mapean al bitmask estable `Cd32Btn`. La parte de hardware (reloj por
// CIA-A PRA bit 7 + dato en POTINP, `os::enable_cd32_pad`) se valida en emulador.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/platform/amiga/308_os_cd32_pad

#include <cstdio>

#include <eng/os/input.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", m);
		++g_fail;
	}
}

} // namespace

int main() {
	using namespace eng::os;

	// Ningún botón: los 7 bits a 1 (no pulsado).
	check(cd32_mask_from_shift(0x7fu) == 0u, "sin botones -> mask 0");

	// Cada botón por separado (bit i a 0).
	check(cd32_mask_from_shift(0x7fu & ~0x01u) == Cd32Blue, "blue");
	check(cd32_mask_from_shift(0x7fu & ~0x02u) == Cd32Red, "red");
	check(cd32_mask_from_shift(0x7fu & ~0x04u) == Cd32Yellow, "yellow");
	check(cd32_mask_from_shift(0x7fu & ~0x08u) == Cd32Green, "green");
	check(cd32_mask_from_shift(0x7fu & ~0x10u) == Cd32Forward, "forward");
	check(cd32_mask_from_shift(0x7fu & ~0x20u) == Cd32Reverse, "reverse");
	check(cd32_mask_from_shift(0x7fu & ~0x40u) == Cd32Play, "play");

	// Todos pulsados.
	check(cd32_mask_from_shift(0x00u) ==
	          (Cd32Blue | Cd32Red | Cd32Yellow | Cd32Green | Cd32Forward | Cd32Reverse | Cd32Play),
	      "todos los botones");

	// Combinación: Blue + Green + Play.
	check(cd32_mask_from_shift(0x7fu & ~(0x01u | 0x08u | 0x40u)) ==
	          (Cd32Blue | Cd32Green | Cd32Play),
	      "combinacion");

	// El bit 7 (firma) no afecta al bitmask de botones.
	check(cd32_mask_from_shift(0xffu) == cd32_mask_from_shift(0x7fu), "la firma no aporta botones");

	if (g_fail == 0) {
		std::printf("OK: decodificador del pad CD32 (bitmask desde el stream serie) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
