#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <eng/audio/auz2.hpp>

namespace {
using namespace eng;

struct Bytes { u8* data = nullptr; usize size = 0u; };

void destroy(Bytes& bytes) { std::free(bytes.data); bytes = {}; }

bool load_file(const char* path, Bytes& out) {
	FILE* file = std::fopen(path, "rb");
	if (!file) return false;
	std::fseek(file, 0, SEEK_END);
	const long length = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);
	if (length <= 0) { std::fclose(file); return false; }
	out.data = static_cast<u8*>(std::malloc(static_cast<usize>(length)));
	out.size = static_cast<usize>(length);
	const bool ok = out.data && std::fread(out.data, 1u, out.size, file) == out.size;
	std::fclose(file);
	if (!ok) destroy(out);
	return ok;
}

bool save_file(const char* path, Span<const u8> bytes) {
	FILE* file = std::fopen(path, "wb");
	if (!file) return false;
	const bool ok = std::fwrite(bytes.data(), 1u, bytes.size(), file) == bytes.size();
	std::fclose(file);
	return ok;
}

u16 le16(const u8* p) { return static_cast<u16>(p[0]) | static_cast<u16>(p[1] << 8u); }
u32 le32(const u8* p) { return static_cast<u32>(le16(p)) | (static_cast<u32>(le16(p + 2u)) << 16u); }

bool load_pcm(const char* path, bool raw, u32 raw_rate, Bytes& pcm, u32& rate) {
	Bytes input;
	if (!load_file(path, input)) return false;
	if (raw) {
		pcm = input; rate = raw_rate; return rate != 0u;
	}
	if (input.size < 44u || std::memcmp(input.data, "RIFF", 4u) != 0 ||
		std::memcmp(input.data + 8u, "WAVE", 4u) != 0) { destroy(input); return false; }
	usize fmt = 0u, data = 0u, data_size = 0u;
	for (usize at = 12u; at + 8u <= input.size;) {
		const u32 size = le32(input.data + at + 4u);
		if (std::memcmp(input.data + at, "fmt ", 4u) == 0) fmt = at + 8u;
		if (std::memcmp(input.data + at, "data", 4u) == 0) { data = at + 8u; data_size = size; break; }
		at += 8u + size + (size & 1u);
	}
	if (!fmt || !data || fmt + 16u > input.size || data + data_size > input.size ||
		le16(input.data + fmt) != 1u || le16(input.data + fmt + 2u) != 1u) { destroy(input); return false; }
	const u16 bits = le16(input.data + fmt + 14u);
	rate = le32(input.data + fmt + 4u);
	if (bits == 8u) {
		pcm.data = static_cast<u8*>(std::malloc(data_size)); pcm.size = data_size;
		if (pcm.data) {
			for (usize i = 0u; i < pcm.size; ++i) {
				// WAV PCM8 es unsigned; AUZ2 conserva la escala signed de Paula.
				pcm.data[i] = static_cast<u8>(input.data[data + i] - 128u);
			}
		}
	} else if (bits == 16u) {
		pcm.size = data_size / 2u; pcm.data = static_cast<u8*>(std::malloc(pcm.size));
		for (usize i = 0u; pcm.data && i < pcm.size; ++i) {
			const s16 sample = static_cast<s16>(le16(input.data + data + i * 2u));
			// El byte alto conserva directamente el patrón signed PCM8.
			pcm.data[i] = static_cast<u8>(static_cast<s32>(sample) >> 8u);
		}
	} else { destroy(input); return false; }
	destroy(input);
	return pcm.data != nullptr && rate != 0u;
}

eng::audio::pcm_codec::Codec parse_codec(const char* text) {
	if (std::strcmp(text, "raw") == 0) return eng::audio::pcm_codec::Codec::None;
	return eng::audio::pcm_codec::Codec::DeltaRle;
}

bool verify(Span<const u8> encoded, Span<const u8> expected) {
	eng::audio::auz2::Header header;
	if (!eng::audio::auz2::read_header(encoded, header)) return false;
	usize cursor = eng::audio::auz2::kHeaderBytes, samples = 0u;
	u8 decoded[2048]{};
	while (samples < header.total_samples) {
		eng::audio::auz2::Chunk chunk;
		Span<const u8> payload;
		if (!eng::audio::auz2::read_chunk(encoded, cursor, chunk, payload) || chunk.raw_samples > sizeof(decoded)) return false;
		if (eng::audio::auz2::decode_chunk(chunk, payload, Span<u8>{decoded, chunk.raw_samples}) < 0) return false;
		for (usize i = 0u; i < chunk.raw_samples; ++i) if (decoded[i] != expected[samples + i]) return false;
		samples += chunk.raw_samples;
	}
	return samples == header.total_samples;
}

