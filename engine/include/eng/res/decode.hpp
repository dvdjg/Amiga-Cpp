#pragma once

/// \file decode.hpp
/// **Etapa genérica de decodificación** de un blob comprimido → bytes sin comprimir
/// (`ROADMAP_RESOURCES.md` R6.5). Es el punto único que la usan el **loader de assets** y el **codec
/// de audio**; hoy soporta `Raw` (copia) y `Zx0`. El contenedor `.engz` (R6.4) llevará el `Codec`
/// en su cabecera y llamará aquí.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/res/zx0.hpp>

namespace eng::res {

/// Codec de un blob comprimido (id en la cabecera de `.engz`).
enum class Codec : eng::u8 { Raw = 0u, Zx0 = 1u };

/// Decodifica `src` en `dst`. Devuelve los bytes escritos, o `-1` si el codec es desconocido, el
/// flujo es inválido o no cabe en `dst`. Para `Raw`, copia `min(src.size(), dst.size())`.
[[nodiscard]] inline eng::s32 decode(Codec codec, eng::Span<const eng::u8> src,
				     eng::Span<eng::u8> dst) noexcept {
	switch (codec) {
	case Codec::Raw: {
		const eng::usize n = src.size() < dst.size() ? src.size() : dst.size();
		for (eng::usize i = 0u; i < n; ++i) {
			dst[i] = src[i];
		}
		return static_cast<eng::s32>(n);
	}
	case Codec::Zx0:
		return zx0::decompress(src, dst);
	default:
		return -1;
	}
}

} // namespace eng::res
