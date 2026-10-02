// ==========================================================================
// audio-compressor: aplicación única de transformación de audio para Amiga.
// ==========================================================================
//
// El pipeline host mantiene separadas las rutas SAMPLE y MUSIC: AUZX mono para samples y ACP1 v2
// multipista para stems WAV. La ruta espectral puede emitir una candidata ACP1 v3 expandida.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <eng/audio/auzx.hpp>
#include <eng/audio/fib_delta.hpp>
#include <eng/audio/media.hpp>
#include <eng/audio/pcm_codec.hpp>

#include "../../../host-tools/pack-pcm/wav_loader.hpp"
#include "acp1_writer.hpp"
#include "acp1_v3_writer.hpp"
#include "hpss.hpp"
#include <audio_compressor/codecs/registry.hpp>
#include <audio_compressor/domain/audio_types.hpp>
#include <audio_compressor/dsp/resampler.hpp>
#include <audio_compressor/dsp/harmonic_separation.hpp>
#include <audio_compressor/dsp/spectral_prototype_separation.hpp>
#include <audio_compressor/dsp/separation_metrics.hpp>
#include <audio_compressor/formats/auzx_sink.hpp>
#include <audio_compressor/io/file_io.hpp>
#include <audio_compressor/io/raw_source.hpp>
#include <audio_compressor/io/wav_source.hpp>
#include <audio_compressor/io/wav_stem_source.hpp>
#include <audio_compressor/pipeline/sample_pipeline.hpp>
#include <audio_compressor/pipeline/candidate_search.hpp>
#include <audio_compressor/pipeline/music_pipeline.hpp>
#include <audio_compressor/playback/acp1_host_player.hpp>
#include <audio_compressor/report/report_writer.hpp>
#include "sdl_player.hpp"

namespace {

using Config = audio_compressor::domain::ApplicationOptions;
using ConversionStats = audio_compressor::domain::ConversionReport;

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
	const std::string token = std::to_string(std::hash<std::string> {}(input));
	wav = (std::filesystem::temp_directory_path() / ("amiga-audio-compressor-input-" + token + ".wav")).string();
	const std::string executable = find_ffmpeg();
	const std::filesystem::path batch = std::filesystem::temp_directory_path() / ("amiga-audio-compressor-ffmpeg-" + token + ".bat");
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
	return audio_compressor::io::read_file(std::filesystem::path {path}, bytes);
}

[[nodiscard]] bool read_spectral_calibration(const std::string& path, eng::u32 rate, std::vector<audio_compressor::dsp::SpectralCalibrationWindow>& windows) {
	std::vector<eng::u8> bytes;
	if (!read_binary(path.c_str(), bytes)) return false;
	const std::string text(bytes.begin(), bytes.end()); const std::size_t begin = text.find("\"calibration\""); const std::size_t end = text.find("\"events\"", begin);
	if (begin == std::string::npos || end == std::string::npos) return false;
	std::size_t cursor = begin;
	while (cursor < end) {
		const std::size_t start_key = text.find("\"start\"", cursor); const std::size_t end_key = text.find("\"end\"", cursor);
		if (start_key == std::string::npos || end_key == std::string::npos || start_key > end) break;
		const std::size_t start_colon = text.find(':', start_key); const std::size_t end_colon = text.find(':', end_key);
		if (start_colon == std::string::npos || end_colon == std::string::npos) return false;
		const double first_second = std::stod(text.substr(start_colon + 1u)); const double last_second = std::stod(text.substr(end_colon + 1u));
		windows.push_back({static_cast<eng::u64>(std::max(0.0, first_second) * rate), static_cast<eng::u64>(std::max(first_second, last_second) * rate)});
		cursor = end_colon + 1u;
	}
	return !windows.empty();
}

/// Escribe una pista PCM8 firmada como WAV mono unsigned de 8 bits para escucha host/evaluación.
[[nodiscard]] bool write_pcm8_wav(const std::filesystem::path& path, const std::vector<eng::u8>& pcm, eng::u16 rate) {
	if (rate == 0u || pcm.size() > 0xffffffffu - 44u) return false;
	std::vector<eng::u8> file(44u + pcm.size(), 0u);
	const auto wr16 = [&](eng::usize at, eng::u16 value) { file[at] = value; file[at + 1u] = value >> 8u; };
	const auto wr32 = [&](eng::usize at, eng::u32 value) { wr16(at, value); wr16(at + 2u, value >> 16u); };
	std::memcpy(file.data(), "RIFF", 4u); wr32(4u, static_cast<eng::u32>(file.size() - 8u)); std::memcpy(file.data() + 8u, "WAVEfmt ", 8u);
	wr32(16u, 16u); wr16(20u, 1u); wr16(22u, 1u); wr32(24u, rate); wr32(28u, rate); wr16(32u, 1u); wr16(34u, 8u);
	std::memcpy(file.data() + 36u, "data", 4u); wr32(40u, static_cast<eng::u32>(pcm.size()));
	for (eng::usize i = 0u; i < pcm.size(); ++i) file[44u + i] = static_cast<eng::u8>(static_cast<eng::s8>(pcm[i]) + 128);
	if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
	return audio_compressor::io::write_file(path, file);
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
	const auto* descriptor = audio_compressor::codecs::find(name);
	return descriptor == nullptr ? eng::audio::pcm_codec::Codec::DeltaRle : descriptor->id;
}

[[nodiscard]] bool supported_codec_name(const std::string& name) {
	return name == "auto" || audio_compressor::codecs::find(name) != nullptr;
}

