// ============================================================================
// Test HOST-371: SoundQueue — vocabulario de sonido sobre el mecanismo de intención.
// ============================================================================
//
// Valida `eng/audio/sound_queue.hpp`: `SoundIntent` sobre la cola genérica `eng::IntentQueue`
// (mecanismo en `eng/core/util/intent_queue.hpp`), el `MixerExecutor` (intención -> `AudioPlan` vía
// `AudioMixer::play`) y la completación (`Done`). Es el audio como «plan análogo» del planner
// (INTENT_PLANNER.md §6): declarar no bloquea; `flush()` vuelca al plan.
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/audio/371_sound_queue

#include <cstdio>

#include <eng/audio/sound_queue.hpp>
#include <eng/core/util/intent_queue.hpp>

using eng::Ticket;
using eng::audio::AudioMixer;
using eng::audio::MixerExecutor;
using eng::audio::SoundIntent;
using eng::audio::SoundQueue;
using eng::audio::sample_event_of;

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

SoundIntent make_intent(eng::u8 voice = 0xff) {
	SoundIntent i {};
	i.sample = eng::AudioSample{g_sample};
	i.length_words = 8;
	i.period = 428;
	i.volume = 64;
	i.voice = voice;
	return i;
}

void test_conversion() {
	const SoundIntent i = make_intent(2);
	const eng::audio::SampleEvent ev = sample_event_of(i);
	check(ev.length_words == 8 && ev.period == 428 && ev.volume == 64 && ev.channel_hint == 2,
	      "sample_event_of mapea todos los campos");
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

	const Ticket t1 = q.enqueue(make_intent());
	const Ticket t2 = q.enqueue(make_intent());
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
	q.enqueue(make_intent(2));
	q.wait_all();
	check(mixer.plan().channels[2].active, "la voz pedida se respeta en el plan");
	check(!mixer.plan().channels[0].active, "las otras voces quedan libres");
}

} // namespace

int main() {
	test_conversion();
	test_lazy_and_flush();
	test_voice_hint();
	if (g_fail == 0) {
		std::printf("OK: SoundQueue (intencion -> AudioPlan) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", g_fail);
	return 1;
}
