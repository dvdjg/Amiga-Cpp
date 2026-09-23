#pragma once

/// \file paula.hpp
/// Backend de audio de Paula: materializa un `eng::audio::AudioPlan` (4 canales)
/// en los registros de Paula y arranca el DMA de los canales activos.
///
/// Paula tiene 4 canales de audio (AHRM cap. 5). Cada canal reproduce una forma
/// de onda desde Chip RAM vía DMA, con longitud, período (frecuencia) y volumen
/// programables de forma independiente. El orden canónico de arranque es:
///   1) AUDxLCH/AUDxLCL (puntero a la muestra, en Chip RAM)
///   2) AUDxLEN (longitud en words)
///   3) AUDxVOL (volumen 0..64)
///   4) AUDxPER (período de muestreo; menor = tono más agudo)
///   5) DMACON = SET/CLR | DMAEN | AUDxEN (arranca el DMA)
///
/// El `AudioPlan` lo produce el `eng::audio::AudioMixer` (puro, host-testable);
/// este backend es la única pieza que conoce los registros de Paula. Por eso vive
/// en `eng/platform/`, no en `eng/audio/`.

#include <eng/audio/audio.hpp>
#include <eng/core/types/types.hpp>

namespace eng::amiga {

/// Backend de Paula: escribe un `AudioPlan` en los registros custom.
///
/// Es close-to-the-metal (escribe `$dff000`+offset). No posee memoria: el puntero
/// de muestra debe apuntar a Chip RAM (lo garantiza la arena del llamador).
class PaulaAudio {
public:
	/// Silencia los 4 canales y apaga su DMA.
	void silence() {
		for (u8 c = 0; c < kChannels; ++c) {
			set_volume(c, 0u);
		}
		// Limpia AUD0..3EN (sin SET/CLR: escribe 0 en esos bits).
		*dmacon_w() = kAudioMask;
		// Limpia la modulacion de audio de ADKCON (ATPER/ATVOL/ATPER2/ATVOL2, bits 1..4). Si
		// quedan set, el periodo/volumen del canal salen de los datos del canal anterior y el
		// ritmo de la IRQ deja de corresponder a `AUDxPER * AUDxLEN` (ADKCON bit 15 = SET/CLR;
		// escribir sin el limpia los bits).
		*adkcon_w() = kAudioModMask;
	}

	/// Materializa el plan en Paula y arranca el DMA de los canales activos.
	void apply(const eng::audio::AudioPlan& plan) {
		u16 enable = 0u;
		for (u8 c = 0; c < kChannels; ++c) {
			const eng::audio::AudioPlan::Channel& ch = plan.channels[c];
			if (!ch.active) {
				set_volume(c, 0u);
				continue;
			}
			set_pointer(c, ch.sample.data());
			set_length(c, ch.length_words);
			set_volume(c, ch.volume);
			set_period(c, ch.period);
			enable = static_cast<u16>(enable | (1u << c));
		}

		// Apaga los canales inactivos y arranca los activos (SET/CLR | DMAEN | AUDxEN).
		*dmacon_w() = static_cast<u16>(kAudioMask & ~enable);
		if (enable != 0u) {
			*dmacon_w() = static_cast<u16>(kSetClr | kDmaMaster | enable);
		}
	}

	/// Puntero + longitud (**en palabras**) de la muestra de un canal: `AUDxLCH/AUDxLCL` +
	/// `AUDxLEN`. Es la reprogramación que hace el streaming dentro de la IRQ de audio (cambio de
	/// buffer sin parar el DMA).
	void set_buffer(u8 channel, const u8* sample, u16 words) {
		set_pointer(channel, sample);
		set_length(channel, words);
	}

	/// Periodo del canal (`AUDxPER`): fija la frecuencia de reproducción (mayor = más grave).
	void set_period(u8 channel, u16 period) {
		channel_regs(channel)[3] = period; // AUDxPER
	}

	/// Volumen del canal 0..64 (`AUDxVOL`, 6 bits; se enmascara).
	void set_volume(u8 channel, u8 volume) {
		channel_regs(channel)[4] = static_cast<u16>(volume & 0x7fu); // AUDxVOL (6 bits)
	}

	/// Arranca el DMA del canal (`DMACON` = SET/CLR | DMAEN | AUDxEN).
	void start_channel(u8 channel) {
		*dmacon_w() = static_cast<u16>(kSetClr | kDmaMaster | (1u << channel));
	}

	/// Para el DMA del canal (`DMACON` = SET/CLR | AUDxEN). El puntero y la longitud no cambian.
	void stop_channel(u8 channel) {
		*dmacon_w() = static_cast<u16>(kSetClr | (1u << channel));
	}

private:
	static constexpr u8 kChannels = 4;
	static constexpr u16 kSetClr = 0x8000;
	static constexpr u16 kDmaMaster = 0x0200;
	static constexpr u16 kAudioMask = 0x000f;    // bits AUD0..3EN
	static constexpr u16 kAudioModMask = 0x001e; // ADKCON: ATPER/ATVOL/ATPER2/ATVOL2

	/// `ADKCON` ($dff09e, SET/CLR): bits de modulacion de audio y de disco. `silence` lo escribe
	/// sin bit 15 para limpiar `kAudioModMask`.
	volatile u16* adkcon_w() const {
		return reinterpret_cast<volatile u16*>(0xdff09eu);
	}

	/// `DMACON` ($dff096) como palabra: se escribe con `kSetClr` para activar/desactivar el
	/// DMA de audio (`kAudioMask` = AUD0..3EN). Lo usan `start`/`stop`.
	volatile u16* dmacon_w() const {
		return reinterpret_cast<volatile u16*>(0xdff096u);
	}

	/// Puntero (32 bits) a la muestra en Chip RAM → AUDxLCH/AUDxLCL.
	void set_pointer(u8 channel, const u8* sample) {
		const uintptr raw = reinterpret_cast<uintptr>(sample);
		volatile u16* base = channel_regs(channel);
		base[0] = static_cast<u16>(raw >> 16);  // AUDxLCH (3 bits altos)
		base[1] = static_cast<u16>(raw & 0xffffu); // AUDxLCL (15 bits bajos)
	}

	/// Longitud de la muestra en **palabras** (no bytes) → `AUDxLEN`.
	void set_length(u8 channel, u16 words) {
		channel_regs(channel)[2] = words; // AUDxLEN
	}

	/// Base de los registros del canal `channel` (0..3): `$dff0a0 + channel*8`, con el orden
	/// `[AUDxLCH, AUDxLCL, AUDxLEN, AUDxPER, AUDxVOL]`. La usan los `set_*`.
	volatile u16* channel_regs(u8 channel) {
		return reinterpret_cast<volatile u16*>(0xdff0a0u) + static_cast<u32>(channel) * 8u;
	}
};

} // namespace eng::amiga
