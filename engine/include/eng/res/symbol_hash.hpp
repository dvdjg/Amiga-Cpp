#pragma once

/// \file symbol_hash.hpp
/// Hash **FNV-1a de 32 bits** para nombres de símbolos de módulos dinámicos (`eng::res`).
///
/// Lo comparten el loader `.englib` (`dynloader.hpp`) y el de ejecutables **HUNK**
/// (`hunk.hpp`) para indexar y buscar símbolos con la misma clave de 32 bits, sin
/// guardar los nombres en RAM. Ver `docs/engine/architecture/RESOURCE_SYSTEM.md` §2.

#include <eng/core/types.hpp>

namespace eng::res {

/// Hash FNV-1a de 32 bits de un nombre C terminado en `\0`. Estable entre compilaciones
/// y máquinas (mismo resultado en host x86 que en m68k).
[[nodiscard]] constexpr eng::u32 symbol_hash(const char* s) noexcept {
	eng::u32 h = 2166136261u;
	while (*s != '\0') {
		h = (h ^ static_cast<eng::u8>(*s)) * 16777619u;
		++s;
	}
	return h;
}

/// Hash FNV-1a de 32 bits de un nombre con longitud explícita (sin exigir `\0`), para
/// los nombres de `HUNK_SYMBOL` que vienen con `name_len_longs` en el propio formato.
[[nodiscard]] constexpr eng::u32 symbol_hash_n(const eng::u8* s, eng::u32 n) noexcept {
	eng::u32 h = 2166136261u;
	for (eng::u32 i = 0u; i < n; ++i) {
		const eng::u8 c = s[i];
		if (c == 0u) {
			break;
		}
		h = (h ^ c) * 16777619u;
	}
	return h;
}

} // namespace eng::res
