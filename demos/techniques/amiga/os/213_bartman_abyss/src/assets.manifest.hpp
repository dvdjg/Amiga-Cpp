#pragma once

/// \file assets.manifest.hpp
/// **Manifiesto de assets de la demo 213** (fuente única de los blobs incrustados). El código de
/// juego no contiene `INCBIN` ni rutas de assets: pide cada recurso por su nombre de dominio. Un
/// generador puede emitir este header desde un manifiesto de datos (ver `ROADMAP_GAME_API.md` §4);
/// mientras tanto se mantiene a mano con el mismo contrato.
///
/// Los `INCBIN` van a **ámbito global**: su asm emite símbolos `incbin_*` sin mangle, y dentro de
/// un `namespace` la declaración C++ se manglearía y no casaría en el enlazado.

#include "support/gcc8_c_support.h"

#include <eng/core/types/types.hpp>

INCBIN(abyss_img, "assets/amiga/sprites/abyss/abyss.bpl");
INCBIN(abyss_bob, "assets/amiga/sprites/abyss/bob.bpl");
INCBIN(abyss_mod, "assets/amiga/audio/testmod.p61");
INCBIN(abyss_pal, "assets/amiga/sprites/abyss/abyss.pal");

namespace abyss {

/// Tamaño del blob `sym`: diferencia de los símbolos `incbin_*_start`/`_end` que emite `INCBIN`.
#define ABYSS_BLOB_SIZE(sym)                                                                          \
	static_cast<eng::u32>(reinterpret_cast<const char*>(&incbin_##sym##_end) -                    \
			      incbin_##sym##_start)

[[nodiscard]] inline const eng::u8* img_data() noexcept { return reinterpret_cast<const eng::u8*>(abyss_img); }
[[nodiscard]] inline eng::u32 img_size() noexcept { return ABYSS_BLOB_SIZE(abyss_img); }
[[nodiscard]] inline const eng::u8* bob_data() noexcept { return reinterpret_cast<const eng::u8*>(abyss_bob); }
[[nodiscard]] inline eng::u32 bob_size() noexcept { return ABYSS_BLOB_SIZE(abyss_bob); }
[[nodiscard]] inline const eng::u8* mod_data() noexcept { return reinterpret_cast<const eng::u8*>(abyss_mod); }
[[nodiscard]] inline eng::u32 mod_size() noexcept { return ABYSS_BLOB_SIZE(abyss_mod); }
[[nodiscard]] inline const eng::u16* pal_words() noexcept {
	return reinterpret_cast<const eng::u16*>(abyss_pal);
}

#undef ABYSS_BLOB_SIZE

} // namespace abyss
