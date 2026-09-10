// ============================================================================
// Test HOST-008: audio de juego (SampleBank + política de voces).
// ============================================================================
//
// Valida en host la capa de gestión de audio de juego (`eng/audio/sfx_bank.hpp`):
// el `SampleBank` (catálogo de sonidos) y la política pura `allow_trigger`
// (cooldown + límite de instancias). Es lógica pura (sin hardware), host-testable.

#include <cstdio>

#include <eng/audio/sfx_bank.hpp>
#include <eng/core/types.hpp>

namespace {

using eng::audio::SfxDef;
using eng::audio::SampleBank;
using eng::audio::VoiceState;
using eng::audio::allow_trigger;
using eng::audio::allow_group;
using eng::audio::kNever;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

void test_bank() {
	std::printf("audio: SampleBank registra y busca por id\n");
	const eng::u8 sample[16] {};

	SampleBank bank {};
	SfxDef boom { eng::Span<const eng::u8>(sample, 16), 3, 4, 6, true };
	bank.add(0, boom);
	bank.add(7, SfxDef{ eng::Span<const eng::u8>(sample, 16) });

	CHECK(bank.find(0) != nullptr);
	CHECK(bank.find(0)->priority == 3);
	CHECK(bank.find(0)->max_instances == 4);
	CHECK(bank.find(0)->cooldown_frames == 6);
	CHECK(bank.find(0)->duck_music == true);
	CHECK(bank.find(7) != nullptr);
	CHECK(bank.find(1) == nullptr); // no registrado
	CHECK(bank.find(200) == nullptr); // id fuera de rango
}

void test_first_trigger_always_allowed() {
	std::printf("audio: el primer disparo siempre se permite (sin cooldown)\n");
	const eng::u8 sample[16] {};
	SfxDef def { eng::Span<const eng::u8>(sample, 16), 0, 1, 10, false };
	VoiceState st {}; // last_frame = kNever, active = 0
	CHECK(allow_trigger(0, def, st) == true);
	CHECK(allow_trigger(5, def, st) == true);
}

void test_cooldown() {
	std::printf("audio: cooldown bloquea disparos cercanos\n");
	const eng::u8 sample[16] {};
	SfxDef def { eng::Span<const eng::u8>(sample, 16), 0, 0, 6, false }; // max sin límite
	VoiceState st {};
	st.last_frame = 10;

	CHECK(allow_trigger(15, def, st) == false); // 15 < 10+6
	CHECK(allow_trigger(16, def, st) == true);  // 16 >= 10+6
	CHECK(allow_trigger(100, def, st) == true);
}

void test_max_instances() {
	std::printf("audio: límite de instancias simultáneas\n");
	const eng::u8 sample[16] {};
	SfxDef def { eng::Span<const eng::u8>(sample, 16), 0, 2, 0, false }; // max 2
	VoiceState st {};
	st.last_frame = 0;
	st.active = 2;
	CHECK(allow_trigger(50, def, st) == false); // 2 activas = límite

	st.active = 1;
	CHECK(allow_trigger(50, def, st) == true); // 1 activa < 2

	def.max_instances = 0; // sin límite
	st.active = 8;
	CHECK(allow_trigger(50, def, st) == true);
}

void test_group_budget() {
	std::printf("audio: presupuesto de grupo compartido\n");
	CHECK(allow_group(1, 3, 0) == true);  // grupo 1, máx 3, 0 activas
	CHECK(allow_group(1, 3, 2) == true);  // 2 activas < 3
	CHECK(allow_group(1, 3, 3) == false); // 3 activas = límite
	CHECK(allow_group(0, 3, 99) == true); // grupo 0 = sin límite
	CHECK(allow_group(1, 0, 99) == true); // máx 0 = sin límite
}

} // namespace

int main() {
	std::printf("Test HOST-008 game_audio\n");
	std::printf("=========================\n");

	test_bank();
	test_first_trigger_always_allowed();
	test_cooldown();
	test_max_instances();
	test_group_budget();

	if (g_failures == 0) {
		std::printf("OK: capa de audio de juego validada (SampleBank + politica de voces).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
