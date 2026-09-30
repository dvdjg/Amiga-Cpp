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
int run_binary(const std::string& bin, const std::string& in, const std::string& out, const char* mode) {
#ifdef _WIN32
	const std::string command = "cmd /c call \"" + bin + "\" \"" + in + "\" --mode " + mode + " --out \"" + out + "\" --force";
#else
	const std::string command = "\"" + bin + "\" \"" + in + "\" --mode " + mode + " --out \"" + out + "\" --force";
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
	const std::string sample_output = dir + "host382_out.auzx";
	const std::string music_output = dir + "host382_music.acp1";
	if (!write_wav(input)) { std::fprintf(stderr, "no se pudo crear WAV de prueba\n"); return 1; }

	const int process = run_binary(binary, input, sample_output, "sample");
	if (process != 0) { std::fprintf(stderr, "audio-compressor terminó con %d\n", process); std::remove(input.c_str()); return 1; }
	std::FILE* file = std::fopen(sample_output.c_str(), "rb");
	if (file == nullptr) { std::fprintf(stderr, "no se pudo abrir AUZX de salida\n"); std::remove(input.c_str()); return 1; }
	std::fseek(file, 0, SEEK_END); const long size = std::ftell(file); std::fclose(file);
	if (size < static_cast<long>(eng::audio::auzx::kHeaderSize)) { std::fprintf(stderr, "AUZX demasiado corto (%ld)\n", size); std::remove(input.c_str()); std::remove(sample_output.c_str()); return 1; }
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
	std::remove(input.c_str()); std::remove(sample_output.c_str()); std::remove(music_output.c_str());
	std::remove((music_output + ".linear.auzx").c_str());
	if (!valid_acp1 || acp1_info.track_count != 2u || acp1_info.total_samples != 4u) {
		std::fprintf(stderr, "ACP1 no conserva las dos pistas WAV sincronizadas\n"); return 1;
	}
	std::printf("OK: CLI produce AUZX sample y ACP1 sincronizado de dos stems WAV.\n");
	return 0;
}
