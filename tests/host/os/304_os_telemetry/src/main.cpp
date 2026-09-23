// ============================================================================
// Test HOST-304: M9 — telemetría de saturación del mini-SO (`eng::os`).
// ============================================================================
//
// Valida `eng/os/telemetry.hpp` (`IrqTelemetry`): acumula los descartes por cola llena
// (`overflows`), los VBlank pisados (`missed`) y las marcas de agua; `saturated()` avisa sin fallo
// silencioso. Tambien comprueba `PrioMsgQueue::depth`/`depth_total` (los datos que lee).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/304_os_telemetry

#include <cstdio>

#include <eng/os/telemetry.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", m);
		++g_fail;
	}
}

eng::os::Msg key_msg() {
	eng::os::Msg m {};
	m.type = eng::os::MsgType::KeyDown;
	return m;
}

} // namespace

int main() {
	std::printf("== HOST-304 os telemetry ==\n");

	eng::os::MsgPort<8> port;
	eng::os::VBlankLatch latch;
	eng::os::IrqTelemetry tel;

	// --- descartes por cola llena (anillo High capacidad N-1 = 7) ---
	for (int i = 0; i < 8; ++i) {
		(void)port.post(key_msg());
	}
	check(port.queue.depth(eng::os::MsgPrio::High) == 7u, "anillo High lleno (7)");
	check(port.queue.depth_total() == 7u, "profundidad total 7");

	tel.sample(port, latch);
	check(tel.queue_overflows == 1u, "1 descarte contado");
	check(tel.peak_high == 7u && tel.peak_depth == 7u, "marca de agua (High/total) = 7");
	check(tel.saturated(), "saturado por descartes");

	// --- no vuelve a contar el mismo descarte ---
	tel.sample(port, latch);
	check(tel.queue_overflows == 1u, "delta de overflows (no re-cuenta)");

	// --- VBlank latched: 3 signals sin consumir -> 2 pisados ---
	latch.signal(1u);
	latch.signal(2u);
	latch.signal(3u);
	tel.sample(port, latch);
	check(tel.vblank_missed == 2u, "2 VBlank pisados");

	// --- take_vblank entrega el mensaje con `missed` y limpia el latch ---
	eng::os::Msg vb {};
	check(eng::os::take_vblank(latch, vb), "take_vblank");
	check(vb.payload.vblank.sequence == 3u && vb.payload.vblank.missed == 2u, "VBlank seq/missed");
	tel.sample(port, latch);
	check(tel.vblank_missed == 2u, "latch limpio: no re-cuenta");

	// --- note_vblank (via mensaje) ---
	tel.reset();
	check(!tel.saturated(), "reset limpia");
	tel.note_vblank(3u);
	check(tel.vblank_missed == 3u, "note_vblank acumula");

	// --- sample_port (sin latch): descartes + marcas de agua, sin missed ---
	tel.reset();
	tel.sample_port(port);
	check(tel.queue_overflows == 1u && tel.peak_depth == 7u && tel.vblank_missed == 0u,
	      "sample_port: overflows + marcas de agua, sin missed");

	// --- observe (solo marcas de agua) ---
	tel.reset();
	tel.observe(port);
	check(tel.peak_depth == 7u && !tel.saturated(), "observe no marca saturado");

	if (g_fail == 0) {
		std::printf("OK: telemetria del mini-SO (overflows/missed/marcas de agua) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
