// ==========================================================================
// audio-compressor: aplicación única de transformación de audio para Amiga.
// ==========================================================================
//
// Esta primera vertical implementa el pipeline SAMPLE completo: ingestión WAV/RAW, configuración,
// clasificación, codec AUZX, round-trip y salida de informe. El pipeline MUSIC se reconoce y valida
// como modo, pero no inventa un ACP1 parcial: su encoder estructural llega en una fase posterior.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#if defined(_WIN32)
#include <cwchar>
#include <windows.h>
#endif
#include <string>
#include <vector>

#include <eng/audio/auzx.hpp>
#include <eng/audio/fib_delta.hpp>
#include <eng/audio/media.hpp>
#include <eng/audio/pcm_codec.hpp>

#include "../../../host-tools/pack-pcm/wav_loader.hpp"
#include "sdl_player.hpp"
#include "sdl_host_io.hpp"

namespace {

/// Escribe un archivo completo usando la API nativa Windows para evitar el fallo de `_wfopen`
/// de MinGW cuando el proceso recibe rutas provenientes de Git Bash/Explorer.
[[nodiscard]] bool write_binary(const std::filesystem::path& path, const std::vector<eng::u8>& bytes) {
#if defined(_WIN32)
	const std::string narrow = path.string();
	const int wide_length = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, nullptr, 0);
	if (wide_length <= 0) return false;
	std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
	if (MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, wide.data(), wide_length) <= 0) return false;
	wchar_t temp_directory[MAX_PATH]{};
	if (GetTempPathW(MAX_PATH, temp_directory) == 0u) return false;
	wchar_t temp_file[MAX_PATH]{};
	if (GetTempFileNameW(temp_directory, L"acp", 0u, temp_file) == 0u) return false;
	HANDLE handle = CreateFileW(temp_file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0u;
	const bool ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != 0 && written == bytes.size();
	CloseHandle(handle);
	const bool copied = ok && CopyFileW(temp_file, wide.c_str(), FALSE) != 0;
	DeleteFileW(temp_file);
	return copied;
#else
	std::FILE* file = std::fopen(path.string().c_str(), "wb");
	if (!file) return false;
	const bool ok = std::fwrite(bytes.data(), 1u, bytes.size(), file) == bytes.size();
	std::fclose(file);
	return ok;
#endif
}

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
	/// Conserva las candidatas alternativas generadas durante la comparación.
	bool keep_candidates = false;
	/// Genera y compara AUZX lineal frente a la envoltura ACP1 mínima.
	bool compare_candidates = false;
};

/// Estadísticas de una conversión, usadas por el informe JSON y por la comparación del corpus.
struct ConversionStats {
	eng::u64 input_bytes = 0u;
	eng::u64 pcm_bytes = 0u;
	eng::u64 output_bytes = 0u;
	eng::u64 samples = 0u;
	eng::u16 sample_rate = 0u;
	eng::u64 squared_error = 0u;
	eng::u64 signal_energy = 0u;
	eng::u8 peak_error = 0u;
	bool round_trip_ok = false;
	eng::u32 repeated_windows = 0u;
};

/// Cuenta repeticiones exactas de ventanas PCM8; sirve como baseline antes de la firma espectral.
[[nodiscard]] eng::u32 count_repeated_windows(const std::vector<eng::u8>& pcm, eng::usize window) {
	if (window == 0u || pcm.size() < window * 2u) return 0u;
	eng::u32 repeated = 0u;
	for (eng::usize current = window; current + window <= pcm.size(); current += window) {
		for (eng::usize previous = 0u; previous < current; previous += window) {
			if (std::memcmp(pcm.data() + previous, pcm.data() + current, window) == 0) { ++repeated; break; }
		}
	}
	return repeated;
}

/// Resuelve ffmpeg desde variables de entorno o PATH para leer MP3/OGG/FLAC sin enlazarlo.
[[nodiscard]] std::string find_ffmpeg() {
	if (const char* value = std::getenv("FFMPEG"); value && *value) return value;
	if (const char* value = std::getenv("FFMPEG_BIN"); value && *value) return value;
	return "ffmpeg";
}

