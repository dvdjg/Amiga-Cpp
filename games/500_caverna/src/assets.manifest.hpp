#pragma once

/// \file assets.manifest.hpp
/// **GENERADO** por `tools/assets/gen-manifest.mjs` desde `assets.manifest.json`. NO EDITAR A
/// MANO: cambia el JSON y regenera (`node tools/assets/gen-manifest.mjs <json>`). Declara los
/// `INCBIN` de los blobs y expone un accesor por asset más `register_assets(Assets&)`, que
/// registra cada recurso con su **geometría** (incrustada aquí, no en el código de juego).

#include "support/gcc8_c_support.h"

#include <eng/core/types/types.hpp>
#include <eng/graphics/sprite_asset.hpp>

INCBIN(caverna_assets_pal, "out/assets/caverna/palette.bin");
INCBIN(caverna_assets_player, "out/assets/caverna/player.bin");
INCBIN(caverna_assets_bat, "out/assets/caverna/bat.bin");
INCBIN(caverna_assets_slime, "out/assets/caverna/slime.bin");
INCBIN(caverna_assets_crab, "out/assets/caverna/crab.bin");
INCBIN(caverna_assets_map_pf1, "out/assets/caverna/map_pf1.bin");
INCBIN(caverna_assets_map_pf2, "out/assets/caverna/map_pf2.bin");
INCBIN(caverna_assets_cave_rows, "out/assets/caverna/cave_rows.bin");
INCBIN(caverna_assets_terrain_rows, "out/assets/caverna/terrain_rows.bin");
INCBIN(caverna_assets_mod, "assets/amiga/audio/testmod.p61");
INCBIN(caverna_assets_sfx_jump, "out/assets/caverna/sfx_jump.raw");
INCBIN(caverna_assets_sfx_stomp, "out/assets/caverna/sfx_stomp.raw");
INCBIN(caverna_assets_sfx_damage, "out/assets/caverna/sfx_damage.raw");
INCBIN(caverna_assets_sfx_land, "out/assets/caverna/sfx_land.raw");

namespace caverna_assets {

/// Tamaño del blob `sym` (diferencia de los símbolos `incbin_*_start`/`_end` de `INCBIN`).
#define CAVERNA_ASSETS_BLOB_SIZE(sym)                                                                          \
	static_cast<eng::u32>(reinterpret_cast<const char*>(&incbin_##sym##_end) -             \
			      incbin_##sym##_start)

[[nodiscard]] inline const eng::u16* pal_words() noexcept { return reinterpret_cast<const eng::u16*>(caverna_assets_pal); }
[[nodiscard]] inline eng::u32 pal_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_pal); }

[[nodiscard]] inline const eng::u8* player_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_player); }
[[nodiscard]] inline eng::u32 player_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_player); }
[[nodiscard]] inline eng::graphics::Bob player_desc() noexcept {
	eng::graphics::Bob d {};
	d.width = 16u;
	d.height = 16u;
	d.planes = 2u;
	d.frame_count = 2u;
	d.frame_stride = 64u;
	d.layout = eng::graphics::BobLayout::Interleaved;
	d.draw = eng::graphics::BobDraw::CookieCut;
	d.mask_pack = eng::graphics::BobMaskPack::SeparatePlane;
	return d;
}

[[nodiscard]] inline const eng::u8* bat_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_bat); }
[[nodiscard]] inline eng::u32 bat_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_bat); }
[[nodiscard]] inline eng::graphics::Bob bat_desc() noexcept {
	eng::graphics::Bob d {};
	d.width = 16u;
	d.height = 16u;
	d.planes = 2u;
	d.frame_count = 2u;
	d.frame_stride = 64u;
	d.layout = eng::graphics::BobLayout::Interleaved;
	d.draw = eng::graphics::BobDraw::CookieCut;
	d.mask_pack = eng::graphics::BobMaskPack::SeparatePlane;
	return d;
}

