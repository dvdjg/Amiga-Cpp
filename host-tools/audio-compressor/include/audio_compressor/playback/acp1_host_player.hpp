#pragma once

/// Player host de ACP1 v1/v2 sobre ventanas, sin tomar ownership del blob ni de los buffers.

#include <eng/audio/media.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace audio_compressor::playback {

class Acp1HostPlayer {
public:
	/// Reconoce un blob ACP1 y conserva solo una vista validada del archivo.
	[[nodiscard]] bool open(eng::Span<const eng::u8> blob) noexcept {
		eng::audio::media::Info parsed {};
		if (!eng::audio::media::open(blob, parsed) || parsed.container != eng::audio::media::Container::Acp1) return false;
		m_blob = blob;
		m_info = parsed;
		return true;
	}

	/// Devuelve la frecuencia común de la composición validada.
	[[nodiscard]] eng::u16 sample_rate() const noexcept { return m_info.sample_rate; }
	/// Devuelve la duración común de la composición validada.
	[[nodiscard]] eng::u32 total_samples() const noexcept { return m_info.total_samples; }
	/// Mezcla una ventana ACP1 usando scratch y acumulador prestados por el llamador.
	[[nodiscard]] eng::s32 read_window(eng::u32 first_sample, eng::Span<eng::u8> output,
		eng::Span<eng::u8> scratch, eng::Span<eng::s16> accumulator) const noexcept {
		return eng::audio::media::mix_window(m_blob, m_info, first_sample, output, scratch, accumulator);
	}

private:
	eng::Span<const eng::u8> m_blob {}; ///< Vista no propietaria del ACP1 validado.
	eng::audio::media::Info m_info {}; ///< Metadatos y tablas validadas del contenedor.
};

} // namespace audio_compressor::playback
