#pragma once

/// \file audio_feeder.hpp
/// **Feeder de audio** para la IRQ de Paula (nivel 4): se llama una vez por petición de DMA de
/// AUD0..3, avanza el stream (repone el siguiente buffer) y lleva la cuenta `irq`/`swaps`/`underrun`
/// de forma **trivial** (sin heap, sin locks, contadores de 16 bits que no se desgarran en la ISR).
///
/// El **drenaje** (bucle del juego) consume los flancos y decide postear `Msg` (`AudioUnderrun`,
/// …): la ISR **no** postea (cuerpo no trivial). Ver `docs/debugging/investigaciones/
/// audio-stream-irq-rate.md` (la lección: el problema no era la IRQ sino el feeder **CPU-bound**;
/// con el buffer ya codificado, `irq == swaps` y 0 underruns) y `docs/reference/emulators/winuae/
/// audio-irq.md`.

#include <eng/core/types/types.hpp>

namespace eng::audio {

/// Contrato del stream que el feeder avanza: `advance()` repone el siguiente buffer (`false` si no
/// había uno listo) y `at_end()` distingue el fin de stream (normal) de un underrun real.
template <class S>
concept FeedableStream = requires(S& s) {
	{ s.advance() };
	{ s.at_end() };
};

/// **Feeder IRQ-apto**: avanza el stream en cada IRQ de audio y cuenta las peticiones.
///
/// Invariante de salud: alimentado a tiempo, `irq == swaps` y `underrun == 0`. Si `advance()`
/// devuelve `false` **sin** estar al final, es un *underrun* (la IRQ pidió buffer y no había).
template <FeedableStream Stream>
class AudioFeeder {
public:
	/// Liga el stream (no propietario; debe vivir más que el feeder).
	constexpr explicit AudioFeeder(Stream& stream) noexcept : m_stream(stream) {}

	/// **ISR de audio (nivel 4)**: avanza un buffer. Devuelve `true` si repuso (y el llamador puede
	/// reprogramar el puntero de la voz de Paula); `false` si fue underrun o fin de stream. No
	/// asigna, no bloquea, no postea.
	[[nodiscard]] bool on_irq() noexcept {
		++m_irq;
		if (m_stream.advance()) {
			++m_swaps;
			return true;
		}
		if (!m_stream.at_end()) {
			++m_underrun;
		}
		return false;
	}

	/// IRQ de audio atendidas.
	[[nodiscard]] u16 irq_count() const noexcept { return m_irq; }
	/// Buffers repuestos a tiempo.
	[[nodiscard]] u16 swap_count() const noexcept { return m_swaps; }
	/// Veces que la IRQ pidió buffer sin haberlo (excluye el fin de stream).
	[[nodiscard]] u16 underrun_count() const noexcept { return m_underrun; }
	/// ¿Salud? Alimentado a tiempo: `irq == swaps` y sin underruns.
	[[nodiscard]] bool healthy() const noexcept { return m_irq == m_swaps && m_underrun == 0u; }
	/// Telemetría compacta `(irq << 16) | swaps` (el `detail` que fijan las demos).
	[[nodiscard]] u32 detail() const noexcept {
		const u32 hi = m_irq;  // ensanchado implícito u16 -> u32
		const u32 lo = m_swaps;
		return (hi << 16u) | lo;
	}

private:
	Stream& m_stream;
	u16 m_irq = 0u;
	u16 m_swaps = 0u;
	u16 m_underrun = 0u;
};

} // namespace eng::audio
