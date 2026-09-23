// ============================================================================
// Test HOST-020: tabla de seno EXACTA del original (eng/core/math/sinetable.hpp).
// ============================================================================
//
// La tabla se genera desde `libmisc/sintab.c` (reconstruyendo los deltas). Aqui se
// fijan invariantes + checksum de regresion y se comprueba que `math2d` la usa.
//
//   bash tools/run-host-tests.sh tests/host/core/020_sinetable

#include <eng/retro/fixed_trig.hpp>
#include <cstdio>

#include <eng/retro/lib2d.hpp>
#include <eng/retro/sintab.hpp>
#include <eng/core/types/types.hpp>
#include <eng/retro/fixed_q.hpp>
using namespace eng::retro;

namespace {
using eng::s16;
using eng::u16;
} // namespace

int main() {
	// Invariantes exactos.
	if (eng::retro::kSinTab[0] != 0 || eng::retro::kSinTab[1024] != 4096 ||
	    eng::retro::kSinTab[2048] != 0 || eng::retro::kSinTab[3072] != -4096) {
		std::printf("[FAIL] invariantes (0/1024/2048/3072)\n");
		return 1;
	}
	// Simetria: sin(4096 - i) == -sin(i) (la tabla es impar respecto a 2048).
	for (int i = 1; i < 4096; ++i) {
		if (eng::retro::kSinTab[(4096 - i) & 4095] != -eng::retro::kSinTab[i]) {
			std::printf("[FAIL] simetria en i=%d\n", i);
			return 1;
		}
	}
	// Cuadrante [0, 1024] monotono no decreciente.
	for (int i = 1; i <= 1024; ++i) {
		if (eng::retro::kSinTab[i] < eng::retro::kSinTab[i - 1]) {
			std::printf("[FAIL] no monotono en i=%d\n", i);
			return 1;
		}
	}
	// Checksum de regresion (fija la tabla byte a byte).
	eng::s32 chk = 0;
	for (int i = 0; i < 4096; ++i) chk = (chk * 31 + eng::retro::kSinTab[i]) | 0;
	if (chk != 1083047936) {
		std::printf("[FAIL] checksum=%d (esperado 1083047936)\n", (int)chk);
		return 1;
	}
	// `math2d` usa la tabla exacta.
	if (eng::retro::sin(turns(512)).v != eng::retro::kSinTab[512] ||
	    eng::retro::cos(turns(0)).v != eng::retro::kSinTab[1024]) {
		std::printf("[FAIL] math2d no usa kSinTab\n");
		return 1;
	}
	// Valor concreto conocido de la tabla exacta (i=512: sin(pi/4)*4096 truncado).
	if (eng::retro::kSinTab[512] != 2896) {
		std::printf("[FAIL] kSinTab[512]=%d (esperado 2896)\n", eng::retro::kSinTab[512]);
		return 1;
	}

	std::printf("OK: sintab exacta del original validada (invariantes + checksum + math2d).\n");
	return 0;
}