[[nodiscard]] bool encode_chunk(eng::Span<const eng::u8> pcm, eng::audio::pcm_codec::Codec codec,
	std::vector<eng::u8>& encoded, eng::u8& fib_seed) {
	encoded.assign(pcm.size() + pcm.size() / 128u + 32u, 0u);
	eng::s32 size = -1;
	if (codec == eng::audio::pcm_codec::Codec::None) {
		if (encoded.size() < pcm.size()) return false;
		std::memcpy(encoded.data(), pcm.data(), pcm.size());
		size = static_cast<eng::s32>(pcm.size());
	} else if (codec == eng::audio::pcm_codec::Codec::FibDelta) {
		size = eng::audio::fib_delta::encode(pcm, {encoded.data(), encoded.size()}, fib_seed);
	} else if (codec == eng::audio::pcm_codec::Codec::ImaAdpcm) {
		size = eng::audio::ima_adpcm::encode(pcm, {encoded.data(), encoded.size()});
	} else {
		size = eng::audio::pcm_codec::encode(pcm, {encoded.data(), encoded.size()},
			static_cast<eng::u8>(codec));
	}
	if (size <= 0) return false;
	encoded.resize(static_cast<eng::usize>(size));
	return true;
}

[[nodiscard]] bool encode_chunk(const std::vector<eng::u8>& pcm, eng::usize start, eng::usize count,
	eng::audio::pcm_codec::Codec codec, std::vector<eng::u8>& encoded, eng::u8& fib_seed) {
	return encode_chunk({pcm.data() + start, count}, codec, encoded, fib_seed);
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
	const auto* best = audio_compressor::pipeline::CandidateSearch::smallest(
		[&](const audio_compressor::codecs::Descriptor& descriptor, eng::u64& size) {
			return audio_compressor::codecs::accepts_chunk(descriptor, config.chunk_samples) &&
				estimate_codec_size(pcm, config.chunk_samples, descriptor.id, size);
		});
	if (best == nullptr) return false;
	config.codec = std::string(best->name);
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
	std::vector<eng::u8> file(cursor, 0u);
	audio_compressor::formats::BinaryWriter binary {file};
	file[0] = 'A'; file[1] = 'U'; file[2] = 'Z'; file[3] = 'X';
	(void)binary.u8(4u, 1u); (void)binary.u8(5u, static_cast<eng::u8>(codec));
	(void)binary.u16(6u, rate); (void)binary.u16(8u, 1u); (void)binary.u8(10u, 8u);
	(void)binary.u32(12u, static_cast<eng::u32>(pcm.size())); (void)binary.u16(16u, config.chunk_samples);
	(void)binary.u16(18u, chunks); (void)binary.u32(20u, eng::audio::auzx::kHeaderSize);
	(void)binary.u32(24u, offsets[0]);
	for (eng::u16 i = 0u; i < chunks; ++i) { const eng::usize at = eng::audio::auzx::kHeaderSize + i * eng::audio::auzx::kChunkEntrySize; (void)binary.u32(at, offsets[i]); (void)binary.u32(at + 4u, sizes[i]); std::memcpy(file.data() + offsets[i], bodies[i].data(), bodies[i].size()); }
	const std::filesystem::path output_path {native_safe_path(output)};
	if (output_path.has_parent_path()) std::filesystem::create_directories(output_path.parent_path());
	if (!audio_compressor::io::write_file(output_path, file)) {
		std::fprintf(stderr, "AUZX escritura falló para %s\n", output_path.string().c_str());
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

/// Codifica un WAV por ventanas para SAMPLE sin retener el PCM completo. La salida comprimida se
/// conserva hasta ensamblar el índice AUZX; el scratch PCM queda limitado a `chunk_samples`.
template <class Source>
[[nodiscard]] bool write_auzx_windowed(Source& source, eng::u16 source_rate, eng::u16 target_rate,
	const Config& config, const std::string& output, ConversionStats& stats) {
	const auto codec = codec_id(config.codec);
	return audio_compressor::pipeline::SamplePipeline::run(source, source_rate, target_rate,
		config.chunk_samples, config.window_samples, static_cast<eng::u8>(codec),
		config.mode == "sample", std::filesystem::path {native_safe_path(output)},
		[codec](eng::Span<const eng::u8> pcm, std::vector<eng::u8>& encoded, eng::u8& fib_seed) {
			return encode_chunk(pcm, codec, encoded, fib_seed);
		},
		[codec](eng::Span<const eng::u8> encoded, eng::Span<eng::u8> decoded, eng::u8) {
			if (codec == eng::audio::pcm_codec::Codec::None) {
				if (encoded.size() != decoded.size()) return static_cast<eng::s32>(-1);
				std::memcpy(decoded.data(), encoded.data(), encoded.size());
				return static_cast<eng::s32>(encoded.size());
			}
			return eng::audio::pcm_codec::decode(encoded, decoded, static_cast<eng::u8>(codec));
		}, stats);
}

/// Genera MUSIC ACP1 v2 desde un WAV intercalado por chunks, sin materializar todos los stems.
[[nodiscard]] bool write_music_windowed(audio_compressor::io::WavStemSource& source, const Config& config,
	const std::string& output) {
	const eng::u16 rate = static_cast<eng::u16>(source.format().sample_rate);
	if (rate == 0u || source.frames() > 0xffffffffu) return false;
	audio_compressor::pipeline::MusicPlan plan {};
	std::vector<std::vector<eng::u8>> unique_pcm_units;
	eng::usize unit_id = 0u;
	const auto encode = [&](const std::vector<eng::u8>& pcm, std::vector<eng::u8>& payload) {
		const std::string unit_path = output + ".window-unit-" + std::to_string(unit_id++);
		ConversionStats stats{};
		stats.pcm_bytes = pcm.size();
		const bool ok = write_auzx(pcm, rate, config, unit_path, stats);
		const std::string safe_unit_path = native_safe_path(unit_path);
		if (!ok) { std::remove(safe_unit_path.c_str()); return false; }
		const bool read_ok = read_binary(safe_unit_path.c_str(), payload);
		std::remove(safe_unit_path.c_str());
		return read_ok;
	};
	if (!audio_compressor::pipeline::MusicPipeline::build_windowed(source, config.chunk_samples, true, 255u,
		encode, plan, unique_pcm_units)) return false;
	plan.sample_rate = rate; plan.total_samples = static_cast<eng::u32>(source.frames());
	std::vector<eng::u8> file;
	if (!audio_compressor::pipeline::MusicPipeline::write_acp1_v2(plan, file)) return false;
	return audio_compressor::io::write_file(std::filesystem::path {native_safe_path(output)}, file);
}

/// Muestra la interfaz de la aplicación única, incluyendo el caso de arrastrar un archivo.
void print_help(const char* exe) { std::printf("Uso: %s <audio|auzx> [--mode auto|sample|music] [--synth-separate] [--synth-max-tracks N] [--synth-listen N] [--synth-export-dir dir] [--spectral-separate] [--spectral-both] [--spectral-max-prototypes N] [--spectral-target-residual R] [--spectral-listen N] [--spectral-calibration file] [--tracker-row-samples N] [--spectral-export-dir dir] [--spectral-fft N] [--spectral-hop N] [--spectral-max-shift-bins N] [--spectral-seed-candidates N] [--spectral-min-activation R] [--spectral-max-dictionary-bytes N] [--spectral-codec auto|none|rle|fib|ima] [--spectral-max-codec-error N] [--acp1-version 2|3] [--config f] [--out f] [--codec auto|rle|fib|ima|none] [--sample-rate Hz] [--chunk muestras] [--report f] [--keep-candidates] [--compare] [--play] [--dry-run]\n", exe); }

} // namespace

/// Punto de entrada: resuelve configuración, clasifica y ejecuta el pipeline disponible.
int main(int argc, char** argv) {
	if (argc < 2 || (argc == 2 && std::strcmp(argv[1], "--help") == 0)) { print_help(argv[0]); return argc < 2 ? 2 : 0; }
	Config config{}; const std::string input = native_safe_path(argv[1]); const char* config_path = nullptr; std::string output; std::string report; std::string synth_export_dir; std::string spectral_export_dir; std::string spectral_calibration_path; std::string spectral_codec = "auto"; bool synth_separate = false; bool spectral_separate = false; bool spectral_both = false; eng::u8 synth_max_tracks = 3u; eng::u8 spectral_max_prototypes = 3u; eng::u16 spectral_fft = 256u; eng::u16 spectral_hop = 64u; eng::s16 spectral_max_shift_bins = 12; eng::u8 spectral_seed_candidates = 8u; eng::u8 spectral_max_codec_error = 8u; eng::u64 spectral_max_dictionary_bytes = 0u; eng::u32 tracker_row_samples = 0u; double spectral_target_residual = 0.0; double spectral_min_activation = 0.02; int synth_listen = -1; int spectral_listen = -1;
	for (int i = 2; i < argc; ++i) {
		if (std::strcmp(argv[i], "--help") == 0) { print_help(argv[0]); return 0; }
		if (std::strcmp(argv[i], "--dry-run") == 0) { config.dry_run = true; continue; }
		if (std::strcmp(argv[i], "--play") == 0) { config.play = true; continue; }
		if (std::strcmp(argv[i], "--force") == 0) { config.force = true; continue; }
		if (std::strcmp(argv[i], "--keep-candidates") == 0) { config.keep_candidates = true; continue; }
		if (std::strcmp(argv[i], "--compare") == 0) { config.compare_candidates = true; continue; }
		if (std::strcmp(argv[i], "--synth-separate") == 0) { synth_separate = true; continue; }
		if (std::strcmp(argv[i], "--synth-max-tracks") == 0) { if (++i >= argc) return 2; synth_max_tracks = static_cast<eng::u8>(std::atoi(argv[i])); continue; }
		if (std::strcmp(argv[i], "--synth-listen") == 0) { if (++i >= argc) return 2; synth_listen = std::atoi(argv[i]); synth_separate = true; continue; }
		if (std::strcmp(argv[i], "--synth-export-dir") == 0) { if (++i >= argc) return 2; synth_export_dir = argv[i]; synth_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-separate") == 0) { spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-both") == 0) { spectral_separate = true; spectral_both = true; continue; }
		if (std::strcmp(argv[i], "--spectral-max-prototypes") == 0) { if (++i >= argc) return 2; spectral_max_prototypes = static_cast<eng::u8>(std::atoi(argv[i])); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-target-residual") == 0) { if (++i >= argc) return 2; spectral_target_residual = std::atof(argv[i]); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-listen") == 0) { if (++i >= argc) return 2; spectral_listen = std::atoi(argv[i]); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-calibration") == 0) { if (++i >= argc) return 2; spectral_calibration_path = argv[i]; spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--tracker-row-samples") == 0) { if (++i >= argc) return 2; tracker_row_samples = static_cast<eng::u32>(std::strtoul(argv[i], nullptr, 10)); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-export-dir") == 0) { if (++i >= argc) return 2; spectral_export_dir = argv[i]; spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-fft") == 0) { if (++i >= argc) return 2; spectral_fft = static_cast<eng::u16>(std::atoi(argv[i])); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-hop") == 0) { if (++i >= argc) return 2; spectral_hop = static_cast<eng::u16>(std::atoi(argv[i])); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-max-shift-bins") == 0) { if (++i >= argc) return 2; spectral_max_shift_bins = static_cast<eng::s16>(std::atoi(argv[i])); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-seed-candidates") == 0) { if (++i >= argc) return 2; spectral_seed_candidates = static_cast<eng::u8>(std::atoi(argv[i])); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-min-activation") == 0) { if (++i >= argc) return 2; spectral_min_activation = std::atof(argv[i]); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-max-dictionary-bytes") == 0) { if (++i >= argc) return 2; spectral_max_dictionary_bytes = static_cast<eng::u64>(std::strtoull(argv[i], nullptr, 10)); spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-codec") == 0) { if (++i >= argc) return 2; spectral_codec = argv[i]; spectral_separate = true; continue; }
		if (std::strcmp(argv[i], "--spectral-max-codec-error") == 0) { if (++i >= argc) return 2; spectral_max_codec_error = static_cast<eng::u8>(std::atoi(argv[i])); spectral_separate = true; continue; }
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
		else if (std::strcmp(argv[i], "--synth-max-tracks") == 0) ++i;
		else if (std::strcmp(argv[i], "--synth-listen") == 0) ++i;
		else if (std::strcmp(argv[i], "--synth-export-dir") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-max-prototypes") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-target-residual") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-listen") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-calibration") == 0) ++i;
		else if (std::strcmp(argv[i], "--tracker-row-samples") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-export-dir") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-fft") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-hop") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-max-shift-bins") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-seed-candidates") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-min-activation") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-max-dictionary-bytes") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-codec") == 0) ++i;
		else if (std::strcmp(argv[i], "--spectral-max-codec-error") == 0) ++i;
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
	if (!synth_separate && !spectral_separate && !config.play && !config.hpss && !needs_ffmpeg(input) &&
		config.acp1_version == 2u &&
		(std::filesystem::path {input}.extension() == ".wav" || std::filesystem::path {input}.extension() == ".WAV")) {
		audio_compressor::io::WavStemSource windowed_music_source;
		if (windowed_music_source.open(input) && windowed_music_source.format().sample_rate <= 65535u) {
			const eng::u16 source_rate = static_cast<eng::u16>(windowed_music_source.format().sample_rate);
			const std::string candidate_mode = classify(config, static_cast<eng::usize>(windowed_music_source.frames()), source_rate);
			if (candidate_mode == "music" && (config.sample_rate == 0u || config.sample_rate == source_rate)) {
				if (output.empty()) output = default_output(input.c_str(), "music");
				output = native_safe_path(output);
				if (!config.force && std::filesystem::exists(std::filesystem::path {output})) { std::fprintf(stderr, "salida existente; use --force\n"); return 1; }
				if (!write_music_windowed(windowed_music_source, config, output)) { std::fprintf(stderr, "no se pudo generar MUSIC windowed\n"); return 1; }
				std::printf("music windowed=ok stems=%u samples=%llu ruta=Paula3\n", windowed_music_source.channels(), static_cast<unsigned long long>(windowed_music_source.frames()));
				return 0;
			}
		}
	}
	if (config.mode == "sample" && !needs_ffmpeg(input) && !config.hpss && config.codec != "auto") {
		audio_compressor::io::WavSource windowed_source;
		if (windowed_source.open(input) && windowed_source.sample_rate() <= 65535u) {
			const eng::u16 source_rate = static_cast<eng::u16>(windowed_source.sample_rate());
			config.sample_rate = config.sample_rate == 0u ? source_rate : config.sample_rate;
			if (output.empty()) output = default_output(input.c_str(), "sample");
			output = native_safe_path(output);
			if (!config.force && std::filesystem::exists(std::filesystem::path {output})) {
				std::fprintf(stderr, "salida existente; use --force\n"); return 1;
			}
			ConversionStats windowed_stats{};
			std::error_code input_error{};
			windowed_stats.input_bytes = std::filesystem::file_size(std::filesystem::path {input}, input_error);
			windowed_stats.pcm_bytes = windowed_source.frames();
			if (!write_auzx_windowed(windowed_source, source_rate, config.sample_rate, config, output, windowed_stats)) {
				std::fprintf(stderr, "no se pudo codificar la fuente WAV por ventanas\n"); return 1;
			}
			if (!report.empty()) audio_compressor::report::write(report, input, "sample", config, windowed_stats);
			std::printf("sample windowed=%llu bytes codec=%s samples=%llu\n",
				static_cast<unsigned long long>(windowed_stats.output_bytes), config.codec.c_str(),
				static_cast<unsigned long long>(windowed_stats.samples));
			return 0;
		}
	}
	if (config.mode == "sample" && !config.hpss && config.codec != "auto" &&
		(std::filesystem::path {input}.extension() == ".raw" || std::filesystem::path {input}.extension() == ".RAW")) {
		audio_compressor::io::RawSource raw_source;
		const eng::u16 raw_rate = config.sample_rate == 0u ? 8000u : config.sample_rate;
		if (!raw_source.open(input, raw_rate)) {
			std::fprintf(stderr, "RAW inválido o frecuencia no válida\n"); return 1;
		}
		if (output.empty()) output = default_output(input.c_str(), "sample");
		output = native_safe_path(output);
		if (!config.force && std::filesystem::exists(std::filesystem::path {output})) {
			std::fprintf(stderr, "salida existente; use --force\n"); return 1;
		}
		ConversionStats raw_stats{};
		std::error_code input_error{};
		raw_stats.input_bytes = std::filesystem::file_size(std::filesystem::path {input}, input_error);
		raw_stats.pcm_bytes = raw_source.frames();
		const eng::u16 target_rate = config.sample_rate == 0u ? raw_rate : config.sample_rate;
		config.sample_rate = target_rate;
		if (!write_auzx_windowed(raw_source, raw_rate, target_rate, config, output, raw_stats)) {
			std::fprintf(stderr, "no se pudo codificar la fuente RAW por ventanas\n"); return 1;
		}
		if (!report.empty()) audio_compressor::report::write(report, input, "sample", config, raw_stats);
		std::printf("sample windowed=%llu bytes codec=%s samples=%llu\n",
			static_cast<unsigned long long>(raw_stats.output_bytes), config.codec.c_str(),
			static_cast<unsigned long long>(raw_stats.samples));
		return 0;
	}
	std::vector<eng::u8> pcm; eng::u16 rate = 0u; std::string decoded_input;
	pack_pcm::WavStems input_stems {};
	const std::string source = needs_ffmpeg(input) ?
		(decode_external_source(input, decoded_input, config.sample_rate) ? decoded_input : std::string{}) : input;
	if (source.empty()) { std::fprintf(stderr, "no se pudo decodificar la fuente externa; configure FFMPEG/FFMPEG_BIN\n"); return 1; }
	if (config.play && !synth_separate) {
		if (!load_playback_pcm(source.c_str(), pcm, rate)) { std::fprintf(stderr, "entrada inválida o no soportada para reproducción\n"); return 1; }
	} else if (!pack_pcm::load_stems(source.c_str(), input_stems, config.sample_rate) ||
		!pack_pcm::downmix(input_stems, pcm)) { std::fprintf(stderr, "entrada inválida o no soportada\n"); return 1; }
	if (input_stems.sample_rate != 0u) rate = input_stems.sample_rate;
	if (config.sample_rate == 0u) config.sample_rate = rate;
	if (rate == 0u || !fits_memory_budget(input_stems, pcm, config)) {
		std::fprintf(stderr, "la entrada excede --ram-budget con el pipeline actual; rate=%u pcm=%lu budget=%llu\n", rate, static_cast<unsigned long>(pcm.size()), static_cast<unsigned long long>(config.ram_budget_bytes));
		return 2;
	}
	std::vector<audio_compressor::dsp::SpectralCalibrationWindow> spectral_calibration;
	if (!spectral_calibration_path.empty() && !read_spectral_calibration(spectral_calibration_path, rate, spectral_calibration)) { std::fprintf(stderr, "manifiesto de calibración espectral inválido: %s\n", spectral_calibration_path.c_str()); return 2; }
	if (spectral_separate) {
		struct SpectralVariantSummary { eng::u8 maximum = 0u; eng::u64 tracker_bytes = 0u; double residual = 1.0; };
		std::vector<SpectralVariantSummary> variant_summaries;
		auto run_spectral = [&](eng::u8 prototype_count) {
		audio_compressor::dsp::SpectralPrototypeOptions options {};
		options.max_prototypes = prototype_count;
		options.fft_size = spectral_fft; options.hop_samples = spectral_hop; options.max_shift_bins = spectral_max_shift_bins;
		options.seed_candidates = spectral_seed_candidates; options.max_dictionary_bytes = spectral_max_dictionary_bytes; options.min_activation_ratio = spectral_min_activation;
		options.calibration_windows = spectral_calibration;
			options.stop_residual_ratio = spectral_target_residual;
			audio_compressor::dsp::SpectralSeparationResult result {};
			if (!audio_compressor::dsp::separate_spectral_prototypes(pcm, rate, options, result)) return false;
			if (spectral_listen >= 0) {
				if (static_cast<eng::usize>(spectral_listen) >= result.reconstructed_tracks.size() || !audio_compressor::play_pcm({result.reconstructed_tracks[spectral_listen].data(), result.reconstructed_tracks[spectral_listen].size()}, rate)) return false;
			}
			const std::string route = result.requires_octamed ? "octamed" : (result.prototypes.size() <= 3u ? "paula3" : "mixer4");
			std::vector<std::string> prototype_codecs;
			eng::u64 compressed_prototype_bytes = 0u;
			auto choose_spectral_codec = [&](const std::vector<eng::u8>& samples, std::string& name, CodecCandidate& metrics) {
				const audio_compressor::codecs::Descriptor* selected = nullptr;
				for (const auto& descriptor : audio_compressor::codecs::kAuzxEncoders) {
					if (spectral_codec != "auto" && descriptor.name != spectral_codec) continue;
					if (!audio_compressor::codecs::accepts_chunk(descriptor, config.chunk_samples)) continue;
					CodecCandidate candidate {};
					if (!evaluate_codec(samples, config.chunk_samples, descriptor.id, candidate) || candidate.peak_error > spectral_max_codec_error) continue;
					if (selected == nullptr || candidate.bytes < metrics.bytes) { selected = &descriptor; metrics = candidate; }
				}
				if (selected == nullptr) return false;
				name = std::string(selected->name); return true;
			};
			for (const auto& prototype : result.prototypes) {
				CodecCandidate selected_metrics {};
				std::string selected_name;
				if (!choose_spectral_codec(prototype.pcm, selected_name, selected_metrics)) return false;
				prototype_codecs.push_back(selected_name); compressed_prototype_bytes += selected_metrics.bytes;
			}
			eng::u64 event_count = 0u;
			const eng::u32 cost_row_samples = tracker_row_samples == 0u ? std::max<eng::u32>(result.fft_size, static_cast<eng::u32>(rate * 6u / 50u)) : std::max<eng::u32>(result.fft_size, tracker_row_samples);
			for (const auto& prototype : result.prototypes) {
				eng::u64 previous_slot = std::numeric_limits<eng::u64>::max();
				for (eng::usize frame = 0u; frame < prototype.activation.size(); ++frame) if (prototype.activation[frame] > 0.0) {
					const eng::u64 slot = cost_row_samples == 0u ? frame : (static_cast<eng::u64>(frame) * result.hop_samples) / cost_row_samples;
					if (slot != previous_slot) { ++event_count; previous_slot = slot; }
				}
			}
			const eng::u64 tracker_bytes = 336u + static_cast<eng::u64>(result.prototypes.size()) * 24u + event_count * 44u + compressed_prototype_bytes;
			variant_summaries.push_back({prototype_count, tracker_bytes, result.metrics.residual_ratio});
			if (!spectral_export_dir.empty()) {
				const std::filesystem::path directory = std::filesystem::path {spectral_export_dir} / ("spectral-" + std::to_string(prototype_count));
				std::filesystem::create_directories(directory);
				for (eng::usize i = 0u; i < result.prototypes.size(); ++i) {
					if (!write_pcm8_wav(directory / ("prototype-" + std::to_string(i) + ".wav"), result.prototypes[i].pcm, rate) ||
						!write_pcm8_wav(directory / ("track-" + std::to_string(i) + ".wav"), result.reconstructed_tracks[i], rate)) return false;
					Config prototype_config = config; prototype_config.codec = prototype_codecs[i];
					ConversionStats prototype_stats {};
					if (!write_auzx(result.prototypes[i].pcm, rate, prototype_config,
						(directory / ("prototype-" + std::to_string(i) + ".auzx")).string(), prototype_stats)) return false;
					const std::pair<eng::usize, eng::usize> regions[] = {{0u, result.prototypes[i].pcm.size() / 5u / 2u * 2u}, {result.prototypes[i].pcm.size() / 5u / 2u * 2u, result.prototypes[i].pcm.size() - result.prototypes[i].pcm.size() / 5u / 2u * 4u}, {result.prototypes[i].pcm.size() - result.prototypes[i].pcm.size() / 5u / 2u * 2u, result.prototypes[i].pcm.size()}};
					const char* region_names[] = {"attack", "sustain", "release"};
					for (eng::usize region = 0u; region < 3u; ++region) {
						const auto [begin, end] = regions[region];
						if (end <= begin) continue;
						std::vector<eng::u8> region_pcm(result.prototypes[i].pcm.begin() + begin, result.prototypes[i].pcm.begin() + end);
						std::string region_codec; CodecCandidate region_metrics {};
						if (!choose_spectral_codec(region_pcm, region_codec, region_metrics)) return false;
						Config region_config = config; region_config.codec = region_codec; ConversionStats region_stats {};
						if (!write_auzx(region_pcm, rate, region_config, (directory / ("prototype-" + std::to_string(i) + "-" + region_names[region] + ".auzx")).string(), region_stats)) return false;
					}
				}
				if (result.prototypes.size() <= 7u) {
					std::vector<audio_compressor::SpectralPcmTrack> compact_tracks;
					for (eng::usize prototype_index = 0u; prototype_index < result.prototypes.size(); ++prototype_index) {
						const auto& prototype = result.prototypes[prototype_index];
						audio_compressor::SpectralPcmTrack track {}; track.route = 0u; track.sample_loop = true; track.pcm = prototype.pcm;
						const auto encoded_codec = codec_id(prototype_codecs[prototype_index]);
						eng::u8 fib_seed = 0u;
						if (!encode_chunk({prototype.pcm.data(), prototype.pcm.size()}, encoded_codec, track.payload, fib_seed)) return false;
						track.codec = encoded_codec == eng::audio::pcm_codec::Codec::None ? 0u : encoded_codec == eng::audio::pcm_codec::Codec::DeltaRle ? 1u : encoded_codec == eng::audio::pcm_codec::Codec::FibDelta ? 5u : encoded_codec == eng::audio::pcm_codec::Codec::ImaAdpcm ? 6u : 0u;
						const double centre = [&] { double weighted = 0.0, total = 0.0; for (eng::usize bin = 0u; bin < prototype.magnitude.size(); ++bin) { weighted += bin * prototype.magnitude[bin]; total += prototype.magnitude[bin]; } return total > 1.0e-9 ? weighted / total : 1.0; }();
						for (eng::usize frame = 0u; frame < prototype.activation.size(); ++frame) if (prototype.activation[frame] > 0.0) {
							const eng::u64 frame_start = static_cast<eng::u64>(frame) * result.hop_samples;
							const eng::u32 row_samples = tracker_row_samples == 0u ? std::max<eng::u32>(result.fft_size, static_cast<eng::u32>(rate * 6u / 50u)) : std::max<eng::u32>(result.fft_size, tracker_row_samples);
							const eng::u64 slot = row_samples == 0u ? 0u : frame_start / row_samples;
							const eng::u64 start = slot * row_samples;
							const eng::u32 duration = static_cast<eng::u32>(std::min<eng::u64>(row_samples, pcm.size() - std::min<eng::u64>(start, pcm.size())));
							if (duration == 0u) continue;
							const double shifted = std::max(1.0, centre + prototype.shift_bins[frame]);
							const eng::s16 pitch = static_cast<eng::s16>(std::clamp(static_cast<double>(std::lround(12.0 * std::log2(shifted / std::max(1.0, centre)) * 256.0)), -32768.0, 32767.0));
							const double gain = std::clamp(prototype.activation[frame] / std::max(1.0e-9, *std::max_element(prototype.activation.begin(), prototype.activation.end())), 0.0, 1.0);
							const eng::u16 gain_q8_8 = static_cast<eng::u16>(std::lround(gain * 256.0));
							if (!track.events.empty() && track.events.back().start_sample == start) {
								if (gain_q8_8 > track.events.back().gain_q8_8) { track.events.back().gain_q8_8 = gain_q8_8; track.events.back().pitch_semitones_q8_8 = pitch; }
							} else track.events.push_back({start, duration, gain_q8_8, pitch});
						}
						if (!track.events.empty()) compact_tracks.push_back(std::move(track));
					}
					std::vector<eng::u8> acp1;
					if (!audio_compressor::build_acp1_v3_spectral(compact_tracks, rate, pcm.size(), acp1) ||
						!audio_compressor::io::write_file(directory / "spectral.acp1", acp1)) return false;
					audio_compressor::playback::Acp1HostPlayer compact_player;
					if (!compact_player.open({acp1.data(), acp1.size()})) { std::fprintf(stderr, "ACP1 compacto no abre tras compresión\n"); return false; }
					std::vector<eng::u8> compact_rebuilt(pcm.size(), 128u), compact_window(config.window_samples), compact_scratch(config.window_samples);
					std::vector<eng::s16> compact_accumulator(config.window_samples);
					for (eng::usize start = 0u; start < pcm.size(); start += config.window_samples) {
						const eng::usize count = std::min<eng::usize>(config.window_samples, pcm.size() - start);
						if (compact_player.read_window(static_cast<eng::u32>(start), {compact_window.data(), count}, {compact_scratch.data(), compact_scratch.size()}, {compact_accumulator.data(), compact_accumulator.size()}) != static_cast<eng::s32>(count)) { std::fprintf(stderr, "ACP1 compacto no reconstruye ventana %lu tras compresión\n", static_cast<unsigned long>(start)); return false; }
						std::copy(compact_window.begin(), compact_window.begin() + count, compact_rebuilt.begin() + start);
					}
					const auto compact_metrics = audio_compressor::dsp::reconstruction_metrics(pcm, compact_rebuilt);
					std::printf("spectral-acp1=validated bytes=%lu MSE_PCM=%.4f SNR_PCM=%.2f\n", static_cast<unsigned long>(acp1.size()), compact_metrics.mse, compact_metrics.snr_db);
				} else {
					const std::string manifest = "route=octamed\nprototypes=" + std::to_string(result.prototypes.size()) + "\n";
					const std::vector<eng::u8> bytes(manifest.begin(), manifest.end());
					if (!audio_compressor::io::write_file(directory / "octamed-route.txt", bytes)) return false;
				}
			}
			std::printf("spectral-separation=ok max=%u prototipos=%lu MSE_mag=%.6f SNR_mag=%.2f residual=%.4f bytes_tracker=%llu eventos=%llu bytes_estimados=%llu bytes_codec=%llu voces=%u periodo_frames=%u periodicidad=%.3f ruta=%s codecs=%s\n", prototype_count, static_cast<unsigned long>(result.prototypes.size()), result.metrics.magnitude_mse, result.metrics.magnitude_snr_db, result.metrics.residual_ratio, static_cast<unsigned long long>(tracker_bytes), static_cast<unsigned long long>(event_count), static_cast<unsigned long long>(result.estimated_bytes), static_cast<unsigned long long>(compressed_prototype_bytes), result.peak_concurrent_prototypes, result.dominant_period_frames, result.periodicity_score, route.c_str(), [&] { std::string value; for (eng::usize i = 0u; i < prototype_codecs.size(); ++i) { if (i != 0u) value += ","; value += prototype_codecs[i]; } return value; }().c_str());
			return true;
		};
		if (spectral_both) { if (!run_spectral(3u) || !run_spectral(8u)) { std::fprintf(stderr, "la separación espectral no produjo una representación válida\n"); return 2; } }
		else if (!run_spectral(spectral_max_prototypes)) { std::fprintf(stderr, "la separación espectral no produjo una representación válida\n"); return 2; }
		if (!variant_summaries.empty()) {
			const auto best = std::min_element(variant_summaries.begin(), variant_summaries.end(), [](const auto& left, const auto& right) { return left.tracker_bytes + left.residual * 1000000.0 < right.tracker_bytes + right.residual * 1000000.0; });
			std::printf("tracker-selection=ok max=%u bytes=%llu residual=%.4f\n", best->maximum, static_cast<unsigned long long>(best->tracker_bytes), best->residual);
		}
		if (!decoded_input.empty()) std::remove(decoded_input.c_str());
		return 0;
	}
	if (synth_separate) {
		audio_compressor::dsp::HarmonicSeparationOptions options {};
		options.window_samples = config.window_samples > 65535u ? 65535u : static_cast<eng::u16>(config.window_samples);
		options.max_tracks = synth_max_tracks;
		std::vector<audio_compressor::dsp::HarmonicTrackModel> models;
		if (!audio_compressor::dsp::separate_harmonic_windowed(pcm, rate, options, models)) {
			std::fprintf(stderr, "la separación armónica no produjo pistas utilizables\n"); return 2;
		}
		if (models.size() > 7u) { std::printf("synth-separation=fallback=octamed modelos=%lu\n", static_cast<unsigned long>(models.size())); return 0; }
		std::vector<audio_compressor::AdditiveTrack> tracks;
		for (const auto& model : models) {
			audio_compressor::AdditiveTrack track {};
			track.fundamental_hz_q16_16 = model.fundamental_hz_q16_16; track.partials = model.partials;
			for (const auto& note : model.notes) track.notes.push_back({note.start_sample, note.duration, note.pitch_semitones_q8_8, note.gain_q8_8});
			if (!track.notes.empty()) {
				auto& last = track.notes.back();
				const eng::u64 pcm_end = static_cast<eng::u64>(pcm.size());
				const eng::u64 note_end = last.start_sample + last.duration;
				if (note_end < pcm_end && pcm_end - last.start_sample <= 0xffffffffull) last.duration = static_cast<eng::u32>(pcm_end - last.start_sample);
			}
			if (!track.notes.empty()) tracks.push_back(std::move(track));
		}
		std::vector<eng::u8> synth_file;
		if (!audio_compressor::build_acp1_v3_additive(tracks, rate, synth_file)) { std::fprintf(stderr, "no se pudo serializar la separación armónica\n"); return 2; }
		if (output.empty()) output = default_output(input.c_str(), "music");
		output = native_safe_path(output);
		if (!config.force && std::filesystem::exists(std::filesystem::path {output})) { std::fprintf(stderr, "salida existente; use --force\n"); return 1; }
		if (!audio_compressor::io::write_file(std::filesystem::path {output}, synth_file)) return 1;
		audio_compressor::playback::Acp1HostPlayer player;
		std::vector<eng::u8> rebuilt(pcm.size()), scratch(config.window_samples), window(config.window_samples);
		std::vector<std::vector<eng::u8>> track_pcm(tracks.size(), std::vector<eng::u8>(pcm.size(), 0x80u));
		std::vector<eng::s16> accumulator(config.window_samples);
		for (eng::usize start = 0u; start < pcm.size(); start += config.window_samples) {
			const eng::usize count = std::min<eng::usize>(config.window_samples, pcm.size() - start);
			if (!player.open({synth_file.data(), synth_file.size()})) { std::fprintf(stderr, "no se pudo abrir el ACP1 sintetizado en la ventana %lu\n", static_cast<unsigned long>(start)); return 2; }
			if (player.read_window(static_cast<eng::u32>(start), {window.data(), count}, {scratch.data(), scratch.size()}, {accumulator.data(), accumulator.size()}) != static_cast<eng::s32>(count)) { std::fprintf(stderr, "no se pudo reconstruir la ventana %lu del ACP1 sintetizado\n", static_cast<unsigned long>(start)); return 2; }
			for (eng::usize i = 0u; i < count; ++i) rebuilt[start + i] = window[i];
			for (eng::usize track = 0u; track < tracks.size(); ++track) if (player.read_track_window(static_cast<eng::u32>(track), static_cast<eng::u32>(start), {track_pcm[track].data() + start, count}, {scratch.data(), scratch.size()}, {accumulator.data(), accumulator.size()}) != static_cast<eng::s32>(count)) { std::fprintf(stderr, "no se pudo reconstruir la pista %lu en la ventana %lu\n", static_cast<unsigned long>(track), static_cast<unsigned long>(start)); return 2; }
		}
		const auto metrics = audio_compressor::dsp::reconstruction_metrics(pcm, rebuilt);
		const double leakage = audio_compressor::dsp::cross_track_leakage_db(track_pcm);
		if (!synth_export_dir.empty()) {
			if (!write_pcm8_wav(std::filesystem::path {synth_export_dir} / "mix.wav", rebuilt, rate)) return 1;
			for (eng::usize track = 0u; track < track_pcm.size(); ++track) if (!write_pcm8_wav(std::filesystem::path {synth_export_dir} / ("track-" + std::to_string(track) + ".wav"), track_pcm[track], rate)) return 1;
		}
		std::printf("synth-separation=ok modelos=%lu bytes=%lu MSE=%.4f SNR=%.2f pico=%u fuga_dB=%.2f salida=%s\n", static_cast<unsigned long>(tracks.size()), static_cast<unsigned long>(synth_file.size()), metrics.mse, metrics.snr_db, metrics.peak_error, leakage, output.c_str());
		if (synth_listen >= 0) {
			if (static_cast<eng::usize>(synth_listen) >= track_pcm.size() || !audio_compressor::play_pcm({track_pcm[synth_listen].data(), track_pcm[synth_listen].size()}, rate)) {
				std::fprintf(stderr, "pista no disponible o reproducción SDL3 no compilada\n"); return 4;
			}
		}
		if (!decoded_input.empty()) std::remove(decoded_input.c_str());
		return 0;
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
				const eng::usize overlap = std::min<eng::usize>(128u, config.window_samples / 2u);
				if (!audio_compressor::hpss_windowed_pcm(stem, config.window_samples, overlap, 256u, layers)) {
					std::fprintf(stderr, "HPSS windowed no pudo procesar el stem\n"); return 1;
				}
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
			if (!audio_compressor::io::write_file(v3_path, v3_file)) return 1;
			eng::audio::acp1_v3::Info v3_info{};
			if (!eng::audio::acp1_v3::parse({v3_file.data(), v3_file.size()}, v3_info)) {
				std::fprintf(stderr, "ACP1 v3 MVP no supera su parser\n"); return 1;
			}
			std::printf("ACP1 v3 MVP=%llu bytes; pistas=%lu; unidades=%lu; segmentos PCM=%lu\n",
				static_cast<unsigned long long>(v3_file.size()), static_cast<unsigned long>(v3_info.track_count),
				static_cast<unsigned long>(v3_info.unit_count), static_cast<unsigned long>(v3_info.segment_count));
			if (!report.empty()) audio_compressor::report::write(report, input, mode, config, linear_stats, v3_file.size());
			if (config.acp1_version == 2u && !config.keep_candidates) std::remove(linear.c_str());
			return 0;
		}
		const bool paula_only = true; // MUSIC con pitch/volumen variable usa exclusivamente AUD1..AUD3.
		const eng::u8 track_gain = paula_only ? 255u : (config.hpss || input_stems.channels.size() > 1u ? 128u : 255u);
		audio_compressor::pipeline::MusicPipeline::Preflight preflight {};
		if (!audio_compressor::pipeline::MusicPipeline::preflight(source_stems, config.chunk_samples, paula_only,
			512u * 1024u, config.ram_budget_bytes, preflight)) {
			std::fprintf(stderr, "la ruta de audio excede las cuotas Paula/Chip/Fast o necesita más de tres voces Paula\n"); return 2;
		}
		std::vector<std::vector<eng::u8>> unique_pcm_units;
		audio_compressor::pipeline::MusicPlan music_plan {};
		if (!audio_compressor::pipeline::MusicPipeline::build_plan(source_stems, config.chunk_samples, track_gain,
			paula_only, music_plan, unique_pcm_units)) {
			std::fprintf(stderr, "la música requiere como máximo tres stems Paula directos\n"); return 1;
		}
		music_plan.sample_rate = config.sample_rate;
		for (eng::usize unit_id = 0u; unit_id < unique_pcm_units.size(); ++unit_id) {
			const eng::usize count = unique_pcm_units[unit_id].size();
			const std::string unit_path = linear + ".unit-" + std::to_string(unit_id) + ".auzx";
			ConversionStats unit_stats{};
			unit_stats.pcm_bytes = count;
			if (!write_auzx(unique_pcm_units[unit_id], config.sample_rate, config, unit_path, unit_stats)) return 1;
			std::vector<eng::u8> bytes;
			const std::string safe_unit_path = native_safe_path(unit_path);
			if (!read_binary(safe_unit_path.c_str(), bytes)) return 1;
			music_plan.payloads[unit_id] = std::move(bytes);
			std::remove(safe_unit_path.c_str());
		}
		std::vector<eng::u8> acp1_file;
		if (!audio_compressor::pipeline::MusicPipeline::write_acp1_v2(music_plan, acp1_file)) return 1;
		if (!audio_compressor::io::write_file(std::filesystem::path {native_safe_path(structural)}, acp1_file)) return 1;
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
		if (!report.empty()) audio_compressor::report::write(report, input, mode, config, structural_stats, structural_bytes);
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
	if (!report.empty()) audio_compressor::report::write(report, input, mode, config, stats);
	if (!decoded_input.empty()) std::remove(decoded_input.c_str());
	return 0;
}
