// ============================================================================
// Test HOST-323: Fibonacci Delta (IFF 8SVX, sCompression = 1).
// ============================================================================
//
// Valida `eng::audio::fib_delta` contra el algoritmo del estandar IFF 8SVX (EA, 1985,
// Apendice C), reimplementado aparte en este test (referencia independiente), y contra un
// vector dorado calculado a mano. Comprueba tambien el dispatch de `pcm_codec` y los
// errores de tamano.
//
//   bash tools/run-host-tests.sh tests/host/audio/323_fib_delta

#include <cstdio>

#include <eng/audio/fib_delta.hpp>
#include <eng/audio/pcm_codec.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Tabla y decodificador del estandar, escritos aparte (referencia independiente del
// Apendice C de la especificacion: `D1Unpack`/`DUnpack`).
constexpr eng::s8 kRefTable[16] = {-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21};

void ref_unpack(const eng::u8* src, eng::usize n, eng::u8* dst) {
	eng::u8 x = src[1]; // muestra inicial
	const eng::usize samples = (n - 2u) * 2u;
	for (eng::usize i = 0u; i < samples; ++i) {
		eng::u8 d = src[2u + (i >> 1u)];
		d = (i & 1u) ? static_cast<eng::u8>(d & 0x0fu) : static_cast<eng::u8>(d >> 4u);
		x = static_cast<eng::u8>(x + kRefTable[d]);
		dst[i] = x;
	}
}

// Vector dorado: [pad=0][x0=0][0x01][0x8F] -> {-34,-55,-55,-34}.
void test_golden_vector() {
	const eng::u8 src[] = {0x00, 0x00, 0x01, 0x8F};
	eng::u8 out[4] {};
	const eng::s32 n = eng::audio::fib_delta::decode(
		eng::Span<const eng::u8>(src, sizeof(src)), eng::Span<eng::u8>(out, sizeof(out)));
	check(n == 4, "fib-delta: 4 muestras");
	check(out[0] == 0xDEu && out[1] == 0xC9u && out[2] == 0xC9u && out[3] == 0xDEu,
	      "fib-delta: vector dorado -34,-55,-55,-34");
}

// Equivalencia del decodificador del engine con la referencia independiente del estandar.
void test_matches_reference() {
	// Flujo pseudoaleatorio (determinista) de 2 + 128 bytes.
	eng::u8 src[130] {};
	eng::u32 seed = 0x12345678u;
	for (eng::usize i = 0u; i < sizeof(src); ++i) {
		seed = seed * 1664525u + 1013904223u;
		src[i] = static_cast<eng::u8>(seed >> 24u);
	}
	eng::u8 a[256] {};
	eng::u8 b[256] {};
	const eng::s32 na = eng::audio::fib_delta::decode(
		eng::Span<const eng::u8>(src, sizeof(src)), eng::Span<eng::u8>(a, sizeof(a)));
	ref_unpack(src, sizeof(src), b);
	check(na == 256, "fib-delta: 2*(n-2) muestras");
	bool same = (na == 256);
	for (int i = 0; same && i < 256; ++i) {
		same = (a[i] == b[i]);
	}
	check(same, "fib-delta: byte a byte frente al estandar (DUnpack)");
}

// Senal construida solo con deltas de la tabla: el greedy debe reproducirla exacta.
void test_lossless_on_tabulated_deltas() {
	eng::u8 pcm[32] {};
	eng::u8 x = 0u;
	// 16 pares (32 muestras): cada salto usa un delta de la tabla, asi que el greedy debe
	// reproducirlo exacto.
	const int codes[16] = {15, 15, 9, 3, 12, 5, 1, 8, 7, 6, 10, 11, 13, 14, 2, 4};
	for (int i = 0; i < 16; ++i) {
		x = static_cast<eng::u8>(x + kRefTable[codes[i]]);
		pcm[2 * i] = x;
		pcm[2 * i + 1] = x;
	}
	eng::u8 enc[64] {};
	const eng::s32 e = eng::audio::fib_delta::encode(eng::Span<const eng::u8>(pcm, 32),
							 eng::Span<eng::u8>(enc, sizeof(enc)));
	check(e > 0, "fib-delta: comprime");
	eng::u8 dec[32] {};
	const eng::s32 d = eng::audio::fib_delta::decode(
		eng::Span<const eng::u8>(enc, static_cast<eng::usize>(e)),
		eng::Span<eng::u8>(dec, sizeof(dec)));
	check(d == 32, "fib-delta: 32 muestras al descomprimir");
	bool same = true;
	for (int i = 0; i < 32; ++i) {
		same = same && (dec[i] == pcm[i]);
	}
	check(same, "fib-delta: sin perdida si los deltas son de la tabla");
	check(eng::audio::fib_delta::encode(eng::Span<const eng::u8>(pcm, 0),
					    eng::Span<eng::u8>(enc, sizeof(enc))) == -1,
	      "fib-delta: PCM vacio -> -1");
}