[[nodiscard]] inline const eng::u8* slime_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_slime); }
[[nodiscard]] inline eng::u32 slime_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_slime); }
[[nodiscard]] inline eng::graphics::Bob slime_desc() noexcept {
	eng::graphics::Bob d {};
	d.width = 16u;
	d.height = 16u;
	d.planes = 2u;
	d.frame_count = 2u;
	d.frame_stride = 64u;
	d.layout = eng::graphics::BobLayout::Interleaved;
	d.draw = eng::graphics::BobDraw::CookieCut;
	d.mask_pack = eng::graphics::BobMaskPack::SeparatePlane;
	return d;
}

[[nodiscard]] inline const eng::u8* crab_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_crab); }
[[nodiscard]] inline eng::u32 crab_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_crab); }
[[nodiscard]] inline eng::graphics::Bob crab_desc() noexcept {
	eng::graphics::Bob d {};
	d.width = 16u;
	d.height = 16u;
	d.planes = 2u;
	d.frame_count = 2u;
	d.frame_stride = 64u;
	d.layout = eng::graphics::BobLayout::Interleaved;
	d.draw = eng::graphics::BobDraw::CookieCut;
	d.mask_pack = eng::graphics::BobMaskPack::SeparatePlane;
	return d;
}

[[nodiscard]] inline const eng::u8* map_pf1_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_map_pf1); }
[[nodiscard]] inline eng::u32 map_pf1_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_map_pf1); }

[[nodiscard]] inline const eng::u8* map_pf2_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_map_pf2); }
[[nodiscard]] inline eng::u32 map_pf2_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_map_pf2); }

[[nodiscard]] inline const eng::u8* cave_rows_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_cave_rows); }
[[nodiscard]] inline eng::u32 cave_rows_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_cave_rows); }

[[nodiscard]] inline const eng::u8* terrain_rows_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_terrain_rows); }
[[nodiscard]] inline eng::u32 terrain_rows_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_terrain_rows); }

[[nodiscard]] inline const eng::u8* mod_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_mod); }
[[nodiscard]] inline eng::u32 mod_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_mod); }

[[nodiscard]] inline const eng::u8* sfx_jump_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_sfx_jump); }
[[nodiscard]] inline eng::u32 sfx_jump_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_sfx_jump); }

[[nodiscard]] inline const eng::u8* sfx_stomp_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_sfx_stomp); }
[[nodiscard]] inline eng::u32 sfx_stomp_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_sfx_stomp); }

[[nodiscard]] inline const eng::u8* sfx_damage_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_sfx_damage); }
[[nodiscard]] inline eng::u32 sfx_damage_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_sfx_damage); }

[[nodiscard]] inline const eng::u8* sfx_land_data() noexcept { return reinterpret_cast<const eng::u8*>(caverna_assets_sfx_land); }
[[nodiscard]] inline eng::u32 sfx_land_size() noexcept { return CAVERNA_ASSETS_BLOB_SIZE(caverna_assets_sfx_land); }

/// Registra los assets del manifiesto; register_sprites:false deja las hojas fuera.
template <class Assets>
[[nodiscard]] inline bool register_assets(Assets& a) noexcept {
	if (!a.template add<eng::PaletteTag>("pal", reinterpret_cast<const eng::u8*>(pal_words()), pal_size())) return false;
	if (!a.template add<eng::MapCellsTag>("map_pf1", map_pf1_data(), map_pf1_size())) return false;
	if (!a.template add<eng::MapCellsTag>("map_pf2", map_pf2_data(), map_pf2_size())) return false;
	if (!a.template add<eng::WorkTag>("cave_rows", cave_rows_data(), cave_rows_size())) return false;
	if (!a.template add<eng::WorkTag>("terrain_rows", terrain_rows_data(), terrain_rows_size())) return false;
	if (!a.template add<eng::MusicTag>("mod", mod_data(), mod_size())) return false;
	if (!a.template add<eng::AudioTag>("sfx_jump", sfx_jump_data(), sfx_jump_size())) return false;
	if (!a.template add<eng::AudioTag>("sfx_stomp", sfx_stomp_data(), sfx_stomp_size())) return false;
	if (!a.template add<eng::AudioTag>("sfx_damage", sfx_damage_data(), sfx_damage_size())) return false;
	if (!a.template add<eng::AudioTag>("sfx_land", sfx_land_data(), sfx_land_size())) return false;
	return true;
}

#undef CAVERNA_ASSETS_BLOB_SIZE

} // namespace caverna_assets
