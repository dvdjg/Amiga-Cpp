#pragma once

/// Serializador host de informes JSON; escapa rutas, modos y nombres de codec.

#include <cmath>
#include <cstdio>
#include <string>

#include <eng/core/types/types.hpp>

namespace audio_compressor::report {

inline void write_json_string(std::FILE* out, const std::string& value) {
	std::fputc('"', out);
	for (const unsigned char character : value) {
		switch (character) {
		case '\\': std::fputs("\\\\", out); break;
		case '"': std::fputs("\\\"", out); break;
		case '\n': std::fputs("\\n", out); break;
		case '\r': std::fputs("\\r", out); break;
		case '\t': std::fputs("\\t", out); break;
		default: std::fputc(character < 0x20u ? '?' : character, out); break;
		}
	}
	std::fputc('"', out);
}

template <class Config, class Stats>
void write(const std::string& path, const std::string& input, const std::string& mode,
	const Config& config, const Stats& stats, eng::u64 structural_bytes = 0u) {
	std::FILE* out = std::fopen(path.c_str(), "wb");
	if (!out) return;
	const double pcm_ratio = stats.pcm_bytes == 0u ? 0.0 : static_cast<double>(stats.output_bytes) / static_cast<double>(stats.pcm_bytes);
	const double source_ratio = stats.input_bytes == 0u ? 0.0 : static_cast<double>(stats.output_bytes) / static_cast<double>(stats.input_bytes);
	const double mse = stats.samples == 0u ? 0.0 : static_cast<double>(stats.squared_error) / static_cast<double>(stats.samples);
	const double snr_db = stats.squared_error == 0u ? 999.0 : 10.0 * std::log10(static_cast<double>(stats.signal_energy) / static_cast<double>(stats.squared_error));
	std::fputs("{\n  \"input\": ", out); write_json_string(out, input);
	std::fputs(",\n  \"mode\": ", out); write_json_string(out, mode);
	std::fputs(",\n  \"codec\": ", out); write_json_string(out, config.codec);
	std::fprintf(out, ",\n  \"sample_rate\": %u,\n  \"chunk_samples\": %u,\n  \"samples\": %llu,\n  \"duration_seconds\": %.6f,\n  \"input_bytes\": %llu,\n  \"pcm_bytes\": %llu,\n  \"output_bytes\": %llu,\n  \"structural_bytes\": %llu,\n  \"ratio_pcm_to_output\": %.6f,\n  \"ratio_source_to_output\": %.6f,\n  \"mse_pcm8\": %.6f,\n  \"snr_db\": %.3f,\n  \"peak_error\": %u,\n  \"repeated_windows\": %u,\n  \"round_trip_ok\": %s\n}\n", config.sample_rate, config.chunk_samples,
		static_cast<unsigned long long>(stats.samples), stats.sample_rate == 0u ? 0.0 : static_cast<double>(stats.samples) / stats.sample_rate,
		static_cast<unsigned long long>(stats.input_bytes), static_cast<unsigned long long>(stats.pcm_bytes), static_cast<unsigned long long>(stats.output_bytes),
		static_cast<unsigned long long>(structural_bytes), pcm_ratio, source_ratio, mse, snr_db, stats.peak_error, stats.repeated_windows,
		stats.round_trip_ok ? "true" : "false");
	std::fclose(out);
}

} // namespace audio_compressor::report
