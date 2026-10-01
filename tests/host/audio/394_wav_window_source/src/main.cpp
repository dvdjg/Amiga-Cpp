// HOST-394: fuente WAV host por ventanas.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "../../../../../host-tools/audio-compressor/include/audio_compressor/io/wav_source.hpp"
#include "../../../../../host-tools/audio-compressor/include/audio_compressor/io/raw_source.hpp"
#include "../../../../../host-tools/audio-compressor/include/audio_compressor/dsp/resampler.hpp"

static void put16(eng::u8* bytes, eng::usize at, eng::u16 value) {
	bytes[at] = static_cast<eng::u8>(value); bytes[at + 1u] = static_cast<eng::u8>(value >> 8u);
}

static void put32(eng::u8* bytes, eng::usize at, eng::u32 value) {
	put16(bytes, at, static_cast<eng::u16>(value)); put16(bytes, at + 2u, static_cast<eng::u16>(value >> 16u));
}

int main() {
	const std::string path = "host394.wav";
	eng::u8 wav[52]{};
	std::memcpy(wav, "RIFF", 4u); put32(wav, 4u, 44u); std::memcpy(wav + 8u, "WAVEfmt ", 8u);
	put32(wav, 16u, 16u); put16(wav, 20u, 1u); put16(wav, 22u, 2u); put32(wav, 24u, 11025u);
	put32(wav, 28u, 22050u); put16(wav, 32u, 2u); put16(wav, 34u, 8u); std::memcpy(wav + 36u, "data", 4u); put32(wav, 40u, 8u);
	wav[44] = 0u; wav[45] = 255u; wav[46] = 64u; wav[47] = 192u; wav[48] = 128u; wav[49] = 128u; wav[50] = 0u; wav[51] = 255u;
	std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(wav), sizeof(wav)); file.close();
	{
		audio_compressor::io::WavSource source;
		if (!source.open(path) || source.sample_rate() != 11025u || source.frames() != 4u) return 1;
		eng::u8 window[2]{};
		if (source.read(1u, {window, 2u}) != 2u || static_cast<eng::s8>(window[0]) != 0 || static_cast<eng::s8>(window[1]) != 0) return 1;
	}
	eng::u8 input[2] {0x80u, 0xffu}; eng::u8 output[8]{};
	audio_compressor::dsp::LinearResampler resampler {11025u, 22050u};
	const eng::usize first = resampler.process({input, 1u}, {output, 8u}, false);
	const eng::usize second = resampler.process({input + 1u, 1u}, {output, 8u}, true);
	if (first != 0u || second != 4u) return 1;
	std::remove(path.c_str());
	std::printf("OK: WavSource lee ventanas y mezcla PCM8 sin cargar el fichero completo.\n");
	return 0;
}
