#include <cstdio>

#include <eng/audio/audio_tuning.hpp>
#include <eng/core/math/fixed.hpp>
#include <eng/core/math/fixed_math.hpp>

template <class S>
bool check_metrics() {
	S original[3] {eng::math::scalar_traits<S>::from_int(1), eng::math::scalar_traits<S>::from_int(2), eng::math::scalar_traits<S>::from_int(3)};
	S rebuilt[3] {original[0], original[1], original[2]};
	const auto metrics = eng::audio::error_metrics(eng::Span<const S>{original}, eng::Span<const S>{rebuilt});
	return metrics.mean_square == eng::math::scalar_traits<S>::zero() &&
		metrics.signal_energy > eng::math::scalar_traits<S>::zero() &&
		metrics.peak_error == eng::math::scalar_traits<S>::zero();
}

int main() {
	using Fixed = eng::math::Fixed<eng::s32, 16>;
	eng::audio::AudioTuning<float> float_tuning {};
	eng::audio::AudioTuning<Fixed> fixed_tuning {};
	if (!check_metrics<float>() || !check_metrics<Fixed>()) return 1;
	if (float_tuning.quality_weight <= 0.0f || fixed_tuning.quality_weight <= Fixed{}) return 1;
	std::printf("OK: parámetros y métricas de audio genéricos en float y Fixed.\n");
	return 0;
}
