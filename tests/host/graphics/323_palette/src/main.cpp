// ============================================================================
// Test HOST-323: paleta de juego (eng::Palette / Color / ColorIndex).
// ============================================================================
//
// Respalda `eng/graphics/palette.hpp`: los tipos fuertes `Color` (RGB444) y `ColorIndex`,
// y las operaciones de intención de `Palette` (`set`/`get`/`fill`/`fade`/`mix`), con el
// índice recortado a 0..31 y la publicación del parche base al `FramePlan`. La aritmética
// (que reutiliza `eng::util`) se valida además en **tiempo de compilación** con
// `static_assert`, por ser `constexpr`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/323_palette

#include <cstdio>

#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/palette.hpp>
#include <eng/graphics/palette32.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

constexpr eng::u16 kRgbWhite = 0x0fffu;

// --- Aritmética en compilación (constexpr) -----------------------------------
constexpr bool constexpr_ops() {
	eng::Palette p {};
	p.fill(eng::Color::rgb(1, 2, 3));
	if (p.get(eng::ColorIndex {31}).value != 0x123u) {
		return false;
	}
	// El índice se recorta: 40 -> 31, no escribe fuera del array.
	p.set(eng::ColorIndex {40u}, eng::Color::rgb(15, 0, 0));
	if (p.get(eng::ColorIndex {31}).value != 0xf00u) {
		return false;
	}
	// rgb() enmascara cada canal a 4 bits.
	if (eng::Color::rgb(20, 0, 0).value != 0x400u) {
		return false;
	}
	// fade: 0 apaga, num==den deja igual.
	eng::Palette f {};
	f.fill(eng::Color::from_raw(kRgbWhite));
	f.fade(1u, 2u);
	if (f.get(eng::ColorIndex {0}).value != 0x777u) {
		return false;
	}
	f.fade(0u, 4u);
	if (f.get(eng::ColorIndex {0}).value != 0x000u) {
		return false;
	}
	return true;
}
static_assert(constexpr_ops(), "Palette: set/get/fill/fade constexpr");

constexpr bool constexpr_mix() {
	eng::Palette32 a {};
	eng::Palette32 b {};
	for (eng::u8 i = 0; i < 32u; ++i) {
		a.color[i] = 0x000u;
		b.color[i] = kRgbWhite;
	}
	eng::Palette m {};
	m.mix(a, b, 4u, 8u, 1u, 4u); // tramo [1,5)
	// Dentro del tramo: interpola a mitad -> 0x777.
	if (m.get(eng::ColorIndex {1}).value != 0x777u) {
		return false;
	}
	if (m.get(eng::ColorIndex {4}).value != 0x777u) {
		return false;
	}
	// Fuera del tramo: queda igual a `a` (negro).
	return m.get(eng::ColorIndex {0}).value == 0x000u && m.get(eng::ColorIndex {5}).value == 0x000u;
}
static_assert(constexpr_mix(), "Palette: mix constexpr con tramo parcial");

} // namespace

int main() {
	std::printf("== HOST-323 palette ==\n");

	// --- set/get + índice recortado -----------------------------------------
	{
		eng::Palette p {};
		p.set(eng::ColorIndex {5}, eng::Color::rgb(15, 8, 0));
		check(p.get(eng::ColorIndex {5}).value == 0xf80u, "set/get: color exacto");
		check(!eng::ColorIndex {40u}.valid(), "ColorIndex 40 no valido");
		check(eng::color_index(40u).value == 31u, "color_index recorta a 31");
	}

	// --- copy_from(PaletteWords) --------------------------------------------
	{
		eng::Palette32 src {};
		for (eng::u8 i = 0; i < 32u; ++i) {
			src.color[i] = static_cast<eng::u16>(0x111u * (i % 8u));
		}
		eng::Palette p {src.words()};
		check(p.storage().color[7] == src.color[7], "copy_from copia todos los colores");
		check(p.get(eng::ColorIndex {7}).value == src.color[7], "get coincide con el origen");
	}

	// --- fade_from no toca el origen ----------------------------------------
	{
		eng::Palette32 src {};
		src.color[0] = kRgbWhite;
		eng::Palette p {};
		p.fade_from(src, 1u, 2u);
		check(p.get(eng::ColorIndex {0}).value == 0x777u, "fade_from: mitad");
		check(src.color[0] == kRgbWhite, "fade_from no modifica el origen");
	}

	// --- apply: parche base en el FramePlan ---------------------------------
	{
		eng::Palette p {};
		p.fill(eng::Color::from_raw(0x0aaau));
		eng::graphics::FramePlan plan;
		check(p.apply(plan), "apply: registra el parche");
		check(plan.palette_patch_count() == 1u, "apply: 1 parche");
		const eng::graphics::PalettePatch& patch = plan.palette_patch(0u);
		check(patch.target == eng::graphics::PalettePatchTarget::Base, "apply: parche base");
		check(patch.first == 0u && patch.count == 32u, "apply: 0..31");
		check(patch.colors.data()[0] == 0x0aaau && patch.colors.data()[31] == 0x0aaau,
		      "apply: colores de la paleta");

		// Tramo parcial.
		eng::graphics::FramePlan plan2;
		check(p.apply(plan2, 16u, 4u), "apply tramo: registra el parche");
		const eng::graphics::PalettePatch& patch2 = plan2.palette_patch(0u);
		check(patch2.first == 16u && patch2.count == 4u, "apply tramo: 16..19");
	}

	// --- words() / operator PaletteWords ------------------------------------
	{
		eng::Palette p {};
		p.set(eng::ColorIndex {2}, eng::Color::rgb(0, 15, 0));
		const eng::PaletteWords w = p.words();
		check(w.size() == 32u, "words: 32 colores");
		check(w.data()[2] == 0x0f0u, "words: refleja el set");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Palette (set/get/fill/fade/mix, indice recortado, parche base) validado.\n");
	return 0;
}
