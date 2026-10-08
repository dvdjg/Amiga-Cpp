// Test host de eng::amiga::BlobBatch (secuencia de registros del lote de blobs con estado
// fijo, generalizacion de OrBlobBatch a OR/cookie-cut/opaco). El lote es host-testable
// porque el bloque de registros se inyecta **tipado** (`HwRegs::for_test` sobre un
// array local) y los punteros de Blitter son `BlitPtr` (direccion Chip). Los pares de
// registros que el motor escribe empaquetados (CON0/CON1, AFWM/ALWM, modulos, punteros) se
// leen como u32 nativo: el mock es RAM del host.
#include <eng/platform/amiga/blob_batch.hpp>
#include <eng/platform/amiga/blob.hpp>
#include <eng/api/screen.hpp>

#include <cstdio>
#include <type_traits>

using eng::amiga::BlobBatch;
using eng::amiga::BlobOp;
using eng::amiga::HwRegs;
using eng::graphics::BlitPtr;

// --- Gate de tipos DMA (compile-time) -----------------------------------------------------------------
// Las APIs que entregan memoria al Blitter **no** deben aceptar punteros crudos: si alguien relaja una
// firma a `const void*`/`void*`, los conceptos de abajo pasan a satisfacerse y los `static_assert`
// dejan de compilar (el test entero falla). Es el gate estático del invariante «DMA en Chip».
// (`AmigaBackend` no es host-incluible —necesita los headers del SDK—; su firma `BlitPtr` se vigila
// desde los builds de Amiga de las demos, que no compilarían con el helper `dma()` ya eliminado.)
template <class B>
concept AceptaPunteroCrudoEnOne = requires(B& b) {
	b.one(static_cast<const void*>(nullptr), static_cast<const void*>(nullptr), static_cast<void*>(nullptr), eng::u8{});
};
static_assert(!AceptaPunteroCrudoEnOne<BlobBatch>, "BlobBatch::one debe recibir BlitPtr, no punteros crudos");
static_assert(!AceptaPunteroCrudoEnOne<eng::amiga::OrBlobBatch>, "OrBlobBatch::one debe recibir BlitPtr, no punteros crudos");
// En positivo: la firma tipada sí compila (la racha del Screen y el lote de BOBs).
static_assert(requires(BlobBatch& b, BlitPtr p) { b.one(p, p, p, eng::u8{}); }, "BlobBatch::one(BlitPtr) debe compilar");
static_assert(std::is_same_v<decltype(eng::BlitStream {}.one),
			     void (*)(void*, eng::graphics::BlitPtr, eng::graphics::BlitPtr, eng::graphics::BlitPtr, eng::u8)>,
	      "BlitStream::one debe usar BlitPtr (misma firma que el backend)");
static_assert(std::is_same_v<decltype(eng::graphics::OrBob {}.source), BlitPtr>, "OrBob::source debe ser BlitPtr");
static_assert(std::is_same_v<decltype(eng::graphics::OrBob {}.dest), BlitPtr>, "OrBob::dest debe ser BlitPtr");
static_assert(std::is_same_v<decltype(eng::graphics::BlitJob {}.source), BlitPtr>, "BlitJob::source debe ser BlitPtr");

// Offsets (en palabras) de los registros custom que programa el lote. Deben coincidir
// con las constantes privadas de `blob_batch.hpp` (0x040/2, 0x096/2, ...).
static constexpr eng::u16 kDmaconr = 0x002u / 2u;
static constexpr eng::u16 kDmacon = 0x096u / 2u;
static constexpr eng::u16 kBltcon0 = 0x040u / 2u;
static constexpr eng::u16 kBltafwm = 0x044u / 2u;
static constexpr eng::u16 kBltalwm = 0x046u / 2u;
static constexpr eng::u16 kBltcpt = 0x048u / 2u;
static constexpr eng::u16 kBltbpt = 0x04cu / 2u;
static constexpr eng::u16 kBltapt = 0x050u / 2u;
static constexpr eng::u16 kBltdpt = 0x054u / 2u;
static constexpr eng::u16 kBltsize = 0x058u / 2u;
static constexpr eng::u16 kBltcmod = 0x060u / 2u;
static constexpr eng::u16 kBltamod = 0x064u / 2u;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

static eng::u32 rd32(volatile eng::u16* r, eng::u16 word) {
	// Los pares empaquetados se escriben con una unica escritura nativa de 32 bits
	// (el `move.l` del original); en el mock de host se leen nativas.
	return *reinterpret_cast<volatile eng::u32*>(&r[word]);
}

/// Puntero de prueba: direccion fija en Chip (frontera declarada del test host).
static BlitPtr chip_ptr(eng::uintptr addr) {
	return BlitPtr {eng::Address<eng::MemoryKind::Chip> {addr}};
}

static volatile eng::u16 regs[0x100] {};

