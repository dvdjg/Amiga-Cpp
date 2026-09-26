// ============================================================================
// Test HOST-361: interfaz de medios (`eng::audio::media`).
// ============================================================================
//
// Valida el punto unico de despacho por contenedor/codec: reconoce PCM crudo y AUZX, y
// `decode_chunk(i, dst)` devuelve el PCM de cada chunk. Se construye un AUZX con 2 chunks
// codificados con Delta+RLE (con el propio encoder del engine) y se comprueba el round-trip
// por chunk; y se comprueba el caso PCM crudo (sin cabecera).
//
//   bash tools/run-hosts-tests.sh tests/host/audio/361_media

#include <cstdio>

#include <eng/audio/media.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using eng::audio::auzx::kChunkEntrySize;
using eng::audio::auzx::kHeaderSize;

// Construye un AUZX de 2 chunks (PCM de `pcm`, 4 muestras cada uno) con payloads Delta+RLE.
eng::usize build_auzx(const eng::u8* pcm, eng::usize total, eng::u8* dst, eng::usize cap) {
	const eng::u16 nchunks = static_cast<eng::u16>(total / 4u);
	const eng::usize entries = static_cast<eng::usize>(nchunks) * kChunkEntrySize;
	eng::u32 at = static_cast<eng::u32>(kHeaderSize + entries);
	if (kHeaderSize + entries > cap) {
		return 0u;
	}
	eng::audio::auzx::wr32(eng::Span<eng::u8>(dst, cap), kHeaderSize, at); // provisional
	for (eng::u16 c = 0u; c < nchunks; ++c) {
		eng::u8 enc[64] {};
		const eng::s32 en = eng::audio::pcm_codec::encode(
		    eng::Span<const eng::u8>(pcm + c * 4u, 4u), eng::Span<eng::u8>(enc, sizeof(enc)));
		if (en <= 0) {
			return 0u;
		}
		const eng::usize e = kHeaderSize + static_cast<eng::usize>(c) * kChunkEntrySize;
		eng::audio::auzx::wr32(eng::Span<eng::u8>(dst, cap), e, at);
		eng::audio::auzx::wr32(eng::Span<eng::u8>(dst, cap), e + 4u, static_cast<eng::u32>(en));
		for (eng::s32 i = 0; i < en; ++i) {
			dst[at + static_cast<eng::usize>(i)] = enc[i];
		}
		at += static_cast<eng::u32>(en);
	}
	eng::Span<eng::u8> f {dst, cap};
	f[0] = 'A';
	f[1] = 'U';
	f[2] = 'Z';
	f[3] = 'X';
	f[4] = 1u;
	f[5] = static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaRle);
	eng::audio::auzx::wr16(f, 6u, 8000u);
	eng::audio::auzx::wr16(f, 8u, 1u);
	f[10] = 8u;
	f[11] = 0u;
	eng::audio::auzx::wr32(f, 12u, static_cast<eng::u32>(total));
	eng::audio::auzx::wr16(f, 16u, 4u);
	eng::audio::auzx::wr16(f, 18u, nchunks);
	eng::audio::auzx::wr32(f, 20u, kHeaderSize);
	eng::audio::auzx::wr32(f, 24u, static_cast<eng::u32>(kHeaderSize + entries));
	eng::audio::auzx::wr32(f, 28u, 0u);
	return at;
}

void test_auzx_dispatch() {
	const eng::u8 pcm[8] = {0, 1, 2, 3, 0, 0, 0, 0};
	eng::u8 file[256] {};
	const eng::usize n = build_auzx(pcm, 8u, file, sizeof(file));
	check(n > 0u, "media: AUZX construido");
	eng::audio::media::Info info {};
	check(eng::audio::media::open(eng::Span<const eng::u8>(file, n), info), "media: open AUZX");
	check(info.container == eng::audio::media::Container::Auzx &&
		  info.codec == eng::audio::pcm_codec::Codec::DeltaRle && info.num_chunks == 2u &&
		  info.chunk_samples == 4u && info.total_samples == 8u,
	      "media: Info del AUZX");
	check(eng::audio::media::chunk_samples(info, 0u) == 4u &&
		  eng::audio::media::chunk_samples(info, 1u) == 4u, "media: muestras por chunk");

	eng::u8 out[8] {};
	const eng::s32 d0 = eng::audio::media::decode_chunk(
	    eng::Span<const eng::u8>(file, n), info, 0u, eng::Span<eng::u8>(out, 4u));
	check(d0 == 4 && out[0] == 0u && out[3] == 3u, "media: chunk 0");
	const eng::s32 d1 = eng::audio::media::decode_chunk(
	    eng::Span<const eng::u8>(file, n), info, 1u, eng::Span<eng::u8>(out + 4u, 4u));
	check(d1 == 4 && out[4] == 0u && out[7] == 0u, "media: chunk 1");
}

void test_raw_pcm() {
	const eng::u8 raw[6] = {10u, 20u, 30u, 40u, 50u, 60u};
	eng::audio::media::Info info {};
	check(eng::audio::media::open(eng::Span<const eng::u8>(raw, 6u), info),
	      "media: open PCM crudo");
	check(info.container == eng::audio::media::Container::Pcm &&
		  info.codec == eng::audio::pcm_codec::Codec::None && info.total_samples == 6u,
	      "media: Info PCM");
	eng::u8 out[6] {};
	const eng::s32 d = eng::audio::media::decode_chunk(
	    eng::Span<const eng::u8>(raw, 6u), info, 0u, eng::Span<eng::u8>(out, 6u));
	check(d == 6 && out[5] == 60u, "media: PCM crudo = copia");
}

void test_rejections() {
	eng::audio::media::Info info {};
	check(!eng::audio::media::open(eng::Span<const eng::u8>(nullptr, 0u), info),
	      "media: blob vacio");
	eng::u8 bad[40] {};
	bad[0] = 'A';
	bad[1] = 'U';
	bad[2] = 'Z';
	bad[3] = 'X';
	check(!eng::audio::media::open(eng::Span<const eng::u8>(bad, 40u), info),
	      "media: AUZX corrupto");
}

} // namespace

int main() {
	test_auzx_dispatch();
	test_raw_pcm();
	test_rejections();
	if (failures == 0) {
		std::printf("OK: interfaz de medios (dispatch AUZX/PCM y decode por chunk).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
