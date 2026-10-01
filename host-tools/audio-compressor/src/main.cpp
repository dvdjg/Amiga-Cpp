// ==========================================================================
// audio-compressor: aplicación única de transformación de audio para Amiga.
// ==========================================================================
//
// El pipeline host mantiene separadas las rutas SAMPLE y MUSIC: AUZX mono para samples y ACP1 v2
// multipista para stems WAV. ACP1 v3 no forma parte de la salida de esta aplicación.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
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
#include "acp1_writer.hpp"
#include "acp1_v3_writer.hpp"
#include "hpss.hpp"
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
	/// Separa cada stem WAV en componentes armónica y percusiva para MUSIC.
	bool hpss = false;
	/// Versión ACP1 de salida para MUSIC: v2 estable o v3 MVP binario.
	eng::u8 acp1_version = 2u;
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

/// Resuelve ffmpeg desde variables de entorno o PATH para leer formatos host sin enlazarlo.
[[nodiscard]] std::string find_ffmpeg() {
	if (const char* value = std::getenv("FFMPEG"); value && *value) return value;
	if (const char* value = std::getenv("FFMPEG_BIN"); value && *value) return value;
	return "ffmpeg";
}

/// Detecta formatos comprimidos o tracker que requieren la conversión host de ffmpeg.
[[nodiscard]] bool needs_ffmpeg(const std::string& path) {
	const std::string ext = std::filesystem::path(path).extension().string();
	return ext == ".mp3" || ext == ".MP3" || ext == ".ogg" || ext == ".OGG" || ext == ".flac" || ext == ".FLAC" ||
		ext == ".mod" || ext == ".MOD";
}

