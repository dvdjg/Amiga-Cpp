// ============================================================================
// Test HOST-242: codec PCM 8-bit Delta + RLE (eng/audio/pcm_codec.hpp).
// ============================================================================
//
// Valida el round-trip byte a byte (encode -> decode) sobre patrones representativos de audio
// (silencio, rampa, cuadrada, ruido pseudoaleatorio), el rechazo de codecs desconocidos y de
// flujos truncados/desbordados, y que el silencio comprime de verdad.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/242_pcm_codec

#include <cstdio>

#include <eng/audio/pcm_codec.hpp>

using namespace eng;
using eng::audio::pcm_codec::decode;
using eng::audio::pcm_codec::encode;

namespace {

constexpr usize kMax = 8192;
u8 g_pcm[kMax];
u8 g_comp[kMax];
u8 g_out[kMax];

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Rellena `g_pcm[0..n)` con el patrón `kind`.
void fill_pattern(u32 kind, usize n) {
	u32 lcg = 0x13579bdu;
	for (usize i = 0; i < n; ++i) {
		switch (kind) {
		case 0: g_pcm[i] = 0; break;                                  // silencio
		case 1: g_pcm[i] = static_cast<u8>(i); break;                 // rampa (delta=1)
		case 2: g_pcm[i] = static_cast<u8>((i / 32u) & 1u ? 100u : 20u); break; // cuadrada
		default:                                                       // ruido
			lcg = lcg * 1664525u + 1013904223u;
			g_pcm[i] = static_cast<u8>(lcg >> 24u);
			break;
		}
	}
}

void round_trip(u32 kind, usize n, const char* name) {
	fill_pattern(kind, n);
	const s32 c = encode(Span<const u8> {g_pcm, n}, Span<u8> {g_comp, kMax});
	check(c > 0, name);
	if (c <= 0) {
		return;
	}
	const s32 d = decode(Span<const u8> {g_comp, static_cast<usize>(c)},
			     Span<u8> {g_out, kMax}, 2u);
	check(d == static_cast<s32>(n), name);
	if (d != static_cast<s32>(n)) {
		return;
	}
	bool same = true;
	for (usize i = 0; i < n; ++i) {
		if (g_out[i] != g_pcm[i]) {
			same = false;
			break;
		}
	}
	check(same, name);
}

void test_round_trips() {
	const usize sizes[] = {1u, 2u, 3u, 128u, 129u, 256u, 1024u, 4096u};
	for (usize n : sizes) {
		round_trip(0u, n, "round-trip silencio");
		round_trip(1u, n, "round-trip rampa");
		round_trip(2u, n, "round-trip cuadrada");
		round_trip(3u, n, "round-trip ruido");
	}
}

void test_ratio() {
	fill_pattern(0u, 4096u); // silencio: deltas 0 -> RLE debe comprimir mucho
	const s32 c = encode(Span<const u8> {g_pcm, 4096u}, Span<u8> {g_comp, kMax});
	check(c > 0 && c <= 64, "el silencio comprime a <=64 bytes");

	fill_pattern(3u, 4096u); // ruido: casi todo literales
	const s32 cr = encode(Span<const u8> {g_pcm, 4096u}, Span<u8> {g_comp, kMax});
	check(cr > 0 && cr <= 4096 + 64, "el ruido no se expande sin control");
}

void test_rejects() {
	fill_pattern(1u, 256u);
	const s32 c = encode(Span<const u8> {g_pcm, 256u}, Span<u8> {g_comp, kMax});
	check(c > 0, "encode para rechazos");

	// Codec desconocido.
	check(decode(Span<const u8> {g_comp, static_cast<usize>(c)}, Span<u8> {g_out, kMax}, 0u) == -1,
	      "decode rechaza codec desconocido");

	// Destino demasiado pequeno.
	check(decode(Span<const u8> {g_comp, static_cast<usize>(c)}, Span<u8> {g_out, 4u}, 2u) == -1,
	      "decode rechaza destino pequeno");

	// Flujo truncado (cortar a la mitad).
	check(decode(Span<const u8> {g_comp, static_cast<usize>(c) / 2u}, Span<u8> {g_out, kMax}, 2u) == -1,
	      "decode rechaza flujo truncado");
}

void test_none_codec() {
	// PCM crudo (`Codec::None`): decode copia tal cual (mismo tamano).
	fill_pattern(1u, 256u);
	const s32 d = decode(Span<const u8> {g_pcm, 256u}, Span<u8> {g_out, 256u}, 3u);
	check(d == 256, "None: copia 256 bytes");
	bool same = true;
	for (usize i = 0; i < 256u; ++i) {
		if (g_out[i] != g_pcm[i]) {
			same = false;
			break;
		}
	}
	check(same, "None: contenido identico");
	// Tamano distinto -> rechazo (el chunk debe traer exactamente las muestras del buffer).
	check(decode(Span<const u8> {g_pcm, 256u}, Span<u8> {g_out, 128u}, 3u) == -1,
	      "None: rechaza tamano distinto");
}

void test_encode_overflow() {
	fill_pattern(3u, 512u);
	// Destino minusculo: encode debe devolver -1 en vez de escribir fuera.
	check(encode(Span<const u8> {g_pcm, 512u}, Span<u8> {g_comp, 8u}) == -1,
	      "encode rechaza destino pequeno");
}

} // namespace

int main() {
	test_round_trips();
	test_ratio();
	test_rejects();
	test_none_codec();
	test_encode_overflow();

	if (failures == 0) {
		std::printf("OK: codec PCM Delta+RLE (round-trip y rechazos) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
