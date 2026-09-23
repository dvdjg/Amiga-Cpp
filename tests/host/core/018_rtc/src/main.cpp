// ============================================================================
// Test HOST-018: reloj de tiempo real desde el contador TOD de la CIA-A.
// ============================================================================
//
// Valida la conversion pura `eng::time::from_tod` (contador de 24 bits a hz 50/60 ->
// hora del dia). Referencia: amiga-bootcamp/01_hardware/common/cia_chips.md
// ("Time-of-Day"): TOD de 24 bits, 50 Hz PAL / 60 Hz NTSC.
//
//   bash tools/run-host-tests.sh tests/host/018_rtc   (solo este)

#include <cstdio>

#include <eng/core/rtc.hpp>
#include <eng/core/types.hpp>

namespace {

using eng::time::TimeOfDay;
using eng::time::from_tod;

bool eq(const TimeOfDay& t, eng::u8 h, eng::u8 m, eng::u8 s, eng::u8 tk) {
	return t.hours == h && t.minutes == m && t.seconds == s && t.ticks == tk;
}

} // namespace

int main() {
	// Origen.
	if (!eq(from_tod(0u, 50u), 0, 0, 0, 0)) {
		std::printf("[FAIL] from_tod(0)\n");
		return 1;
	}
	// Subsegundo.
	if (!eq(from_tod(25u, 50u), 0, 0, 0, 25)) {
		std::printf("[FAIL] from_tod(25) ticks\n");
		return 1;
	}
	// 1 s y 1:01:01 (50 Hz).
	if (!eq(from_tod(50u, 50u), 0, 0, 1, 0)) {
		std::printf("[FAIL] from_tod(50)\n");
		return 1;
	}
	if (!eq(from_tod(50u * 3661u, 50u), 1, 1, 1, 0)) {
		std::printf("[FAIL] from_tod(1:01:01)\n");
		return 1;
	}
	// 23:59:59.
	if (!eq(from_tod(50u * 86399u, 50u), 23, 59, 59, 0)) {
		std::printf("[FAIL] from_tod(23:59:59)\n");
		return 1;
	}
	// Envuelve a las 24 h.
	if (!eq(from_tod(50u * 90000u, 50u), 1, 0, 0, 0)) {
		std::printf("[FAIL] from_tod wrap 24h\n");
		return 1;
	}
	// NTSC 60 Hz: 60 ticks = 1 s.
	if (!eq(from_tod(60u, 60u), 0, 0, 1, 0)) {
		std::printf("[FAIL] from_tod NTSC 60 Hz\n");
		return 1;
	}
	if (from_tod(60u, 60u).hz != 60u) {
		std::printf("[FAIL] hz no propagado\n");
		return 1;
	}
	// total_seconds coherente.
	if (from_tod(50u * 3661u, 50u).total_seconds != 3661u) {
		std::printf("[FAIL] total_seconds\n");
		return 1;
	}

	std::printf("OK: RTC (TOD de la CIA-A) validado (50/60 Hz, wrap 24h, ticks).\n");
	return 0;
}
