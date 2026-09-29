// HOST-382: primera vertical de la aplicación única audio-compressor.
// El test genera un WAV mínimo, ejecuta la aplicación como proceso host y valida AUZX.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <eng/audio/auzx.hpp>
#include <eng/core/types/span.hpp>

/// Escribe una palabra little-endian en el WAV sintético.
void put16(eng::u8* data, eng::usize at, eng::u16 value) { data[at] = static_cast<eng::u8>(value); data[at + 1u] = static_cast<eng::u8>(value >> 8u); }
/// Escribe un largo little-endian en el WAV sintético.
void put32(eng::u8* data, eng::usize at, eng::u32 value) { put16(data, at, static_cast<eng::u16>(value)); put16(data, at + 2u, static_cast<eng::u16>(value >> 16u)); }

/// Genera cuatro muestras PCM8 estéreo que el loader debe convertir a mono.
bool write_wav(const std::string& path) {
	eng::u8 data[52]{}; std::memcpy(data, "RIFF", 4u); put32(data, 4u, 44u); std::memcpy(data + 8u, "WAVEfmt ", 8u);
	put32(data, 16u, 16u); put16(data, 20u, 1u); put16(data, 22u, 2u); put32(data, 24u, 11025u); put32(data, 28u, 22050u); put16(data, 32u, 2u); put16(data, 34u, 8u);
	std::memcpy(data + 36u, "data", 4u); put32(data, 40u, 8u); data[44] = 0u; data[45] = 255u; data[46] = 64u; data[47] = 192u; data[48] = 128u; data[49] = 128u; data[50] = 0u; data[51] = 255u;
	std::FILE* out = std::fopen(path.c_str(), "wb"); if (!out) { std::perror("WAV fopen"); return false; } const bool ok = std::fwrite(data, 1u, sizeof(data), out) == sizeof(data); std::fclose(out); return ok;
}

/// Ejecuta el binario construido por el README del test sobre el WAV sintético.
int main() {
	char input_name[L_tmpnam]{}; char output_name[L_tmpnam]{};
	if (!std::tmpnam(input_name) || !std::tmpnam(output_name)) return 1;
	const std::string input = input_name; const std::string output = output_name;
	if (!write_wav(input)) { std::fprintf(stderr, "no se pudo crear WAV de prueba\n"); return 1; }
	const char* binary = std::getenv("AUDIO_COMPRESSOR_BIN");
	if (binary == nullptr) binary = "out/tmp/audio-compressor/audio-compressor.exe";
	const std::string command = std::string{"cmd /c call \""} + binary + "\" \"" + input + "\" --mode sample --out \"" + output + "\" --force";
	const int process = std::system(command.c_str());
	if (process != 0) { std::fprintf(stderr, "audio-compressor terminó con %d\n", process); return 1; }
	std::FILE* file = std::fopen(output.c_str(), "rb"); if (!file) { std::fprintf(stderr, "no se pudo abrir AUZX de salida\n"); return 1; } std::fseek(file, 0, SEEK_END); const long size = std::ftell(file); std::fclose(file);
	std::remove(input.c_str()); std::remove(output.c_str());
	if (size < static_cast<long>(eng::audio::auzx::kHeaderSize)) return 1;
	std::printf("OK: aplicación audio-compressor produce AUZX desde WAV.\n"); return 0;
}
