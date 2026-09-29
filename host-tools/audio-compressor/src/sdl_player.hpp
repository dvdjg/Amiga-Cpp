#pragma once

/// \file sdl_player.hpp
/// Backend opcional de escucha host mediante SDL3.
///
/// La utilidad se puede compilar sin SDL3 para conversión batch. Cuando `AUDIO_COMPRESSOR_SDL3`
/// está definido, este módulo abre una ventana de reproducción SDL3 y consume PCM8 mono firmado;
/// no se usa en el decoder Amiga ni se arrastra como dependencia del engine.

#if defined(AUDIO_COMPRESSOR_SDL3)
#include <SDL3/SDL.h>
#include <eng/core/types/span.hpp>

namespace audio_compressor {

/// Reproduce una ventana PCM8 a la frecuencia indicada y espera hasta que termine.
[[nodiscard]] inline bool play_pcm(eng::Span<const eng::u8> pcm, eng::u16 sample_rate) {
	if (!SDL_Init(SDL_INIT_AUDIO)) return false;
	SDL_AudioSpec spec {};
	spec.format = SDL_AUDIO_S8;
	spec.channels = 1u;
	spec.freq = sample_rate;
	SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if (!stream) { SDL_Quit(); return false; }
	const bool queued = SDL_PutAudioStreamData(stream, pcm.data(), static_cast<int>(pcm.size()));
	SDL_ResumeAudioStreamDevice(stream);
	while (queued && SDL_GetAudioStreamQueued(stream) > 0) SDL_Delay(10u);
	SDL_DestroyAudioStream(stream);
	SDL_Quit();
	return queued;
}

} // namespace audio_compressor
#else

#include <eng/core/types/types.hpp>
#include <eng/core/types/span.hpp>

namespace audio_compressor {

/// Stub que produce un error claro cuando la utilidad se compiló sin soporte SDL3.
[[nodiscard]] inline bool play_pcm(eng::Span<const eng::u8>, eng::u16) noexcept { return false; }

} // namespace audio_compressor
#endif
