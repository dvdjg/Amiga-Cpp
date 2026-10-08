// Test host de eng::amiga::OrBlobBatch (secuencia de registros del lote de BOBs OR
// intercalado portada de `DrawObject` de bobs3d). El lote es host-testable porque el bloque
// de registros se inyecta **tipado** (`CustomRegs::from_storage` sobre un array local) y los
// punteros de Blitter son `BlitPtr` (direccion Chip). Los pares de punteros se escriben con
// una unica escritura nativa de 32 bits y se leen nativas (el mock es RAM del host).
#include <eng/platform/amiga/blob.hpp>

#include <cstdio>

using eng::amiga::CustomRegs;
using eng::amiga::OrBlobBatch;
using eng::graphics::BlitPtr;

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

static eng::u32 rd32(volatile eng::u16* r, eng::u16 word) {
	return *reinterpret_cast<volatile eng::u32*>(&r[word]);
}

/// Puntero de prueba: direccion fija en Chip (frontera declarada del test host).
static BlitPtr chip_ptr(eng::uintptr addr) {
	return BlitPtr {eng::Address<eng::MemoryKind::Chip> {addr}};
}

static volatile eng::u16 regs[0x100] {};

// Servicio de espera de prueba: cuenta las vueltas y, a la 3.a, simula que el Blitter acaba
// bajando BBUSY. Verifica que `wait()` drena el fondo en vez de perder ciclos en el sondeo.
static int g_drained = 0;
static eng::u16 g_last_vpos = 0xffffu;
static void drain_stub(void*, eng::u16 vpos) {
	++g_drained;
	g_last_vpos = vpos;
	if (g_drained >= 3) {
		regs[kDmaconr] = static_cast<eng::u16>(regs[kDmaconr] & 0xBfffu);
	}
}

int main() {
	OrBlobBatch batch;
	const CustomRegs cregs = CustomRegs::from_storage(regs);

	// begin: fija las constantes del lote una sola vez.
	batch.begin(cregs, 3, 96, /*amod=*/0, /*dmod=*/26);
	check(regs[kDmacon] == static_cast<eng::u16>(0x8000u | 0x0200u | 0x0040u),
	      "DMACON = SET de MASTER|BLITTER (no toca BLTPRI)");
	check(regs[kBltcon1] == 0, "BLTCON1 = 0 (sin BSH; leccion de bobs3d)");
	check(regs[kBltafwm] == 0xffff && regs[kBltalwm] == 0xffff, "BLTAFWM/BLTALWM completos");
	check(regs[kBltamod] == 0, "BLTAMOD = 0 (atlas denso)");
	check(regs[kBltbmod] == 26 && regs[kBltdmod] == 26, "BLTBMOD = BLTDMOD = 26");
	check(regs[kBltsize] == 0, "begin no programa BLTSIZE");

	// one: un BOB con desplazamiento fino. Comprobamos el valor del puntero (extremo a
	// extremo) y el resto de campos.
	const BlitPtr src = chip_ptr(0x12345000u);
	const BlitPtr dst = chip_ptr(0x00abcd00u);
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

	// Espera con servicio de fondo: con BBUSY puesto, el lote llama al servicio en cada vuelta
	// hasta que el Blitter (simulado) lo baja. El servicio se registra en `begin`.
	regs[kDmaconr] = 0x4000u; // Blitter "ocupado"
	g_drained = 0;
	batch.begin(cregs, 3, 96, /*amod=*/0, /*dmod=*/26, drain_stub, nullptr);
	batch.end();
	check(g_drained == 3, "wait() drena el servicio de fondo hasta que BBUSY baja");
	check((regs[kDmaconr] & 0x4000u) == 0u, "BBUSY queda bajo (el Blitter 'acabo')");

	if (failures == 0) {
		std::printf("OK: OrBlobBatch (secuencia de registros del lote) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
