// HOST-382: primera vertical de la aplicación única audio-compressor.
// El test genera un WAV mínimo, ejecuta la aplicación como proceso host y valida AUZX.
//
// Es un test **host-only** (no host del engine): depende de un binario externo. Si el binario no
// existe se **omite** (código 77) en vez de fallar, para no romper la suite en entornos sin él.
// El comando se adapta a la plataforma (sin `cmd` en POSIX). El binario se elige por
// `AUDIO_COMPRESSOR_BIN` o por defecto `out/tmp/audio-compressor/audio-compressor[.exe]`.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif

#include <eng/audio/acp1.hpp>
#include <eng/audio/auzx.hpp>
#include <eng/audio/media.hpp>
#include <eng/core/types/span.hpp>

/// Escribe una palabra little-endian en el WAV sintético.
void put16(eng::u8* data, eng::usize at, eng::u16 value) { data[at] = static_cast<eng::u8>(value); data[at + 1u] = static_cast<eng::u8>(value >> 8u); }
/// Escribe un largo little-endian en el WAV sintético.
void put32(eng::u8* data, eng::usize at, eng::u32 value) { put16(data, at, static_cast<eng::u16>(value)); put16(data, at + 2u, static_cast<eng::u16>(value >> 16u)); }

/// Genera cuatro muestras PCM8 estéreo que la CLI usa como fixture de sample y stems.
bool write_wav(const std::string& path) {
	eng::u8 data[52]{}; std::memcpy(data, "RIFF", 4u); put32(data, 4u, 44u); std::memcpy(data + 8u, "WAVEfmt ", 8u);
	put32(data, 16u, 16u); put16(data, 20u, 1u); put16(data, 22u, 2u); put32(data, 24u, 11025u); put32(data, 28u, 22050u); put16(data, 32u, 2u); put16(data, 34u, 8u);
	std::memcpy(data + 36u, "data", 4u); put32(data, 40u, 8u); data[44] = 0u; data[45] = 255u; data[46] = 64u; data[47] = 192u; data[48] = 128u; data[49] = 128u; data[50] = 0u; data[51] = 255u;
	std::FILE* out = std::fopen(path.c_str(), "wb"); if (!out) { std::perror("WAV fopen"); return false; } const bool ok = std::fwrite(data, 1u, sizeof(data), out) == sizeof(data); std::fclose(out); return ok;
}

/// Genera un WAV PCM8 **mono** de cuatro muestras (HPSS de un mono da 2 stems, dentro de la
/// política de tres voces Paula de MUSIC).
bool write_wav_mono(const std::string& path) {
	eng::u8 data[48]{}; std::memcpy(data, "RIFF", 4u); put32(data, 4u, 40u); std::memcpy(data + 8u, "WAVEfmt ", 8u);
	put32(data, 16u, 16u); put16(data, 20u, 1u); put16(data, 22u, 1u); put32(data, 24u, 11025u); put32(data, 28u, 11025u); put16(data, 32u, 1u); put16(data, 34u, 8u);
	std::memcpy(data + 36u, "data", 4u); put32(data, 40u, 4u); data[44] = 0u; data[45] = 255u; data[46] = 64u; data[47] = 192u;
	std::FILE* out = std::fopen(path.c_str(), "wb"); if (!out) { std::perror("WAV fopen"); return false; } const bool ok = std::fwrite(data, 1u, sizeof(data), out) == sizeof(data); std::fclose(out); return ok;
}

/// Ruta del binario: `AUDIO_COMPRESSOR_BIN` o el defecto por plataforma.
std::string binary_path() {
	const char* env = std::getenv("AUDIO_COMPRESSOR_BIN");
	if (env != nullptr && env[0] != '\0') {
		return env;
	}
#ifdef _WIN32
	return "out/tmp/audio-compressor/audio-compressor.exe";
#else
	return "out/tmp/audio-compressor/audio-compressor";
#endif
}

