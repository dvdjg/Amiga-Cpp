#pragma once

/// Registro compile-time de los codecs que la aplicación host puede codificar para AUZX.

#include <array>
#include <string_view>

#include <eng/audio/pcm_codec.hpp>

namespace audio_compressor::codecs {

struct Descriptor {
	std::string_view name;
	eng::audio::pcm_codec::Codec id;
	bool lossy;
	bool requires_even_chunks;
	bool exact_round_trip;
};

inline constexpr std::array<Descriptor, 4> kAuzxEncoders {{
	{"none", eng::audio::pcm_codec::Codec::None, false, false, true},
	{"rle", eng::audio::pcm_codec::Codec::DeltaRle, false, false, true},
	{"fib", eng::audio::pcm_codec::Codec::FibDelta, true, true, false},
	{"ima", eng::audio::pcm_codec::Codec::ImaAdpcm, true, true, false},
}};

[[nodiscard]] constexpr const Descriptor* find(std::string_view name) noexcept {
	for (const Descriptor& descriptor : kAuzxEncoders) {
		if (descriptor.name == name) return &descriptor;
	}
	return nullptr;
}

/// Comprueba las restricciones de tamaño que el runtime Amiga exige al chunk del codec.
[[nodiscard]] constexpr bool accepts_chunk(const Descriptor& descriptor, eng::usize samples) noexcept {
	return samples != 0u && (!descriptor.requires_even_chunks || (samples & 1u) == 0u);
}

} // namespace audio_compressor::codecs
