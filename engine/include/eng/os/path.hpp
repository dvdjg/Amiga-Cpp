#pragma once

/// \file path.hpp
/// **Normalización de paths** del VFS (`ROADMAP_RESOURCES.md` R6.1): colapsa separadores repetidos,
/// resuelve `.` y `..` y **rechaza** escapar por encima de la raíz. Es una función **pura** (sin
/// E/S, host-testable) que consumirá la fachada `Vfs` (dispositivos/mounts/requests). Trabaja con
/// `/` (compatible con AmigaOS y POSIX).
///
/// Reglas:
///   - `""` → `Empty`;
///   - los componentes vacíos (separadores repetidos) y `.` se omiten;
///   - `..` sube un nivel; subir por encima de la raíz → `EscapesRoot`;
///   - se conserva el carácter **absoluto** (un `/` inicial); un path relativo que quede vacío
///     (p. ej. `.`) normaliza a longitud **0** (la raíz del consumidor);
///   - el **dispositivo** (`DF0:`, `PROG:`) no se interpreta aquí: es parte del primer componente
///     y no se normaliza.
///
/// No es la API final de la app (esa es una `Vfs` con handles y requests): es el ladrillo de
/// normalización, con nombre y semántica de error explícitos.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/expected.hpp>
#include <eng/core/util/string_view.hpp>

namespace eng::os {

/// Causa de fallo de `normalize_path`.
enum class PathError : eng::u8 { Empty, TooLong, EscapesRoot, TooManyComponents };

/// Máximo de componentes de un path normalizado (`a/b/c/...`).
inline constexpr eng::u16 kMaxPathComponents = 64u;

/// Normaliza `src` en `out` (**sin terminador**). Devuelve la **longitud** escrita, o el error.
[[nodiscard]] inline eng::util::Expected<eng::usize, PathError>
normalize_path(eng::util::StringView src, eng::Span<char> out) noexcept {
	if (src.empty()) {
		return eng::util::unexpected(PathError::Empty);
	}
	const bool absolute = src[0] == '/';
	// Índices (offset, longitud) de los componentes que sobreviven, en orden.
	eng::u16 off[eng::u16(kMaxPathComponents)] {};
	eng::u16 len[eng::u16(kMaxPathComponents)] {};
	eng::u16 count = 0u;
	eng::usize i = 0u;
	while (i < src.size()) {
		while (i < src.size() && src[i] == '/') {
			++i;
		}
		if (i >= src.size()) {
			break;
		}
		const eng::usize start = i;
		while (i < src.size() && src[i] != '/') {
			++i;
		}
		const eng::usize clen = i - start;
		if (clen == 1u && src[start] == '.') {
			continue;
		}
		if (clen == 2u && src[start] == '.' && src[start + 1u] == '.') {
			if (count == 0u) {
				return eng::util::unexpected(PathError::EscapesRoot);
			}
			--count;
			continue;
		}
		if (count >= kMaxPathComponents) {
			return eng::util::unexpected(PathError::TooManyComponents);
		}
		off[count] = static_cast<eng::u16>(start);
		len[count] = static_cast<eng::u16>(clen);
		++count;
	}

	eng::usize w = 0u;
	/// Escribe el byte `c` si cabe en `out` (falso si no, para devolver `TooLong`).
	const auto push = [&](char c) noexcept -> bool {
		if (w >= out.size()) {
			return false;
		}
		out[w++] = c;
		return true;
	};
	if (absolute && !push('/')) {
		return eng::util::unexpected(PathError::TooLong);
	}
	for (eng::u16 c = 0u; c < count; ++c) {
		if ((c != 0u) && !push('/')) {
			return eng::util::unexpected(PathError::TooLong);
		}
		for (eng::u16 k = 0u; k < len[c]; ++k) {
			if (!push(src[off[c] + k])) {
				return eng::util::unexpected(PathError::TooLong);
			}
		}
	}
	return w;
}

} // namespace eng::os
