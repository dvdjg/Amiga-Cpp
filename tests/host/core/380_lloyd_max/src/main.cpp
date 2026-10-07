// HOST-380: Lloyd-Max genérico para tablas de cuantización sin heap.
// Prueba float y Fixed<s32,16> para impedir que la plantilla imponga una representación concreta.

#include <cstdio>

#include <eng/core/math/fixed.hpp>
#include <eng/core/math/fixed_math.hpp>
#include <eng/core/util/quantizer.hpp>

/// Entrena cuatro niveles sobre una distribución pequeña y comprueba el contrato de resultado.
template <class S>
bool train_scalar() {
	// La entrada contiene ocho residuales y la tabla dispone de cuatro niveles de reconstrucción.
	const S samples[8] {
		eng::math::scalar_traits<S>::from_int(-3), eng::math::scalar_traits<S>::from_int(-2),
		eng::math::scalar_traits<S>::from_int(-1), eng::math::scalar_traits<S>::from_int(0),
		eng::math::scalar_traits<S>::from_int(0), eng::math::scalar_traits<S>::from_int(1),
		eng::math::scalar_traits<S>::from_int(2), eng::math::scalar_traits<S>::from_int(3)};
	// El scratch de centroides pertenece al llamador; Lloyd-Max no reserva memoria.
	S centroids[4] {};
	const auto result = eng::util::lloyd_max(eng::Span<const S>{samples}, eng::Span<S>{centroids}, 8u);
	return result.valid && result.iterations == 8u && centroids[0] <= centroids[3];
}

/// Ejecuta las dos variantes de escalar exigidas por la librería genérica.
int main() {
	using Fixed = eng::math::Fixed<eng::s32, 16>;
	if (!train_scalar<float>() || !train_scalar<Fixed>()) return 1;

	// El tope de niveles es parámetro de plantilla y dimensiona el scratch interno.
	float c4[4] {};
	const float smp[4] = {0.0f, 1.0f, 2.0f, 3.0f};
	const auto r4 = eng::util::lloyd_max<float, 4>(eng::Span<const float>{smp},
						       eng::Span<float>{c4}, 4u);
	if (!r4.valid) return 1;

	std::printf("OK: Lloyd-Max genérico validado con float y Fixed.\n");
	return 0;
}
