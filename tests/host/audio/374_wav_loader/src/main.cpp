#include <cstdio>
#include <vector>

#include "../../../../../host-tools/pack-pcm/wav_loader.hpp"

namespace {

void put16(std::vector<eng::u8>& out, eng::usize at, eng::u16 value) {
	out[at] = static_cast<eng::u8>(value); out[at + 1u] = static_cast<eng::u8>(value >> 8u);
}
void put32(std::vector<eng::u8>& out, eng::usize at, eng::u32 value) {
	put16(out, at, static_cast<eng::u16>(value)); put16(out, at + 2u, static_cast<eng::u16>(value >> 16u));
}

std::vector<eng::u8> wav8_stereo() {
	std::vector<eng::u8> out(48u + 8u, 0u);
	std::memcpy(out.data(), "RIFF", 4u); put32(out, 4u, static_cast<eng::u32>(out.size() - 8u));
	std::memcpy(out.data() + 8u, "WAVEfmt ", 8u); put32(out, 16u, 16u); put16(out, 20u, 1u);
	put16(out, 22u, 2u); put32(out, 24u, 11025u); put32(out, 28u, 22050u); put16(out, 32u, 2u); put16(out, 34u, 8u);
	std::memcpy(out.data() + 36u, "data", 4u); put32(out, 40u, 8u);
	out[44] = 0u; out[45] = 255u; out[46] = 64u; out[47] = 192u;
	out[48] = 128u; out[49] = 128u; out[50] = 0u; out[51] = 255u;
	return out;
}

std::vector<eng::u8> wav16_mono() {
	std::vector<eng::u8> out(48u, 0u);
	std::memcpy(out.data(), "RIFF", 4u); put32(out, 4u, 40u); std::memcpy(out.data() + 8u, "WAVEfmt ", 8u);
	put32(out, 16u, 16u); put16(out, 20u, 1u); put16(out, 22u, 1u); put32(out, 24u, 16000u);
	put32(out, 28u, 32000u); put16(out, 32u, 2u); put16(out, 34u, 16u); std::memcpy(out.data() + 36u, "data", 4u); put32(out, 40u, 4u);
	put16(out, 44u, 0x8000u); put16(out, 46u, 0x7f00u); return out;
}

int failures = 0;
void check(bool ok, const char* message) { if (!ok) { std::printf("FAIL: %s\n", message); ++failures; } }

} // namespace

int main() {
	// El loader opera sobre rutas; estos tests usan ficheros temporales deterministas del host.
	const char* stereo_path = "out/tmp/wav-loader-stereo.wav";
	const char* mono_path = "out/tmp/wav-loader-mono.wav";
	std::vector<eng::u8> stereo = wav8_stereo(), mono = wav16_mono();
	std::FILE* file = std::fopen(stereo_path, "wb"); if (!file) return 1; std::fwrite(stereo.data(), 1u, stereo.size(), file); std::fclose(file);
	file = std::fopen(mono_path, "wb"); if (!file) return 1; std::fwrite(mono.data(), 1u, mono.size(), file); std::fclose(file);
	std::vector<eng::u8> pcm; eng::u16 rate = 0u;
	check(pack_pcm::load(stereo_path, pcm, rate), "WAV8 estéreo acepta");
	check(rate == 11025u && pcm.size() == 4u && pcm[0] == 0x00u && pcm[1] == 0x00u && pcm[2] == 0x00u && pcm[3] == 0x00u, "downmix WAV8 y signo");
	check(pack_pcm::load(mono_path, pcm, rate, 22050u), "WAV16 mono acepta");
	check(rate == 22050u && pcm.size() == 2u && pcm[0] == 0x80u && pcm[1] == 0x7fu, "WAV16 y override de tasa");
	std::remove(stereo_path); std::remove(mono_path);
	if (failures == 0) { std::printf("OK: ingestión WAV PCM8/PCM16 mono/estéreo validada.\n"); return 0; }
	return 1;
}
