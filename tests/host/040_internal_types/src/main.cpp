// ============================================================================
// Test HOST-040: sistema de tipos internos (vistas con tag + unidades fuertes)
// ============================================================================
//
// Valida `eng/core/typed.hpp`: los buffers con dominio NO son intercambiables
// (fallo de compilación intencionado, comprobado con conceptos), la reinterpretación
// byte<->word es explícita, y las vistas no añaden tamaño sobre `Span`.

#include <cstdio>

#include <eng/core/typed.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

// Tags de dominio de juguete.
struct PatternTag {};
struct AudioTag {};
struct PaletteTag {};

using Pattern = eng::ByteView<PatternTag>;
using Audio = eng::ByteView<AudioTag>;
using PatternBytes = eng::Bytes<PatternTag>;
using PaletteWords = eng::WordView<PaletteTag>;

// ¿Se puede construir `To` desde `From`? (conversion implicita/por llaves)
template <class From, class To>
concept ConstructibleFrom = requires(From f) { To {f}; };

// Una funcion que SOLO acepta el dominio Pattern.
constexpr eng::u32 sum_pattern(Pattern p) {
	eng::u32 s = 0;
	for (eng::usize i = 0; i < p.size(); ++i) s += p[i];
	return s;
}

template <class T>
concept AcceptsPatternSum = requires(T t) { sum_pattern(t); };

// Mismo tag -> copiable; distinto tag -> NO construible.
static_assert(ConstructibleFrom<Pattern, Pattern>, "mismo dominio debe copiarse");
static_assert(!ConstructibleFrom<Audio, Pattern>, "Audio no debe convertirse a Pattern");
static_assert(!ConstructibleFrom<PaletteWords, PatternBytes>, "words no son bytes de Pattern");
// Un `Span<u8>` crudo tampoco entra sin pasar por `from`/`raw`.
static_assert(!ConstructibleFrom<eng::Span<eng::u8>, PatternBytes>, "no constructor desde Span");
// La funcion de dominio no acepta otro dominio.
static_assert(!AcceptsPatternSum<Audio>, "sum_pattern no acepta Audio");
static_assert(AcceptsPatternSum<Pattern>, "sum_pattern acepta Pattern");
// Sin coste: la vista es del tamano de un `Span`.
static_assert(sizeof(PatternBytes) == sizeof(eng::Span<eng::u8>), "Bytes sin sobrecoste");
static_assert(sizeof(Pattern) == sizeof(eng::Span<const eng::u8>), "ByteView sin sobrecoste");
static_assert(sizeof(eng::PlaneIndex) == 1u && sizeof(eng::RowBytes) == 2u, "unidades densas");
} // namespace

int main() {
	alignas(2) eng::u8 data[8] = {1, 2, 3, 4, 5, 6, 7, 8};

	// Bytes<Tag> mutable: acceso, subspan, reinterpretacion explicita y fill.
	PatternBytes b {data, 8u};
	check(b.size() == 8u && b[0] == 1u && b.at(7) == 8u, "Bytes acceso");
	check(b.subspan(2, 3).size() == 3u && b.subspan(2, 3)[0] == 3u, "Bytes subspan");
	auto words = b.as_words();
	check(words.size() == 4u && words.data() == reinterpret_cast<eng::u16*>(data),
	      "as_words comparte memoria");
	b.fill(9u);
	check(b[0] == 9u && b[7] == 9u, "Bytes fill");

	// ByteView<Tag> de solo lectura desde `const`.
	const eng::u8 ro[4] = {10, 20, 30, 40};
	Pattern view {ro, 4u};
	check(view.size() == 4u && view[2] == 30u, "ByteView acceso");
	check(sum_pattern(view) == 100u, "sum_pattern sobre ByteView");

	// Words<Tag> mutable.
	eng::u16 pal[3] = {0x111, 0x222, 0x333};
	eng::Words<PaletteTag> pw {pal, 3u};
	check(pw.size() == 3u && pw[1] == 0x222u, "Words acceso");
	pw[2] = 0x444u;
	check(pal[2] == 0x444u, "Words escribe la memoria");

	// Unidades fuertes y validacion de plano.
	eng::PlaneCount planes {4};
	check(eng::PlaneIndex::make(2u, planes).valid(planes), "PlaneIndex valido");
	check(!eng::PlaneIndex {3}.valid(eng::PlaneCount {3}), "PlaneIndex fuera de rango");
	check(eng::RowBytes {66}.value == 66u && eng::PixelWidth {320}.value == 320u, "unidades");

	// Direcciones con semantica distinta.
	eng::BitmapBase bb {data};
	eng::FrontBase fb {data};
	check(bb.value == fb.value, "misma memoria, tipos distintos");
	static_assert(!ConstructibleFrom<eng::FrontBase, eng::BitmapBase>, "base y front no se mezclan");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: sistema de tipos internos (tags, unidades, direcciones) validado.\n");
	return 0;
}
