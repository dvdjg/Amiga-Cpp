// ============================================================================
// Test HOST-040: sistema de tipos internos (vistas con tag + unidades fuertes)
// ============================================================================
//
// Valida `eng/core/types/typed.hpp`: los buffers con dominio NO son intercambiables
// (fallo de compilación intencionado, comprobado con conceptos), la reinterpretación
// byte<->word es explícita, y las vistas no añaden tamaño sobre `Span`.

#include <cstdio>

#include <eng/core/types/typed.hpp>
#include <eng/memory/mem_bank.hpp>

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

	// Ergonomia tipo Span: array nativo (deduce tamano), iteradores y conversiones.
	{
		eng::u8 arr[5] = {5, 6, 7, 8, 9};
		PatternBytes ab {arr};                 // deduce N = 5
		check(ab.size() == 5u && ab.back() == 9u && ab.front() == 5u, "array deduce tamano");
		eng::u32 sum = 0;
		for (eng::u8 v : ab) sum += v;          // range-for via begin/end
		check(sum == 35u, "range-for itera");
		check(*ab.begin() == 5u && ab.end() == ab.begin() + 5, "begin/end");

		const Pattern cab = ab.as_const();
		check(cab.size() == 5u && cab[2] == 7u, "as_const");

		eng::u16 w[2] = {0x1111, 0x2222};
		eng::Words<PaletteTag> wv {w};          // array deduce N = 2
		check(wv.size() == 2u && wv.front() == 0x1111u, "Words array");
		check(wv.as_bytes().size() == 4u, "as_bytes");
		auto words_from_bytes = wv.as_bytes().as_words();
		check(words_from_bytes.size() == 2u && words_from_bytes[1] == 0x2222u, "bytes<->words redondo");
	}

	// Dirección DMA-visible de Chip RAM: de una fuente **certificada** (`MemBank<Chip>`), no
	// fabricando un `Address<Chip>` con un cast. El rol (base/front) no es un tipo: lo da el nombre
	// del método.
	static eng::u8 chip_buf[8] {};
	eng::MemBank<eng::MemoryKind::Chip> bank {};
	bank.configure(chip_buf, sizeof(chip_buf), 2u);
	eng::Address<eng::MemoryKind::Chip> chip_addr {};
	check(!chip_addr.valid(), "dirección Chip por defecto vacía");
	chip_addr = bank.reserve<PatternTag>(8u, 2u).address();
	check(chip_addr.valid() && chip_addr.cptr() == chip_buf, "dirección Chip certificada");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: sistema de tipos internos (tags, unidades, direcciones) validado.\n");
	return 0;
}
