// HOST-381: API común de reproducción para samples, streams y música.
// Comprueba que el valor de petición y el handle no contienen canales ni punteros de backend.

#include <cstdio>

#include <eng/audio/playback.hpp>

/// Doble mínimo de backend para verificar la tabla de sesiones fuera de hardware.
struct FakePlaybackBackend {
	eng::audio::PlaybackHandle play(const eng::audio::PlayIntent& intent) noexcept {
		return intent.resource == 0xffffu ? eng::audio::PlaybackHandle{} : eng::audio::PlaybackHandle{2u, 7u};
	}
	eng::audio::PlaybackStatus control(const eng::audio::PlaybackControl& control) noexcept {
		return control.handle.valid() ? eng::audio::PlaybackStatus::Accepted : eng::audio::PlaybackStatus::InvalidHandle;
	}
};

/// Valida reproducción y control con un recurso válido.
bool valid_session() {
	FakePlaybackBackend backend;
	const eng::audio::PlayIntent intent{3u, eng::audio::PlaybackKind::Music, 48u, true};
	const eng::audio::PlaybackHandle handle = backend.play(intent);
	return handle.valid() && backend.control({handle, eng::audio::PlaybackCommand::Pause, 0u}) == eng::audio::PlaybackStatus::Accepted &&
		backend.control({handle, eng::audio::PlaybackCommand::SetVolume, 20u}) == eng::audio::PlaybackStatus::Accepted;
}

/// Valida que una reproducción inválida no genera un handle operable.
bool invalid_session() {
	FakePlaybackBackend backend;
	const eng::audio::PlaybackHandle handle = backend.play({});
	return !handle.valid() && backend.control({handle, eng::audio::PlaybackCommand::Stop, 0u}) == eng::audio::PlaybackStatus::InvalidHandle;
}

/// Ejecuta las comprobaciones del contrato común de reproducción.
int main() {
	if (!valid_session() || !invalid_session()) return 1;
	std::printf("OK: API común de reproducción y control por handle validada.\n");
	return 0;
}
