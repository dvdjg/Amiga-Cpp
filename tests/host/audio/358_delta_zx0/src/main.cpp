// ============================================================================
// Test HOST-358: Delta + ZX0 (sin perdida) — preprocesado delta y su integracion.
// ============================================================================
//
// El esquema sin perdida para audio es: **diferenciar** el PCM (`D_n = S_n - S_{n-1}`) y
// comprimir el vector de diferencias con **ZX0**. En la descompresion, tras el ZX0 se hace
// una pasada de **integracion**. Este test valida:
//
//   1) la capa delta (`differentiate`/`integrate_deltas` son inversas);
//   2) el dispatch `Codec::DeltaZx0` con un **flujo ZX0 real** del compresor de referencia
//      (vector de HOST-271): `decode(DeltaZx0) == integrate(zx0::decompress)` byte a byte;
//   3) que el encoder DeltaZx0 devuelve -1 (lo produce la herramienta host con ZX0).
//
//   bash tools/run-host-tests.sh tests/host/audio/358_delta_zx0

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

// Flujo ZX0 real (compresor de referencia) de una rampa 0x80..0x8F repetida 2x (HOST-271).
constexpr eng::u8 kZX0[] = {0x00, 0xF5, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
			    0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0xE0, 0xD5, 0x55, 0x60};
constexpr eng::u8 kPCM[] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
			    0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
			    0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F};

void test_delta_layer() {
	eng::u8 buf[64] {};
	eng::u32 seed = 0xC0FFEEu;
	for (eng::usize i = 0u; i < sizeof(buf); ++i) {
		seed = seed * 1664525u + 1013904223u;
		buf[i] = static_cast<eng::u8>(seed >> 25u);
	}
	eng::u8 orig[64] {};
	for (eng::usize i = 0u; i < sizeof(buf); ++i) {
		orig[i] = buf[i];
	}
	eng::audio::pcm_codec::differentiate(eng::Span<eng::u8>(buf, sizeof(buf)));
	eng::audio::pcm_codec::integrate_deltas(eng::Span<eng::u8>(buf, sizeof(buf)));
	bool same = true;
	for (eng::usize i = 0u; i < sizeof(buf); ++i) {
		same = same && (buf[i] == orig[i]);
	}
	check(same, "delta: integrate(differentiate(x)) == x");
}

void test_deltazx0_end_to_end() {
	// Referencia en dos pasos: ZX0 -> deltas, luego integrar.
	eng::u8 two_step[64] {};
	const eng::s32 dn = eng::audio::zx0::decompress(
		eng::Span<const eng::u8>(kZX0, sizeof(kZX0)), eng::Span<eng::u8>(two_step, sizeof(two_step)));
	check(dn == 32, "delta+zx0: ZX0 da 32 bytes");
	eng::audio::pcm_codec::integrate_deltas(
		eng::Span<eng::u8>(two_step, static_cast<eng::usize>(dn)));

	// Camino integrado del codec.
	eng::u8 one_step[64] {};
	const eng::s32 n = eng::audio::pcm_codec::decode(
		eng::Span<const eng::u8>(kZX0, sizeof(kZX0)), eng::Span<eng::u8>(one_step, sizeof(one_step)),
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaZx0));
	check(n == 32, "delta+zx0: decode -> 32");
	bool same = (n == 32) && (dn == 32);
	for (int i = 0; same && i < 32; ++i) {
		same = (one_step[i] == two_step[i]);
	}
	check(same, "delta+zx0: decode == integrate(zx0::decompress)");
}

void test_encoder_is_host_side() {
	eng::u8 out[64] {};
	check(eng::audio::pcm_codec::encode(eng::Span<const eng::u8>(kPCM, 32),
					    eng::Span<eng::u8>(out, sizeof(out)),
					    static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaZx0)) ==
		  -1,
	      "delta+zx0: el encoder lo produce la herramienta host -> -1");
}

} // namespace

int main() {
	test_delta_layer();
	test_deltazx0_end_to_end();
	test_encoder_is_host_side();
	if (failures == 0) {
		std::printf("OK: Delta+ZX0 (capa delta e integracion sobre flujo ZX0 real).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
