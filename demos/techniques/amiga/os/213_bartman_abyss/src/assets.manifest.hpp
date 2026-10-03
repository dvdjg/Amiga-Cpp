#pragma once

/// \file assets.manifest.hpp
/// **GENERADO** por `tools/assets/gen-manifest.mjs` desde `assets.manifest.json`. NO EDITAR A
/// MANO: cambia el JSON y regenera (`node tools/assets/gen-manifest.mjs <json>`). Declara los
/// `INCBIN` de los blobs y expone un accesor por asset más `register_assets(Assets&)`, que
/// registra cada recurso con su **geometría** (incrustada aquí, no en el código de juego).

#include "support/gcc8_c_support.h"

#include <eng/core/types/types.hpp>
#include <eng/graphics/sprite_asset.hpp>

INCBIN(abyss_img, "assets/amiga/sprites/abyss/abyss.bpl");
INCBIN(abyss_bob, "assets/amiga/sprites/abyss/bob.bpl");
INCBIN(abyss_mod, "assets/amiga/audio/testmod.p61");
INCBIN(abyss_pal, "assets/amiga/sprites/abyss/abyss.pal");

namespace abyss {

/// Tamaño del blob `sym` (diferencia de los símbolos `incbin_*_start`/`_end` de `INCBIN`).
#define ABYSS_BLOB_SIZE(sym)                                                                          \
	static_cast<eng::u32>(reinterpret_cast<const char*>(&incbin_##sym##_end) -             \
			      incbin_##sym##_start)

[[nodiscard]] inline const eng::u8* img_data() noexcept { return reinterpret_cast<const eng::u8*>(abyss_img); }
[[nodiscard]] inline eng::u32 img_size() noexcept { return ABYSS_BLOB_SIZE(abyss_img); }

[[nodiscard]] inline const eng::u8* bob_data() noexcept { return reinterpret_cast<const eng::u8*>(abyss_bob); }
[[nodiscard]] inline eng::u32 bob_size() noexcept { return ABYSS_BLOB_SIZE(abyss_bob); }
[[nodiscard]] inline eng::graphics::Bob bob_desc() noexcept {
	eng::graphics::Bob d {};
	d.width = 32u;
	d.height = 16u;
	d.planes = 5u;
	d.frame_count = 6u;
	d.frame_stride = 640u;
	d.layout = eng::graphics::BobLayout::Interleaved;
	d.draw = eng::graphics::BobDraw::CookieCut;
	d.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
	return d;
}

[[nodiscard]] inline const eng::u8* mod_data() noexcept { return reinterpret_cast<const eng::u8*>(abyss_mod); }
[[nodiscard]] inline eng::u32 mod_size() noexcept { return ABYSS_BLOB_SIZE(abyss_mod); }

[[nodiscard]] inline const eng::u16* pal_words() noexcept { return reinterpret_cast<const eng::u16*>(abyss_pal); }
[[nodiscard]] inline eng::u32 pal_size() noexcept { return ABYSS_BLOB_SIZE(abyss_pal); }

/// Registra **todos** los assets del manifiesto en el `Assets` del juego, con su geometría.
/// `Assets` es plantilla para no acoplar el header a `eng/api/assets.hpp` (lo incluye el juego).
template <class Assets>
[[nodiscard]] inline bool register_assets(Assets& a) noexcept {
	if (!a.add_bitmap("img", img_data(), img_size(), 320u, 256u, 5u, eng::graphics::PlaneLayout::Interleaved)) return false;
	if (!a.add_sprite("bob", bob_data(), bob_size(), bob_desc())) return false;
	if (!a.template add<eng::MusicTag>("mod", mod_data(), mod_size())) return false;
	if (!a.template add<eng::PaletteTag>("pal", reinterpret_cast<const eng::u8*>(pal_words()), pal_size())) return false;
	return true;
}

#undef ABYSS_BLOB_SIZE

} // namespace abyss
