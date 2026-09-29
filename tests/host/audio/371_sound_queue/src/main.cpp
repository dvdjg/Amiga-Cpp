// ============================================================================
// Test HOST-371: SoundQueue — cola de SampleEvent sobre el mecanismo de intención.
// ============================================================================
//
// Valida `eng/audio/sound_queue.hpp`: `SampleEvent` sobre la cola genérica `eng::IntentQueue`
// (mecanismo en `eng/core/util/intent_queue.hpp`), el `MixerExecutor` (SampleEvent -> `AudioPlan`
// vía `AudioMixer::play`) y la completación (`Done`). Es el audio como «plan análogo» del planner
// (INTENT_PLANNER.md §6): declarar no bloquea; `flush()` vuelca al plan.
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/audio/371_sound_queue

#include <cstdio>

#include <eng/audio/sound_queue.hpp>
#include <eng/core/util/intent_queue.hpp>

using eng::Ticket;
using eng::audio::AudioMixer;
using eng::audio::MixerExecutor;
using eng::audio::SampleEvent;
using eng::audio::SoundQueue;

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

/// Política `Done` de test: recuerda el último ticket avisado.
struct LastDone {
	Ticket* last = nullptr;
	void operator()(Ticket t) const { *last = t; }
};

eng::u8 g_sample[32] {};

SampleEvent make_event(eng::u8 voice = 0xff) {
	SampleEvent ev {};
	ev.sample = eng::AudioSample{g_sample};
	ev.length_words = 8;
	ev.period = 428;
	ev.volume = 64;
	ev.channel_hint = voice;
	return ev;
}

void test_lazy_and_flush() {
	AudioMixer mixer {};
	MixerExecutor exec {mixer};
	LastDone done {};
	Ticket last = 0;
	done.last = &last;
	SoundQueue<8u, MixerExecutor, LastDone> q {};
	q.bind(exec);
	q.bind_done(done);

	const Ticket t1 = q.enqueue(make_event());
	const Ticket t2 = q.enqueue(make_event());
	check(t1 == 1u && t2 == 2u, "tickets secuenciales");
	check(mixer.active_count() == 0u, "declarar no ejecuta (no bloquea)");

	q.flush();
	check(mixer.active_count() == 2u, "flush() vuelca las 2 voces al plan");
	check(last == 2u, "el aviso Done ve el ultimo ticket");
	check(q.empty(), "la cola queda vacia tras flush");
}

void test_voice_hint() {
	AudioMixer mixer {};
	MixerExecutor exec {mixer};
	SoundQueue<4u, MixerExecutor, eng::NoDone> q {};
	q.bind(exec);
	q.enqueue(make_event(2));
	q.wait_all();
	check(mixer.plan().channels[2].active, "la voz pedida se respeta en el plan");
	check(!mixer.plan().channels[0].active, "las otras voces quedan libres");
}

} // namespace

int main() {
	test_lazy_and_flush();
	test_voice_hint();
	if (g_fail == 0) {
		std::printf("OK: SoundQueue (SampleEvent -> AudioPlan) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", g_fail);
	return 1;
}
