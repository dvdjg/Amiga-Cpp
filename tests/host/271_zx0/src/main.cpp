// ============================================================================
// Test HOST-271: descompresor ZX0 (A4) — vector del compresor de referencia.
// ============================================================================
//
// Verifica `eng::audio::zx0::decompress` contra un flujo ZX0 REAL generado por el compresor de
// referencia de Einar Saukas (`zx0`, https://github.com/einar-saukas/ZX0), no por un codificador
// propio: literales (0x80..0x8F) + un match de nuevo offset (offset 16, longitud 16) + EOF.
// Ademas comprueba el dispatch de `pcm_codec::decode` (Codec::Zx0 y Delta+RLE) y el fallo por
// destino pequeno. Ver ROADMAP_AUDIO.md (A4; planificado como HOST-243).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/271_zx0

#include <cstdio>

#include <eng/audio/pcm_codec.hpp>
#include <eng/audio/zx0.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Flujo ZX0 de 32 bytes de PCM (rampa 0x80..0x8F repetida 2x), generado con el `zx0` de
// referencia (n=32, out_size=22). El segundo bloque es un match de nuevo offset (offset 16, len 16).
constexpr eng::u8 kZX0[] = {0x00, 0xF5, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
			    0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0xE0, 0xD5, 0x55, 0x60};
constexpr eng::u8 kPCM[] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
			    0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
			    0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F};

void test_reference_vector() {
	eng::u8 out[64] {};
	const eng::s32 n = eng::audio::zx0::decompress(
		eng::Span<const eng::u8>(kZX0, sizeof(kZX0)), eng::Span<eng::u8>(out, sizeof(out)));
	check(n == 32, "ZX0 referencia: 32 bytes");
	if (n == 32) {
		bool same = true;
		for (int i = 0; i < 32; ++i) {
			if (out[i] != kPCM[i]) {
				same = false;
			}
		}
		check(same, "ZX0 referencia: PCM byte a byte (literales + match)");
	}
}

void test_dispatch_and_bounds() {
	eng::u8 out[64] {};
	const eng::s32 n = eng::audio::pcm_codec::decode(
		eng::Span<const eng::u8>(kZX0, sizeof(kZX0)), eng::Span<eng::u8>(out, sizeof(out)),
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::Zx0));
	check(n == 32, "dispatch: Codec::Zx0 -> 32");

	const eng::s32 small = eng::audio::zx0::decompress(
		eng::Span<const eng::u8>(kZX0, sizeof(kZX0)), eng::Span<eng::u8>(out, 2));
	check(small == -1, "ZX0: -1 si no cabe en destino");

	const eng::s32 unknown = eng::audio::pcm_codec::decode(
		eng::Span<const eng::u8>(kZX0, sizeof(kZX0)), eng::Span<eng::u8>(out, sizeof(out)),
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::APLib));
	check(unknown == -1, "dispatch: aPLib aun no implementado -> -1");
}

} // namespace

int main() {
	test_reference_vector();
	test_dispatch_and_bounds();
	if (failures == 0) {
		std::printf("OK: ZX0 (vector del compresor de referencia) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