int pack(int argc, char** argv) {
	if (argc < 4) return 2;
	const char* input_path = argv[2]; const char* output_path = argv[3];
	bool raw = false, do_verify = false; u32 rate = 0u; u16 chunk_samples = 1024u;
	eng::audio::pcm_codec::Codec codec = eng::audio::pcm_codec::Codec::DeltaRle;
	for (int i = 4; i < argc; ++i) {
		if (std::strcmp(argv[i], "--raw-rate") == 0 && i + 1 < argc) { raw = true; rate = static_cast<u32>(std::strtoul(argv[++i], nullptr, 10)); }
		else if (std::strcmp(argv[i], "--chunk") == 0 && i + 1 < argc) chunk_samples = static_cast<u16>(std::strtoul(argv[++i], nullptr, 10));
		else if (std::strcmp(argv[i], "--codec") == 0 && i + 1 < argc) codec = parse_codec(argv[++i]);
		else if (std::strcmp(argv[i], "--verify") == 0) do_verify = true;
		else if (std::strcmp(argv[i], "--raw") == 0) raw = true;
	}
	Bytes pcm;
	if (!load_pcm(input_path, raw, rate, pcm, rate) || chunk_samples == 0u || chunk_samples > 2048u) { std::fprintf(stderr, "entrada WAV/RAW o parámetros inválidos\n"); return 1; }
	const u16 chunks = static_cast<u16>((pcm.size + chunk_samples - 1u) / chunk_samples);
	const usize capacity = eng::audio::auz2::kHeaderBytes + static_cast<usize>(chunks) * (eng::audio::auz2::kChunkHeaderBytes + chunk_samples + 4u);
	u8* encoded = static_cast<u8*>(std::malloc(capacity));
	if (!encoded) { destroy(pcm); return 1; }
	eng::audio::auz2::Header header{eng::audio::auz2::kVersion, rate, 1u, 8u, static_cast<u32>(pcm.size), chunk_samples, chunks};
	if (!eng::audio::auz2::write_header(header, Span<u8>{encoded, capacity})) { std::free(encoded); destroy(pcm); return 1; }
	usize out = eng::audio::auz2::kHeaderBytes;
	u8 packed[4096]{};
	for (usize at = 0u; at < pcm.size;) {
		const u16 samples = static_cast<u16>((pcm.size - at < chunk_samples) ? pcm.size - at : chunk_samples);
		const s32 packed_size = codec == eng::audio::pcm_codec::Codec::None ? samples : eng::audio::pcm_codec::encode(Span<const u8>{pcm.data + at, samples}, Span<u8>{packed, sizeof(packed)});
		if (packed_size < 0) { std::free(encoded); destroy(pcm); return 1; }
		const usize n = static_cast<usize>(packed_size);
		encoded[out++] = static_cast<u8>(samples); encoded[out++] = static_cast<u8>(samples >> 8u);
		encoded[out++] = static_cast<u8>(n); encoded[out++] = static_cast<u8>(n >> 8u); encoded[out++] = static_cast<u8>(codec); encoded[out++] = 0u; encoded[out++] = 0u; encoded[out++] = 0u;
		if (codec == eng::audio::pcm_codec::Codec::None) std::memcpy(encoded + out, pcm.data + at, n); else std::memcpy(encoded + out, packed, n);
		out += n; at += samples;
	}
	const bool ok = save_file(output_path, Span<const u8>{encoded, out}) && (!do_verify || verify(Span<const u8>{encoded, out}, Span<const u8>{pcm.data, pcm.size}));
	std::printf("AUZ2: %lu muestras, %u chunks, %lu bytes, ratio %.3f\n", static_cast<unsigned long>(pcm.size), chunks, static_cast<unsigned long>(out), static_cast<double>(out) / static_cast<double>(pcm.size));
	std::free(encoded); destroy(pcm); return ok ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
	if (argc >= 2 && std::strcmp(argv[1], "pack") == 0) return pack(argc, argv);
	std::fprintf(stderr, "Uso: %s pack input.wav output.auz2 [--codec delta-rle|raw] [--chunk N] [--raw-rate Hz] [--verify]\n", argv[0]);
	return 2;
}
