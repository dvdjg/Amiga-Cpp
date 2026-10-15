#pragma once

/// \file profile.hpp
/// **Perfil 1D precalculado en compilación** (`Profile1D`): una tabla de `N+1` alturas que
/// describe siluetas típicas (colina, valle) sin pagar multiplicación ni división en el
/// bucle de relleno — el runtime solo indexa (`Hill2D`).
///
/// La usa el pintor de terreno para la silueta de montaña (`Feature::Mountain`), y sirve
/// para cualquier efecto que necesite un perfil separable (proa de barco, cuesta de
/// carretera, arco de puente): se evalúa como producto separable barato `h(x)+h(y)` o
/// como altura 1D por columna.
///
/// ```text
///   Profile1D<8>::hill(amp=6, base=2)        Profile1D<8>::valley(amp=6, base=2)
///   8 ┤      ▄▄                               8 ┤▄▄          ▄▄
///   2 ┤▄▄  ▄▀  ▀▄  ▄▄                         2 ┤  ▀▄▄  ▄▄▀
///     └──────────────► d                        └──────────────► d
///     0   2   4  6  8                          0   2   4  6  8
/// ```
///
/// La escala de los valores es libre, pero `Hill2D` y el pintor la asumen en **0..255**
/// (la cuantización usa `>> 8` como en `quantize_plain`): construir los perfiles con
/// `amp + base <= 255`.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-442`); sin demo que la ejercite en hardware.

#include <eng/core/types/types.hpp>

namespace eng::gfx::proc {

/// Perfil 1D de `N+1` muestras (`d = 0..N`, extremos incluidos), en escala 0..255.
/// Se usa con `y[x % (N+1)]` para extenderlo periódicamente por una silueta.
/// \tparam N  número de tramos (la tabla tiene `N+1` muestras).
template <eng::u16 N>
struct Profile1D {
	/// Altura de cada muestra `d = 0..N` (escala 0..255 por convenio del pintor).
	eng::u16 y[N + 1] {};

	/// Perfil por defecto (todo ceros).
	constexpr Profile1D() = default;

	/// **Colina simétrica**: `base` en los extremos y `base + amp` en el centro; el valor
	/// se interpola linealmente con `d` (`base + amp·min(d, N−d)/⌊N/2⌋`). La división es
	/// de compilación (solo se paga una vez, no por evaluación); el producto se hace en
	/// 32 bits (`eng::u32(amp)`) para no desbordar en ABIs de `int` de 16 bits.
	/// \param amp   altura de la colina sobre `base` (0..255-base).
	/// \param base  altura de los extremos (0..255).
	/// \return perfil de `N+1` muestras listo para `Hill2D`.
	[[nodiscard]] static constexpr Profile1D hill(eng::u16 amp, eng::u16 base) {
		Profile1D p {};
		const eng::u16 half = N / 2u;
		for (eng::u16 d = 0; d <= N; ++d) {
			const eng::u16 dd = (d > half) ? eng::u16(N - d) : d;
			p.y[d] = half == 0u
					 ? eng::u16(base + amp)
					 : eng::u16(base + (eng::u32(amp) * dd) / half);
		}
		return p;
	}

	/// **Valle**: la colina invertida (`2·base + amp − hill`), con el mínimo en el centro.
	/// \param amp   profundidad del valle bajo los extremos (0..255-base).
	/// \param base  altura de los extremos (0..255).
	/// \return perfil de `N+1` muestras.
	[[nodiscard]] static constexpr Profile1D valley(eng::u16 amp, eng::u16 base) {
		Profile1D p = hill(amp, base);
		for (eng::u16 d = 0; d <= N; ++d) {
			p.y[d] = eng::u16(2u * base + amp - p.y[d]);
		}
		return p;
	}
};

} // namespace eng::gfx::proc