/// ¿Existe el fichero? (sin `<filesystem>`; `fopen` en lectura basta).
bool file_exists(const std::string& path) {
	std::FILE* f = std::fopen(path.c_str(), "rb");
	if (f == nullptr) {
		return false;
	}
	std::fclose(f);
	return true;
}

/// Ejecuta el binario sobre el WAV; adapta el shell a la plataforma (sin `cmd` en POSIX).
int run_binary(const std::string& bin, const std::string& in, const std::string& out, const char* mode,
	bool hpss = false, const char* extra = "") {
#ifdef _WIN32
	const std::string command = "cmd /c call \"" + bin + "\" \"" + in + "\" --mode " + mode + " --out \"" + out + "\" --force" + (hpss ? " --hpss" : "") + extra;
#else
	const std::string command = "\"" + bin + "\" \"" + in + "\" --mode " + mode + " --out \"" + out + "\" --force" + (hpss ? " --hpss" : "") + extra;
#endif
	return std::system(command.c_str());
}

/// Devuelve una carpeta temporal host utilizable por el CRT MinGW y por el proceso probado.
std::string temp_directory() {
#ifdef _WIN32
	char path[MAX_PATH]{};
	if (GetTempPathA(MAX_PATH, path) == 0u) return {};
	return path;
#else
	return "/tmp/";
#endif
}

