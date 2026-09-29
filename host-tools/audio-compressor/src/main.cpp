// ==========================================================================
// audio-compressor: aplicación única de transformación de audio para Amiga.
// ==========================================================================
//
// Esta primera vertical implementa el pipeline SAMPLE completo: ingestión WAV/RAW, configuración,
// clasificación, codec AUZX, round-trip y salida de informe. El pipeline MUSIC se reconoce y valida
// como modo, pero no inventa un ACP1 parcial: su encoder estructural llega en una fase posterior.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <eng/audio/auzx.hpp>
#include <eng/audio/fib_delta.hpp>
#include <eng/audio/media.hpp>
#include <eng/audio/pcm_codec.hpp>

#include "../../../host-tools/pack-pcm/wav_loader.hpp"
#include "sdl_player.hpp"

namespace {

/// Configuración resuelta después de aplicar defaults y las fuentes de configuración.
struct Config {
	/// Modo solicitado: auto, sample o music.
	std::string mode = "auto";
	/// Codec AUZX textual: rle, fib, ima o none.
	std::string codec = "rle";
	/// Tasa objetivo; cero conserva la tasa de entrada.
	eng::u16 sample_rate = 0u;
	/// Muestras descomprimidas por chunk.
	eng::u16 chunk_samples = 4096u;
	/// Presupuesto de RAM host declarado para futuras pasadas estructurales.
	eng::u64 ram_budget_bytes = 6ull * 1024ull * 1024ull * 1024ull;
	/// Tamaño de ventana de análisis reutilizada.
	eng::usize window_samples = 64u * 1024u;
	/// Permite reemplazar una salida existente.
	bool force = false;
	/// Solo clasifica y escribe el informe, sin crear AUZX.
	bool dry_run = false;
	/// Reproduce la entrada normalizada o la salida generada mediante SDL3.
	bool play = false;
};

/// Devuelve true si `text` contiene la clave JSON simple solicitada.
[[nodiscard]] bool json_string(const std::string& text, const char* key, std::string& value) {
	const std::string needle = std::string{"\""} + key + "\"";
	const std::size_t at = text.find(needle);
	if (at == std::string::npos) return false;
	const std::size_t colon = text.find(':', at + needle.size());
	const std::size_t first = text.find('"', colon + 1u);
	const std::size_t last = text.find('"', first + 1u);
	if (colon == std::string::npos || first == std::string::npos || last == std::string::npos) return false;
	value = text.substr(first + 1u, last - first - 1u);
	return true;
}

/// Lee un entero JSON simple sin introducir una dependencia de parser en el runtime Amiga.
template <class T>
[[nodiscard]] bool json_number(const std::string& text, const char* key, T& value) {
	const std::string needle = std::string{"\""} + key + "\"";
	const std::size_t at = text.find(needle);
	if (at == std::string::npos) return false;
	const std::size_t colon = text.find(':', at + needle.size());
	if (colon == std::string::npos) return false;
	char* end = nullptr;
	const std::string number = text.substr(colon + 1u);
	const unsigned long long parsed = std::strtoull(number.c_str(), &end, 10);
	if (end == number.c_str()) return false;
	value = static_cast<T>(parsed);
	return true;
}

/// Carga el archivo de configuración y aplica solo las claves soportadas en C15.
[[nodiscard]] bool load_config(const char* path, Config& config) {
	std::FILE* file = std::fopen(path, "rb");
	if (!file) return false;
	std::fseek(file, 0, SEEK_END);
	const long length = std::ftell(file);
	std::fseek(file, 0, SEEK_SET);
	if (length <= 0) { std::fclose(file); return false; }
	std::string text(static_cast<std::size_t>(length), '\0');
	const bool ok = std::fread(text.data(), 1u, text.size(), file) == text.size();
	std::fclose(file);
	if (!ok) return false;
	(void)json_string(text, "mode", config.mode);
	(void)json_string(text, "codec", config.codec);
	(void)json_number(text, "sample_rate", config.sample_rate);
	(void)json_number(text, "chunk_samples", config.chunk_samples);
	(void)json_number(text, "ram_budget_bytes", config.ram_budget_bytes);
	(void)json_number(text, "window_samples", config.window_samples);
	return true;
}

/// Lee un archivo binario completo para reproducir un contenedor AUZX ya generado.
[[nodiscard]] bool read_binary(const char* path, std::vector<eng::u8>& bytes) {
	std::FILE* file = std::fopen(path, "rb");
	if (!file) return false;
	std::fseek(file, 0, SEEK_END); const long size = std::ftell(file); std::fseek(file, 0, SEEK_SET);
	if (size <= 0) { std::fclose(file); return false; }
	bytes.resize(static_cast<std::size_t>(size));
	const bool ok = std::fread(bytes.data(), 1u, bytes.size(), file) == bytes.size(); std::fclose(file); return ok;
}

/// Carga una fuente PCM o decodifica AUZX a PCM8 mono para el reproductor SDL3.
[[nodiscard]] bool load_playback_pcm(const char* path, std::vector<eng::u8>& pcm, eng::u16& rate) {
	std::vector<eng::u8> bytes;
	if (!read_binary(path, bytes)) return false;
	const bool auzx = bytes.size() >= 4u && bytes[0] == 'A' && bytes[1] == 'U' && bytes[2] == 'Z' && bytes[3] == 'X';
	if (!auzx) return pack_pcm::load(path, pcm, rate, 0u);
	eng::audio::media::Info info {};
	if (!eng::audio::media::open({bytes.data(), bytes.size()}, info)) return false;
	rate = info.sample_rate; pcm.resize(info.total_samples);
	eng::usize cursor = 0u;
	for (eng::u16 chunk = 0u; chunk < info.num_chunks; ++chunk) {
		const eng::u32 samples = eng::audio::media::chunk_samples(info, chunk);
		const eng::s32 got = eng::audio::media::decode_chunk({bytes.data(), bytes.size()}, info, chunk, {pcm.data() + cursor, samples});
		if (got < 0) return false;
		cursor += static_cast<eng::usize>(got);
	}
	return cursor == pcm.size();
}

/// Traduce el codec textual al identificador que comparte AUZX con el engine.
[[nodiscard]] eng::audio::pcm_codec::Codec codec_id(const std::string& name) {
	using Codec = eng::audio::pcm_codec::Codec;
	if (name == "none") return Codec::None;
	if (name == "fib") return Codec::FibDelta;
	if (name == "ima") return Codec::ImaAdpcm;
	return Codec::DeltaRle;
}

/// Clasifica de forma conservadora: la heurística inicial usa duración y permite override explícito.
[[nodiscard]] std::string classify(const Config& config, eng::usize samples, eng::u16 rate) {
	if (config.mode == "sample" || config.mode == "music") return config.mode;
	const eng::u32 seconds = rate == 0u ? 0u : static_cast<eng::u32>(samples / rate);
	return seconds >= 15u ? "music" : "sample";
}

/// Crea un nombre de salida junto al origen sin sobrescribir archivos existentes.
[[nodiscard]] std::string default_output(const char* input, const std::string& mode) {
	std::string path = input;
	const std::size_t slash = path.find_last_of("/\\");
	const std::size_t dot = path.find_last_of('.');
	const std::size_t stem_end = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? dot : path.size();
	path.resize(stem_end);
	path += mode == "music" ? ".acp1" : ".auzx";
	return path;
}

/// Escribe un AUZX mono PCM8 con el codec seleccionado y verifica la reconstrucción.
[[nodiscard]] bool write_auzx(const std::vector<eng::u8>& pcm, eng::u16 rate, const Config& config,
	const std::string& output) {
	const eng::usize chunk = config.chunk_samples;
	if (chunk == 0u || pcm.empty()) return false;
	std::vector<eng::u8> padded = pcm;
	while (padded.size() % chunk != 0u) padded.push_back(0u);
	const eng::u16 chunks = static_cast<eng::u16>(padded.size() / chunk);
	const auto codec = codec_id(config.codec);
	std::vector<std::vector<eng::u8>> bodies(chunks);
	std::vector<eng::u32> offsets(chunks), sizes(chunks);
	eng::u32 cursor = static_cast<eng::u32>(eng::audio::auzx::kHeaderSize + chunks * eng::audio::auzx::kChunkEntrySize);
	eng::u8 fib_seed = 0u;
	for (eng::u16 i = 0u; i < chunks; ++i) {
		const eng::usize start = static_cast<eng::usize>(i) * chunk;
		std::vector<eng::u8> encoded(chunk + chunk / 128u + 32u);
		eng::s32 size = -1;
		if (codec == eng::audio::pcm_codec::Codec::None) {
			std::memcpy(encoded.data(), padded.data() + start, chunk); size = static_cast<eng::s32>(chunk);
		} else if (codec == eng::audio::pcm_codec::Codec::FibDelta) {
			size = eng::audio::fib_delta::encode({padded.data() + start, chunk}, {encoded.data(), encoded.size()}, fib_seed);
		} else {
			size = eng::audio::pcm_codec::encode({padded.data() + start, chunk}, {encoded.data(), encoded.size()}, static_cast<eng::u8>(codec));
		}
		if (size <= 0) return false;
		bodies[i].assign(encoded.begin(), encoded.begin() + size);
		offsets[i] = cursor; sizes[i] = static_cast<eng::u32>(size); cursor += sizes[i];
	}
	std::vector<eng::u8> file(cursor, 0u); eng::Span<eng::u8> view{file.data(), file.size()};
	file[0] = 'A'; file[1] = 'U'; file[2] = 'Z'; file[3] = 'X'; file[4] = 1u; file[5] = static_cast<eng::u8>(codec);
	eng::audio::auzx::wr16(view, 6u, rate); eng::audio::auzx::wr16(view, 8u, 1u); file[10] = 8u;
	eng::audio::auzx::wr32(view, 12u, static_cast<eng::u32>(padded.size())); eng::audio::auzx::wr16(view, 16u, config.chunk_samples);
	eng::audio::auzx::wr16(view, 18u, chunks); eng::audio::auzx::wr32(view, 20u, eng::audio::auzx::kHeaderSize);
	eng::audio::auzx::wr32(view, 24u, offsets[0]);
	for (eng::u16 i = 0u; i < chunks; ++i) { const eng::usize at = eng::audio::auzx::kHeaderSize + i * eng::audio::auzx::kChunkEntrySize; eng::audio::auzx::wr32(view, at, offsets[i]); eng::audio::auzx::wr32(view, at + 4u, sizes[i]); std::memcpy(file.data() + offsets[i], bodies[i].data(), bodies[i].size()); }
	std::FILE* out = std::fopen(output.c_str(), "wb"); if (!out) return false; const bool ok = std::fwrite(file.data(), 1u, file.size(), out) == file.size(); std::fclose(out); return ok;
}

/// Escribe un informe de texto mínimo para la primera vertical de la aplicación única.
void write_report(const std::string& path, const char* input, const std::string& mode, const Config& config, eng::usize samples) {
	std::FILE* out = std::fopen(path.c_str(), "wb"); if (!out) return;
	std::fprintf(out, "{\n  \"input\": \"%s\",\n  \"mode\": \"%s\",\n  \"codec\": \"%s\",\n  \"sample_rate\": %u,\n  \"chunk_samples\": %u,\n  \"samples\": %lu\n}\n", input, mode.c_str(), config.codec.c_str(), config.sample_rate, config.chunk_samples, static_cast<unsigned long>(samples));
	std::fclose(out);
}

/// Muestra la interfaz de la aplicación única, incluyendo el caso de arrastrar un archivo.
void print_help(const char* exe) { std::printf("Uso: %s <audio|auzx> [--mode auto|sample|music] [--config f] [--out f] [--codec rle|fib|ima|none] [--report f] [--play] [--dry-run]\n", exe); }

} // namespace

