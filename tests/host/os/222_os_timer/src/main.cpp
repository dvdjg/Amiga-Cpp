// ============================================================================
// Test HOST-222: timers de usuario (eng/os/timer.hpp).
// ============================================================================
//
// Valida `TimerService`: one-shot y periodico en frames, timer en microsegundos, `stop` por
// handle e id, id autoasignado, capacidad, fase preservada, deadlines wrap-safe, catch-up
// (Coalesce/SkipToNext/CatchUpAll) y handles generacionales (TIME-005..008).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/os/222_os_timer

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
	const TimerHandle h = t.start(0u, 3u, TimerUnit::Frames, false, /*frame_now*/10u, 0u);
	check(h.valid(), "start one-shot");
	check(t.active_count() == 1u, "un timer activo");
	check(t.poll_and_post(port, 12u, 0u) == 0u, "no vence antes del deadline");
	check(t.poll_and_post(port, 13u, 0u) == 1u, "vence en el deadline");
	Msg m {};
	check(port.pop(m) && m.type == MsgType::Timer && m.payload.timer.id == 1u,
	      "postea Timer con su id autoasignado");
	check(m.payload.timer.handle == h.packed(), "el handle del mensaje coincide");
	check(m.payload.timer.expirations == 1u, "one-shot: 1 vencimiento");
	check(t.active_count() == 0u, "el one-shot termina");
}

void test_periodic_frames() {
	TimerService t;
	MsgPort<8> port;
	const TimerHandle h = t.start(7u, 2u, TimerUnit::Frames, true, 0u, 0u);
	check(h.valid() && h.slot == 0u, "id explicito y slot 0");
	check(t.poll_and_post(port, 2u, 0u) == 1u, "periodico vence 1");
	check(t.poll_and_post(port, 3u, 0u) == 0u, "no repite antes de tiempo");
	check(t.poll_and_post(port, 4u, 0u) == 1u, "periodico vence 2");
	check(t.active_count() == 1u, "sigue activo");
	Msg m {};
	(void)port.pop(m);
	(void)port.pop(m);
	check(m.payload.timer.id == 7u, "el id del periodico es el mismo");
}

void test_microseconds() {
	TimerService t;
	MsgPort<8> port;
	const u32 ticks = us_to_ticks(1000u); // ~709 ticks
	const TimerHandle h = t.start(0u, 1000u, TimerUnit::Microseconds, false, 0u, 0u);
	check(h.valid(), "start en us");
	check(t.poll_and_post(port, 0u, ticks - 1u) == 0u, "us: no vence antes");
	check(t.poll_and_post(port, 0u, ticks) == 1u, "us: vence en el tick");
}

// TIME-007: dos instancias en la misma ranura tienen handles con generacion distinta; `stop`
// cancela solo la instancia pedida y un handle viejo se rechaza.
void test_handle_generation() {
	TimerService t;
	MsgPort<8> port;
	const TimerHandle a = t.start(0u, 5u, TimerUnit::Frames, true, 0u, 0u);
	check(t.stop(a), "stop del handle a");
	const TimerHandle b = t.start(0u, 5u, TimerUnit::Frames, true, 0u, 0u);
	check(b.valid() && b.slot == a.slot, "reutiliza la ranura 0");
	check(b.generation != a.generation, "generacion distinta");
	check(!t.stop(a), "un handle obsoleto no cancela la nueva instancia");
	check(t.stop(b), "el handle nuevo si cancela");
	check(!t.stop(b), "no se puede parar dos veces");
}

// TIME-005: un periodico que llega tarde conserva la fase (deadline += period), no `now + period`.
void test_periodic_phase() {
	TimerService t;
	MsgPort<8> port;
	const TimerHandle h = t.start(0u, 3u, TimerUnit::Frames, true, 0u, 0u);
	check(h.valid(), "periodico de 3");
	// Primer vencimiento exacto en t=3.
	check(t.poll_and_post(port, 3u, 0u) == 1u, "vence en 3");
	Msg m {};
	(void)port.pop(m);
	// Sondeo tardio en t=7: vencio en t=6 (uno pendiente). Coalesce -> 1 mensaje.
	check(t.poll_and_post(port, 7u, 0u) == 1u, "un mensaje pese a llegar tarde");
	check(port.pop(m) && m.payload.timer.expirations == 1u, "1 vencimiento condensado");
	// La fase se preservo: el siguiente vencimiento es t=9 (3+3+3), no t=10 (7+3).
	check(t.poll_and_post(port, 8u, 0u) == 0u, "aun no vence en 8");
	check(t.poll_and_post(port, 9u, 0u) == 1u, "vence en 9 (fase conservada)");
}