/// Detecta formatos comprimidos que requieren la conversión host de ffmpeg.
[[nodiscard]] bool needs_ffmpeg(const std::string& path) {
	const std::string ext = std::filesystem::path(path).extension().string();
	return ext == ".mp3" || ext == ".MP3" || ext == ".ogg" || ext == ".OGG" || ext == ".flac" || ext == ".FLAC";
}

/// Decodifica una fuente comprimida a WAV PCM16 temporal, manteniendo el archivo fuera del repo.
[[nodiscard]] bool decode_external_source(const std::string& input, std::string& wav) {
	wav = (std::filesystem::temp_directory_path() / "amiga-audio-compressor-input.wav").string();
	const std::string executable = find_ffmpeg();
	const std::filesystem::path batch = std::filesystem::temp_directory_path() / "amiga-audio-compressor-ffmpeg.bat";
#if defined(_WIN32)
	std::FILE* script = std::fopen(batch.string().c_str(), "wb");
	if (!script) return false;
	std::fprintf(script, "@echo off\r\n\"%s\" -y -v error -i \"%s\" -vn -acodec pcm_s16le -ar 22050 -ac 2 \"%s\"\r\n",
		executable.c_str(), input.c_str(), wav.c_str());
	std::fclose(script);
	const std::string command = "cmd /c call \"" + batch.string() + "\"";
	const bool ok = std::system(command.c_str()) == 0;
	std::remove(batch.string().c_str());
	return ok;
#else
	const std::string command = "\"" + executable + "\" -y -v error -i \"" + input + "\" -vn -acodec pcm_s16le -ar 22050 -ac 2 \"" + wav + "\"";
	return std::system(command.c_str()) == 0;
#endif
}

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
	return audio_compressor::load_file(path, bytes);
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

/// Convierte una ruta relativa en absoluta y usa separadores `/`, que acepta el runtime MinGW
/// aunque el proceso se haya lanzado desde Git Bash, Explorer o un acceso directo de Windows.
[[nodiscard]] std::string native_safe_path(const std::string& path) {
	const std::filesystem::path candidate {path};
	const std::filesystem::path absolute = candidate.has_root_name() || candidate.is_absolute()
		? candidate : std::filesystem::absolute(candidate);
	return absolute.string();
}

/// Escribe un AUZX mono PCM8 con el codec seleccionado y verifica la reconstrucción.
[[nodiscard]] bool write_auzx(const std::vector<eng::u8>& pcm, eng::u16 rate, const Config& config,
	const std::string& output, ConversionStats& stats) {
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
		if (size <= 0) { std::fprintf(stderr, "codec no pudo codificar chunk %u (codec=%u, muestras=%lu)\n", i, static_cast<unsigned>(codec), static_cast<unsigned long>(chunk)); return false; }
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
	const std::filesystem::path output_path {native_safe_path(output)};
	if (output_path.has_parent_path()) std::filesystem::create_directories(output_path.parent_path());
	if (!write_binary(output_path, file)) {
#if defined(_WIN32)
		std::fprintf(stderr, "AUZX escritura falló para %s (Win32=%lu)\n", output_path.string().c_str(), static_cast<unsigned long>(GetLastError()));
#else
		std::fprintf(stderr, "AUZX escritura falló para %s\n", output_path.string().c_str());
#endif
		return false;
	}
	stats.output_bytes = static_cast<eng::u64>(file.size());
	stats.samples = pcm.size();
	stats.sample_rate = rate;
	std::vector<eng::u8> rebuilt(padded.size());
	eng::audio::media::Info info {};
	std::vector<eng::u8> stored;
	if (read_binary(output_path.string().c_str(), stored) && eng::audio::media::open({stored.data(), stored.size()}, info)) {
		eng::usize cursor = 0u;
		for (eng::u16 i = 0u; i < info.num_chunks; ++i) {
			const eng::u32 count = eng::audio::media::chunk_samples(info, i);
			const eng::s32 got = eng::audio::media::decode_chunk({stored.data(), stored.size()}, info, i, {rebuilt.data() + cursor, count});
			if (got < 0) return false;
			cursor += static_cast<eng::usize>(got);
		}
		stats.round_trip_ok = cursor == rebuilt.size();
		for (eng::usize i = 0u; i < pcm.size() && i < rebuilt.size(); ++i) {
			const eng::s32 error = static_cast<eng::s8>(pcm[i]) - static_cast<eng::s8>(rebuilt[i]);
			const eng::u32 absolute = static_cast<eng::u32>(error < 0 ? -error : error);
			stats.squared_error += static_cast<eng::u64>(error * error);
			const eng::s32 sample = static_cast<eng::s8>(pcm[i]);
			stats.signal_energy += static_cast<eng::u64>(sample * sample);
			if (absolute > stats.peak_error) stats.peak_error = static_cast<eng::u8>(absolute);
		}
	}
	return true;
}

