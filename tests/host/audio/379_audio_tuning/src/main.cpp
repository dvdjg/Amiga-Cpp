// HOST-379: la API de tuning y sus métricas deben aceptar escalares distintos.
// El test instancia el mismo algoritmo con float y Fixed<s32,16>; no prueba el backend Amiga.
#include <cstdio>

#include <eng/audio/audio_tuning.hpp>
#include <eng/core/math/fixed.hpp>
#include <eng/core/math/fixed_math.hpp>

/// Construye dos ventanas idénticas y comprueba que el error sea exactamente cero para `S`.
template <class S>
bool check_metrics() {
	// Ambas ventanas comparten el mismo patrón para aislar la comprobación del escalar.
	S original[3] {eng::math::scalar_traits<S>::from_int(1), eng::math::scalar_traits<S>::from_int(2), eng::math::scalar_traits<S>::from_int(3)};
	S rebuilt[3] {original[0], original[1], original[2]};
	const auto metrics = eng::audio::error_metrics(eng::Span<const S>{original}, eng::Span<const S>{rebuilt});
	return metrics.mean_square == eng::math::scalar_traits<S>::zero() &&
		metrics.signal_energy > eng::math::scalar_traits<S>::zero() &&
		metrics.peak_error == eng::math::scalar_traits<S>::zero();
}

/// Punto de entrada del test: valida parámetros por defecto y las dos instanciaciones obligatorias.
int main() {
	using Fixed = eng::math::Fixed<eng::s32, 16>;
	eng::audio::AudioTuning<float> float_tuning {};
	eng::audio::AudioTuning<Fixed> fixed_tuning {};
	if (!check_metrics<float>() || !check_metrics<Fixed>()) return 1;
	if (float_tuning.quality_weight <= 0.0f || fixed_tuning.quality_weight <= Fixed{}) return 1;
	std::printf("OK: parámetros y métricas de audio genéricos en float y Fixed.\n");
	return 0;
}