int main() {
	BlobBatch batch;
	const HwRegs cregs = HwRegs::for_test(regs);

	// --- Cookie-cut interleaved (el caso de la demo 213): A=mascara, B=imagen, C=D=fondo.
	// Geometria: 2 palabras por fila, 80 filas fisicas (16 logicas x 5 planos).
	batch.begin(cregs, BlobOp::CookieCut, /*words=*/2, /*height=*/80, /*amod=*/4, /*bmod=*/4,
		    /*cmod=*/36, /*dmod=*/36);
	check(regs[kDmacon] == static_cast<eng::u16>(0x8000u | 0x0200u | 0x0040u),
	      "DMACON = SET de MASTER|BLITTER (no toca BLTPRI)");
	check(rd32(regs, kBltcon0) == (static_cast<eng::u32>(0x0f00u | 0x00cau) << 16u),
	      "begin: CON0 = A|B|C|D | $CA y CON1 = 0 (empaquetados)");
	check(regs[kBltafwm] == 0xffff && regs[kBltalwm] == 0xffff, "BLTAFWM/BLTALWM completos");
	check(rd32(regs, kBltamod) == ((4u << 16u) | 36u), "modulos (AMOD,DMOD) empaquetados");
	check(rd32(regs, kBltcmod) == ((36u << 16u) | 4u), "modulos (CMOD,BMOD) empaquetados");
	check(regs[kBltsize] == 0, "begin no programa BLTSIZE");

	const BlitPtr mask = chip_ptr(0x20000000u);
	const BlitPtr image = chip_ptr(0x20000004u);
	const BlitPtr dest = chip_ptr(0x00c00000u);
	batch.one(mask, image, dest, /*shift=*/5u);

	// BLTCON0 = ASH(5) | A|B|C|D | cookie-cut($CA) = 0x5000 | 0x0F00 | 0x00CA = 0x5FCA;
	// BLTCON1 = BSH(5) = 0x5000 (par empaquetado en una escritura).
	check(rd32(regs, kBltcon0) == ((static_cast<eng::u32>(0x5fcau) << 16u) | 0x5000u),
	      "BLTCON0 = ASH(5) | A|B|C|D | $CA y BLTCON1 = BSH(5)");
	check(regs[kBltsize] == static_cast<eng::u16>((80u << 6u) | 2u), "BLTSIZE = (80<<6)|2");
	check(rd32(regs, kBltapt) == 0x20000000u, "BLTAPT = mascara");
	check(rd32(regs, kBltbpt) == 0x20000004u, "BLTBPT = imagen");
	check(rd32(regs, kBltcpt) == 0x00c00000u, "BLTCPT = destino");
	check(rd32(regs, kBltdpt) == 0x00c00000u, "BLTDPT = destino");

	// Un segundo blob con otro shift reescribe BLTCON0/1 sin tocar las constantes.
	batch.one(mask, image, dest, /*shift=*/0u);
	check(rd32(regs, kBltcon0) == (static_cast<eng::u32>(0x0fcau) << 16u),
	      "BLTCON0 sin shift (CON1 = 0)");
	check(rd32(regs, kBltamod) == ((4u << 16u) | 36u), "constantes intactas entre blobs");

	// --- OR (D = A | D, B = D = destino), como bobs3d.
	batch.begin(cregs, BlobOp::Or, /*words=*/3, /*height=*/96, /*amod=*/0, /*bmod=*/26,
		    /*cmod=*/0, /*dmod=*/26);
	batch.one(image, dest, dest, /*shift=*/7u);
	check(rd32(regs, kBltcon0) ==
		      (static_cast<eng::u32>((7u << 12u) | 0x0800u | 0x0400u | 0x0100u | 0x00fcu)
		       << 16u),
	      "OR: BLTCON0 = ASH(7) | A|B|D | $FC (CON1 = 0)");
	check(rd32(regs, kBltbpt) == 0x00c00000u, "OR: BLTBPT = destino");

	// --- Opaco (D = A, sin mascara).
	batch.begin(cregs, BlobOp::Opaque, /*words=*/2, /*height=*/16, /*amod=*/0, /*bmod=*/0,
		    /*cmod=*/0, /*dmod=*/0);
	batch.one(image, dest, dest, /*shift=*/0u);
	check(rd32(regs, kBltcon0) == (static_cast<eng::u32>(0x0800u | 0x0100u | 0x00f0u) << 16u),
	      "Opaco: BLTCON0 = A|D | $F0");

	// --- Clear (D = 0, solo canal D), para rachas de borrado homogeneas.
	batch.begin(cregs, BlobOp::Clear, /*words=*/20, /*height=*/56, /*amod=*/0, /*bmod=*/0,
		    /*cmod=*/0, /*dmod=*/0);
	batch.one(BlitPtr {}, BlitPtr {}, dest, /*shift=*/0u);
	check(rd32(regs, kBltcon0) == (static_cast<eng::u32>(0x0100u) << 16u),
	      "Clear: BLTCON0 = D | minterm 0 ($00)");
	check(rd32(regs, kBltdpt) == 0x00c00000u, "Clear: BLTDPT = destino");
	check(regs[kBltsize] == static_cast<eng::u16>((56u << 6u) | 20u), "Clear: BLTSIZE = (56<<6)|20");

	// --- Copy (D = C, copia recta; C = origen).
	const BlitPtr copy_src = chip_ptr(0x20000040u);
	batch.begin(cregs, BlobOp::Copy, /*words=*/10, /*height=*/20, /*amod=*/0, /*bmod=*/0,
		    /*cmod=*/38, /*dmod=*/38);
	batch.one(copy_src, BlitPtr {}, dest, /*shift=*/0u);
	check(rd32(regs, kBltcon0) ==
		      (static_cast<eng::u32>(0x0200u | 0x0100u | 0x00aau) << 16u),
	      "Copy: BLTCON0 = C|D | $AA");
	check(rd32(regs, kBltcpt) == 0x20000040u, "Copy: BLTCPT = origen");
	check(rd32(regs, kBltdpt) == 0x00c00000u, "Copy: BLTDPT = destino");
	check(regs[kBltsize] == static_cast<eng::u16>((20u << 6u) | 10u), "Copy: BLTSIZE = (20<<6)|10");

	check(batch.end(), "end() devuelve true");

	if (failures == 0) {
		std::printf("OK: BlobBatch (secuencia de registros OR/cookie-cut/opaco/clear) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
