// ============================================================================
// Test HOST-370: presupuesto de AudioPlan (voces + palabras DMA).
// ============================================================================
//
// Valida el presupuesto de audio (`AudioBudget`/`AudioBudgetLimits`/`AudioBudgetReport` en
// `eng/audio/audio.hpp`), el análogo de `BlitBudget` para el `FramePlan`: el `AudioMixer` lo
// actualiza al repartir canales y el plan lo compara contra sus límites (Ok/Warning/Exceeded).
// Lógica pura, sin hardware. Ver `INTENT_PLANNER.md` (§6, el audio como plan análogo).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/audio/370_audio_plan

#include <cstdio>
#include <type_traits>

#include <eng/audio/audio.hpp>
#include <eng/core/types/types.hpp>

namespace {

using eng::audio::AudioBudgetLimits;
using eng::audio::AudioBudgetStatus;
using eng::audio::AudioMixer;
using eng::audio::AudioPlan;
using eng::audio::SampleEvent;

static_assert(std::is_trivially_copyable_v<AudioPlan>);

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

eng::u8 g_sample[64] {};

SampleEvent make_event(eng::u16 words) {
	SampleEvent ev {};
	ev.sample = eng::AudioSample{g_sample};
	ev.length_words = words;
	ev.period = 428;
	ev.volume = 64;
	return ev;
}

void test_default() {
	std::printf("audio_plan: plan por defecto con presupuesto cero y Ok\n");
	AudioMixer m {};
	CHECK(m.active_count() == 0u);
	CHECK(m.budget().voices == 0u && m.budget().words == 0u);
	CHECK(m.budget_report().status == AudioBudgetStatus::Ok);
}

void test_accounting() {
	std::printf("audio_plan: play() acumula voces y palabras\n");
	AudioMixer m {};
	for (int i = 0; i < 4; ++i) {
		CHECK(m.play(make_event(4)) == true);
	}
	CHECK(m.active_count() == 4u);
	CHECK(m.budget().voices == 4u);
	CHECK(m.budget().words == 16u);
	CHECK(m.budget_report().status == AudioBudgetStatus::Ok); // límite por defecto = 4
	CHECK(m.play(make_event(4)) == false);                    // 5º sfx rechazado
	CHECK(m.budget().voices == 4u);                           // no crece al rechazar
}

void test_limits_exceeded() {
	std::printf("audio_plan: superar max_voices -> Exceeded\n");
	AudioMixer m {};
	AudioBudgetLimits l {};
	l.warning_voices = 1u;
	l.max_voices = 2u;
	m.plan().set_limits(l);
	for (int i = 0; i < 4; ++i) {
		CHECK(m.play(make_event(10)) == true);
	}
	CHECK(m.budget().voices == 4u);
	CHECK(m.budget().words == 40u);
	CHECK(m.budget_report().voices_exceeded == true);
	CHECK(m.budget_report().voices_warning == true);
	CHECK(m.budget_report().status == AudioBudgetStatus::Exceeded);
}

void test_warning() {
	std::printf("audio_plan: entre warning y max -> Warning\n");
	AudioMixer m {};
	AudioBudgetLimits l {};
	l.warning_voices = 2u;
	l.max_voices = 4u;
	m.plan().set_limits(l);
	for (int i = 0; i < 3; ++i) {
		CHECK(m.play(make_event(1)) == true);
	}
	CHECK(m.budget_report().voices_warning == true);
	CHECK(m.budget_report().voices_exceeded == false);
	CHECK(m.budget_report().status == AudioBudgetStatus::Warning);
}

void test_words_and_reset() {
	std::printf("audio_plan: límite de palabras y reset con begin_frame()\n");
	AudioMixer m {};
	AudioBudgetLimits l {};
	l.warning_words = 8u;
	l.max_words = 100u;
	m.plan().set_limits(l);
	CHECK(m.play(make_event(10)) == true);
	CHECK(m.budget_report().words_warning == true);
	CHECK(m.budget_report().words_exceeded == false);
	CHECK(m.budget_report().status == AudioBudgetStatus::Warning);

	m.begin_frame();
	CHECK(m.active_count() == 0u);
	CHECK(m.budget().voices == 0u && m.budget().words == 0u);
	CHECK(m.budget_report().status == AudioBudgetStatus::Ok);
	CHECK(m.plan().limits.warning_words == 8u); // los límites sobreviven al reset
}

} // namespace

int main() {
	test_default();
	test_accounting();
	test_limits_exceeded();
	test_warning();
	test_words_and_reset();
	if (g_failures == 0) {
		std::printf("OK: presupuesto de AudioPlan (voces + palabras DMA) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", g_failures);
	return 1;
}
