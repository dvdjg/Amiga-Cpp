// ============================================================================
// Test HOST-270: eventos de audio (MusicEnd / AudioUnderrun) una vez por evento (A2).
// ============================================================================
//
// Valida `eng/audio/audio_events.hpp` (puro): `AudioMsgEdges` convierte el estado continuo de los
// backends en mensajes del mini-SO **solo en el flanco de subida** — un evento sostenido se reporta
// una vez (no por buffer), y al cesar se re-arma. Ver §8 de GAME_AUDIO.md y ROADMAP_AUDIO.md (A2;
// planificado como HOST-241).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/270_audio_events

#include <cstdio>

#include <eng/audio/audio_events.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

void test_music_end_once() {
	eng::audio::AudioMsgEdges e;
	// El modulo termina: se reporta UNA vez aunque el estado siga en "terminado" (no por buffer).
	check(e.on_tick(true, false).music_end, "MusicEnd en el flanco (1a vez)");
	check(!e.on_tick(true, false).music_end, "MusicEnd NO se repite con el estado sostenido");
	check(!e.on_tick(true, false).music_end, "MusicEnd sigue sin repetirse");
	// Vuelve a sonar y termina de nuevo: se re-arma y reporta otra vez.
	check(!e.on_tick(false, false).music_end, "sin evento al re-armar");
	check(e.on_tick(true, false).music_end, "MusicEnd tras re-armar");
}

void test_underrun_once() {
	eng::audio::AudioMsgEdges e;
	check(!e.on_tick(false, false).underrun, "sin underrun al inicio");
	check(e.on_tick(false, true).underrun, "AudioUnderrun en el flanco");
	check(!e.on_tick(false, true).underrun, "AudioUnderrun NO se repite (no por buffer)");
	check(!e.on_tick(false, false).underrun, "se re-arma al cesar");
	check(e.on_tick(false, true).underrun, "AudioUnderrun de nuevo (evento nuevo)");
}

void test_both_and_reset() {
	eng::audio::AudioMsgEdges e;
	const eng::audio::AudioMsgOut both = e.on_tick(true, true);
	check(both.music_end && both.underrun, "ambos eventos en el mismo tick");
	check(!e.on_tick(true, true).music_end && !e.on_tick(true, true).underrun,
	      "ninguno se repite");
	e.reset();
	const eng::audio::AudioMsgOut after = e.on_tick(true, true);
	check(after.music_end && after.underrun, "reset() re-arma ambos");
}

} // namespace

int main() {
	test_music_end_once();
	test_underrun_once();
	test_both_and_reset();
	if (failures == 0) {
		std::printf("OK: eventos de audio (MusicEnd/AudioUnderrun, uno por evento) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
