// Test host de eng::amiga::OrBlobBatch (secuencia de registros del lote de BOBs OR
// intercalado portada de `DrawObject` de bobs3d). El lote es host-testable porque la base
// de registros se inyecta como parametro: aqui se pasa un array local.
#include <eng/platform/amiga/blob.hpp>

#include <cstdio>

using eng::amiga::OrBlobBatch;
using Reg = volatile eng::u16;

// Offsets (en palabras) de los registros custom que programa el lote. Deben coincidir
// con las constantes privadas de `blob.hpp` (0x040/2, 0x096/2, ...).
static constexpr eng::u16 kDmaconr = 0x002u / 2u;
static constexpr eng::u16 kDmacon = 0x096u / 2u;
static constexpr eng::u16 kBltcon0 = 0x040u / 2u;
static constexpr eng::u16 kBltcon1 = 0x042u / 2u;
static constexpr eng::u16 kBltafwm = 0x044u / 2u;
static constexpr eng::u16 kBltalwm = 0x046u / 2u;
static constexpr eng::u16 kBltbpt = 0x04cu / 2u;
static constexpr eng::u16 kBltapt = 0x050u / 2u;
static constexpr eng::u16 kBltdpt = 0x054u / 2u;
static constexpr eng::u16 kBltsize = 0x058u / 2u;
static constexpr eng::u16 kBltbmod = 0x062u / 2u;
static constexpr eng::u16 kBltamod = 0x064u / 2u;
static constexpr eng::u16 kBltdmod = 0x066u / 2u;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

static eng::u32 rd32(Reg* r, eng::u16 word) {
	return *reinterpret_cast<volatile eng::u32*>(&r[word]);
}

int main() {
	static Reg regs[0x100] {};
	OrBlobBatch batch;

	// begin: fija las constantes del lote una sola vez.
	batch.begin(regs, 3, 96, /*amod=*/0, /*dmod=*/26);
	check(regs[kDmacon] == static_cast<eng::u16>(0x8000u | 0x0200u | 0x0040u),
	      "DMACON = SET de MASTER|BLITTER (no toca BLTPRI)");
	check(regs[kBltcon1] == 0, "BLTCON1 = 0 (sin BSH; leccion de bobs3d)");
	check(regs[kBltafwm] == 0xffff && regs[kBltalwm] == 0xffff, "BLTAFWM/BLTALWM completos");
	check(regs[kBltamod] == 0, "BLTAMOD = 0 (atlas denso)");
	check(regs[kBltbmod] == 26 && regs[kBltdmod] == 26, "BLTBMOD = BLTDMOD = 26");
	check(regs[kBltsize] == 0, "begin no programa BLTSIZE");

	// one: un BOB con desplazamiento fino. Comprobamos el valor del puntero (extremo a
	// extremo, sin depender del orden de palabras) y el resto de campos.
	const void* src = reinterpret_cast<const void*>(static_cast<eng::uintptr>(0x12345000u));
	void* dst = reinterpret_cast<void*>(static_cast<eng::uintptr>(0x00abcd00u));
	batch.one(src, dst, 5u);

	const eng::u16 expected_con0 =
		static_cast<eng::u16>((5u << 12u) | 0x0800u | 0x0400u | 0x0100u | 0x00fcu);
	check(regs[kBltcon0] == expected_con0, "BLTCON0 = ASH(5) | A|B|D | A_OR_B");
	check(regs[kBltsize] == static_cast<eng::u16>((96u << 6u) | 3u), "BLTSIZE = (96<<6)|3");
	check(rd32(regs, kBltapt) == 0x12345000u, "BLTAPT = source");
	check(rd32(regs, kBltbpt) == 0x00abcd00u, "BLTBPT = dest");
	check(rd32(regs, kBltdpt) == 0x00abcd00u, "BLTDPT = dest");

	// Un segundo BOB con otro shift reescribe BLTCON0 sin tocar las constantes.
	batch.one(src, dst, 0u);
	check(regs[kBltcon0] == static_cast<eng::u16>(0x0800u | 0x0400u | 0x0100u | 0x00fcu),
	      "BLTCON0 sin shift");
	check(regs[kBltamod] == 0 && regs[kBltbmod] == 26, "constantes intactas entre BOBs");

	check(batch.end(), "end() devuelve true");

	if (failures == 0) {
		std::printf("OK: OrBlobBatch (secuencia de registros del lote) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
