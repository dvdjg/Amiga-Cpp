// ============================================================================
// Test HOST-238: tiempo y profiling (eng/os/time.hpp).
// ============================================================================
//
// Valida las conversiones ticks<->us (PAL/NTSC) y `ScopedTimer` con una `TickSource` falsa.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/os/238_os_time

#include <cstdio>

#include <eng/os/time.hpp>

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

eng::u32 g_now = 0u;
eng::u32 fake_now(void* user) {
	(void)user;
	return g_now;
}

void test_conversions() {
	check(ticks_to_us(709u) == 1000u, "709 ticks = 1000 us (PAL)");
	check(ticks_to_us(7090u) == 10000u, "7090 ticks = 10 ms");
	check(us_to_ticks(1000u) == 709u, "1000 us = 709 ticks (PAL)");
	check(ticks_to_us(715u, kCiaKHzNtsc) == 1000u, "715 ticks = 1000 us (NTSC)");
	check(us_to_ticks(1000u, kCiaKHzNtsc) == 715u, "1000 us = 715 ticks (NTSC)");
}

void test_scoped() {
	TickSource src {fake_now, nullptr};
	check(src.ticks() == 0u, "tick source lee");
	g_now = 1000u;
	ScopedTimer t {src};
	g_now += 709u;
	check(t.elapsed_ticks() == 709u, "elapsed_ticks");
	check(t.elapsed_us() == 1000u, "elapsed_us");
	g_now += 709u;
	check(t.elapsed_ticks() == 1418u, "elapsed acumula");
}

} // namespace

int main() {
	test_conversions();
	test_scoped();

	if (failures == 0) {
		std::printf("OK: tiempo (ticks/us PAL-NTSC, ScopedTimer) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
