// Test host de eng::amiga::BlobBatch (secuencia de registros del lote de blobs con estado
// fijo, generalizacion de OrBlobBatch a OR/cookie-cut/opaco). El lote es host-testable
// porque la base de registros se inyecta como parametro: aqui se pasa un array local.
#include <eng/platform/amiga/blob_batch.hpp>

#include <cstdio>

using eng::amiga::BlobBatch;
using eng::amiga::BlobOp;
using Reg = volatile eng::u16;

// Offsets (en palabras) de los registros custom que programa el lote. Deben coincidir
// con las constantes privadas de `blob_batch.hpp` (0x040/2, 0x096/2, ...).
static constexpr eng::u16 kDmaconr = 0x002u / 2u;
static constexpr eng::u16 kDmacon = 0x096u / 2u;
static constexpr eng::u16 kBltcon0 = 0x040u / 2u;
static constexpr eng::u16 kBltcon1 = 0x042u / 2u;
static constexpr eng::u16 kBltafwm = 0x044u / 2u;
static constexpr eng::u16 kBltalwm = 0x046u / 2u;
static constexpr eng::u16 kBltcpt = 0x048u / 2u;
static constexpr eng::u16 kBltbpt = 0x04cu / 2u;
static constexpr eng::u16 kBltapt = 0x050u / 2u;
static constexpr eng::u16 kBltdpt = 0x054u / 2u;
static constexpr eng::u16 kBltsize = 0x058u / 2u;
static constexpr eng::u16 kBltcmod = 0x060u / 2u;
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
	// El mock modela registros del Amiga (**big-endian**): la palabra alta va en la dirección
	// menor (`PTH`), la baja en `word+1` (`PTL`). El motor escribe los punteros con dos stores de
	// 16 bits (alto primero); leer como `u32` nativo en el host (little-endian) los invertiría.
	return static_cast<eng::u32>((static_cast<eng::u32>(r[word]) << 16u) | r[static_cast<eng::u16>(word + 1u)]);
}

static Reg regs[0x100] {};

