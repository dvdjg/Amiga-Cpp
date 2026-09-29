#pragma once

/// \file stream_intent.hpp
/// Intención portable para reproducir un sample largo mediante el feeder de audio.
///
/// La intención no contiene punteros al medio ni decide si el destino es una voz DMA de Paula o
/// una voz del mixer. El backend resuelve el `source` contra su banco de recursos y prepara
/// `PcmStream`/`AudioFeeder` o el mixer según el plan vigente.

#include <eng/core/types/types.hpp>
#include <eng/core/util/intent_queue.hpp>

namespace eng::audio {

/// Identificador estable de un medio de audio preparado por el sistema de recursos.
struct AudioStreamId {
	/// Índice del recurso; `0xffff` representa una petición sin recurso válido.
	u16 value = 0xffffu;

	/// Indica si el identificador apunta a un recurso válido.
	[[nodiscard]] constexpr bool valid() const noexcept { return value != 0xffffu; }
};

/// Intención de reproducción continua de un sample AUZX/PCM.
struct StreamIntent {
	/// Recurso de audio que debe reproducirse, sin exponer su almacenamiento.
	AudioStreamId source {};
	/// Volumen lógico solicitado, en el rango de Paula 0..64.
	u8 volume = 64u;
	/// Voz preferida; `0xff` deja la elección al ejecutor.
	u8 voice = 0xffu;
	/// Número de buffers PCM que el backend debe reservar, normalmente 2 o 3.
	u8 buffer_count = 2u;
	/// Tamaño descomprimido del chunk, en muestras; debe coincidir con AUZX.
	u16 chunk_samples = 4096u;
	/// Si es true, la reproducción puede repetir desde el primer chunk al llegar al final.
	bool loop = false;
};

/// Valida la configuración que puede comprobarse antes de tocar el backend.
[[nodiscard]] constexpr bool valid(const StreamIntent& intent) noexcept {
	return intent.source.valid() && intent.volume <= 64u && intent.buffer_count >= 2u &&
		intent.buffer_count <= 8u && intent.chunk_samples != 0u;
}

/// Ejecuta intenciones de stream sobre una política de backend inyectada por plantilla.
/// `Backend::start_stream` recibe el valor y devuelve false si no hay buffers/voz disponibles.
template <class Backend>
concept StreamBackend = requires(Backend& backend, const StreamIntent& intent) {
	{ static_cast<bool>(backend.start_stream(intent)) };
};

/// Adaptador para usar `StreamIntent` con el mecanismo común de colas del engine.
template <u16 Capacity, StreamBackend Backend>
class StreamExecutor {
public:
	/// Liga el ejecutor al backend no propietario que materializa la intención.
	constexpr explicit StreamExecutor(Backend& backend) noexcept : m_backend(backend) {}

	/// Indica que la cola puede aceptar trabajo; el backend decide al ejecutar.
	[[nodiscard]] constexpr bool ready() const noexcept { return true; }

	/// Materializa la intención en el backend y devuelve si se pudo iniciar.
	[[nodiscard]] bool run(const StreamIntent& intent) noexcept { return m_backend.start_stream(intent); }

private:
	/// Backend no propietario que posee los buffers y la conexión con Paula/mixer.
	Backend& m_backend;
};

/// Cola no bloqueante de intenciones de streaming para el planner de audio.
template <u16 Capacity, StreamBackend Backend, class Done>
using StreamQueue = eng::IntentQueue<Capacity, StreamIntent, StreamExecutor<Capacity, Backend>, Done>;

} // namespace eng::audio
