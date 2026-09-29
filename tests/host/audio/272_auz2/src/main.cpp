#include <cstdio>

#include <eng/audio/auz2.hpp>

int main() {
	using namespace eng;
	u8 pcm[257]{}; for (usize i = 0u; i < 257u; ++i) pcm[i] = static_cast<u8>(i * 3u);
	u8 compressed[512]{}; const s32 size = audio::pcm_codec::encode(Span<const u8>{pcm, 128u}, Span<u8>{compressed});
	if (size <= 0) return 1;
	u8 file[1024]{}; audio::auz2::Header header{audio::auz2::kVersion, 11025u, 1u, 8u, 128u, 128u, 1u};
	if (!audio::auz2::write_header(header, Span<u8>{file})) return 1;
	usize cursor = audio::auz2::kHeaderBytes;
	const u16 samples = 128u;
	file[cursor++] = static_cast<u8>(samples); file[cursor++] = static_cast<u8>(samples >> 8u);
	file[cursor++] = static_cast<u8>(size); file[cursor++] = static_cast<u8>(size >> 8u);
	file[cursor++] = static_cast<u8>(audio::pcm_codec::Codec::DeltaRle); cursor += 3u;
	for (s32 i = 0; i < size; ++i) file[cursor++] = compressed[i];
	// Cabecera y payload se validan aquí; los vectores de codecs tienen tests propios.
	audio::auz2::Header decoded{};
	if (!audio::auz2::read_header(Span<const u8>{file}, decoded)) return 1;
	if (decoded.total_samples != 128u || decoded.chunk_samples != 128u || decoded.chunk_count != 1u) return 1;
	audio::auz2::Chunk chunk{}; Span<const u8> payload{}; usize read_cursor = audio::auz2::kHeaderBytes;
	if (!audio::auz2::read_chunk(Span<const u8>{file}, read_cursor, chunk, payload)) return 1;
	u8 output[128]{};
	if (audio::auz2::decode_chunk(chunk, payload, Span<u8>{output}) != 128) return 1;
	for (usize i = 0u; i < 128u; ++i) if (output[i] != pcm[i]) return 1;
	std::printf("OK: cabecera y chunk AUZ2 portables validados.\n"); return 0;
}
