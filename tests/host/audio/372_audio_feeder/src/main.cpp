// ============================================================================
// Test HOST-372: AudioFeeder — feeder IRQ-apto con contadores irq/swaps/underrun.
// ============================================================================
//
// Valida `eng/audio/audio_feeder.hpp`: al avanzar un buffer por IRQ de audio, alimentado a tiempo
// se cumple `irq == swaps` y 0 underruns; si no hay buffer (y no es fin de stream) cuenta underrun;
// al final del stream el `false` es fin normal (no underrun). Es la invariante que fijan las demos
// 272/278 y `audio-stream-irq-rate.md`. El feeder es IRQ-apto (estado trivial, sin heap/locks).
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/audio/372_audio_feeder

#include <cstdio>

#include <eng/audio/audio_feeder.hpp>

using eng::audio::AudioFeeder;

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

/// Stream de doble: repone si `ready`; `end` marca fin de stream.
struct FakeStream {
	bool ready = true;
	bool end = false;
	int swaps = 0;
	bool advance() {
		if (!ready) {
			return false;
		}
		++swaps;
		return true;
	}
	[[nodiscard]] bool at_end() const { return end; }
};

void test_healthy() {
	FakeStream s {};
	AudioFeeder<FakeStream> f {s};
	for (int i = 0; i < 100; ++i) {
		(void)f.on_irq();
	}
	check(f.irq_count() == 100u, "100 IRQ contadas");
	check(f.swap_count() == 100u, "100 swaps (alimentado a tiempo)");
	check(f.underrun_count() == 0u, "sin underruns");
	check(f.healthy(), "irq == swaps y 0 underruns -> healthy");
	check(f.detail() == ((100u << 16u) | 100u), "detail = (irq<<16)|swaps");
}

void test_underrun() {
	FakeStream s {};
	s.ready = false; // la IRQ pide buffer y no hay (ni fin de stream)
	AudioFeeder<FakeStream> f {s};
	for (int i = 0; i < 10; ++i) {
		(void)f.on_irq();
	}
	check(f.irq_count() == 10u && f.swap_count() == 0u, "IRQ sin swaps");
	check(f.underrun_count() == 10u, "cada IRQ sin buffer cuenta underrun");
	check(!f.healthy(), "con underrun no es healthy");
}

void test_end_is_not_underrun() {
	FakeStream s {};
	s.ready = false;
	s.end = true; // fin de stream: el false es normal
	AudioFeeder<FakeStream> f {s};
	for (int i = 0; i < 5; ++i) {
		(void)f.on_irq();
	}
	check(f.underrun_count() == 0u, "el fin de stream no cuenta underrun");
	check(f.irq_count() == 5u && f.swap_count() == 0u, "IRQ sin swaps al final");
}

} // namespace

int main() {
	test_healthy();
	test_underrun();
	test_end_is_not_underrun();
	if (g_fail == 0) {
		std::printf("OK: AudioFeeder (irq/swaps/underrun) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", g_fail);
	return 1;
}
