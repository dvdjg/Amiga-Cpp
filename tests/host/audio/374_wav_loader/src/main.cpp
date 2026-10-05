#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif

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

/// Construye dos frames WAV PCM8 de tres canales con muestras distinguibles.
std::vector<eng::u8> wav8_three_channel() {
	std::vector<eng::u8> out(44u + 6u, 0u);
	std::memcpy(out.data(), "RIFF", 4u); put32(out, 4u, static_cast<eng::u32>(out.size() - 8u));
	std::memcpy(out.data() + 8u, "WAVEfmt ", 8u); put32(out, 16u, 16u); put16(out, 20u, 1u);
	put16(out, 22u, 3u); put32(out, 24u, 32000u); put32(out, 28u, 96000u); put16(out, 32u, 3u); put16(out, 34u, 8u);
	std::memcpy(out.data() + 36u, "data", 4u); put32(out, 40u, 6u);
	out[44] = 0u; out[45] = 127u; out[46] = 255u;
	out[47] = 128u; out[48] = 64u; out[49] = 192u;
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
	char temp_dir[512]{};
#if defined(_WIN32)
	if (GetTempPathA(static_cast<DWORD>(sizeof(temp_dir)), temp_dir) == 0u) return 1;
#else
	std::strcpy(temp_dir, "/tmp/");
#endif
	char stereo_path[512]{}, multichannel_path[512]{}, mono_path[512]{};
	std::snprintf(stereo_path, sizeof(stereo_path), "%swav-loader-stereo.tmp", temp_dir);
	std::snprintf(multichannel_path, sizeof(multichannel_path), "%swav-loader-multichannel.tmp", temp_dir);
	std::snprintf(mono_path, sizeof(mono_path), "%swav-loader-mono.tmp", temp_dir);
	std::vector<eng::u8> stereo = wav8_stereo(), multichannel = wav8_three_channel(), mono = wav16_mono();
	std::FILE* file = std::fopen(stereo_path, "wb"); if (!file) { std::fprintf(stderr, "WAV fixture open failed: %s (%s)\n", stereo_path, std::strerror(errno)); return 1; } std::fwrite(stereo.data(), 1u, stereo.size(), file); std::fclose(file);
	file = std::fopen(multichannel_path, "wb"); if (!file) { std::fprintf(stderr, "WAV fixture open failed: %s (%s)\n", multichannel_path, std::strerror(errno)); return 1; } std::fwrite(multichannel.data(), 1u, multichannel.size(), file); std::fclose(file);
	file = std::fopen(mono_path, "wb"); if (!file) { std::fprintf(stderr, "WAV fixture open failed: %s (%s)\n", mono_path, std::strerror(errno)); return 1; } std::fwrite(mono.data(), 1u, mono.size(), file); std::fclose(file);
	std::vector<eng::u8> pcm; eng::u16 rate = 0u;
	check(pack_pcm::load(stereo_path, pcm, rate), "WAV8 estéreo acepta");
	check(rate == 11025u && pcm.size() == 4u && pcm[0] == 0x00u && pcm[1] == 0x00u && pcm[2] == 0x00u && pcm[3] == 0x00u, "downmix WAV8 y signo");
	pack_pcm::WavStems stems {};
	check(pack_pcm::load_stems(stereo_path, stems), "WAV8 estéreo conserva sus canales");
	check(stems.sample_rate == 11025u && stems.channels.size() == 2u && stems.channels[0].size() == 4u &&
		stems.channels[0][0] == 0x80u && stems.channels[0][1] == 0xc0u &&
		stems.channels[1][0] == 0x7fu && stems.channels[1][1] == 0x40u, "stems estéreo mantienen canal, orden y signo");
	// Sin override: conserva los canales intercalados y la tasa del WAV.
	check(pack_pcm::load_stems(multichannel_path, stems), "WAV8 de tres canales acepta");
	check(stems.sample_rate == 32000u && stems.source_sample_rate == 32000u &&
		stems.channels.size() == 3u && stems.channels[0].size() == 2u &&
		stems.channels[0][0] == 0x80u && stems.channels[0][1] == 0x00u &&
		stems.channels[1][0] == 0xffu && stems.channels[1][1] == 0xc0u &&
		stems.channels[2][0] == 0x7fu && stems.channels[2][1] == 0x40u,
		"WAV multicanal conserva stems intercalados y convierte PCM8 sin mezclar");
	// Con override: remuestrea a 22050 manteniendo la separación de canales.
	check(pack_pcm::load_stems(multichannel_path, stems, 22050u), "WAV8 de tres canales acepta override");
	check(stems.sample_rate == 22050u && stems.source_sample_rate == 32000u &&
		stems.channels.size() == 3u && stems.channels[0].size() == 1u &&
		stems.channels[0][0] == 0x80u && stems.channels[1][0] == 0xffu &&
		stems.channels[2][0] == 0x7fu,
		"WAV multicanal con override remuestrea conservando los canales");
	// Sin override (PCM16 -> PCM8) y con override (remuestreo 16000 -> 22050).
	check(pack_pcm::load(mono_path, pcm, rate), "WAV16 mono acepta");
	check(rate == 16000u && pcm.size() == 2u && pcm[0] == 0x80u && pcm[1] == 0x7fu,
		"WAV16 PCM8 y tasa de origen");
	check(pack_pcm::load(mono_path, pcm, rate, 22050u), "WAV16 mono acepta override");
	check(rate == 22050u && pcm.size() == 3u && pcm[0] == 0x80u && pcm[1] == 0x39u && pcm[2] == 0x7fu,
		"WAV16 con override remuestrea");
	std::remove(stereo_path); std::remove(multichannel_path); std::remove(mono_path);
	if (failures == 0) { std::printf("OK: ingestión WAV PCM8/PCM16 mono/estéreo y stems multicanal validada.\n"); return 0; }
	return 1;
}