/// Envuelve un AUZX completo como una unidad y un track ACP1 mínimo.
///
/// Este formato estructural inicial no deduplica todavía: demuestra que el pipeline puede separar
/// recurso, track y evento sin cambiar el payload AUZX. C12 sustituirá la unidad única por el
/// diccionario HPSS/multipista sin cambiar la idea de los offsets validados.
[[nodiscard]] bool write_acp1_wrapper(const std::vector<eng::u8>& auzx, eng::u16 rate,
	const std::string& output) {
	constexpr eng::usize header_size = 32u;
	constexpr eng::usize unit_size = 24u;
	constexpr eng::usize track_size = 8u;
	constexpr eng::usize event_size = 20u;
	const eng::u32 units_offset = header_size;
	const eng::u32 data_offset = units_offset + unit_size;
	const eng::u32 tracks_offset = data_offset + static_cast<eng::u32>(auzx.size());
	const eng::u32 events_offset = tracks_offset + track_size;
	const eng::u32 end_offset = events_offset + event_size;
	std::vector<eng::u8> file(end_offset, 0u);
	eng::Span<eng::u8> out{file.data(), file.size()};
	file[0] = 'A'; file[1] = 'C'; file[2] = 'P'; file[3] = '1';
	eng::audio::auzx::wr16(out, 4u, 1u); eng::audio::auzx::wr16(out, 6u, 0x0004u);
	eng::audio::auzx::wr32(out, 8u, rate); eng::audio::auzx::wr16(out, 12u, 1u);
	file[14] = 1u; file[15] = 0u; file[16] = 3u;
	eng::audio::auzx::wr32(out, 20u, 0u); eng::audio::auzx::wr32(out, 24u, units_offset);
	eng::audio::auzx::wr32(out, 28u, end_offset);
	// UnitHeader: id, payload offset/size, reconstrucción, modo AUZX, flags, gain, phase.
	eng::audio::auzx::wr32(out, units_offset, 0u); eng::audio::auzx::wr32(out, units_offset + 4u, data_offset);
	eng::audio::auzx::wr32(out, units_offset + 8u, static_cast<eng::u32>(auzx.size()));
	eng::audio::auzx::wr16(out, units_offset + 12u, 0u); file[units_offset + 14u] = 0u; file[units_offset + 15u] = 0u;
	file[units_offset + 16u] = 255u; eng::audio::auzx::wr16(out, units_offset + 17u, 0u);
	std::memcpy(file.data() + data_offset, auzx.data(), auzx.size());
	// TrackHeader: destino Paula 0, flags pitch, un evento y offset de eventos.
	file[tracks_offset] = 0u; file[tracks_offset + 1u] = 1u; eng::audio::auzx::wr16(out, tracks_offset + 2u, 1u); eng::audio::auzx::wr32(out, tracks_offset + 4u, events_offset);
	// TrackEvent: unit id, start, duration, gain, pitch, fades, reserved.
	eng::audio::auzx::wr32(out, events_offset, 0u); eng::audio::auzx::wr32(out, events_offset + 4u, 0u); eng::audio::auzx::wr16(out, events_offset + 8u, 0u); file[events_offset + 10u] = 255u; file[events_offset + 11u] = 0u; file[events_offset + 12u] = 0u; file[events_offset + 13u] = 0u;
	std::error_code error; std::filesystem::create_directories(std::filesystem::path{output}.parent_path(), error);
	return write_binary(std::filesystem::path{output}, file);
}