int main() {
	const std::string binary = binary_path();
	if (!file_exists(binary)) {
		// Test host-only dependiente de un binario externo: ausente -> se omite (exit 3, como el
		// resto de tests condicionados del repo).
		std::printf("SKIP: audio-compressor no compilado en %s (ver README)\n", binary.c_str());
		return 3;
	}

	const std::string dir = temp_directory();
	if (dir.empty()) { std::fprintf(stderr, "no se pudo resolver el directorio temporal\n"); return 1; }
	const std::string input = dir + "host382_in.wav";
	const std::string mono_input = dir + "host382_in_mono.wav";
	const std::string sample_output = dir + "host382_out.auzx";
	const std::string music_output = dir + "host382_music.acp1";
	const std::string hpss_output = dir + "host382_hpss.acp1";
	const std::string hpss_mono_output = dir + "host382_hpss_mono.acp1";
	const std::string resampled_output = dir + "host382_resampled.auzx";
	const std::string invalid_output = dir + "host382_invalid.auzx";
	if (!write_wav(input) || !write_wav_mono(mono_input)) { std::fprintf(stderr, "no se pudo crear WAV de prueba\n"); return 1; }

	const int process = run_binary(binary, input, sample_output, "sample");
	if (process != 0) { std::fprintf(stderr, "audio-compressor terminó con %d\n", process); std::remove(input.c_str()); return 1; }
	std::FILE* file = std::fopen(sample_output.c_str(), "rb");
	if (file == nullptr) { std::fprintf(stderr, "no se pudo abrir AUZX de salida\n"); std::remove(input.c_str()); return 1; }
	std::fseek(file, 0, SEEK_END); const long size = std::ftell(file); std::fclose(file);
	if (size < static_cast<long>(eng::audio::auzx::kHeaderSize)) { std::fprintf(stderr, "AUZX demasiado corto (%ld)\n", size); std::remove(input.c_str()); std::remove(sample_output.c_str()); return 1; }
	if (run_binary(binary, input, resampled_output, "sample", false, " --sample-rate 22050") != 0) {
		std::fprintf(stderr, "remuestreo solicitado terminó con error\n"); return 1;
	}
	file = std::fopen(resampled_output.c_str(), "rb");
	if (file == nullptr) return 1;
	std::fseek(file, 0, SEEK_END); const long resampled_size = std::ftell(file); std::rewind(file);
	std::vector<eng::u8> resampled_bytes(static_cast<std::size_t>(resampled_size));
	const bool resampled_read = std::fread(resampled_bytes.data(), 1u, resampled_bytes.size(), file) == resampled_bytes.size(); std::fclose(file);
	eng::audio::auzx::Header resampled_header{};
	if (!resampled_read || !eng::audio::auzx::parse({resampled_bytes.data(), resampled_bytes.size()}, resampled_header) ||
		resampled_header.sample_rate != 22050u || resampled_header.total_samples != 8u) {
		std::fprintf(stderr, "--sample-rate no remuestrea la señal\n"); return 1;
	}
	if (run_binary(binary, input, invalid_output, "sample", false, " --codec typo") == 0) {
		std::fprintf(stderr, "codec inválido fue aceptado\n"); return 1;
	}
	const int music_process = run_binary(binary, input, music_output, "music");
	if (music_process != 0) { std::fprintf(stderr, "audio-compressor music terminó con %d\n", music_process); std::remove(input.c_str()); std::remove(sample_output.c_str()); return 1; }
	file = std::fopen(music_output.c_str(), "rb");
	if (file == nullptr) { std::fprintf(stderr, "no se pudo abrir ACP1 de salida\n"); return 1; }
	std::fseek(file, 0, SEEK_END); const long acp1_size = std::ftell(file); std::rewind(file);
	std::vector<eng::u8> acp1_bytes(static_cast<std::size_t>(acp1_size));
	const bool read_ok = std::fread(acp1_bytes.data(), 1u, acp1_bytes.size(), file) == acp1_bytes.size();
	std::fclose(file);
	eng::audio::acp1::Info acp1_info{};
	const bool valid_acp1 = read_ok && eng::audio::acp1::parse({acp1_bytes.data(), acp1_bytes.size()}, acp1_info);
	if (!valid_acp1 || acp1_info.track_count != 2u || acp1_info.total_samples != 4u) {
		std::fprintf(stderr, "ACP1 no conserva las dos pistas WAV sincronizadas\n"); return 1;
	}
	const eng::u8 expected[2][4] {{0x80u, 0xc0u, 0x00u, 0x80u}, {0x7fu, 0x40u, 0x00u, 0x7fu}};
	for (eng::u16 track_index = 0u; track_index < 2u; ++track_index) {
		eng::audio::acp1::Unit unit{};
		if (!eng::audio::acp1::unit({acp1_bytes.data(), acp1_bytes.size()}, acp1_info, track_index, unit)) return 1;
		eng::audio::media::Info media_info{};
		if (!eng::audio::media::open(unit.payload, media_info)) return 1;
		eng::u8 rebuilt[4]{};
		eng::usize cursor = 0u;
		for (eng::u16 chunk = 0u; chunk < media_info.num_chunks; ++chunk) {
			const eng::u32 count = eng::audio::media::chunk_samples(media_info, chunk);
			const eng::s32 decoded = eng::audio::media::decode_chunk(unit.payload, media_info, chunk,
				{rebuilt + cursor, count});
			if (decoded < 0) return 1;
			cursor += static_cast<eng::usize>(decoded);
		}
		if (cursor < 4u || std::memcmp(rebuilt, expected[track_index], 4u) != 0) {
			std::fprintf(stderr, "round-trip ACP1 alteró muestras del stem %u\n", track_index); return 1;
		}
	}
	// Política de salida (ROADMAP_AUDIO_COMPRESSOR_REFACTOR §Política): MUSIC con pitch/volumen
	// variable se restringe a tres voces Paula y falla antes de publicar ACP1 si necesita más.
	// HPSS de un estéreo da 4 stems -> debe rechazarse; HPSS de un mono (2 stems) sí cabe.
	if (run_binary(binary, input, hpss_output, "music", true) == 0) {
		std::fprintf(stderr, "--hpss de estéreo (4 pistas) fue aceptado pese a la política de tres voces Paula\n"); return 1;
	}
	const int hpss_process = run_binary(binary, mono_input, hpss_mono_output, "music", true);
	if (hpss_process != 0) { std::fprintf(stderr, "audio-compressor music --hpss (mono) terminó con %d\n", hpss_process); return 1; }
	file = std::fopen(hpss_mono_output.c_str(), "rb");
	if (file == nullptr) { std::fprintf(stderr, "no se pudo abrir salida ACP1 HPSS\n"); return 1; }
	std::fseek(file, 0, SEEK_END); const long hpss_size = std::ftell(file); std::rewind(file);
	std::vector<eng::u8> hpss_bytes(static_cast<std::size_t>(hpss_size));
	const bool hpss_read = std::fread(hpss_bytes.data(), 1u, hpss_bytes.size(), file) == hpss_bytes.size(); std::fclose(file);
	eng::audio::acp1::Info hpss_info{};
	if (!hpss_read || !eng::audio::acp1::parse({hpss_bytes.data(), hpss_bytes.size()}, hpss_info) || hpss_info.track_count != 2u) {
		std::fprintf(stderr, "HPSS de mono no genera las dos capas armónica/percusiva\n"); return 1;
	}
	const std::string flac_input = dir + "host382_in.flac";
	const std::string flac_output = dir + "host382_ffmpeg.acp1";
#ifdef _WIN32
	const std::string ffmpeg_encode = "cmd /c ffmpeg -y -v error -i \"" + input + "\" -af apad=pad_len=1024 -c:a flac \"" + flac_input + "\"";
#else
	const std::string ffmpeg_encode = "ffmpeg -y -v error -i \"" + input + "\" -af apad=pad_len=1024 -c:a flac \"" + flac_input + "\"";
#endif
	const bool ffmpeg_available = std::system(ffmpeg_encode.c_str()) == 0;
	if (ffmpeg_available) {
		const int ffmpeg_process = run_binary(binary, flac_input, flac_output, "music");
		if (ffmpeg_process != 0) { std::fprintf(stderr, "audio-compressor no preservó WAV multicanal decodificado por FFmpeg\n"); return 1; }
		file = std::fopen(flac_output.c_str(), "rb");
		if (file == nullptr) { std::fprintf(stderr, "no se pudo abrir ACP1 de FFmpeg\n"); return 1; }
		std::fseek(file, 0, SEEK_END); const long ffmpeg_size = std::ftell(file); std::rewind(file);
		std::vector<eng::u8> ffmpeg_bytes(static_cast<std::size_t>(ffmpeg_size));
		const bool ffmpeg_read = std::fread(ffmpeg_bytes.data(), 1u, ffmpeg_bytes.size(), file) == ffmpeg_bytes.size(); std::fclose(file);
		eng::audio::acp1::Info ffmpeg_info{};
		if (!ffmpeg_read || !eng::audio::acp1::parse({ffmpeg_bytes.data(), ffmpeg_bytes.size()}, ffmpeg_info) || ffmpeg_info.track_count != 2u) {
			std::fprintf(stderr, "FFmpeg downmixó o perdió canales al normalizar FLAC\n"); return 1;
		}
	}
	std::remove(input.c_str()); std::remove(mono_input.c_str()); std::remove(sample_output.c_str()); std::remove(resampled_output.c_str());
	std::remove(invalid_output.c_str()); std::remove(music_output.c_str());
	std::remove(hpss_output.c_str()); std::remove(hpss_mono_output.c_str());
	std::remove(flac_input.c_str()); std::remove(flac_output.c_str());
	std::remove((music_output + ".linear.auzx").c_str());
	std::remove((hpss_output + ".linear.auzx").c_str());
	std::remove((hpss_mono_output + ".linear.auzx").c_str());
	std::printf("OK: CLI produce AUZX sample, ACP1 sincronizado, HPSS mono (2 pistas) y rechaza HPSS estéreo (>3 voces Paula)%s.\n", ffmpeg_available ? "; FFmpeg conserva canales FLAC" : "");
	return 0;
}