// Continuidad entre chunks: sin encadenar la semilla, cada chunk arranca en 0 y salta (clic).
void test_chunk_chain_continuity() {
	eng::u8 pcm[64] {};
	for (int i = 0; i < 64; ++i) {
		pcm[i] = static_cast<eng::u8>(static_cast<eng::s8>(i)); // rampa 0..63
	}
	eng::u8 e0[40] {}, e1[40] {};
	eng::u8 seed = 0u;
	const eng::s32 n0 = eng::audio::fib_delta::encode(
	    eng::Span<const eng::u8>(pcm, 32u), eng::Span<eng::u8>(e0, sizeof(e0)), seed);
	const eng::s32 n1 = eng::audio::fib_delta::encode(
	    eng::Span<const eng::u8>(pcm + 32, 32u), eng::Span<eng::u8>(e1, sizeof(e1)), seed);
	check(n0 > 0 && n1 > 0, "cadena: codifica los dos chunks");
	eng::u8 d0[32] {}, d1[32] {};
	(void)eng::audio::fib_delta::decode(eng::Span<const eng::u8>(e0, static_cast<eng::usize>(n0)),
					    eng::Span<eng::u8>(d0, sizeof(d0)));
	(void)eng::audio::fib_delta::decode(eng::Span<const eng::u8>(e1, static_cast<eng::usize>(n1)),
					    eng::Span<eng::u8>(d1, sizeof(d1)));
	const int boundary = static_cast<int>(static_cast<eng::s8>(d1[0])) -
			     static_cast<int>(static_cast<eng::s8>(d0[31]));
	check(boundary >= -4 && boundary <= 16, "cadena: la frontera sigue la rampa (continua)");

	// Con semilla 0 en el chunk 1, su primer valor se queda en el delta maximo (clic).
	eng::u8 f1[40] {};
	eng::u8 zero = 0u;
	(void)eng::audio::fib_delta::encode(eng::Span<const eng::u8>(pcm + 32, 32u),
					    eng::Span<eng::u8>(f1, sizeof(f1)), zero);
	eng::u8 g1[32] {};
	(void)eng::audio::fib_delta::decode(eng::Span<const eng::u8>(f1, 40u),
					    eng::Span<eng::u8>(g1, sizeof(g1)));
	const int jump = static_cast<int>(static_cast<eng::s8>(g1[0])) -
			 static_cast<int>(static_cast<eng::s8>(d0[31]));
	check(jump < boundary - 8 || jump > boundary + 8, "cadena: sin semilla hay salto (detectado)");
}

// Dispatch del codec y errores de tamano.
void test_codec_dispatch() {
	const eng::u8 src[] = {0x00, 0x00, 0x01, 0x8F};
	eng::u8 out[8] {};
	const eng::s32 n = eng::audio::pcm_codec::decode(
		eng::Span<const eng::u8>(src, sizeof(src)), eng::Span<eng::u8>(out, sizeof(out)),
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::FibDelta));
	check(n == 4, "dispatch: Codec::FibDelta -> 4");

	const eng::s32 tiny = eng::audio::pcm_codec::decode(
		eng::Span<const eng::u8>(src, sizeof(src)), eng::Span<eng::u8>(out, 2),
		static_cast<eng::u8>(eng::audio::pcm_codec::Codec::FibDelta));
	check(tiny == -1, "dispatch: -1 si no cabe");

	const eng::u8 short_src[] = {0x00, 0x00};
	check(eng::audio::pcm_codec::decode(eng::Span<const eng::u8>(short_src, 2),
					    eng::Span<eng::u8>(out, sizeof(out)),
					    static_cast<eng::u8>(eng::audio::pcm_codec::Codec::FibDelta)) ==
		  -1,
	      "dispatch: flujo corto -> -1");

	// El encoder del codec soporta FibDelta; ZX0/aPLib/DeltaZx0 los produce el host.
	eng::u8 enc[16] {};
	check(eng::audio::pcm_codec::encode(eng::Span<const eng::u8>(src, 3),
					    eng::Span<eng::u8>(enc, sizeof(enc)),
					    static_cast<eng::u8>(eng::audio::pcm_codec::Codec::FibDelta)) >
		  0,
	      "dispatch: encode FibDelta");
	check(eng::audio::pcm_codec::encode(eng::Span<const eng::u8>(src, 3),
					    eng::Span<eng::u8>(enc, sizeof(enc)),
					    static_cast<eng::u8>(eng::audio::pcm_codec::Codec::Zx0)) == -1,
	      "dispatch: encode ZX0 -> -1 (herramienta host)");
}

} // namespace

int main() {
	test_golden_vector();
	test_matches_reference();
	test_lossless_on_tabulated_deltas();
	test_chunk_chain_continuity();
	test_codec_dispatch();
	if (failures == 0) {
		std::printf("OK: Fibonacci Delta (IFF 8SVX) validado contra el estandar.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
