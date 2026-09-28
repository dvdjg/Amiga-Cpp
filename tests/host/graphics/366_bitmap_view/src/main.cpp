// Test host de eng::graphics::BitmapView (zona rectangular de memoria grafica: planos + geometria +
// layout, con la memoria etiquetada tag+banco). Sustituye al BobTarget (u8* suelto) usando
// MemView<Tag, Bank> (la procedencia va en el tipo).
#define ENG_SCALAR_RETRO16
#include <eng/core/types/domains.hpp>
#include <eng/graphics/bitmap_view.hpp>

#include <cstdio>

using eng::graphics::BitmapView;
using eng::graphics::PlaneLayout;
constexpr eng::MemoryKind Chip = eng::MemoryKind::Chip;
constexpr eng::MemoryKind Fast = eng::MemoryKind::Fast;

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

int main() {
	static eng::u8 buf[256u * 256u * 3u] {};
	// Zona Chip 256x256x3 interleaved, fila de plano 32 B. En host se usa from_storage (escape del
	// test); en objetivo viene de un Bitmap/Block Chip.
	const auto mem = eng::MemView<eng::PlaneTag, Chip> {
		eng::Address<Chip>::from_storage(buf), sizeof(buf)};
	const BitmapView<eng::PlaneTag, Chip> v {mem, 256, 256, 32, 3, PlaneLayout::Interleaved};

	check(v.valid(), "valid (Chip, 256x256x3)");
	check(v.words_per_row() == 16, "words_per_row = row_bytes/2 = 16");
	check(v.bitmap_row_bytes() == 96u, "bitmap_row_bytes (interleaved) = 32*3 = 96");
	check(v.interleaved(), "interleaved");
	check(!v.plane_stride(), "interleaved -> plane_stride = 0");
	check(v.byte_count() == 32u * 256u * 3u, "byte_count = 32*256*3");

	// Separado: los planos van contiguos -> plane_stride = row_bytes*height.
	const BitmapView<eng::PlaneTag, Chip> s {mem, 256, 256, 32, 3, PlaneLayout::Separate};
	check(!s.interleaved(), "separado no interleaved");
	check(s.bitmap_row_bytes() == 32u, "separado bitmap_row_bytes = row_bytes = 32");
	check(s.plane_stride() == 32u * 256u, "separado plane_stride = row_bytes*height");

	// El BANCO va en el tipo: Chip y Fast son TIPOS DISTINTOS (`BitmapView<PlaneTag, Chip>` !=
	// `BitmapView<PlaneTag, Fast>`) -> no se pueden mezclar (el compilador lo impide).
	using ChipView = BitmapView<eng::PlaneTag, Chip>;
	static_assert(requires(ChipView c) { c.valid(); });

	// Zona invalida (sin memoria / sin geometria).
	check(!BitmapView<eng::PlaneTag, Chip>{}.valid(), "vacio -> no valid");

	if (failures == 0) {
		std::printf("OK: BitmapView (zona rectangular con tag+banco) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
