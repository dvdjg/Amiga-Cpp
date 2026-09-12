// ============================================================================
// Test HOST-021: datos del plasma (tablas + paleta) fieles al original.
// ============================================================================
//
// Fija los checksums de `tab1/2/3` (calculados desde la sintab EXACTA del original,
// con el `>>16` aritmetico y el envolvimiento a `s8`) y de la paleta (256 RGB12).
//
//   bash tools/run-host-tests.sh tests/host/021_plasma_tables

#include <cstdio>

#include "../../../../demos/amiga/082_plasma/src/data/plasma_colors.hpp"
#include "../../../../demos/amiga/082_plasma/src/data/plasma_tables.hpp"

int main() {
	const auto& t = plasma_data::kTables;
	eng::s32 c1 = 0, c2 = 0, c3 = 0;
	for (int i = 0; i < 256; ++i) {
		c1 = (c1 * 31 + t.tab1[i]) | 0;
		c2 = (c2 * 31 + t.tab2[i]) | 0;
		c3 = (c3 * 31 + t.tab3[i]) | 0;
	}
	if (c1 != -425422980 || c2 != -1961918596 || c3 != 753422204) {
		std::printf("[FAIL] checksums tablas: %d %d %d\n", (int)c1, (int)c2, (int)c3);
		return 1;
	}
	// Valores concretos (rad2 = i*32: 64 -> sin=0, cos=-4096).
	if (t.tab1[64] != 0 || t.tab2[64] != -93 || t.tab3[64] != 0) {
		std::printf("[FAIL] tab[64]: %d %d %d\n", (int)t.tab1[64], (int)t.tab2[64], (int)t.tab3[64]);
		return 1;
	}

	eng::s32 cp = 0;
	for (int i = 0; i < 256; ++i) cp = (cp * 31 + plasma_data::kColors[i]) | 0;
	if (cp != 402750400) {
		std::printf("[FAIL] checksum paleta: %d\n", (int)cp);
		return 1;
	}
	// Primer color exacto (0x03bf; el resto lo fija el checksum).
	if (plasma_data::kColors[0] != 0x03bfu) {
		std::printf("[FAIL] paleta[0]=%04x\n", plasma_data::kColors[0]);
		return 1;
	}

	std::printf("OK: datos del plasma (tab1/2/3 + paleta 256) fieles al original.\n");
	return 0;
}
