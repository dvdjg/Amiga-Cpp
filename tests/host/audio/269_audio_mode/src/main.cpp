// ============================================================================
// Test HOST-269: modos de audio y reparto de canales (A0).
// ============================================================================
//
// Valida `eng/audio/audio_mode.hpp` (puro, sin hardware): el reparto de canales por modo
// (`channel_quota`) nunca solapa mixer y música, usa máscaras de 4 bits válidas y habilita los
// backends correctos; y `paula::period_for_hz` acota 124..65535 y da el periodo PAL esperado.
// Ver §7 de GAME_AUDIO.md y ROADMAP_AUDIO.md (A0; el test se planificó como HOST-240).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/audio/269_audio_mode

#include <cstdio>

#include <eng/audio/audio_mode.hpp>

namespace {

using eng::audio::AudioMode;
using eng::audio::channel_quota;
using eng::audio::ChannelQuota;

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

void test_quota() {
	const ChannelQuota silent = channel_quota(AudioMode::Silent);
	check(silent.mixer_hw_mask == 0u && silent.music_hw_mask == 0u && !silent.sfx_enabled &&
		      !silent.music_enabled,
	      "Silent: sin canales y sin backends");

	const ChannelQuota game = channel_quota(AudioMode::Game);
	check(game.mixer_hw_mask == 0x1u && game.music_hw_mask == 0xEu,
	      "Game: AUD0 mixer + AUD1..AUD3 música");
	check(game.sfx_enabled && game.music_enabled, "Game: SFX y música activos");

	const ChannelQuota sfx = channel_quota(AudioMode::GameSfxOnly);
	check(sfx.mixer_hw_mask == 0xFu && sfx.music_hw_mask == 0x0u,
	      "GameSfxOnly: 4 canales para el mixer");
	check(sfx.sfx_enabled && !sfx.music_enabled, "GameSfxOnly: sin música");

	const ChannelQuota med = channel_quota(AudioMode::TitleOctaMED);
	check(med.music_hw_mask == 0xFu && med.mixer_hw_mask == 0x0u,
	      "TitleOctaMED: 4 canales para OctaMED");
	check(!med.sfx_enabled && med.music_enabled, "TitleOctaMED: solo música");

	// Garantia de A0: ningun modo solapa mixer y musica, y las mascaras son de 4 bits.
	const AudioMode modes[] = {AudioMode::Silent, AudioMode::Game, AudioMode::GameSfxOnly,
				   AudioMode::TitleOctaMED};
	for (AudioMode m : modes) {
		check(!channel_quota(m).overlaps(), "sin solape mixer/musica");
		check(eng::audio::paula::valid_channel_mask(channel_quota(m).mixer_hw_mask),
		      "mascara de mixer valida (4 bits)");
		check(eng::audio::paula::valid_channel_mask(channel_quota(m).music_hw_mask),
		      "mascara de musica valida (4 bits)");
	}
}

void test_period() {
	using eng::audio::paula::period_for_hz;
	check(period_for_hz(0u) == 65535u, "hz=0 -> 65535 (silencio)");
	check(period_for_hz(3546895u) == 124u, "hz=clock -> acota a 124");
	check(period_for_hz(1u) == 65535u, "hz=1 -> acota a 65535");
	check(period_for_hz(11025u) == 321u, "hz=11025 -> 321 (~11 kHz PAL)");
	check(period_for_hz(8287u) == 428u, "hz=8287 -> 428");
	check(period_for_hz(16000u) == 221u, "hz=16000 -> 221");
}

struct FakeMixer {
	eng::Ref<eng::MemoryManager> memory {};
	eng::MemoryBlock chip {};
	eng::MemoryBlock slow {};
	bool fail_second = false;
	bool active = false;
	eng::u32 starts = 0u;
	eng::u32 stops = 0u;

	bool init(eng::MemoryManager& memory) {
		this->memory = memory;
		chip = memory.chip().pool().allocate(128u, 4u);
		if (!chip.valid()) return false;
		slow = memory.slow().pool().allocate(64u, 4u);
		if (fail_second) return false; // simula fallo de setup tras dos reservas válidas
		if (!slow.valid()) return false;
		active = true;
		++starts;
		return true;
	}
	void shutdown() {
		if (memory.valid()) {
			if (chip.valid()) memory->chip().pool().free(chip.data);
			if (slow.valid()) memory->slow().pool().free(slow.data);
		}
		chip = {};
		slow = {};
		memory.reset();
		active = false;
		++stops;
	}
	void set_master_volume(eng::u8) {}
};

void test_audio_config_and_mixer_rollback() {
	eng::audio::AudioConfig bad {};
	bad.mixer_hw_mask = 1u;
	bad.music_hw_mask = 1u;
	check(!eng::audio::valid_audio_config(bad), "reject overlapping audio masks");
	eng::audio::AudioConfig good {};
	check(eng::audio::valid_audio_config(good), "accept default Game audio profile");

	eng::u8 chip[1024] {};
	eng::u8 slow[128] {};
	eng::MemoryManager memory {};
	memory.configure(chip, sizeof(chip), slow, sizeof(slow), nullptr, 0u, 4u);
	FakeMixer mixer;
	const eng::u32 chip_free = memory.chip().free_bytes();
	const eng::u32 slow_free = memory.slow().free_bytes();
	mixer.fail_second = true;
	check(!eng::audio::init_mixer_transaction(mixer, memory), "failed partial init runs shutdown rollback");
	check(!mixer.active && mixer.stops >= 1u, "failed init leaves mixer stopped");
	check(memory.chip().free_bytes() == chip_free && memory.slow().free_bytes() == slow_free,
	      "partial init rollback restores both bank free lists");
	mixer.fail_second = false;
	check(eng::audio::init_mixer_transaction(mixer, memory), "retry init can succeed");
	check(mixer.active && mixer.starts == 1u, "successful retry has one active instance");
	const eng::u32 chip_used = memory.chip().used_bytes();
	const eng::u32 slow_used = memory.slow().used_bytes();
	check(chip_used == 128u && slow_used == 64u, "successful init owns precisely its two reservations");
	mixer.shutdown();
	check(!mixer.active, "shutdown returns fake mixer to idle");
	check(memory.chip().used_bytes() == 0u && memory.slow().used_bytes() == 0u,
	      "partial init rollback y shutdown devuelven todos los bloques");
	check(mixer.stops >= 2u, "shutdown repeated by transaction and caller is safe");
}

} // namespace

int main() {
	test_quota();
	test_period();
	test_audio_config_and_mixer_rollback();
	if (failures == 0) {
		std::printf("OK: modos de audio (reparto de canales + period_for_hz) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
