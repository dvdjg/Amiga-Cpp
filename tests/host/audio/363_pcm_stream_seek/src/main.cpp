// ============================================================================
// Test HOST-363: PcmStream con triple buffer y seek.
// ============================================================================
//
// Valida `PcmStream<3>` (triple buffer: mas margen ante seeks/descompresion irregular) y el
// nuevo `seek(chunk)`: reposiciona el stream en un chunk sin leer los anteriores, rellenando
// despues los buffers. Se usa Delta+RLE (encoder+decoder del engine).
//
//   bash tools/run-host-tests.sh tests/host/audio/363_pcm_stream_seek

#include <cstdio>

#include <eng/audio/pcm_codec.hpp>
#include <eng/audio/pcm_stream.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u16 kChunk = 16u;
constexpr eng::u16 kChunks = 8u;

// PCM de prueba: el byte i vale `i` (determinista).
void make_pcm(eng::u8* pcm, eng::usize n) {
	for (eng::usize i = 0u; i < n; ++i) {
		pcm[i] = static_cast<eng::u8>(i & 0xffu);
	}
}

void test_triple_buffer() {
	eng::u8 pcm[kChunk * kChunks] {};
	eng::u8 enc[kChunk * kChunks] {};
	make_pcm(pcm, sizeof(pcm));
	eng::u16 enc_off[kChunks] {};
	eng::u16 enc_len[kChunks] {};
	eng::u16 at = 0u;
	for (eng::u16 c = 0u; c < kChunks; ++c) {
		const eng::s32 n = eng::audio::pcm_codec::encode(
		    eng::Span<const eng::u8>(pcm + c * kChunk, kChunk),
		    eng::Span<eng::u8>(enc + at, sizeof(enc) - at));
		if (n <= 0) {
			check(false, "encode del caso triple fallo");
			return;
		}
		enc_off[c] = at;
		enc_len[c] = static_cast<eng::u16>(n);
		at = static_cast<eng::u16>(at + n);
	}

	eng::u8 b0[kChunk] {}, b1[kChunk] {}, b2[kChunk] {};
	eng::audio::PcmStream<3> stream {};
	const eng::audio::PcmStream<3>::Config cfg {8000u, kChunk, kChunks,
						    static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaRle)};
	const eng::Span<eng::u8> bufs[3] = {eng::Span<eng::u8>(b0, kChunk),
					    eng::Span<eng::u8>(b1, kChunk),
					    eng::Span<eng::u8>(b2, kChunk)};
	stream.begin(cfg, bufs);

	// Llena los TRES buffers (triple buffer) y comprueba que hay 3 libres al principio.
	check(stream.free_mask() == 0x07u, "seek: 3 buffers libres (triple)");
	for (eng::u8 i = 0u; i < 3u; ++i) {
		const eng::u8 idx = stream.first_free();
		const eng::u16 c = stream.next_chunk();
		check(stream.provide(idx, eng::Span<const eng::u8>(enc + enc_off[c], enc_len[c])),
		      "seek: provide");
	}
	check(stream.next_chunk() == 3u, "seek: 3 chunks consumidos");

	// Seek al chunk 6 y rellenar: el siguiente `provide` usa el chunk 6.
	stream.seek(6u);
	check(stream.next_chunk() == 6u, "seek: posiciona en el chunk 6");
	check(stream.needs_data(), "seek: pide rellenar");
	const eng::u8 idx = stream.first_free();
	check(stream.provide(idx, eng::Span<const eng::u8>(enc + enc_off[6], enc_len[6])),
	      "seek: provide tras seek");
	check(stream.next_chunk() == 7u, "seek: continua por el 7");
}

} // namespace

int main() {
	test_triple_buffer();
	if (failures == 0) {
		std::printf("OK: PcmStream triple buffer + seek.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
