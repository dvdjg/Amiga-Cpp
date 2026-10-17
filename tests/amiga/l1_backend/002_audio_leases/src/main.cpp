// L1-002: canario de **audio en target** (mixer ASM real; no corre en host).
//
// Comprueba el contrato de canales/leases del `AudioSystem` sobre Paula:
//   1. `init(GameSfxOnly)` arranca el mixer y expone 4 canales software.
//   2. Cuatro `play_on` (MixCh0..MixCh3, prioridad 5, loop) ocupan los 4 canales.
//   3. Reproducir en un canal ocupado con **prioridad menor** se rechaza (-1).
//   4. Tras `stop_sfx`, reocupar el canal con prioridad mayor vuelve a sonar.
//   5. Con SFX sonando, DMACONR tiene AUD0EN (el mixer es MIXER_SINGLE: un canal
//      hardware, `support/audio_mixer/mixer_config.i`); tras `shutdown`, no queda
//      ningún canal de audio DMA activo.
//
// Evidencia: `detail` = 6 bits de checks | DMACONR (mitad alta) leído con SFX activo.
// Ver `docs/engine/architecture/GAME_AUDIO.md` §7-8 y `verify.sh`.

#include <eng/api/api.hpp>
#include <eng/audio/audio_system.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0u,
	0u,
};
}

namespace {

constexpr eng::u32 kBeepBytes = 64u;        // 32 words: tono cuadrado
constexpr eng::s16 kPriorityHigh = 5;       // voz que ocupa el canal
constexpr eng::s16 kPriorityLow = 1;        // intento de robo: debe perder
constexpr eng::s16 kPrioritySteal = 6;      // reocupación tras el stop: gana
constexpr eng::u16 kDmaconAudioMask = 0x000fu; // AUD0EN..AUD3EN

/// Tono cuadrado alrededor del centro (sin desbordar 8 bits): el mixer lo reproduce
/// mientras el canal está ocupado; lo relevante del canario es el contrato, no el sonido.
void fill_beep(eng::u8* dst, eng::u32 bytes) {
	for (eng::u32 i = 0u; i < bytes; ++i) {
		dst[i] = (i & 8u) != 0u ? 0x60u : 0xa0u;
	}
}

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_game_memory()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00020001u);
		return 0;
	}
	auto beep_block = backend.memory_manager().chip().reserve<eng::AudioTag>(kBeepBytes, 4u);
	if (!beep_block.valid()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00020002u);
		return 0;
	}
	fill_beep(beep_block.view.data(), kBeepBytes);

	eng::audio::AudioSystem audio {};
	eng::audio::AudioConfig cfg {};
	cfg.mode = eng::audio::AudioMode::GameSfxOnly;
	cfg.mixer_hw_mask = 0x0fu; // los 4 canales al mixer (modo GameSfxOnly)
	cfg.music_hw_mask = 0x00u; // sin música de tracker
	if (!audio.init(backend.memory_manager(), cfg)) {
		eng::debug::mark_failed(g_eng_run_status, 0x00020003u);
		return 0;
	}

	eng::u32 checks = 0u;
	// (1) El mixer expone 4 canales software.
	if (audio.total_sfx_channels() == 4u) {
		checks |= 1u;
	}

	const eng::audio::SfxSample sample {eng::Span<const eng::u8> {beep_block.view.data(), kBeepBytes}};
	eng::audio::SfxChannel ch[4] {-1, -1, -1, -1};
	// (2) Los 4 canales se ocupan.
	bool all_playing = true;
	for (eng::u8 i = 0u; i < 4u; ++i) {
		const eng::u16 mixer_channel = static_cast<eng::u16>(eng::audio::MixCh0 << i);
		ch[i] = audio.play_sfx_on(mixer_channel, sample, kPriorityHigh, eng::audio::LoopMode::Loop);
		all_playing = all_playing && ch[i] >= 0 && audio.sfx_playing(ch[i]);
	}
	if (all_playing) {
		checks |= 2u;
	}

	// (3) Prioridad menor en canal ocupado: rechazado (sin robar la voz).
	if (audio.play_sfx_on(eng::audio::MixCh0, sample, kPriorityLow, eng::audio::LoopMode::Loop) < 0) {
		checks |= 4u;
	}

	// (4) Tras `stop_sfx`, el canal se puede reocupar: la voz de prioridad 6 gana la
	// plaza (el mixer resuelve el stop/robo por prioridad). El estado del mixer se lee
	// en el siguiente tick de su IRQ, así que se espera un VBlank.
	audio.stop_sfx(ch[1]);
	backend.wait_vblank();
	ch[1] = audio.play_sfx_on(eng::audio::MixCh1, sample, kPrioritySteal, eng::audio::LoopMode::Loop);
	if (ch[1] >= 0 && audio.sfx_playing(ch[1])) {
		checks |= 8u;
	}

	// (5) DMA de audio activo (mixer single -> AUD0EN) y apagado tras `shutdown`.
	volatile eng::u16* const dmaconr = reinterpret_cast<volatile eng::u16*>(0xdff002u);
	const eng::u16 dma_playing = static_cast<eng::u16>(*dmaconr & kDmaconAudioMask);
	if (dma_playing == 0x0001u) {
		checks |= 16u;
	}
	audio.shutdown();
	const eng::u16 dma_after = static_cast<eng::u16>(*dmaconr & kDmaconAudioMask);
	if (dma_after == 0u) {
		checks |= 32u;
	}

	const eng::u32 detail = checks | (static_cast<eng::u32>(dma_playing) << 16u);
	if (checks != 63u) {
		eng::debug::mark_failed(g_eng_run_status, detail);
	} else {
		eng::debug::mark_ready(g_eng_run_status, detail);
	}
	return 0;
}
