#pragma once

/// \file dsp.hpp
/// **Primitivas de audio** (`eng::util`): envolvente ADSR, filtro de un polo, línea de
/// retardo, recorte suave y osciladores de forma de onda. Genéricas sobre el escalar
/// `S` (`float`, `MiniFloat16`, un fixed con división…) como el resto del engine.
///
/// Están pensadas para sintetizar y modular SFX en el juego (o en el pipeline de audio)
/// sin `float` obligatorio ni heap. Las tasas del ADSR son incrementos **por muestra**
/// (no segundos), así que no dividen: el llamador las fija a la frecuencia del mixer.
///
/// Uso:
///   eng::util::Adsr<MiniFloat16> env { /*attack*/ ..., /*decay*/ ..., /*sustain*/ ..., /*release*/ ... };
///   env.note_on();
///   const auto a = env.tick();          // amplitud del sample actual
///   eng::util::OnePole<MiniFloat16> lp { /*alpha*/ ... };
///   y = lp.process(x);

#include <eng/core/linalg.hpp>
#include <eng/core/scalar_math.hpp>
#include <eng/core/util/ring_buffer.hpp>

namespace eng::util {

using eng::math::mul_norm;
using eng::math::scalar_const;
using eng::math::scalar_sin;
using eng::math::scalar_traits;

/// Fases de la envolvente.
enum class AdsrPhase : u8 {
	Idle,
	Attack,
	Decay,
	Sustain,
	Release,
};

/// Envolvente **ADSR** lineal con tasas por muestra (sin división). El llamador fija
/// `attack_step`/`decay_step`/`release_step` (incremento de nivel por muestra) y
/// `sustain` (nivel de sostenido en `[0,1]`).
template <typename S>
struct Adsr {
	S attack_step {};
	S decay_step {};
	S release_step {};
	S sustain {};
	S level {};
	AdsrPhase phase = AdsrPhase::Idle;

	constexpr void note_on() noexcept { phase = AdsrPhase::Attack; }
	constexpr void note_off() noexcept {
		if (phase != AdsrPhase::Idle) {
			phase = AdsrPhase::Release;
		}
	}
	[[nodiscard]] constexpr bool active() const noexcept {
		return phase != AdsrPhase::Idle;
	}

	/// Avanza una muestra y devuelve el nivel actual en `[0,1]`.
	constexpr S tick() noexcept {
		const S one = scalar_traits<S>::one();
		const S zero = scalar_traits<S>::zero();
		switch (phase) {
			case AdsrPhase::Attack:
				level = level + attack_step;
				if (!(level < one)) {
					level = one;
					phase = AdsrPhase::Decay;
				}
				break;
			case AdsrPhase::Decay:
				level = level - decay_step;
				if (level < sustain) {
					level = sustain;
					phase = AdsrPhase::Sustain;
				}
				break;
			case AdsrPhase::Sustain:
				level = sustain;
				break;
			case AdsrPhase::Release:
				level = level - release_step;
				if (!(zero < level)) { // level <= 0
					level = zero;
					phase = AdsrPhase::Idle;
				}
				break;
			case AdsrPhase::Idle:
			default:
				level = zero;
				break;
		}
		return level;
	}
};

/// Filtro de **un polo** (low-pass): `y += alpha·(x − y)`. `alpha` en `(0,1]` (mayor =
/// más rápido); `0` congela. Sin división (`mul_norm`).
template <typename S>
struct OnePole {
	S alpha {};
	S y {};

	[[nodiscard]] constexpr S process(S x) noexcept {
		y = y + mul_norm(alpha, x - y);
		return y;
	}
	constexpr void reset() noexcept { y = scalar_traits<S>::zero(); }
};

/// **Línea de retardo** de `N` muestras (cola circular). `read(delay)` con `delay == 0`
/// devuelve la última muestra escrita; fuera de rango devuelve 0.
template <typename S, usize N>
class DelayLine {
	static_assert(N > 0u, "DelayLine: N debe ser mayor que 0");

public:
	constexpr void clear() noexcept {
		m_buf.clear();
		for (usize i = 0; i < N; ++i) m_buf.push(scalar_traits<S>::zero());
	}
	constexpr void push(S x) noexcept {
		if (m_buf.full()) {
			m_buf.pop_discard();
		}
		m_buf.push(x);
	}
	[[nodiscard]] constexpr S read(usize delay) const noexcept {
		if (delay >= m_buf.size()) {
			return scalar_traits<S>::zero();
		}
		return m_buf[m_buf.size() - 1u - delay];
	}
	/// Devuelve la muestra retrasada y guarda `x` (eco).
	constexpr S process(S x, usize delay) noexcept {
		const S out = read(delay);
		push(x);
		return out;
	}
	[[nodiscard]] constexpr usize size() const noexcept { return m_buf.size(); }

private:
	RingBuffer<S, N> m_buf {};
};

/// Recorte suave (saturación lineal a `±limit`).
template <typename S>
[[nodiscard]] constexpr S soft_clip(S x, S limit) noexcept {
	if (x < -limit) {
		return -limit;
	}
	return limit < x ? limit : x;
}

// --- Osciladores de forma de onda (fase normalizada en [0,1)) ---------------

/// Diente de sierra en `[-1,1)`.
template <typename S>
[[nodiscard]] constexpr S osc_saw(S phase) noexcept {
	return mul_norm(scalar_traits<S>::from_int(2), phase) - scalar_traits<S>::one();
}

/// Cuadrada en `{-1,+1}` (50% de ciclo).
template <typename S>
[[nodiscard]] constexpr S osc_square(S phase) noexcept {
	return phase < scalar_const<S>::from(0.5) ? scalar_traits<S>::one()
						  : -scalar_traits<S>::one();
}

/// Triangular en `[-1,1]` (pico en `phase == 0.5`).
template <typename S>
[[nodiscard]] constexpr S osc_triangle(S phase) noexcept {
	const S half = scalar_const<S>::from(0.5);
	const S d = phase < half ? half - phase : phase - half; // |phase − 0.5|
	return scalar_traits<S>::one() - mul_norm(scalar_traits<S>::from_int(4), d);
}

/// Senoidal en `[-1,1]` (requiere `sin` del escalar).
template <typename S>
[[nodiscard]] constexpr S osc_sine(S phase) noexcept {
	return scalar_sin<S>::op(phase * scalar_const<S>::from(6.283185307179586));
}

} // namespace eng::util
