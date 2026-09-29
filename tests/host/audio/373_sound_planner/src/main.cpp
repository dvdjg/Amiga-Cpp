// ============================================================================
// Test HOST-373: SoundPlanner — intención + plan + Msg de completación/underrun.
// ============================================================================
//
// Valida `eng/audio/sound_planner.hpp`: `declare()` encola sin bloquear; `flush()` drena al
// `AudioPlan` y **postea un `Msg IntentDone`** por petición; `notify_underrun()` + `flush()` postea
// `AudioUnderrun` **una vez por evento** (flanco). Une las tres piezas que antes vivían sueltas:
// la cola (`eng/core/util`), el plan (`AudioMixer`) y el aviso (`eng/os`). INTENT_PLANNER.md §6.
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/audio/373_sound_planner

#include <cstdio>

#include <eng/audio/sound_planner.hpp>

using eng::Ticket;
using eng::audio::AudioMixer;
using eng::audio::SampleEvent;
using eng::audio::SoundPlanner;
using eng::os::Msg;
using eng::os::MsgPort;
using eng::os::MsgType;

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

eng::u8 g_sample[32] {};

SampleEvent make_event() {
	SampleEvent i {};
	i.sample = eng::AudioSample{g_sample};
	i.length_words = 4;
	i.period = 428;
	i.volume = 64;
	return i;
}

/// Cuenta mensajes de un tipo en el puerto (vacía).
int drain_count(MsgPort<8u>& port, MsgType type) {
	int n = 0;
	Msg m;
	while (port.pop(m)) {
		if (m.type == type) {
			++n;
		}
	}
	return n;
}

void test_declare_flush_completion() {
	AudioMixer mixer {};
	MsgPort<8u> port {};
	SoundPlanner<8u, 8u> planner {mixer, port};

	planner.begin_frame();
	const Ticket t1 = planner.declare(make_event());
	const Ticket t2 = planner.declare(make_event());
	check(t1 == 1u && t2 == 2u, "tickets secuenciales");
	check(!planner.empty(), "la cola tiene peticiones pendientes");
	check(port.empty(), "declarar no postea nada (aun)");
	check(mixer.active_count() == 0u, "declarar no ejecuta (no bloquea)");

	planner.flush();
	check(mixer.active_count() == 2u, "flush() vuelca 2 voces al plan");
	check(planner.empty(), "la cola queda vacia");
	check(drain_count(port, MsgType::IntentDone) == 2, "un IntentDone por peticion");
	check(port.empty(), "sin mensajes extra");
}

void test_underrun_edge() {
	AudioMixer mixer {};
	MsgPort<8u> port {};
	SoundPlanner<8u, 8u> planner {mixer, port};

	planner.begin_frame();
	planner.notify_underrun();
	planner.flush();
	check(drain_count(port, MsgType::AudioUnderrun) == 1, "underrun reportado una vez");

	// Sin nuevo underrun, el flanco baja: no se vuelve a postear.
	planner.flush();
	check(drain_count(port, MsgType::AudioUnderrun) == 0, "no se repite sin nuevo evento");
}

} // namespace

int main() {
	test_declare_flush_completion();
	test_underrun_edge();
	if (g_fail == 0) {
		std::printf("OK: SoundPlanner (intencion -> plan -> Msg) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", g_fail);
	return 1;
}
