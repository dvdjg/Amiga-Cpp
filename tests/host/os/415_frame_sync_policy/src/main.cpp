// ============================================================================
// Test HOST-415: politica de sincronizacion de frame y catch-up (eng/os/port.hpp).
// ============================================================================
//
// Valida el contrato de VBlank unico (TIME-001/002/009/010 de
// vblank-timer-inconsistencies.md): el `VBlankLatch` es la unica fuente de secuencia, la
// instantanea `VBlankTick` expone `{sequence, missed}` y `frames_elapsed(from)`, y las tres
// politicas de notificacion (`Event`/`Latch`/`Disabled`) no duplican contador.
//
// El `App` real no es host-testable (necesita backend), asi que aqui se modela su despacho de
// `on_vblank` segun el modo, sobre un `MsgPort` y un `VBlankLatch` reales.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/os/415_frame_sync_policy

#include <cstdio>

#include <eng/os/port.hpp>

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

/// Modela la politica de `App::on_vblank`: una sola secuencia; el modo decide como se entrega.
enum class SyncMode { Event, Latch, Disabled };

struct FakeFrameSource {
	u32 count = 0u;         ///< secuencia unica (m_vblank_count)
	VBlankLatch latch {};   ///< latch del modo Latch
	MsgPort<16> port {};    ///< puerto del modo Event

	void beat(SyncMode mode) noexcept {
		++count;
		switch (mode) {
		case SyncMode::Event: {
			Msg m {};
			m.type = MsgType::VBlank;
			m.time_stamp = count;
			(void)port.post(m);
			break;
		}
		case SyncMode::Latch:
			latch.signal(count);
			break;
		case SyncMode::Disabled:
			break;
		}
	}
};

// Modo Event: un mensaje FIFO por latido (comportamiento historico).
void test_event_mode() {
	FakeFrameSource s;
	for (u32 i = 0u; i < 5u; ++i) {
		s.beat(SyncMode::Event);
	}
	u32 got = 0u;
	Msg m {};
	while (s.port.pop(m)) {
		check(m.type == MsgType::VBlank, "Event: mensaje VBlank");
		++got;
	}
	check(got == 5u, "Event: un mensaje por latido");
	check(s.count == 5u, "Event: contador unico = 5");
}

// Modo Latch: no encola; la instantanea da secuencia y missed.
void test_latch_mode() {
	FakeFrameSource s;
	s.beat(SyncMode::Latch);
	s.beat(SyncMode::Latch);
	s.beat(SyncMode::Latch);
	check(s.port.empty(), "Latch: no se encola nada");

	VBlankTick t {};
	check(take_vblank(s.latch, t), "Latch: hay un tick pendiente");
	check(t.sequence == 3u, "Latch: secuencia = la ultima");
	check(t.missed == 2u, "Latch: 2 pisados");
	check(!take_vblank(s.latch, t), "Latch: como maximo uno pendiente");

	// frames_elapsed entre dos lecturas.
	const u32 prev = t.sequence;
	s.beat(SyncMode::Latch);
	s.beat(SyncMode::Latch);
	check(take_vblank(s.latch, t), "Latch: nuevo tick");
	check(t.frames_elapsed(prev) == 2u, "frames_elapsed = 2");
	check(t.missed == 1u, "un latido pisado entre lecturas");
}

// Modo Disabled: el contador avanza pero no se notifica nada.
void test_disabled_mode() {
	FakeFrameSource s;
	for (u32 i = 0u; i < 4u; ++i) {
		s.beat(SyncMode::Disabled);
	}
	check(s.port.empty(), "Disabled: sin mensajes");
	VBlankTick t {};
	check(!take_vblank(s.latch, t), "Disabled: sin latch");
	check(s.count == 4u, "Disabled: el contador sigue contando (1 fuente de verdad)");
}

// Catch-up: la secuencia salta (latidos perdidos) y frames_elapsed lo refleja sin segundo contador.
void test_catchup_frames_elapsed() {
	VBlankLatch latch;
	VBlankTick t {};
	latch.signal(100u);
	check(take_vblank(latch, t), "tick inicial");
	const u32 from = t.sequence; // 100
	latch.signal(107u);          // el productor salto 7 (el update tardo)
	check(take_vblank(latch, t) && t.sequence == 107u, "secuencia tras el salto");
	check(t.frames_elapsed(from) == 7u, "frames_elapsed refleja el salto (catch-up)");
	check(t.missed == 0u, "sin pisados: no habia tick previo sin consumir");
}

// Wrap: frames_elapsed es aritmetica unsigned, segura ante el wrap de u32.
void test_wrap_elapsed() {
	VBlankLatch latch;
	VBlankTick t {};
	latch.signal(0xfffffffdu);
	(void)take_vblank(latch, t);
	const u32 from = t.sequence;
	latch.signal(0x00000002u); // una vuelta + 5
	(void)take_vblank(latch, t);
	check(t.frames_elapsed(from) == 5u, "frames_elapsed atraviesa el wrap");
}

} // namespace

int main() {
	test_event_mode();
	test_latch_mode();
	test_disabled_mode();
	test_catchup_frames_elapsed();
	test_wrap_elapsed();

	if (failures == 0) {
		std::printf("OK: politica de frame sync (Event/Latch/Disabled) y catch-up validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
