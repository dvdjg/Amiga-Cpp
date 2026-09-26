// ============================================================================
// Test HOST-362: descompresor aPLib (vector del compresor de referencia).
// ============================================================================
//
// Verifica `eng::audio::aplib::decompress` contra un flujo aPLib REAL generado con `apultra`
// (emmanuel-marty/apultra, licencia zlib): 96 bytes de patron periodico comprimidos a 14.
// Comprueba tambien el dispatch de `pcm_codec` (Codec::APLib) y los rechazos.
//
//   bash tools/run-host-tests.sh tests/host/audio/362_aplib

#include <cstdio>

#include <eng/audio/aplib.hpp>
#include <eng/audio/pcm_codec.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Vector: 96 muestras periodicas b[i] = (i%8)*16 -> aPLib de 14 bytes (apultra).
constexpr eng::u8 kAPLib[] = {0x00, 0x01, 0x10, 0x20, 0x30, 0x40, 0x50,
			      0x60, 0x70, 0x4E, 0x08, 0xF9, 0x80, 0x00};

void test_reference_vector() {
	eng::u8 out[128] {};
	const eng::s32 n = eng::audio::aplib::decompress(
	    eng::Span<const eng::u8>(kAPLib, sizeof(kAPLib)), eng::Span<eng::u8>(out, sizeof(out)));
	check(n == 96, "aplib: 96 bytes descomprimidos");
	bool ok = (n == 96);
	for (int i = 0; ok && i < 96; ++i) {
		ok = (out[i] == static_cast<eng::u8>((i % 8) * 16));
	}
	check(ok, "aplib: patron periodico byte a byte");
}

void test_dispatch_and_bounds() {
	eng::u8 out[128] {};
	const eng::s32 n = eng::audio::pcm_codec::decode(
	    eng::Span<const eng::u8>(kAPLib, sizeof(kAPLib)), eng::Span<eng::u8>(out, sizeof(out)),
	    static_cast<eng::u8>(eng::audio::pcm_codec::Codec::APLib));
	check(n == 96, "aplib: dispatch Codec::APLib -> 96");

	const eng::s32 small = eng::audio::aplib::decompress(
	    eng::Span<const eng::u8>(kAPLib, sizeof(kAPLib)), eng::Span<eng::u8>(out, 4u));
	check(small == -1, "aplib: -1 si no cabe en destino");

	check(eng::audio::aplib::decompress(eng::Span<const eng::u8>(nullptr, 0u),
					    eng::Span<eng::u8>(out, sizeof(out))) == -1,
	      "aplib: flujo vacio -> -1");
}

} // namespace

int main() {
	test_reference_vector();
	test_dispatch_and_bounds();
	if (failures == 0) {
		std::printf("OK: aPLib (vector del compresor apultra de referencia).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