/// Escribe un informe de texto mínimo para la primera vertical de la aplicación única.
void write_report(const std::string& path, const std::string& input, const std::string& mode, const Config& config, const ConversionStats& stats, eng::u64 structural_bytes = 0u) {
	std::FILE* out = std::fopen(path.c_str(), "wb"); if (!out) return;
	const double pcm_ratio = stats.pcm_bytes == 0u ? 0.0 : static_cast<double>(stats.output_bytes) / static_cast<double>(stats.pcm_bytes);
	const double source_ratio = stats.input_bytes == 0u ? 0.0 : static_cast<double>(stats.output_bytes) / static_cast<double>(stats.input_bytes);
	const double mse = stats.samples == 0u ? 0.0 : static_cast<double>(stats.squared_error) / static_cast<double>(stats.samples);
	const double snr_db = stats.squared_error == 0u ? 999.0 : 10.0 * std::log10(static_cast<double>(stats.signal_energy) / static_cast<double>(stats.squared_error));
	std::fprintf(out, "{\n  \"input\": \"%s\",\n  \"mode\": \"%s\",\n  \"codec\": \"%s\",\n  \"sample_rate\": %u,\n  \"chunk_samples\": %u,\n  \"samples\": %llu,\n  \"duration_seconds\": %.6f,\n  \"input_bytes\": %llu,\n  \"pcm_bytes\": %llu,\n  \"output_bytes\": %llu,\n  \"structural_bytes\": %llu,\n  \"ratio_pcm_to_output\": %.6f,\n  \"ratio_source_to_output\": %.6f,\n  \"mse_pcm8\": %.6f,\n  \"snr_db\": %.3f,\n  \"peak_error\": %u,\n  \"repeated_windows\": %u,\n  \"round_trip_ok\": %s\n}\n", input.c_str(), mode.c_str(), config.codec.c_str(), config.sample_rate, config.chunk_samples, static_cast<unsigned long long>(stats.samples), stats.sample_rate == 0u ? 0.0 : static_cast<double>(stats.samples) / stats.sample_rate, static_cast<unsigned long long>(stats.input_bytes), static_cast<unsigned long long>(stats.pcm_bytes), static_cast<unsigned long long>(stats.output_bytes), static_cast<unsigned long long>(structural_bytes), pcm_ratio, source_ratio, mse, snr_db, stats.peak_error, stats.repeated_windows, stats.round_trip_ok ? "true" : "false");
	std::fclose(out);
}

/// Muestra la interfaz de la aplicación única, incluyendo el caso de arrastrar un archivo.
void print_help(const char* exe) { std::printf("Uso: %s <audio|auzx> [--mode auto|sample|music] [--config f] [--out f] [--codec rle|fib|ima|none] [--report f] [--keep-candidates] [--compare] [--play] [--dry-run]\n", exe); }

} // namespace