int main() {
	BlobBatch batch;

	// --- Cookie-cut interleaved (el caso de la demo 213): A=mascara, B=imagen, C=D=fondo.
	// Geometria: 2 palabras por fila, 80 filas fisicas (16 logicas x 5 planos).
	batch.begin(regs, BlobOp::CookieCut, /*words=*/2, /*height=*/80, /*amod=*/4, /*bmod=*/4,
		    /*cmod=*/36, /*dmod=*/36);
	check(regs[kDmacon] == static_cast<eng::u16>(0x8000u | 0x0200u | 0x0040u),
	      "DMACON = SET de MASTER|BLITTER (no toca BLTPRI)");
	check(regs[kBltcon1] == 0, "begin no fija BLTCON1 (lo fija `one` con BSH)");
	check(regs[kBltafwm] == 0xffff && regs[kBltalwm] == 0xffff, "BLTAFWM/BLTALWM completos");
	check(regs[kBltamod] == 4 && regs[kBltbmod] == 4, "BLTAMOD = BLTBMOD = 4");
	check(regs[kBltcmod] == 36 && regs[kBltdmod] == 36, "BLTCMOD = BLTDMOD = 36");
	check(regs[kBltsize] == 0, "begin no programa BLTSIZE");

	const void* mask = reinterpret_cast<const void*>(static_cast<eng::uintptr>(0x20000000u));
	const void* image = reinterpret_cast<const void*>(static_cast<eng::uintptr>(0x20000004u));
	void* dest = reinterpret_cast<void*>(static_cast<eng::uintptr>(0x00c00000u));
	batch.one(mask, image, dest, /*shift=*/5u);

	// BLTCON0 = ASH(5) | A|B|C|D | cookie-cut($CA) = 0x5000 | 0x0F00 | 0x00CA = 0x5FCA
	check(regs[kBltcon0] == static_cast<eng::u16>(0x5fcau), "BLTCON0 = ASH(5) | A|B|C|D | $CA");
	check(regs[kBltcon1] == static_cast<eng::u16>(5u << 12u), "BLTCON1 = BSH(5)");
	check(regs[kBltsize] == static_cast<eng::u16>((80u << 6u) | 2u), "BLTSIZE = (80<<6)|2");
	check(rd32(regs, kBltapt) == 0x20000000u, "BLTAPT = mascara");
	check(rd32(regs, kBltbpt) == 0x20000004u, "BLTBPT = imagen");
	check(rd32(regs, kBltcpt) == 0x00c00000u, "BLTCPT = destino");
	check(rd32(regs, kBltdpt) == 0x00c00000u, "BLTDPT = destino");

	// Un segundo blob con otro shift reescribe BLTCON0/1 sin tocar las constantes.
	batch.one(mask, image, dest, /*shift=*/0u);
	check(regs[kBltcon0] == static_cast<eng::u16>(0x0fcau), "BLTCON0 sin shift");
	check(regs[kBltcon1] == 0u, "BLTCON1 sin shift");
	check(regs[kBltamod] == 4 && regs[kBltcmod] == 36, "constantes intactas entre blobs");

	// --- OR (D = A | D, B = D = destino), como bobs3d.
	batch.begin(regs, BlobOp::Or, /*words=*/3, /*height=*/96, /*amod=*/0, /*bmod=*/26,
		    /*cmod=*/0, /*dmod=*/26);
	batch.one(image, dest, dest, /*shift=*/7u);
	check(regs[kBltcon0] ==
		      static_cast<eng::u16>((7u << 12u) | 0x0800u | 0x0400u | 0x0100u | 0x00fcu),
	      "OR: BLTCON0 = ASH(7) | A|B|D | $FC");
	check(regs[kBltcon1] == 0u, "OR: BLTCON1 = 0 (sin BSH)");
	check(rd32(regs, kBltbpt) == 0x00c00000u, "OR: BLTBPT = destino");

	// --- Opaco (D = A, sin mascara).
	batch.begin(regs, BlobOp::Opaque, /*words=*/2, /*height=*/16, /*amod=*/0, /*bmod=*/0,
		    /*cmod=*/0, /*dmod=*/0);
	batch.one(image, dest, dest, /*shift=*/0u);
	check(regs[kBltcon0] == static_cast<eng::u16>(0x0800u | 0x0100u | 0x00f0u),
	      "Opaco: BLTCON0 = A|D | $F0");

	// --- Clear (D = 0, solo canal D), para rachas de borrado homogeneas.
	batch.begin(regs, BlobOp::Clear, /*words=*/20, /*height=*/56, /*amod=*/0, /*bmod=*/0,
		    /*cmod=*/0, /*dmod=*/0);
	batch.one(nullptr, nullptr, dest, /*shift=*/0u);
	check(regs[kBltcon0] == static_cast<eng::u16>(0x0100u | 0x0000u),
	      "Clear: BLTCON0 = D | minterm 0 ($00)");
	check(rd32(regs, kBltdpt) == 0x00c00000u, "Clear: BLTDPT = destino");
	check(regs[kBltsize] == static_cast<eng::u16>((56u << 6u) | 20u), "Clear: BLTSIZE = (56<<6)|20");

	// --- Copy (D = C, copia recta; C = origen).
	const void* copy_src = reinterpret_cast<const void*>(static_cast<eng::uintptr>(0x20000040u));
	batch.begin(regs, BlobOp::Copy, /*words=*/10, /*height=*/20, /*amod=*/0, /*bmod=*/0,
		    /*cmod=*/38, /*dmod=*/38);
	batch.one(copy_src, nullptr, dest, /*shift=*/0u);
	check(regs[kBltcon0] == static_cast<eng::u16>(0x0200u | 0x0100u | 0x00aau),
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
