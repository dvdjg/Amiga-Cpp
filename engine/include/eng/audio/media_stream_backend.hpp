#pragma once

/// \file media_stream_backend.hpp
/// Adaptador entre un recurso de audio reconocido por `media::Info` y `StreamIntent`.
///
/// El backend concreto resuelve el identificador en su tabla de assets y prepara los buffers
/// `PcmStream`. Esta cabecera solo valida el contrato común; no toca Paula, no asigna memoria y no
/// conoce si la reproducción final usará DMA directo o una voz del mixer.

#include <eng/audio/media.hpp>
#include <eng/audio/stream_intent.hpp>

namespace eng::audio {

/// Fuente que resuelve un recurso y arranca su reproducción según la política del backend.
template <class Source>
concept MediaStreamSource = requires(Source& source, AudioStreamId id, media::Info& info,
	const StreamIntent& intent) {
	{ static_cast<bool>(source.describe(id, info)) };
	{ static_cast<bool>(source.start_stream(intent, info)) };
};

/// Backend fino que inserta la validación de `media::Info` antes de arrancar el stream.
template <MediaStreamSource Source>
class MediaStreamBackend {
public:
	/// Liga la fuente no propietaria que resuelve los recursos del juego.
	constexpr explicit MediaStreamBackend(Source& source) noexcept : m_source(source) {}

	/// Comprueba la intención, consulta el medio y delega la reserva al backend concreto.
	[[nodiscard]] bool start_stream(const StreamIntent& intent) noexcept {
		if (!valid(intent)) return false;
		media::Info info {};
		if (!m_source.describe(intent.source, info)) return false;
		if (info.channels != 1u || info.bits != 8u || info.chunk_samples != intent.chunk_samples) return false;
		return m_source.start_stream(intent, info);
	}

private:
	/// Fuente no propietaria de metadatos y buffers del backend.
	Source& m_source;
};

} // namespace eng::audio
