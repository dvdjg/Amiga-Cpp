#pragma once

/// \file playback.hpp
/// API común de reproducción para samples, streams y música estructurada.
///
/// El juego recibe un `PlaybackHandle` al declarar una reproducción y usa ese valor para pausar,
/// detener o cambiar volumen. El backend mantiene la tabla de sesiones y resuelve Paula, mixer,
/// AUZX o ACP1 sin exponer canales ni punteros.

#include <eng/core/types/types.hpp>

namespace eng::audio {

/// Identificador generacional de una reproducción viva.
struct PlaybackHandle {
	/// Ranura de la tabla de reproducciones; `0xff` significa inválido.
	u8 slot = 0xffu;
	/// Generación de la ranura para rechazar handles obsoletos.
	u8 generation = 0u;

	/// Comprueba si la ranura puede identificar una reproducción.
	[[nodiscard]] constexpr bool valid() const noexcept { return slot != 0xffu; }
};

/// Clase de medio que resuelve el backend.
enum class PlaybackKind : u8 { Sample, Stream, Music };

/// Petición común de reproducción.
struct PlayIntent {
	/// Identificador del recurso preparado por el pipeline.
	u16 resource = 0xffffu;
	/// Tipo de recurso: sample, AUZX/stream o composición ACP1.
	PlaybackKind kind = PlaybackKind::Sample;
	/// Volumen inicial compatible con Paula, 0..64.
	u8 volume = 64u;
	/// Repetición al final cuando el medio lo permite.
	bool loop = false;
};

/// Operación de control sobre una reproducción.
enum class PlaybackCommand : u8 { Pause, Resume, Stop, SetVolume };

/// Intención de control no bloqueante.
struct PlaybackControl {
	/// Reproducción a controlar.
	PlaybackHandle handle {};
	/// Operación solicitada.
	PlaybackCommand command = PlaybackCommand::Stop;
	/// Volumen usado por `SetVolume`; se ignora en las demás operaciones.
	u8 volume = 0u;
};

/// Resultado de una operación de control.
enum class PlaybackStatus : u8 { Accepted, InvalidHandle, Rejected };

} // namespace eng::audio
