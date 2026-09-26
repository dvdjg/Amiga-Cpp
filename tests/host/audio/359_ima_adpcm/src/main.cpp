// ============================================================================
// Test HOST-359: IMA ADPCM 4-bit (con perdida).
// ============================================================================
//
// Valida `eng::audio::ima_adpcm` (tablas estandar IMA/DVI, bloque autocontenido con
// predictor + indice de paso). Comprueba el decodificador contra una referencia
// independiente escrita aparte en este test, el round-trip `decode(encode(x))` con error
// acotado sobre una senal suave, el dispatch de `pcm_codec` y los rechazos de tamano.
//
//   bash tools/run-host-tests.sh tests/host/audio/359_ima_adpcm

#include <cstdio>

#include <eng/audio/ima_adpcm.hpp>
#include <eng/audio/pcm_codec.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Tablas y decodificador del estandar, escritos aparte (referencia independiente).
constexpr eng::s8 kRefIdx[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
constexpr eng::s16 kRefStep[89] = {
	7,   8,   9,   10,  11,  12,  13,  14,  16,  17,  19,  21,  23,  25,  28,  31,
	34,  37,  41,  45,  50,  55,  60,  66,  73,  80,  88,  97,  107, 118, 130, 143,
	157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
	724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
	2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
	9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
	29794, 32767};

void ref_decode(const eng::u8* src, eng::usize len, eng::u8* dst) {
	eng::s32 pred = static_cast<eng::s16>(src[2] | (src[3] << 8));
	eng::s32 index = src[0] > 88u ? 88 : src[0];
	const eng::usize pairs = len - 4u;
	eng::usize out = 0;
	for (eng::usize i = 0; i < pairs; ++i) {
		for (int half = 0; half < 2; ++half) {
			const eng::u8 code = half == 0 ? (src[4 + i] >> 4) : (src[4 + i] & 0x0f);
			const eng::s32 stp = kRefStep[index];
			eng::s32 diff = stp >> 3;
			if (code & 1) diff += stp >> 2;
			if (code & 2) diff += stp >> 1;
			if (code & 4) diff += stp;
			pred += (code & 8) ? -diff : diff;
			if (pred > 32767) pred = 32767;
			if (pred < -32768) pred = -32768;
			index += kRefIdx[code];
			if (index < 0) index = 0;
			if (index > 88) index = 88;
			dst[out++] = static_cast<eng::u8>(static_cast<eng::s8>(pred >> 8));
		}
	}
}

void test_matches_reference() {
	eng::u8 src[36] {};
	src[0] = 20u; // step_index
	src[2] = 0x00;
	src[3] = 0x40; // pred = 0x4000 = 16384
	eng::u32 seed = 0x2468ACE0u;
	for (eng::usize i = 4u; i < sizeof(src); ++i) {
		seed = seed * 1664525u + 1013904223u;
		src[i] = static_cast<eng::u8>(seed >> 24u);
	}
	eng::u8 a[64] {};
	eng::u8 b[64] {};
	const eng::s32 n = eng::audio::ima_adpcm::decode(
		eng::Span<const eng::u8>(src, sizeof(src)), eng::Span<eng::u8>(a, sizeof(a)));
	ref_decode(src, sizeof(src), b);
	check(n == 64, "ima: 2*(len-4) muestras");
	bool same = (n == 64);
	for (int i = 0; same && i < 64; ++i) {
		same = (a[i] == b[i]);
	}
	check(same, "ima: byte a byte frente al estandar");
}

void test_round_trip_bounded() {
	// Senal suave (seno de baja frecuencia, 8-bit) — como el audio real, con cambios
	// pequenos entre muestras.
	eng::u8 pcm[128] {};
	// Triangular suave: pendiente de 4 por muestra, amplitud +/-16 (cambios pequenos entre
	// muestras, como el audio real).
	for (int i = 0; i < 128; ++i) {
		const int t = i & 15;                       // 0..15
		const int v = (t < 8) ? -16 + 4 * t : -16 + 4 * (15 - t);
		pcm[i] = static_cast<eng::u8>(static_cast<eng::s8>(v));
	}
	eng::u8 enc[128] {};
	const eng::s32 e = eng::audio::ima_adpcm::encode(eng::Span<const eng::u8>(pcm, 128),
							 eng::Span<eng::u8>(enc, sizeof(enc)));
	check(e > 0 && e < 128, "ima: comprime (4 bits/muestra)");
	eng::u8 dec[128] {};
	const eng::s32 d = eng::audio::ima_adpcm::decode(
		eng::Span<const eng::u8>(enc, static_cast<eng::usize>(e)),
		eng::Span<eng::u8>(dec, sizeof(dec)));
	check(d >= 128, "ima: decodifica >= 128 muestras");
	int max_err = 0;
	for (int i = 0; i < 128; ++i) {
		const int a = static_cast<eng::s8>(pcm[i]);
		const int b = static_cast<eng::s8>(dec[i]);
		const int e2 = a > b ? a - b : b - a;
		if (e2 > max_err) {
			max_err = e2;
		}
	}
	check(max_err <= 8, "ima: error maximo <= 8 en senal suave");
	check(pcm[0] == dec[0], "ima: primera muestra exacta");
}

void test_dispatch_and_bounds() {
	eng::u8 src[12] {};
	src[3] = 0x10;
	eng::u8 out[32] {};
	const eng::s32 n = eng::audio::pcm_codec::decode(
		eng::Span<const eng::u8>(src, sizeof(src)), eng::Span<eng::u8>(out, sizeof(out)),
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::ImaAdpcm));
	check(n == 16, "dispatch: Codec::ImaAdpcm -> 16");

	const eng::s32 tiny = eng::audio::pcm_codec::decode(
		eng::Span<const eng::u8>(src, sizeof(src)), eng::Span<eng::u8>(out, 2),
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::ImaAdpcm));
	check(tiny == -1, "dispatch: -1 si no cabe");

	const eng::u8 short_src[3] = {0u, 0u, 0u};
	check(eng::audio::pcm_codec::decode(eng::Span<const eng::u8>(short_src, 3),
					    eng::Span<eng::u8>(out, sizeof(out)),
					    static_cast<eng::u8>(eng::audio::pcm_codec::Codec::ImaAdpcm)) == -1,
	      "dispatch: bloque corto -> -1");

	eng::u8 enc[32] {};
	check(eng::audio::pcm_codec::encode(eng::Span<const eng::u8>(out, 3),
					    eng::Span<eng::u8>(enc, sizeof(enc)),
					    static_cast<eng::u8>(eng::audio::pcm_codec::Codec::ImaAdpcm)) > 0,
	      "dispatch: encode ImaAdpcm");
}

} // namespace

int main() {
	test_matches_reference();
	test_round_trip_bounded();
	test_dispatch_and_bounds();
	if (failures == 0) {
		std::printf("OK: IMA ADPCM (referencia independiente + round-trip acotado).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
