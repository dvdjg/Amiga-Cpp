#pragma once

/// \file version.hpp
/// **Versionado de demos** (norma del repo, 2026-10): toda demo lleva `major.minor.build`.
/// El **build** lo inyecta el script de compilación (`-DENG_BUILD_NUMBER=n`; contador
/// persistente en `out/.build-number`, ver `tools/build/build-demo.sh`). La demo declara
/// su major/minor y compone la etiqueta con `version_label()` para la **franja de
/// nombre+versión** obligatoria (16 px de alto, 4 bpp, parte inferior de la pantalla).
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/types/types.hpp>

#ifndef ENG_BUILD_NUMBER
#define ENG_BUILD_NUMBER 0
#endif

namespace eng::debug {

/// Versión de una demo: `major.minor.build` (build = cuenta de compilación inyectada).
struct Version {
	eng::u16 major = 1u;
	eng::u16 minor = 0u;
	eng::u32 build = static_cast<eng::u32>(ENG_BUILD_NUMBER);
};

/// Formatea `"v<major>.<minor>.<build>"` en `out` (capacidad `cap`, mínimo 16 bytes).
/// Devuelve la longitud escrita (sin el NUL). Sin stdlib: conversión entera a mano.
[[nodiscard]] inline eng::u8 version_label(Version v, char* out, eng::u8 cap) noexcept {
	eng::u8 n = 0u;
	auto put = [&](char c) {
		if (n + 1u < cap) {
			out[n++] = c;
		}
	};
	auto put_num = [&](eng::u32 value) {
		char tmp[10];
		eng::u8 t = 0u;
		do {
			tmp[t++] = static_cast<char>('0' + (value % 10u));
			value /= 10u;
		} while (value != 0u && t < 10u);
		while (t > 0u) {
			put(tmp[--t]);
		}
	};
	put('v');
	put_num(v.major);
	put('.');
	put_num(v.minor);
	put('.');
	put_num(v.build);
	if (n < cap) {
		out[n] = '\0';
	}
	return n;
}

} // namespace eng::debug
