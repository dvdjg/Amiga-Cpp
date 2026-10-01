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
};

inline constexpr std::array<Descriptor, 4> kAuzxEncoders {{
	{"none", eng::audio::pcm_codec::Codec::None, false},
	{"rle", eng::audio::pcm_codec::Codec::DeltaRle, false},
	{"fib", eng::audio::pcm_codec::Codec::FibDelta, true},
	{"ima", eng::audio::pcm_codec::Codec::ImaAdpcm, true},
}};

[[nodiscard]] constexpr const Descriptor* find(std::string_view name) noexcept {
	for (const Descriptor& descriptor : kAuzxEncoders) {
		if (descriptor.name == name) return &descriptor;
	}
	return nullptr;
}

} // namespace audio_compressor::codecs