/// Decodifica una fuente comprimida a WAV PCM16 temporal, manteniendo el archivo fuera del repo.
[[nodiscard]] bool decode_external_source(const std::string& input, std::string& wav, eng::u16 target_rate) {
	wav = (std::filesystem::temp_directory_path() / "amiga-audio-compressor-input.wav").string();
	const std::string executable = find_ffmpeg();
	const std::filesystem::path batch = std::filesystem::temp_directory_path() / "amiga-audio-compressor-ffmpeg.bat";
#if defined(_WIN32)
	std::FILE* script = std::fopen(batch.string().c_str(), "wb");
	if (!script) return false;
	std::fprintf(script, "@echo off\r\n\"%s\" -y -v error -i \"%s\" -vn -acodec pcm_s16le -ar %u \"%s\"\r\n",
		executable.c_str(), input.c_str(), target_rate == 0u ? 22050u : target_rate, wav.c_str());
	std::fclose(script);
	const std::string command = "cmd /c call \"" + batch.string() + "\"";
	const bool ok = std::system(command.c_str()) == 0;
	std::remove(batch.string().c_str());
	return ok;
#else
	const std::string command = "\"" + executable + "\" -y -v error -i \"" + input + "\" -vn -acodec pcm_s16le -ar " +
		std::to_string(target_rate == 0u ? 22050u : target_rate) + " \"" + wav + "\"";
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

[[nodiscard]] bool supported_codec_name(const std::string& name) {
	return name == "none" || name == "rle" || name == "fib" || name == "ima" || name == "auto";
}

[[nodiscard]] bool encode_chunk(const std::vector<eng::u8>& pcm, eng::usize start, eng::usize count,
	eng::audio::pcm_codec::Codec codec, std::vector<eng::u8>& encoded, eng::u8& fib_seed) {
	encoded.assign(count + count / 128u + 32u, 0u);
	eng::s32 size = -1;
	if (codec == eng::audio::pcm_codec::Codec::None) {
		if (encoded.size() < count) return false;
		std::memcpy(encoded.data(), pcm.data() + start, count);
		size = static_cast<eng::s32>(count);
	} else if (codec == eng::audio::pcm_codec::Codec::FibDelta) {
		size = eng::audio::fib_delta::encode({pcm.data() + start, count}, {encoded.data(), encoded.size()}, fib_seed);
	} else if (codec == eng::audio::pcm_codec::Codec::ImaAdpcm) {
		size = eng::audio::ima_adpcm::encode({pcm.data() + start, count}, {encoded.data(), encoded.size()});
	} else {
		size = eng::audio::pcm_codec::encode({pcm.data() + start, count}, {encoded.data(), encoded.size()},
			static_cast<eng::u8>(codec));
	}
	if (size <= 0) return false;
	encoded.resize(static_cast<eng::usize>(size));
	return true;
}

[[nodiscard]] bool estimate_codec_size(const std::vector<eng::u8>& pcm, eng::u16 chunk,
	eng::audio::pcm_codec::Codec codec, eng::u64& total) {
	if (chunk == 0u || pcm.empty()) return false;
	if ((codec == eng::audio::pcm_codec::Codec::FibDelta || codec == eng::audio::pcm_codec::Codec::ImaAdpcm) &&
		(((chunk & 1u) != 0u) || ((pcm.size() % chunk) != 0u && ((pcm.size() % chunk) & 1u) != 0u))) return false;
	total = eng::audio::auzx::kHeaderSize +
		((pcm.size() + chunk - 1u) / chunk) * eng::audio::auzx::kChunkEntrySize;
	eng::u8 fib_seed = 0u;
	std::vector<eng::u8> encoded;
	for (eng::usize start = 0u; start < pcm.size(); start += chunk) {
		const eng::usize count = std::min<eng::usize>(chunk, pcm.size() - start);
		if (!encode_chunk(pcm, start, count, codec, encoded, fib_seed)) return false;
		total += encoded.size();
	}
	return true;
}

struct CodecCandidate {
	const char* name = "";
	eng::u64 bytes = 0u;
	eng::u64 squared_error = 0u;
	eng::u8 peak_error = 0u;
};

[[nodiscard]] bool evaluate_codec(const std::vector<eng::u8>& pcm, eng::u16 chunk,
	eng::audio::pcm_codec::Codec codec, CodecCandidate& result) {
	if (chunk == 0u || pcm.empty()) return false;
	if ((codec == eng::audio::pcm_codec::Codec::FibDelta || codec == eng::audio::pcm_codec::Codec::ImaAdpcm) &&
		(((chunk & 1u) != 0u) || ((pcm.size() % chunk) != 0u && ((pcm.size() % chunk) & 1u) != 0u))) return false;
	result.bytes = eng::audio::auzx::kHeaderSize +
		((pcm.size() + chunk - 1u) / chunk) * eng::audio::auzx::kChunkEntrySize;
	eng::u8 fib_seed = 0u;
	std::vector<eng::u8> encoded;
	std::vector<eng::u8> decoded;
	for (eng::usize start = 0u; start < pcm.size(); start += chunk) {
		const eng::usize count = std::min<eng::usize>(chunk, pcm.size() - start);
		if (!encode_chunk(pcm, start, count, codec, encoded, fib_seed)) return false;
		decoded.assign(count, 0u);
		const eng::s32 written = codec == eng::audio::pcm_codec::Codec::None
			? (std::memcpy(decoded.data(), encoded.data(), count), static_cast<eng::s32>(count))
			: eng::audio::pcm_codec::decode({encoded.data(), encoded.size()}, {decoded.data(), decoded.size()},
				static_cast<eng::u8>(codec));
		if (written != static_cast<eng::s32>(count)) return false;
		result.bytes += encoded.size();
		for (eng::usize i = 0u; i < count; ++i) {
			const eng::s32 error = static_cast<eng::s8>(pcm[start + i]) - static_cast<eng::s8>(decoded[i]);
			const eng::u32 absolute = static_cast<eng::u32>(error < 0 ? -error : error);
			result.squared_error += static_cast<eng::u64>(error * error);
			if (absolute > result.peak_error) result.peak_error = static_cast<eng::u8>(absolute);
		}
	}
	return true;
}

[[nodiscard]] bool select_codec(const std::vector<eng::u8>& pcm, Config& config) {
	if (config.codec != "auto") return supported_codec_name(config.codec);
	struct Candidate { const char* name; eng::audio::pcm_codec::Codec codec; };
	constexpr Candidate candidates[] = {
		{"none", eng::audio::pcm_codec::Codec::None},
		{"rle", eng::audio::pcm_codec::Codec::DeltaRle},
		{"fib", eng::audio::pcm_codec::Codec::FibDelta},
		{"ima", eng::audio::pcm_codec::Codec::ImaAdpcm},
	};
	const Candidate* best = nullptr;
	eng::u64 best_size = ~eng::u64 {0};
	for (const Candidate& candidate : candidates) {
		eng::u64 size = 0u;
		if (estimate_codec_size(pcm, config.chunk_samples, candidate.codec, size) && size < best_size) {
			best = &candidate;
			best_size = size;
		}
	}
	if (best == nullptr) return false;
	config.codec = best->name;
	return true;
}

void print_codec_candidates(const std::vector<eng::u8>& pcm, const Config& config) {
	struct Candidate { const char* name; eng::audio::pcm_codec::Codec codec; };
	constexpr Candidate candidates[] = {
		{"none", eng::audio::pcm_codec::Codec::None},
		{"rle", eng::audio::pcm_codec::Codec::DeltaRle},
		{"fib", eng::audio::pcm_codec::Codec::FibDelta},
		{"ima", eng::audio::pcm_codec::Codec::ImaAdpcm},
	};
	std::printf("candidatas codec (chunk=%u):\n", config.chunk_samples);
	for (const Candidate& candidate : candidates) {
		CodecCandidate metrics{};
		metrics.name = candidate.name;
		if (!evaluate_codec(pcm, config.chunk_samples, candidate.codec, metrics)) {
			std::printf("  %s: no disponible\n", candidate.name);
			continue;
		}
		const double mse = pcm.empty() ? 0.0 : static_cast<double>(metrics.squared_error) / pcm.size();
		std::printf("  %s: %llu bytes, MSE=%.4f, pico=%u\n", candidate.name,
			static_cast<unsigned long long>(metrics.bytes), mse, metrics.peak_error);
	}
}

[[nodiscard]] bool fits_memory_budget(const pack_pcm::WavStems& stems, const std::vector<eng::u8>& pcm,
	const Config& config) {
	eng::u64 bytes = static_cast<eng::u64>(pcm.size());
	for (const auto& channel : stems.channels) bytes += static_cast<eng::u64>(channel.size());
	// The current host pipeline retains the normalized stems and at least one working copy. HPSS
	// needs two additional layer buffers per stem; reject early rather than silently exceeding the
	// declared budget. A future streaming loader can lower this bound without changing the format.
	const eng::u64 multiplier = config.hpss ? 4u : 2u;
	if (bytes > (~eng::u64 {0}) / multiplier) return false;
	return bytes * multiplier <= config.ram_budget_bytes;
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
	const eng::usize chunk_count = (pcm.size() + chunk - 1u) / chunk;
	if (chunk_count > 65535u || pcm.size() > 0xffffffffu) return false;
	const eng::u16 chunks = static_cast<eng::u16>(chunk_count);
	const auto codec = codec_id(config.codec);
	if ((codec == eng::audio::pcm_codec::Codec::FibDelta || codec == eng::audio::pcm_codec::Codec::ImaAdpcm) &&
		((chunk & 1u) != 0u || ((pcm.size() % chunk) != 0u && ((pcm.size() % chunk) & 1u) != 0u))) {
		std::fprintf(stderr, "FibDelta e IMA requieren chunks con un número par de muestras en el runtime Amiga\n");
		return false;
	}
	std::vector<std::vector<eng::u8>> bodies(chunks);
	std::vector<eng::u32> offsets(chunks), sizes(chunks);
	eng::u32 cursor = static_cast<eng::u32>(eng::audio::auzx::kHeaderSize + chunks * eng::audio::auzx::kChunkEntrySize);
	eng::u8 fib_seed = 0u;
	for (eng::u16 i = 0u; i < chunks; ++i) {
		const eng::usize start = static_cast<eng::usize>(i) * chunk;
		const eng::usize count = pcm.size() - start < chunk ? pcm.size() - start : chunk;
		std::vector<eng::u8> encoded;
		if (!encode_chunk(pcm, start, count, codec, encoded, fib_seed)) {
			std::fprintf(stderr, "codec no pudo codificar chunk %u (codec=%u, muestras=%lu)\n", i,
				static_cast<unsigned>(codec), static_cast<unsigned long>(count));
			return false;
		}
		bodies[i] = std::move(encoded);
		offsets[i] = cursor; sizes[i] = static_cast<eng::u32>(bodies[i].size());
		if (cursor > 0xffffffffu - sizes[i]) return false;
		cursor += sizes[i];
	}
	std::vector<eng::u8> file(cursor, 0u); eng::Span<eng::u8> view{file.data(), file.size()};
	file[0] = 'A'; file[1] = 'U'; file[2] = 'Z'; file[3] = 'X'; file[4] = 1u; file[5] = static_cast<eng::u8>(codec);
	eng::audio::auzx::wr16(view, 6u, rate); eng::audio::auzx::wr16(view, 8u, 1u); file[10] = 8u;
	eng::audio::auzx::wr32(view, 12u, static_cast<eng::u32>(pcm.size())); eng::audio::auzx::wr16(view, 16u, config.chunk_samples);
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
	std::vector<eng::u8> rebuilt(pcm.size());
	eng::audio::media::Info info {};
	std::vector<eng::u8> stored;
	if (read_binary(output_path.string().c_str(), stored) && eng::audio::media::open({stored.data(), stored.size()}, info)) {
		eng::usize cursor = 0u;
		bool exact = true;
		for (eng::u16 i = 0u; i < info.num_chunks; ++i) {
			const eng::u32 count = eng::audio::media::chunk_samples(info, i);
			const eng::s32 got = eng::audio::media::decode_chunk({stored.data(), stored.size()}, info, i,
				{rebuilt.data() + cursor, count});
			if (got != static_cast<eng::s32>(count)) { exact = false; break; }
			cursor += static_cast<eng::usize>(got);
		}
		stats.round_trip_ok = exact && cursor == rebuilt.size();
		if (!stats.round_trip_ok) return false;
		for (eng::usize i = 0u; i < pcm.size() && i < rebuilt.size(); ++i) {
			const eng::s32 error = static_cast<eng::s8>(pcm[i]) - static_cast<eng::s8>(rebuilt[i]);
			const eng::u32 absolute = static_cast<eng::u32>(error < 0 ? -error : error);
			stats.squared_error += static_cast<eng::u64>(error * error);
			const eng::s32 sample = static_cast<eng::s8>(pcm[i]);
			stats.signal_energy += static_cast<eng::u64>(sample * sample);
			if (absolute > stats.peak_error) stats.peak_error = static_cast<eng::u8>(absolute);
		}
	} else return false;
	return true;
}

/// Escribe un archivo ACP1 v1 con una pista sincronizada por cada stem AUZX.
[[nodiscard]] bool write_acp1(const std::vector<std::vector<eng::u8>>& stems, eng::u16 rate,
	eng::u32 samples, const std::string& output,
	const std::vector<std::vector<eng::audio::acp1::Event>>& events) {
	std::vector<eng::u8> file;
	if (!audio_compressor::build_acp1(stems, rate, samples, file, events)) return false;
	const std::filesystem::path output_path {native_safe_path(output)};
	if (output_path.has_parent_path()) std::filesystem::create_directories(output_path.parent_path());
	return write_binary(output_path, file);
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
void print_help(const char* exe) { std::printf("Uso: %s <audio|auzx> [--mode auto|sample|music] [--acp1-version 2|3] [--config f] [--out f] [--codec auto|rle|fib|ima|none] [--sample-rate Hz] [--chunk muestras] [--report f] [--keep-candidates] [--compare] [--play] [--dry-run]\n", exe); }

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
		if (std::strcmp(argv[i], "--hpss") == 0) { config.hpss = true; continue; }
		if (std::strcmp(argv[i], "--no-hpss") == 0) { config.hpss = false; continue; }
		if (i + 1 >= argc) return 2;
		if (std::strcmp(argv[i], "--mode") == 0) config.mode = argv[++i];
		else if (std::strcmp(argv[i], "--acp1-version") == 0) config.acp1_version = static_cast<eng::u8>(std::atoi(argv[++i]));
		else if (std::strcmp(argv[i], "--config") == 0) config_path = argv[++i];
		else if (std::strcmp(argv[i], "--out") == 0) output = argv[++i];
		else if (std::strcmp(argv[i], "--report") == 0) report = argv[++i];
		else if (std::strcmp(argv[i], "--codec") == 0) config.codec = argv[++i];
		else if (std::strcmp(argv[i], "--sample-rate") == 0) config.sample_rate = static_cast<eng::u16>(std::atoi(argv[++i]));
		else if (std::strcmp(argv[i], "--chunk") == 0) config.chunk_samples = static_cast<eng::u16>(std::atoi(argv[++i]));
		else return 2;
	}
	if (config_path && !load_config(config_path, config)) { std::fprintf(stderr, "configuración inválida\n"); return 1; }
	// CLI overrides the config file. The first pass records the config path and output paths; this
	// pass reapplies option values after loading JSON so precedence is defaults < config < CLI.
	for (int i = 2; i < argc; ++i) {
		if (std::strcmp(argv[i], "--mode") == 0) config.mode = argv[++i];
		else if (std::strcmp(argv[i], "--acp1-version") == 0) config.acp1_version = static_cast<eng::u8>(std::atoi(argv[++i]));
		else if (std::strcmp(argv[i], "--codec") == 0) config.codec = argv[++i];
		else if (std::strcmp(argv[i], "--sample-rate") == 0) config.sample_rate = static_cast<eng::u16>(std::atoi(argv[++i]));
		else if (std::strcmp(argv[i], "--chunk") == 0) config.chunk_samples = static_cast<eng::u16>(std::atoi(argv[++i]));
		else if (std::strcmp(argv[i], "--force") == 0) config.force = true;
		else if (std::strcmp(argv[i], "--hpss") == 0) config.hpss = true;
		else if (std::strcmp(argv[i], "--no-hpss") == 0) config.hpss = false;
	}
	if (!supported_codec_name(config.codec)) {
		std::fprintf(stderr, "codec no soportado por el encoder actual: %s (use auto|none|rle|fib|ima)\n", config.codec.c_str());
		return 2;
	}
	if (config.chunk_samples == 0u || config.ram_budget_bytes == 0u) {
		std::fprintf(stderr, "chunk y ram-budget deben ser mayores que cero\n");
		return 2;
	}
	if (config.acp1_version != 2u && config.acp1_version != 3u) {
		std::fprintf(stderr, "versión ACP1 no soportada: %u (use 2 o 3)\n", config.acp1_version);
		return 2;
	}
	std::vector<eng::u8> pcm; eng::u16 rate = 0u; std::string decoded_input;
	pack_pcm::WavStems input_stems {};
	const std::string source = needs_ffmpeg(input) ?
		(decode_external_source(input, decoded_input, config.sample_rate) ? decoded_input : std::string{}) : input;
	if (source.empty()) { std::fprintf(stderr, "no se pudo decodificar la fuente externa; configure FFMPEG/FFMPEG_BIN\n"); return 1; }
	if (config.play) {
		if (!load_playback_pcm(source.c_str(), pcm, rate)) { std::fprintf(stderr, "entrada inválida o no soportada para reproducción\n"); return 1; }
	} else if (!pack_pcm::load_stems(source.c_str(), input_stems, config.sample_rate) ||
		!pack_pcm::downmix(input_stems, pcm)) { std::fprintf(stderr, "entrada inválida o no soportada\n"); return 1; }
	if (input_stems.sample_rate != 0u) rate = input_stems.sample_rate;
	if (config.sample_rate == 0u) config.sample_rate = rate;
	if (rate == 0u || !fits_memory_budget(input_stems, pcm, config)) {
		std::fprintf(stderr, "la entrada excede --ram-budget con el pipeline actual; reduzca la entrada o aumente --ram-budget\n");
		return 2;
	}
	if (config.compare_candidates || config.codec == "auto") print_codec_candidates(pcm, config);
	if (!select_codec(pcm, config)) {
		std::fprintf(stderr, "no se pudo seleccionar un codec válido para la entrada\n");
		return 2;
	}
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
		if (input_stems.channels.empty() || input_stems.channels.size() > eng::audio::acp1::kMaxTracks) {
			std::fprintf(stderr, "ACP1 admite de 1 a 7 stems WAV\n"); return 1;
		}
		std::FILE* existing = std::fopen(output.c_str(), "rb");
		if (!config.force && existing != nullptr) { std::fclose(existing); std::fprintf(stderr, "salida existente; use --force\n"); return 1; }
		if (existing != nullptr) std::fclose(existing);
		const std::string linear = output + ".linear.auzx";
		ConversionStats linear_stats{};
		std::error_code input_error{};
		linear_stats.input_bytes = std::filesystem::file_size(std::filesystem::path{input}, input_error);
		linear_stats.pcm_bytes = static_cast<eng::u64>(pcm.size());
		linear_stats.repeated_windows = count_repeated_windows(pcm, config.chunk_samples);
		if (config.acp1_version == 2u && !write_auzx(pcm, config.sample_rate, config, linear, linear_stats)) return 1;
		const std::string structural = output.empty() ? default_output(input.c_str(), "music") : output;
		std::vector<std::vector<eng::u8>> source_stems;
		if (config.hpss) {
			for (const auto& stem : input_stems.channels) {
				audio_compressor::HpssResult layers {};
				if (!audio_compressor::hpss(stem, 256u, layers)) { std::fprintf(stderr, "HPSS no pudo procesar el stem\n"); return 1; }
				source_stems.push_back(std::move(layers.harmonic));
				source_stems.push_back(std::move(layers.percussive));
			}
		} else {
			source_stems = input_stems.channels;
		}
		if (source_stems.size() > eng::audio::acp1::kMaxTracks) {
			std::fprintf(stderr, "HPSS produce más de siete pistas ACP1; desactive --hpss o reduzca canales\n"); return 1;
		}
		if (config.acp1_version == 3u) {
			std::vector<eng::u8> v3_file;
			if (!audio_compressor::build_acp1_v3(source_stems, config.sample_rate, config.chunk_samples, v3_file)) {
				std::fprintf(stderr, "no se pudo generar ACP1 v3 MVP\n"); return 1;
			}
			const std::filesystem::path v3_path {native_safe_path(structural)};
			if (v3_path.has_parent_path()) std::filesystem::create_directories(v3_path.parent_path());
			if (!write_binary(v3_path, v3_file)) return 1;
			eng::audio::acp1_v3::Info v3_info{};
			if (!eng::audio::acp1_v3::parse({v3_file.data(), v3_file.size()}, v3_info)) {
				std::fprintf(stderr, "ACP1 v3 MVP no supera su parser\n"); return 1;
			}
			std::printf("ACP1 v3 MVP=%llu bytes; pistas=%lu; unidades=%lu; segmentos PCM=%lu\n",
				static_cast<unsigned long long>(v3_file.size()), static_cast<unsigned long>(v3_info.track_count),
				static_cast<unsigned long>(v3_info.unit_count), static_cast<unsigned long>(v3_info.segment_count));
			if (!report.empty()) write_report(report, input, mode, config, linear_stats, v3_file.size());
			if (config.acp1_version == 2u && !config.keep_candidates) std::remove(linear.c_str());
			return 0;
		}
		std::vector<std::vector<eng::u8>> encoded_stems;
		std::vector<std::vector<eng::audio::acp1::Event>> events(source_stems.size());
		const eng::u8 track_gain = config.hpss || input_stems.channels.size() > 1u ? 128u : 255u;
		std::vector<std::vector<eng::u8>> unique_pcm_units;
		for (eng::usize track = 0u; track < source_stems.size(); ++track) {
			for (eng::usize start = 0u; start < source_stems[track].size(); start += config.chunk_samples) {
				const eng::usize count = source_stems[track].size() - start < config.chunk_samples
					? source_stems[track].size() - start : config.chunk_samples;
				eng::usize unit_id = 0u;
				for (; unit_id < unique_pcm_units.size(); ++unit_id) {
					if (unique_pcm_units[unit_id].size() == count &&
						std::memcmp(unique_pcm_units[unit_id].data(), source_stems[track].data() + start, count) == 0) break;
				}
				if (unit_id == unique_pcm_units.size()) {
					if (unit_id >= 65535u) { std::fprintf(stderr, "demasiadas unidades ACP1\n"); return 1; }
					unique_pcm_units.emplace_back(source_stems[track].begin() + start,
						source_stems[track].begin() + start + count);
					const std::string unit_path = linear + ".unit-" + std::to_string(unit_id) + ".auzx";
					ConversionStats unit_stats{};
					unit_stats.pcm_bytes = count;
					if (!write_auzx(unique_pcm_units.back(), config.sample_rate, config, unit_path, unit_stats)) return 1;
					std::vector<eng::u8> bytes;
					const std::string safe_unit_path = native_safe_path(unit_path);
					if (!read_binary(safe_unit_path.c_str(), bytes)) return 1;
					encoded_stems.push_back(std::move(bytes));
					std::remove(safe_unit_path.c_str());
				}
				events[track].push_back({static_cast<eng::u32>(unit_id), static_cast<eng::u32>(start),
					static_cast<eng::u32>(count), track_gain});
			}
		}
	if (!write_acp1(encoded_stems, config.sample_rate, 0u, structural, events)) return 1;
		const eng::u64 structural_bytes = std::filesystem::file_size(std::filesystem::path{structural});
		std::vector<eng::u8> acp1_bytes;
		if (!read_binary(native_safe_path(structural).c_str(), acp1_bytes)) return 1;
		eng::audio::media::Info acp1_info{};
		if (!eng::audio::media::open({acp1_bytes.data(), acp1_bytes.size()}, acp1_info)) return 1;
		std::vector<eng::u8> rebuilt(pcm.size());
		std::vector<eng::u8> scratch(config.chunk_samples);
		std::vector<eng::s16> accumulator(pcm.size());
		const eng::s32 rebuilt_count = eng::audio::media::mix_window({acp1_bytes.data(), acp1_bytes.size()}, acp1_info,
			0u, {rebuilt.data(), rebuilt.size()}, {scratch.data(), scratch.size()}, {accumulator.data(), accumulator.size()});
		if (rebuilt_count != static_cast<eng::s32>(pcm.size())) { std::fprintf(stderr, "no se pudo reconstruir la mezcla ACP1\n"); return 1; }
		ConversionStats structural_stats = linear_stats;
		structural_stats.output_bytes = structural_bytes;
		structural_stats.squared_error = 0u; structural_stats.signal_energy = 0u; structural_stats.peak_error = 0u;
		for (eng::usize i = 0u; i < pcm.size(); ++i) {
			const eng::s32 error = static_cast<eng::s8>(pcm[i]) - static_cast<eng::s8>(rebuilt[i]);
			const eng::u32 absolute = static_cast<eng::u32>(error < 0 ? -error : error);
			structural_stats.squared_error += static_cast<eng::u64>(error * error);
			const eng::s32 sample = static_cast<eng::s8>(pcm[i]); structural_stats.signal_energy += static_cast<eng::u64>(sample * sample);
			if (absolute > structural_stats.peak_error) structural_stats.peak_error = static_cast<eng::u8>(absolute);
		}
		structural_stats.round_trip_ok = true;
		const char* hpss_label = config.hpss ? "hpss=on" : "hpss=off";
		const double denominator = pcm.empty() ? 1.0 : static_cast<double>(pcm.size());
		const double linear_mse = static_cast<double>(linear_stats.squared_error) / denominator;
		const double structural_mse = static_cast<double>(structural_stats.squared_error) / denominator;
		std::printf("AUZX=%llu bytes MSE=%.4f pico=%u; ACP1=%llu bytes MSE=%.4f pico=%u; pistas=%lu; %s; ventanas repetidas=%u\n", static_cast<unsigned long long>(linear_stats.output_bytes), linear_mse, linear_stats.peak_error, static_cast<unsigned long long>(structural_bytes), structural_mse, structural_stats.peak_error, static_cast<unsigned long>(acp1_info.acp1_info.track_count), hpss_label, linear_stats.repeated_windows);
		if (!report.empty()) write_report(report, input, mode, config, structural_stats, structural_bytes);
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