/// Punto de entrada: resuelve configuración, clasifica y ejecuta el pipeline disponible.
int main(int argc, char** argv) {
	if (argc < 2 || (argc == 2 && std::strcmp(argv[1], "--help") == 0)) { print_help(argv[0]); return argc < 2 ? 2 : 0; }
	Config config{}; const std::string input = native_safe_path(argv[1]); const char* config_path = nullptr; std::string output; std::string report;
	for (int i = 2; i < argc; ++i) {
		if (std::strcmp(argv[i], "--help") == 0) { print_help(argv[0]); return 0; }
		if (std::strcmp(argv[i], "--dry-run") == 0) { config.dry_run = true; continue; }
		if (std::strcmp(argv[i], "--play") == 0) { config.play = true; continue; }
		if (std::strcmp(argv[i], "--force") == 0) { config.force = true; continue; }
		if (std::strcmp(argv[i], "--keep-candidates") == 0) { config.keep_candidates = true; continue; }
		if (std::strcmp(argv[i], "--compare") == 0) { config.compare_candidates = true; continue; }
		if (i + 1 >= argc) return 2;
		if (std::strcmp(argv[i], "--mode") == 0) config.mode = argv[++i];
		else if (std::strcmp(argv[i], "--config") == 0) config_path = argv[++i];
		else if (std::strcmp(argv[i], "--out") == 0) output = argv[++i];
		else if (std::strcmp(argv[i], "--report") == 0) report = argv[++i];
		else if (std::strcmp(argv[i], "--codec") == 0) config.codec = argv[++i];
		else if (std::strcmp(argv[i], "--sample-rate") == 0) config.sample_rate = static_cast<eng::u16>(std::atoi(argv[++i]));
		else if (std::strcmp(argv[i], "--chunk") == 0) config.chunk_samples = static_cast<eng::u16>(std::atoi(argv[++i]));
		else return 2;
	}
	if (config_path && !load_config(config_path, config)) { std::fprintf(stderr, "configuración inválida\n"); return 1; }
	std::vector<eng::u8> pcm; eng::u16 rate = 0u; std::string decoded_input;
	const std::string source = needs_ffmpeg(input) ? (decode_external_source(input, decoded_input) ? decoded_input : std::string{}) : input;
	if (source.empty()) { std::fprintf(stderr, "no se pudo decodificar la fuente externa; configure FFMPEG/FFMPEG_BIN\n"); return 1; }
	if (config.play) {
		if (!load_playback_pcm(source.c_str(), pcm, rate)) { std::fprintf(stderr, "entrada inválida o no soportada para reproducción\n"); return 1; }
	} else if (!pack_pcm::load(source.c_str(), pcm, rate, config.sample_rate)) { std::fprintf(stderr, "entrada inválida o no soportada\n"); return 1; }
	if (config.sample_rate == 0u) config.sample_rate = rate;
	const std::string mode = classify(config, pcm.size(), rate);
	if (output.empty()) output = default_output(input.c_str(), mode);
	output = native_safe_path(output);
	std::printf("clasificación=%s muestras=%lu salida=%s\n", mode.c_str(), static_cast<unsigned long>(pcm.size()), output.c_str());
	if (config.play) {
		if (!audio_compressor::play_pcm({pcm.data(), pcm.size()}, config.sample_rate)) {
			std::fprintf(stderr, "reproducción no disponible: compile con SDL3 y AUDIO_COMPRESSOR_SDL3=1\n");
			return 4;
		}
		return 0;
	}
	if (config.dry_run) return 0;
	if (mode == "music") {
		const std::string linear = output + ".linear.auzx";
		ConversionStats linear_stats{};
		std::error_code input_error{};
		linear_stats.input_bytes = std::filesystem::file_size(std::filesystem::path{input}, input_error);
		linear_stats.pcm_bytes = static_cast<eng::u64>(pcm.size());
		linear_stats.repeated_windows = count_repeated_windows(pcm, config.chunk_samples);
		if (!write_auzx(pcm, config.sample_rate, config, linear, linear_stats)) return 1;
		const std::string structural = output.empty() ? default_output(input.c_str(), "music") : output;
		if (!write_acp1_wrapper([&] { std::vector<eng::u8> bytes; return read_binary(linear.c_str(), bytes) ? bytes : std::vector<eng::u8>{}; }(), config.sample_rate, structural)) return 1;
		const eng::u64 structural_bytes = std::filesystem::file_size(std::filesystem::path{structural});
		std::printf("candidata linear AUZX=%llu bytes; candidata ACP1 mínima=%llu bytes; repeticiones exactas=%u\n", static_cast<unsigned long long>(linear_stats.output_bytes), static_cast<unsigned long long>(structural_bytes), linear_stats.repeated_windows);
		if (!report.empty()) write_report(report, input, mode, config, linear_stats, structural_bytes);
		if (!config.keep_candidates) std::remove(linear.c_str());
		return 0;
	}
	std::FILE* existing = std::fopen(output.c_str(), "rb");
	if (!config.force && existing != nullptr) { std::fclose(existing); std::fprintf(stderr, "salida existente; use --force\n"); return 1; }
	if (existing != nullptr) std::fclose(existing);
	ConversionStats stats {};
	std::error_code input_error {};
	stats.input_bytes = std::filesystem::file_size(std::filesystem::path {input}, input_error);
	stats.pcm_bytes = static_cast<eng::u64>(pcm.size());
	stats.repeated_windows = count_repeated_windows(pcm, config.chunk_samples);
	if (!write_auzx(pcm, config.sample_rate, config, output, stats)) return 1;
	if (!report.empty()) write_report(report, input, mode, config, stats);
	if (!decoded_input.empty()) std::remove(decoded_input.c_str());
	return 0;
}
