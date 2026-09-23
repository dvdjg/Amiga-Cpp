// ============================================================================
// Test HOST-222: timers de usuario (eng/os/timer.hpp).
// ============================================================================
//
// Valida `TimerService`: one-shot y periodico en frames, timer en microsegundos, `stop`, id
// autoasignado y capacidad.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/222_os_timer

#include <cstdio>

#include <eng/os/timer.hpp>

using namespace eng;
using namespace eng::os;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

void test_one_shot_frames() {
	TimerService t;
	MsgPort<8> port;
	const u16 id = t.start(0u, 3u, TimerUnit::Frames, false, /*frame_now*/10u, 0u);
	check(id != 0u, "start one-shot");
	check(t.active_count() == 1u, "un timer activo");
	check(t.poll_and_post(port, 12u, 0u) == 0u, "no vence antes del deadline");
	check(t.poll_and_post(port, 13u, 0u) == 1u, "vence en el deadline");
	Msg m {};
	check(port.pop(m) && m.type == MsgType::Timer && m.payload.timer.id == id,
	      "postea Timer con su id");
	check(t.active_count() == 0u, "el one-shot termina");
}

void test_periodic_frames() {
	TimerService t;
	MsgPort<8> port;
	const u16 id = t.start(7u, 2u, TimerUnit::Frames, true, 0u, 0u);
	check(id == 7u, "id explicito");
	check(t.poll_and_post(port, 2u, 0u) == 1u, "periodico vence 1");
	check(t.poll_and_post(port, 3u, 0u) == 0u, "no repite antes de tiempo");
	check(t.poll_and_post(port, 4u, 0u) == 1u, "periodico vence 2");
	check(t.active_count() == 1u, "sigue activo");
	Msg m {};
	(void)port.pop(m);
	(void)port.pop(m);
	check(m.payload.timer.id == id, "el id del periodico es el mismo");
}

void test_microseconds() {
	TimerService t;
	MsgPort<8> port;
	const u32 ticks = us_to_ticks(1000u); // ~709 ticks
	const u16 id = t.start(0u, 1000u, TimerUnit::Microseconds, false, 0u, 0u);
	check(id != 0u, "start en us");
	check(t.poll_and_post(port, 0u, ticks - 1u) == 0u, "us: no vence antes");
	check(t.poll_and_post(port, 0u, ticks) == 1u, "us: vence en el tick");
}

void test_stop_and_capacity() {
	TimerService t;
	MsgPort<8> port;
	const u16 id = t.start(0u, 5u, TimerUnit::Frames, true, 0u, 0u);
	t.stop(id);
	check(t.active_count() == 0u, "stop desactiva");
	check(t.poll_and_post(port, 100u, 0u) == 0u, "no postea tras stop");

	TimerService full;
	bool all_ok = true;
	for (u8 i = 0u; i < kMaxTimers; ++i) {
		if (full.start(0u, 1000u, TimerUnit::Frames, false, 0u, 0u) == 0u) {
			all_ok = false;
		}
	}
	check(all_ok, "se llenan todos los slots");
	check(full.start(0u, 1u, TimerUnit::Frames, false, 0u, 0u) == 0u, "sin slots -> 0");
}

} // namespace

int main() {
	test_one_shot_frames();
	test_periodic_frames();
	test_microseconds();
	test_stop_and_capacity();

	if (failures == 0) {
		std::printf("OK: timers de usuario (frames/us, periodico, stop, capacidad) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
