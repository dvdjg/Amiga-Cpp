// ============================================================================
// Test HOST-495: contrato del planner - una cola generica, dos planes.
// ============================================================================
//
// Verifica que dibujo y audio usan el MISMO mecanismo `eng::IntentQueue`
// (`Ticket`, no bloqueo hasta `wait`, `Done` por peticion) y que sus planes
// (`FramePlan`/`AudioPlan`) son sumideros separados por subsistema.
//
//   bash tools/run-host-tests.sh tests/host/graphics/495_intent_plan_contract

#include <cstdio>
#include <type_traits>

#include <eng/audio/sound_queue.hpp>
#include <eng/graphics/intent_queue.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("[FAIL] %s\n", msg);
		++g_fail;
	}
}

template <class Item>
struct FakeExec {
	int ran = 0;
	bool ready() const noexcept { return true; }
	void run(const Item&) noexcept { ++ran; }
};

struct FakeDone {
	int* count = nullptr;
	eng::Ticket last = 0u;
	void operator()(eng::Ticket t) noexcept {
		++*count;
		last = t;
	}
};

} // namespace

int main() {
	using eng::Ticket;
	using eng::graphics::DrawIntent;
	using eng::graphics::DrawKind;
	using eng::graphics::DrawQueue;
	using eng::audio::SampleEvent;
	using eng::audio::SoundQueue;

	using DrawExec = FakeExec<DrawIntent>;
	using AudioExec = FakeExec<SampleEvent>;

	// Mismo mecanismo generico para ambos subsistemas.
	static_assert(std::is_same_v<DrawQueue<4u, DrawExec, FakeDone>,
		eng::IntentQueue<4u, DrawIntent, DrawExec, FakeDone>>);
	static_assert(std::is_same_v<SoundQueue<4u, AudioExec, FakeDone>,
		eng::IntentQueue<4u, SampleEvent, AudioExec, FakeDone>>);

	DrawExec draw_exec {};
	AudioExec audio_exec {};
	int draw_done = 0;
	int audio_done = 0;
	DrawQueue<4u, DrawExec, FakeDone> draw_queue {};
	SoundQueue<4u, AudioExec, FakeDone> audio_queue {};
	draw_queue.bind(draw_exec);
	audio_queue.bind(audio_exec);
	draw_queue.bind_done(FakeDone {&draw_done});
	audio_queue.bind_done(FakeDone {&audio_done});

	const Ticket draw_ticket =
		draw_queue.enqueue(DrawIntent {DrawKind::Rect, 0, 0, 4u, 4u, 0, 0, 1u, 0u});
	SampleEvent sound {};
	sound.sample = eng::AudioSample {};
	sound.length_words = 4u;
	const Ticket audio_ticket = audio_queue.enqueue(sound);

	check(draw_exec.ran == 0 && audio_exec.ran == 0,
	      "declarar no ejecuta en ningun subsistema");
	check(draw_done == 0 && audio_done == 0, "declarar no completa todavia");

	draw_queue.flush();
	audio_queue.flush();
	check(draw_exec.ran == 1 && audio_exec.ran == 1, "flush ejecuta ambos planes");
	check(draw_done == 1 && audio_done == 1, "Done se emite una vez por peticion");
	check(draw_ticket == audio_ticket, "cada cola lleva su propia secuencia de tickets");

	// El AudioPlan es un valor copiable; el FramePlan es el sumidero de blits.
	static_assert(std::is_trivially_copyable_v<eng::audio::AudioPlan>);

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: planner (IntentQueue + FramePlan/AudioPlan) contrato validado.\n");
	return 0;
}