// TIME-005: el catch-up condensa varios periodos con `expirations` = periodos perdidos.
void test_catchup_coalesce() {
	TimerService t;
	MsgPort<8> port;
	(void)t.start(0u, 2u, TimerUnit::Frames, true, 0u, 0u);
	// De t=0 a t=7: vencieron en 2,4,6 (3 periodos). Coalesce -> 1 mensaje, 3 expiraciones.
	check(t.poll_and_post(port, 7u, 0u) == 1u, "un unico mensaje condensado");
	Msg m {};
	check(port.pop(m) && m.payload.timer.expirations == 3u, "3 periodos condensados");
	// Tras consumir el atraso, el periodico sigue con fase: siguiente en 8.
	check(t.poll_and_post(port, 8u, 0u) == 1u, "vence en 8");
}

// TIME-005: `SkipToNext` descarta el atraso y reprograma desde ahora.
void test_catchup_skip() {
	TimerService t;
	MsgPort<8> port;
	(void)t.start(0u, 2u, TimerUnit::Frames, true, 0u, 0u, TimerCatchUp::SkipToNext);
	check(t.poll_and_post(port, 7u, 0u) == 1u, "skip: un mensaje");
	Msg m {};
	(void)port.pop(m);
	check(t.poll_and_post(port, 8u, 0u) == 0u, "skip: no vence en 8");
	check(t.poll_and_post(port, 9u, 0u) == 1u, "skip: vence en 7+2=9");
}

// TIME-005: `CatchUpAll` entrega un mensaje por cada periodo vencido.
void test_catchup_all() {
	TimerService t;
	MsgPort<8> port;
	(void)t.start(0u, 2u, TimerUnit::Frames, true, 0u, 0u, TimerCatchUp::CatchUpAll);
	check(t.poll_and_post(port, 7u, 0u) == 3u, "catch-up-all: 3 mensajes (2,4,6)");
}

// TIME-006: comparacion wrap-safe; un deadline cerca del wrap vence correctamente.
void test_wrap_safe() {
	TimerService t;
	MsgPort<8> port;
	const u32 near = 0xfffffff0u; // 16 antes del wrap
	// deadline = 0xfffffff0 + 0x20 = 0x00000010 (tras el wrap de u32).
	const TimerHandle h = t.start(0u, 0x20u, TimerUnit::Frames, false, near, 0u);
	check(h.valid(), "one-shot cerca del wrap");
	check(t.poll_and_post(port, 0x0000000fu, 0u) == 0u, "no vence un frame antes");
	check(t.poll_and_post(port, 0x00000010u, 0u) == 1u, "vence tras el wrap de u32");
}

void test_stop_and_capacity() {
	TimerService t;
	MsgPort<8> port;
	const TimerHandle h = t.start(0u, 5u, TimerUnit::Frames, true, 0u, 0u);
	check(t.stop(h), "stop del handle");
	check(t.active_count() == 0u, "stop desactiva");
	check(t.poll_and_post(port, 100u, 0u) == 0u, "no postea tras stop");

	TimerService full;
	bool all_ok = true;
	for (u8 i = 0u; i < kMaxTimers; ++i) {
		if (!full.start(0u, 1000u, TimerUnit::Frames, false, 0u, 0u).valid()) {
			all_ok = false;
		}
	}
	check(all_ok, "se llenan todos los slots");
	check(!full.start(0u, 1u, TimerUnit::Frames, false, 0u, 0u).valid(), "sin slots -> invalido");
	// Periodic delay 0 se rechaza (evita bucle infinito).
	TimerService t2;
	check(!t2.start(0u, 0u, TimerUnit::Frames, true, 0u, 0u).valid(), "periodico delay 0 rechazado");
}

} // namespace

int main() {
	test_one_shot_frames();
	test_periodic_frames();
	test_microseconds();
	test_handle_generation();
	test_periodic_phase();
	test_catchup_coalesce();
	test_catchup_skip();
	test_catchup_all();
	test_wrap_safe();
	test_stop_and_capacity();

	if (failures == 0) {
		std::printf("OK: timers (frames/us, fase, wrap, catch-up, handles) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
