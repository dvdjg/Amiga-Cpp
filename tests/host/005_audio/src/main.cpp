// ============================================================================
// Test HOST-005: audio (eng::audio::AudioMixer).
// ============================================================================
//
// Valida en host la lógica de asignación de canales del mezclador de Paula
// (paso 7 de ENGINE_DESIGN.md §5): `SampleEvent`/`MusicEvent` + `AudioMixer`.
// Es lógica pura (sin hardware), por eso se prueba con g++.
//
// Comprobaciones:
//   1) El plan por defecto no tiene canales activos.
//   2) `play()` asigna canales por orden (first-fit).
//   3) Con los 4 canales ocupados, un quinto sfx se rechaza.
//   4) `channel_hint` se respeta cuando es válido.

#include <cstdio>
#include <type_traits>

#include <eng/audio/audio.hpp>
#include <eng/core/types.hpp>

namespace {

using eng::audio::AudioMixer;
using eng::audio::SampleEvent;

static_assert(std::is_trivially_copyable_v<SampleEvent>);
static_assert(std::is_trivially_copyable_v<eng::audio::AudioPlan>);

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

void test_default_empty() {
	std::printf("audio: plan por defecto sin canales activos\n");
	AudioMixer mixer {};
	CHECK(mixer.active_count() == 0u);
}

void test_first_fit() {
	std::printf("audio: play() asigna por orden (first-fit)\n");
	AudioMixer mixer {};
	const eng::u8 sample[8] {};

	SampleEvent ev {};
	ev.sample = sample;
	ev.length_words = 4;
	for (int i = 0; i < 4; ++i) {
		CHECK(mixer.play(ev) == true);
	}
	CHECK(mixer.active_count() == 4u);
	// Los 4 canales están ocupados: un quinto sfx se rechaza.
	CHECK(mixer.play(ev) == false);
}

void test_channel_hint() {
	std::printf("audio: channel_hint se respeta\n");
	AudioMixer mixer {};
	const eng::u8 sample[8] {};

	SampleEvent ev {};
	ev.sample = sample;
	ev.length_words = 4;
	ev.channel_hint = 2;
	CHECK(mixer.play(ev) == true);
	CHECK(mixer.plan().channels[2].active == true);
	CHECK(mixer.plan().channels[0].active == false);
}

void test_begin_frame_resets() {
	std::printf("audio: begin_frame() limpia el plan\n");
	AudioMixer mixer {};
	const eng::u8 sample[8] {};

	SampleEvent ev {};
	ev.sample = sample;
	ev.length_words = 4;
	mixer.play(ev);
	CHECK(mixer.active_count() == 1u);

	mixer.begin_frame();
	CHECK(mixer.active_count() == 0u);
}

} // namespace

int main() {
	std::printf("Test HOST-005 audio\n");
	std::printf("===================\n");

	test_default_empty();
	test_first_fit();
	test_channel_hint();
	test_begin_frame_resets();

	if (g_failures == 0) {
		std::printf("OK: mezclador de audio validado (SampleEvent/AudioMixer/AudioPlan).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
