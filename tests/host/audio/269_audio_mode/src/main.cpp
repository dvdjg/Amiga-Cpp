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

} // namespace

int main() {
	test_quota();
	test_period();
	if (failures == 0) {
		std::printf("OK: modos de audio (reparto de canales + period_for_hz) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