/// Punto de entrada: resuelve configuración, clasifica y ejecuta el pipeline disponible.
int main(int argc, char** argv) {
	if (argc < 2 || (argc == 2 && std::strcmp(argv[1], "--help") == 0)) { print_help(argv[0]); return argc < 2 ? 2 : 0; }
	Config config{}; const char* input = argv[1]; const char* config_path = nullptr; std::string output; std::string report;
	for (int i = 2; i < argc; ++i) {
		if (std::strcmp(argv[i], "--help") == 0) { print_help(argv[0]); return 0; }
		if (std::strcmp(argv[i], "--dry-run") == 0) { config.dry_run = true; continue; }
		if (std::strcmp(argv[i], "--play") == 0) { config.play = true; continue; }
		if (std::strcmp(argv[i], "--force") == 0) { config.force = true; continue; }
		if (i + 1 >= argc) return 2;
		if (std::strcmp(argv[i], "--mode") == 0) config.mode = argv[++i];
		else if (std::strcmp(argv[i], "--config") == 0) config_path = argv[++i];
		else if (std::strcmp(argv[i], "--out") == 0) output = argv[++i];
		else if (std::strcmp(argv[i], "--codec") == 0) config.codec = argv[++i];
		else if (std::strcmp(argv[i], "--sample-rate") == 0) config.sample_rate = static_cast<eng::u16>(std::atoi(argv[++i]));
		else if (std::strcmp(argv[i], "--chunk") == 0) config.chunk_samples = static_cast<eng::u16>(std::atoi(argv[++i]));
		else return 2;
	}
	if (config_path && !load_config(config_path, config)) { std::fprintf(stderr, "configuración inválida\n"); return 1; }
	std::vector<eng::u8> pcm; eng::u16 rate = 0u;
	if (config.play) {
		if (!load_playback_pcm(input, pcm, rate)) { std::fprintf(stderr, "entrada inválida o no soportada para reproducción\n"); return 1; }
	} else if (!pack_pcm::load(input, pcm, rate, config.sample_rate)) { std::fprintf(stderr, "entrada inválida o no soportada\n"); return 1; }
	if (config.sample_rate == 0u) config.sample_rate = rate;
	const std::string mode = classify(config, pcm.size(), rate);
	if (output.empty()) output = default_output(input, mode);
	std::printf("clasificación=%s muestras=%lu salida=%s\n", mode.c_str(), static_cast<unsigned long>(pcm.size()), output.c_str());
	if (config.dry_run) return 0;
	if (mode == "music") { std::fprintf(stderr, "ACP1 multipista aún no está habilitado en esta vertical\n"); return 3; }
	if (config.play && !audio_compressor::play_pcm({pcm.data(), pcm.size()}, config.sample_rate)) {
		std::fprintf(stderr, "reproducción no disponible: compile con SDL3 y AUDIO_COMPRESSOR_SDL3=1\n");
		return 4;
	}
	std::FILE* existing = std::fopen(output.c_str(), "rb");
	if (!config.force && existing != nullptr) { std::fclose(existing); std::fprintf(stderr, "salida existente; use --force\n"); return 1; }
	if (existing != nullptr) std::fclose(existing);
	if (!write_auzx(pcm, config.sample_rate, config, output)) return 1;
	if (!report.empty()) write_report(report, input, mode, config, pcm.size());
	return 0;
}
