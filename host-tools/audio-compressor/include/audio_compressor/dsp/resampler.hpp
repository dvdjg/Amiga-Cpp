#pragma once

/// Remuestreador lineal host con fase persistente entre ventanas.

#include <algorithm>

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace audio_compressor::dsp {

class LinearResampler {
public:
	LinearResampler(eng::u32 source_rate, eng::u32 target_rate) noexcept
		: m_step(source_rate == 0u || target_rate == 0u ? 1.0 :
			static_cast<double>(source_rate) / static_cast<double>(target_rate)) {}

	/// Convierte una ventana y conserva la última muestra y la fase para la siguiente llamada.
	[[nodiscard]] eng::usize process(eng::Span<const eng::u8> input, eng::Span<eng::u8> output, bool final) noexcept {
		if (input.empty() || output.empty()) return 0u;
		const bool had_previous = m_have_previous;
		const eng::usize virtual_size = input.size() + (m_have_previous ? 1u : 0u);
		eng::usize written = 0u;
		while (written < output.size() && (m_phase + 1.0 < static_cast<double>(virtual_size) ||
			(final && m_phase < static_cast<double>(virtual_size)))) {
			const eng::usize index = static_cast<eng::usize>(m_phase);
			const double fraction = m_phase - static_cast<double>(index);
			const eng::s32 a = sample_at(input, index);
			const eng::s32 b = sample_at(input, std::min(index + 1u, virtual_size - 1u));
			const eng::s32 value = static_cast<eng::s32>(a + (b - a) * fraction);
			output[written++] = static_cast<eng::u8>(std::clamp(value, -128, 127));
			m_phase += m_step;
		}
		// The first window has no synthetic previous sample; subsequent windows do.
		// Keep the phase relative to the carried last sample so upsampling does not
		// restart or index before the beginning of the next window.
		m_phase -= static_cast<double>(had_previous ? input.size() : input.size() - 1u);
		m_previous = input[input.size() - 1u];
		m_have_previous = true;
		return written;
	}

private:
	[[nodiscard]] eng::s32 sample_at(eng::Span<const eng::u8> input, eng::usize index) const noexcept {
		if (m_have_previous && index == 0u) return static_cast<eng::s8>(m_previous);
		const eng::usize input_index = index - (m_have_previous ? 1u : 0u);
		return static_cast<eng::s8>(input[input_index]);
	}

	double m_step = 1.0;
	double m_phase = 0.0;
	eng::u8 m_previous = 0u;
	bool m_have_previous = false;
};

} // namespace audio_compressor::dsp
