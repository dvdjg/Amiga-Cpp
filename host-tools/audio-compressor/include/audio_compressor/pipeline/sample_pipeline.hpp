#pragma once

/// Pipeline host de SAMPLE: ventanas de entrada, remuestreo, codec, round-trip y sink AUZX.

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

#include <eng/audio/pcm_codec.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

#include <audio_compressor/dsp/resampler.hpp>
#include <audio_compressor/formats/auzx_sink.hpp>

namespace audio_compressor::pipeline {

struct SamplePipeline {
	template <class Source, class Encode, class Decode, class Stats>
	[[nodiscard]] static bool run(Source& source, eng::u16 source_rate, eng::u16 target_rate,
		eng::u16 chunk_samples, eng::usize window_samples, eng::u8 codec,
		const std::filesystem::path& output, Encode&& encode, Decode&& decode, Stats& stats) {
		if (source.frames() == 0u || source.frames() > 0xffffffffu || source_rate == 0u || target_rate == 0u || chunk_samples == 0u) return false;
		const eng::usize output_samples = static_cast<eng::usize>((static_cast<eng::u64>(source.frames()) * target_rate + source_rate - 1u) / source_rate);
		const eng::usize chunk = chunk_samples;
		const eng::usize chunk_count = (output_samples + chunk - 1u) / chunk;
		if (output_samples == 0u || output_samples > 0xffffffffu || chunk_count > 65535u ||
			((codec == static_cast<eng::u8>(eng::audio::pcm_codec::Codec::FibDelta) || codec == static_cast<eng::u8>(eng::audio::pcm_codec::Codec::ImaAdpcm)) && (chunk & 1u) != 0u)) return false;
		const eng::usize input_window_size = std::max<eng::usize>(1u, window_samples);
		std::vector<eng::u8> input_window(input_window_size);
		std::vector<eng::u8> resampled(input_window_size * target_rate / source_rate + 3u);
		std::vector<eng::u8> pending(chunk), encoded, rebuilt(chunk);
		eng::usize pending_size = 0u;
		eng::u8 fib_seed = 0u;
		dsp::LinearResampler resampler {source_rate, target_rate};
		formats::AuzxSink sink;
		if (!sink.open(output, target_rate, static_cast<eng::u32>(output_samples), chunk_samples,
			static_cast<eng::u16>(chunk_count), codec)) return false;
		auto flush_pending = [&](eng::usize count) {
			if (!encode({pending.data(), count}, encoded, fib_seed)) return false;
			rebuilt.assign(count, 0u);
			if (decode({encoded.data(), encoded.size()}, {rebuilt.data(), rebuilt.size()}, codec) != static_cast<eng::s32>(count)) return false;
			for (eng::usize i = 0u; i < count; ++i) {
				const eng::s32 error = static_cast<eng::s8>(pending[i]) - static_cast<eng::s8>(rebuilt[i]);
				const eng::u32 absolute = static_cast<eng::u32>(error < 0 ? -error : error);
				stats.squared_error += static_cast<eng::u64>(error * error);
				const eng::s32 sample = static_cast<eng::s8>(pending[i]);
				stats.signal_energy += static_cast<eng::u64>(sample * sample);
				if (absolute > stats.peak_error) stats.peak_error = static_cast<eng::u8>(absolute);
			}
			return sink.append({encoded.data(), encoded.size()});
		};
		for (eng::u64 start = 0u; start < source.frames(); start += input_window_size) {
			const eng::usize count = static_cast<eng::usize>(std::min<eng::u64>(input_window_size, source.frames() - start));
			if (source.read(start, {input_window.data(), count}) != count) return false;
			const eng::usize produced = resampler.process({input_window.data(), count}, {resampled.data(), resampled.size()}, start + count == source.frames());
			for (eng::usize i = 0u; i < produced; ++i) {
				pending[pending_size++] = resampled[i];
				if (pending_size == chunk) {
					if (!flush_pending(pending_size)) return false;
					pending_size = 0u;
				}
			}
		}
		if (pending_size != 0u && !flush_pending(pending_size)) return false;
		if (!sink.finalize()) return false;
		std::error_code output_error{};
		stats.output_bytes = std::filesystem::file_size(output, output_error);
		if (output_error) return false;
		stats.samples = output_samples;
		stats.sample_rate = target_rate;
		stats.round_trip_ok = true;
		return true;
	}
};

} // namespace audio_compressor::pipeline
