#pragma once

/// Selección compile-time de la candidata de menor tamaño para una señal normalizada.

#include <limits>

#include <audio_compressor/codecs/registry.hpp>

namespace audio_compressor::pipeline {

struct CandidateSearch {
	/// Elige el descriptor cuyo estimador devuelve el menor tamaño válido.
	template <class Estimate>
	[[nodiscard]] static const codecs::Descriptor* smallest(Estimate&& estimate) noexcept {
		const codecs::Descriptor* best = nullptr;
		eng::u64 best_size = std::numeric_limits<eng::u64>::max();
		for (const codecs::Descriptor& descriptor : codecs::kAuzxEncoders) {
			eng::u64 size = 0u;
			if (estimate(descriptor, size) && size < best_size) {
				best = &descriptor;
				best_size = size;
			}
		}
		return best;
	}
};

} // namespace audio_compressor::pipeline
