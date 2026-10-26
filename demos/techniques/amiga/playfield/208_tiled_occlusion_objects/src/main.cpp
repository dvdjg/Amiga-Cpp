// Demo 208: objetos Tiled con profundidad y occluders sobre Beginning Fields.
//
// La casa del fondo se cuece en la base; el árbol izquierdo (id 43) se emite
// como `foreground` con `foot_y` y la farola (id 143) como `occluder`. Los
// actores se dibujan intercalados con los props por su pie: si su `foot_y` es
// menor que el del árbol, quedan detrás; si es mayor, delante. El occluder se
// pinta después de todos los actores.
//
// El arte de los objetos lo cocina `tools/tiled/cook-objects.mjs` (planar 6
// planos + máscara 1bpp con guarda por fila y hoja par interleaved). Los props
// usan la hoja par con `Sprite` (un blit); los actores, `add_world_bitmap_masked`
// (un blit por plano). El runtime solo resuelve posición de mundo, shift fino y
// orden; no parsea Tiled ni construye un BlitJob a mano.
//
//   bash tools/build/build-demo.sh demos/techniques/amiga/playfield/208_tiled_occlusion_objects --release --clean
//   bash tools/run/run-demo.sh demos/techniques/amiga/playfield/208_tiled_occlusion_objects

#include <eng/api/api.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/map/asset.hpp>
#include <eng/map/objects.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

#include "../../../../../../out/assets/tiled/beginning-fields-objects/selected/map_bundle.h"
#include "../../../../../../out/assets/tiled/beginning-fields-objects/selected/objects.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

// Banco base interleaved (sin los dos árboles de runtime).
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tiled_bank\ng_tiled_bank:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields-objects/selected/tilebank.xlimited.bin\"\n"
	".globl g_tiled_bank_size\ng_tiled_bank_size:\n"
	".long . - g_tiled_bank");
extern "C" const unsigned char g_tiled_bank[];
extern "C" const unsigned int g_tiled_bank_size;

// Arte de los props en hoja par interleaved (imagen+máscara+guardas), 6 planos.
// Un `Sprite`/`Bob` con esta hoja dibuja el objeto en UN blit cookie-cut.
__asm__(".section tiled_obj_pair.MEMF_CHIP, \"aw\"\n"
	".globl g_object_pair\ng_object_pair:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields-objects/selected/object_pair.bin\"\n"
	".globl g_object_pair_size\ng_object_pair_size:\n"
	".long . - g_object_pair");
extern "C" const unsigned char g_object_pair[];
extern "C" const unsigned int g_object_pair_size;

namespace {
namespace playfield = eng::playfield;

constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kViewportW = 320u;
constexpr eng::u16 kViewportH = 256u; // total: 208 de campo + 48 de HUD
constexpr eng::u16 kFieldH = kViewportH - 48u;
constexpr eng::u16 kDisplayH = 288u;  // anillo: 208 visibles + 2 filas de staging
constexpr eng::s32 kCameraX = 200;    // ventana sobre la casa y los árboles
constexpr eng::s32 kCameraY = 0;
constexpr eng::u8 kPlanes = TILED_MAP_PLANES;
constexpr eng::u16 kPaletteCount = (kPlanes == 6u) ? 32u : (1u << kPlanes);
constexpr eng::debug::Version kVersion {1u, 0u};
constexpr bool kUseActorPair = true; // hoja par de un blit para actores
constexpr eng::u32 kReadyDetail = 0x20800000u;

// Anillo vertical de 288: el mapa (640 px) se recorre en Y; X lo resuelve el
// display con el anillo de 352 columnas.
constexpr eng::playfield::ScrollConsts kScrollConsts {
	kTile, kTile, kDisplayH, kDisplayH * kPlanes, kPlanes};

enum class Fail : eng::u32 {
	Memory = 0x00020801u,
	MapAsset = 0x00020802u,
	SceneBegin = 0x00020803u,
	Fill = 0x00020804u,
	Scroll = 0x00020805u,
	ComposeInit = 0x00020806u,
	ObjectDraw = 0x00020807u,
	ActorArt = 0x00020808u,
	Blitter = 0x00020809u,
	ComposeFrame = 0x0002080au,
};

eng::u16 g_palette[32] {};
eng::u16 g_hud_palette[16] {};

void build_palette(const eng::map::MapAssetView& map) {
	const eng::u16 count = map.palette_count() < kPaletteCount ? map.palette_count() : kPaletteCount;
	for (eng::u16 i = 0u; i < count; ++i) g_palette[i] = map.palette_word(i);
}

// --- Actor procedural ---------------------------------------------------------------
// Explorador de 16x24 con 16 px de guarda a la derecha: la fila del sheet mide
// 2 palabras (arte + guarda) y el Blitter puede desplazar 0..15 px sin arrastrar
// bits de la fila siguiente. Colores del perfil EHB base (1..31); 0 = transparente.
// La pareja rojo/naranja/azul contrasta con el verde del árbol y el ocre del suelo.
constexpr eng::u16 kActorVisibleW = 16u;
constexpr eng::u16 kActorH = 24u;
constexpr eng::u16 kActorW = 32u; // arte 16 + 16 de guarda
constexpr eng::u16 kActorRowWords = kActorW / 16u;
constexpr eng::u8 kActorInk = 1u;      // contorno, cinturón y botas
constexpr eng::u8 kActorHatBand = 20u; // cinta del sombrero (morado)
constexpr eng::u8 kActorSkin = 26u;    // cara y brazos (naranja)
constexpr eng::u8 kActorArm = 28u;     // antebrazos (naranja claro)
constexpr eng::u8 kActorTunic = 17u;   // túnica (rojo)
constexpr eng::u8 kActorLegs = 24u;    // pantalón (azul)

[[nodiscard]] constexpr eng::u8 actor_pixel(eng::u16 y, eng::u16 x, bool step) noexcept {
	if (x >= kActorVisibleW || y >= kActorH) return 0u;

	// Sombrero (filas 0..3).
	if (y < 4u) {
		if (x >= 4u && x < 12u) return (y == 3u) ? kActorHatBand : kActorInk;
		return 0u;
	}
	// Cara (filas 4..7).
	if (y < 8u) {
		if (x >= 5u && x < 11u) {
			if (x == 5u || x == 10u) return kActorInk;
			if (y == 5u && (x == 6u || x == 9u)) return kActorInk; // ojos
			return kActorSkin;
		}
		return 0u;
	}
	// Torso (filas 8..16): túnica roja, cinturón y brazos.
	if (y < 17u) {
		if (x >= 5u && x < 11u) return (y == 12u) ? kActorInk : kActorTunic;
		if (x == 4u || x == 11u) return (y < 15u) ? kActorSkin : kActorArm;
		return 0u;
	}
	// Piernas (17..21): zancada alterna; botas (22..23).
	const eng::s16 shift = step ? 1 : -1;
	if (y < 22u) {
		if (x >= static_cast<eng::u16>(5 + shift) && x < static_cast<eng::u16>(8 + shift)) return kActorLegs;
		if (x >= static_cast<eng::u16>(8 - shift) && x < static_cast<eng::u16>(11 - shift)) return kActorLegs;
		return 0u;
	}
	const eng::u16 foot_shift = step ? 1u : 0u;
	if (x >= 4u + foot_shift && x < 8u + foot_shift) return kActorInk;
	if (x >= 8u - foot_shift && x < 12u - foot_shift) return kActorInk;
	return 0u;
}

template <eng::u16 W, eng::u16 H, eng::u8 Planes, class Fn>
[[nodiscard]] constexpr auto make_sheet(Fn pixel) {
	constexpr eng::u16 kW = W / 16u;
	eng::util::Array<eng::u16, static_cast<eng::usize>(Planes) * H * kW> out {};
	for (eng::u16 y = 0u; y < H; ++y) {
		for (eng::u16 x = 0u; x < W; ++x) {
			const eng::u8 color = static_cast<eng::u8>(pixel(y, x) & 0x1fu);
			if (color == 0u) continue;
			for (eng::u8 p = 0u; p < Planes; ++p) {
				if ((color & static_cast<eng::u8>(1u << p)) == 0u) continue;
				const eng::usize at = (static_cast<eng::usize>(p) * H + y) * kW + (x >> 4u);
				out[at] = static_cast<eng::u16>(out[at] | static_cast<eng::u16>(0x8000u >> (x & 15u)));
			}
		}
	}
	return out;
}

template <eng::u16 W, eng::u16 H, class Fn>
[[nodiscard]] constexpr auto make_mask(Fn pixel) {
	constexpr eng::u16 kW = W / 16u;
	eng::util::Array<eng::u16, static_cast<eng::usize>(H) * kW> out {};
	for (eng::u16 y = 0u; y < H; ++y) {
		for (eng::u16 x = 0u; x < W; ++x) {
			if (pixel(y, x) == 0u) continue;
			const eng::usize at = static_cast<eng::usize>(y) * kW + (x >> 4u);
			out[at] = static_cast<eng::u16>(out[at] | static_cast<eng::u16>(0x8000u >> (x & 15u)));
		}
	}
	return out;
}

constexpr auto kActorIdle = make_sheet<kActorW, kActorH, kPlanes>([](eng::u16 y, eng::u16 x) {
	return actor_pixel(y, x, false);
});
constexpr auto kActorStep = make_sheet<kActorW, kActorH, kPlanes>([](eng::u16 y, eng::u16 x) {
	return actor_pixel(y, x, true);
});
constexpr auto kActorMask = make_mask<kActorW, kActorH>([](eng::u16 y, eng::u16 x) {
	return actor_pixel(y, x, false);
});

// --- Actores -----------------------------------------------------------------------
struct Walker {
	static constexpr eng::u8 kPathCount = 10u;
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u8 target = 1u;
	eng::u8 step = 0u;
	eng::u8 phase = 0u;
	eng::u8 tick = 0u;
	eng::s16 painted_x = 0;
	eng::s16 painted_y = 0;
	bool saved = false;
	bool active = true; // false = explorador de guardia, ya horneado en el anillo

	[[nodiscard]] eng::s16 foot_y() const noexcept {
		return static_cast<eng::s16>(y + static_cast<eng::s16>(kActorH));
	}
	void update() noexcept {
		// Ruta en mundo (píxeles). El tramo x=272 cruza el pie del árbol ordenado
		// (id 43): y=55..65 queda detrás (pie < 90) y y=66..80 delante. Después
		// el actor pasa bajo la farola occluder (id 143), que siempre se pinta encima.
		static constexpr eng::s16 kPathX[kPathCount] = {280, 320, 320, 272, 272, 272, 230, 230, 230, 280};
		static constexpr eng::s16 kPathY[kPathCount] = {180, 180, 60, 55, 65, 80, 80, 50, 180, 160};
		const eng::s16 tx = kPathX[target];
		const eng::s16 ty = kPathY[target];
		if (x != tx) x = static_cast<eng::s16>(x + (tx > x ? 1 : -1));
		if (y != ty) y = static_cast<eng::s16>(y + (ty > y ? 1 : -1));
		if (x == tx && y == ty) target = static_cast<eng::u8>((target + 1u) % kPathCount);
		++tick;
		if ((tick & 7u) == phase) step ^= 1u;
	}
};

struct ActorArt {
	const eng::u16* sheet = nullptr;
	const eng::u16* mask = nullptr;
	eng::u16 width = kActorVisibleW;
	eng::u16 height = kActorH;
	eng::u16 row_words = kActorRowWords;
	eng::u8 planes = kPlanes;
};

struct DemoGame {
	eng::map::MapAssetView m_map {eng::Span<const eng::u8> {kMapBundle, sizeof(kMapBundle)}};
	eng::map::LayerView m_layer {};
	eng::map::MapObjectsView m_objects {};

	playfield::XlimitedScene<kScrollConsts, eng::map::LayerView> scene {};
	playfield::XlimitedSceneConfigT<eng::map::LayerView> scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::debug::HostHud host_hud {};

	Walker m_walkers[2] {};
	ActorArt m_actor_art[2] {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_actor_sheet[2] {};
	eng::Block<eng::MaskTag, eng::MemoryKind::Chip> m_actor_mask {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_actor_pair[2] {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_actor_save[2] {};
	eng::graphics::Sprite m_actor_pair_sprites[2] {};
	eng::graphics::Sprite m_prop_sprites[TILED_MAP_OBJECT_IMAGE_COUNT] {};
	eng::graphics::BobTarget m_bob_target {};

	bool ready = false;

	bool upload_actor_art(eng::amiga::AmigaBackend& backend) {
		const eng::u32 sheet_bytes = static_cast<eng::u32>(kActorIdle.size() * 2u);
		const eng::u32 mask_bytes = static_cast<eng::u32>(kActorMask.size() * 2u);
		m_actor_sheet[0] = backend.memory_manager().chip().reserve<eng::MaskTag>(sheet_bytes, 16u);
		m_actor_sheet[1] = backend.memory_manager().chip().reserve<eng::MaskTag>(sheet_bytes, 16u);
		m_actor_mask = backend.memory_manager().chip().reserve<eng::MaskTag>(mask_bytes, 16u);
		if (!m_actor_sheet[0].valid() || !m_actor_sheet[1].valid() || !m_actor_mask.valid()) return false;
		const auto idle = m_actor_sheet[0].view.as_words();
		const auto step = m_actor_sheet[1].view.as_words();
		const auto mask = m_actor_mask.view.as_words();
		for (eng::usize i = 0u; i < kActorIdle.size(); ++i) {
			idle[i] = kActorIdle[i];
			step[i] = kActorStep[i];
		}
		for (eng::usize i = 0u; i < kActorMask.size(); ++i) mask[i] = kActorMask[i];
		for (eng::u8 f = 0u; f < 2u; ++f) {
			m_actor_art[f].sheet = reinterpret_cast<const eng::u16*>(m_actor_sheet[f].data());
			m_actor_art[f].mask = reinterpret_cast<const eng::u16*>(m_actor_mask.data());
			m_actor_art[f].width = kActorVisibleW;
			m_actor_art[f].height = kActorH;
			m_actor_art[f].row_words = kActorRowWords;
			m_actor_art[f].planes = kPlanes;
		}
		return true;
	}

	bool build_actor_pair_sprites(eng::amiga::AmigaBackend& backend) {
		for (eng::u8 f = 0u; f < 2u; ++f) {
			eng::util::Array<eng::u8, static_cast<eng::usize>(kActorVisibleW) * kActorH> pixels {};
			eng::util::Array<eng::u8, static_cast<eng::usize>(kActorVisibleW) * kActorH> mask {};
			for (eng::u16 y = 0u; y < kActorH; ++y) {
				for (eng::u16 x = 0u; x < kActorVisibleW; ++x) {
					const eng::u8 color = actor_pixel(y, x, f != 0u);
					pixels[static_cast<eng::usize>(y) * kActorVisibleW + x] = color;
					mask[static_cast<eng::usize>(y) * kActorVisibleW + x] = color != 0u ? 1u : 0u;
				}
			}
			eng::util::Array<eng::u16, static_cast<eng::usize>(kActorH) * kPlanes * 4u> pair {};
			eng::graphics::pack_interleaved_pair<kActorVisibleW, kActorH, kPlanes>(
				pixels.data(), mask.data(), pair.data());
			const eng::u32 bytes = static_cast<eng::u32>(pair.size() * 2u);
			m_actor_pair[f] = backend.memory_manager().chip().reserve<eng::BobTag>(bytes, 16u);
			const eng::u32 save_bytes = 2u * 2u * static_cast<eng::u32>(kActorH) * kPlanes;
			m_actor_save[f] = backend.memory_manager().chip().reserve<eng::BobTag>(save_bytes, 16u);
			if (!m_actor_pair[f].valid() || !m_actor_save[f].valid()) return false;
			const auto words = m_actor_pair[f].view.as_words();
			for (eng::usize i = 0u; i < pair.size(); ++i) words[i] = pair[i];
			eng::graphics::Bob bob {};
			bob.sheet = m_actor_pair[f].mem_view();
			bob.width = static_cast<eng::u16>(kActorVisibleW * 2u);
			bob.height = kActorH;
			bob.planes = kPlanes;
			bob.frame_count = 1u;
			bob.layout = eng::graphics::BobLayout::Interleaved;
			bob.draw = eng::graphics::BobDraw::CookieCut;
			bob.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
			m_actor_pair_sprites[f] = eng::graphics::Sprite {bob, bytes, 0u};
		}
		return true;
	}

	bool build_prop_sprites() {
		for (eng::u16 i = 0u; i < TILED_MAP_OBJECT_IMAGE_COUNT; ++i) {
			const eng::map::MapObjectImageView& image = m_objects.image(i);
			const eng::u32 bytes = static_cast<eng::u32>(image.height) * kPlanes *
				static_cast<eng::u32>(4u * (image.width / 16u)) * 2u;
			const eng::u32 off = kMapObjectPairWordOffset[i] * 2u;
			eng::graphics::Bob bob {};
			bob.sheet = eng::ChipView<eng::BobTag> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(g_object_pair + off), bytes};
			bob.width = static_cast<eng::u16>(image.width * 2u);
			bob.height = image.height;
			bob.planes = kPlanes;
			bob.frame_count = 1u;
			bob.layout = eng::graphics::BobLayout::Interleaved;
			bob.draw = eng::graphics::BobDraw::CookieCut;
			bob.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
			m_prop_sprites[i] = eng::graphics::Sprite {bob, bytes, 0u};
		}
		return true;
	}

	/// Dibuja una hoja par interleaved en coordenadas de mundo. El destino es el
	/// anillo de la escena: `y` es la línea del anillo, `x` la columna del bitmap.
	bool draw_sprite(eng::graphics::FramePlan& frame_plan, const eng::graphics::Sprite& sprite,
			 eng::u16 visible_w, eng::s32 world_x, eng::s32 world_y) {
		const eng::s32 mapw = static_cast<eng::s32>(m_layer.width) * static_cast<eng::s32>(kTile);
		eng::s32 sx = (world_x - scene.bg().mapposx()) % mapw;
		if (sx < 0) sx += mapw;
		if (sx + static_cast<eng::s32>(visible_w) > static_cast<eng::s32>(kViewportW)) return true;
		eng::s32 sy = (world_y - scene.bg().display_offset()) % static_cast<eng::s32>(kDisplayH);
		if (sy < 0) sy += static_cast<eng::s32>(kDisplayH);
		if (sy >= static_cast<eng::s32>(kFieldH) &&
		    sy + static_cast<eng::s32>(sprite.height()) <= static_cast<eng::s32>(kDisplayH)) {
			return true; // completamente por debajo de la ventana visible
		}
		eng::s32 rx = world_x % static_cast<eng::s32>(m_bob_target.width);
		if (rx < 0) rx += static_cast<eng::s32>(m_bob_target.width);
		eng::s32 ry = world_y % static_cast<eng::s32>(m_bob_target.height);
		if (ry < 0) ry += static_cast<eng::s32>(m_bob_target.height);
		return sprite.draw(frame_plan, m_bob_target, 0u,
				   static_cast<eng::s16>(rx), static_cast<eng::s16>(ry));
	}

	bool draw_prop(eng::graphics::FramePlan& frame_plan, const eng::map::MapObjectView& object) {
		const eng::map::MapObjectImageView& image = m_objects.image_of(object);
		return draw_sprite(frame_plan, m_prop_sprites[object.image], image.width, object.x, object.y);
	}

	/// Actores por el camino planar enmascarado (6 blits/actor): el pack interleaved
	/// par del motor aún no cubre bien este arte pequeño en el anillo; los props sí
	/// usan la hoja par de un solo blit.
	bool restore_actor_under(eng::graphics::FramePlan& frame_plan, eng::u8 slot) {
		Walker& walker = m_walkers[slot];
		if (!walker.active || !walker.saved) return true;
		const eng::graphics::Bob& bob = m_actor_pair_sprites[0].bob();
		const bool ok = eng::graphics::bob_restore_box(
			frame_plan, bob, bob.width, bob.height, walker.painted_x, walker.painted_y,
			m_bob_target, m_actor_save[slot].mem_view(), 2u, kActorH);
		walker.saved = false;
		return ok;
	}

	bool draw_actor(eng::graphics::FramePlan& frame_plan, Walker& walker, eng::u8 slot) {
		const eng::u8 frame = walker.step != 0u ? 1u : 0u;
		if constexpr (kUseActorPair) {
			const eng::graphics::Bob& bob = m_actor_pair_sprites[frame].bob();
			const eng::s32 mapw = static_cast<eng::s32>(m_layer.width) * static_cast<eng::s32>(kTile);
			eng::s32 sx = (walker.x - scene.bg().mapposx()) % mapw;
			if (sx < 0) sx += mapw;
			const bool visible = sx + static_cast<eng::s32>(kActorVisibleW) <=
					     static_cast<eng::s32>(kViewportW);
			eng::s32 sy = (walker.y - scene.bg().display_offset()) % static_cast<eng::s32>(kDisplayH);
			if (sy < 0) sy += static_cast<eng::s32>(kDisplayH);
			const bool in_window = !(sy >= static_cast<eng::s32>(kFieldH) &&
				sy + static_cast<eng::s32>(kActorH) <= static_cast<eng::s32>(kDisplayH));
			if (!visible || !in_window) {
				return restore_actor_under(frame_plan, slot);
			}
			eng::s32 rx = walker.x % static_cast<eng::s32>(m_bob_target.width);
			if (rx < 0) rx += static_cast<eng::s32>(m_bob_target.width);
			eng::s32 ry = walker.y % static_cast<eng::s32>(m_bob_target.height);
			if (ry < 0) ry += static_cast<eng::s32>(m_bob_target.height);
			if (!eng::graphics::bob_save_box(
				    frame_plan, bob, bob.width, bob.height, static_cast<eng::s16>(rx),
				    static_cast<eng::s16>(ry), m_bob_target, m_actor_save[slot].mem_view(),
				    2u, kActorH)) {
				return false;
			}
			if (!m_actor_pair_sprites[frame].draw(
				    frame_plan, m_bob_target, 0u, static_cast<eng::s16>(rx),
				    static_cast<eng::s16>(ry))) {
				return false;
			}
			walker.painted_x = static_cast<eng::s16>(rx);
			walker.painted_y = static_cast<eng::s16>(ry);
			walker.saved = true;
			return true;
		}
		const ActorArt& art = m_actor_art[frame];
		const eng::s32 mapw = static_cast<eng::s32>(m_layer.width) * static_cast<eng::s32>(kTile);
		eng::s32 sx = (walker.x - scene.bg().mapposx()) % mapw;
		if (sx < 0) sx += mapw;
		if (sx + static_cast<eng::s32>(art.width) > static_cast<eng::s32>(kViewportW)) return true;
		eng::s32 sy = (walker.y - scene.bg().display_offset()) % static_cast<eng::s32>(kDisplayH);
		if (sy < 0) sy += static_cast<eng::s32>(kDisplayH);
		if (sy >= static_cast<eng::s32>(kFieldH) &&
		    sy + static_cast<eng::s32>(art.height) <= static_cast<eng::s32>(kDisplayH)) {
			return true;
		}
		const eng::u32 plane_words = static_cast<eng::u32>(art.row_words) * art.height;
		const eng::Span<const eng::u16> src {art.sheet, static_cast<eng::usize>(plane_words) * art.planes};
		const eng::Span<const eng::u16> mask {art.mask, static_cast<eng::usize>(plane_words)};
		return scene.bg().add_world_bitmap_masked(
			frame_plan, src, mask, walker.x, walker.y, art.width, art.height,
			static_cast<eng::u16>(art.row_words * 2u), plane_words * 2u, art.planes,
			static_cast<eng::u8>(walker.x & 15));
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({104u * 1024u, 16u * 1024u, 0u})) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Memory);
			return;
		}
		if (!m_map.valid() || m_map.planes() != kPlanes || !m_map.palette_count()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::MapAsset);
			return;
		}
		m_layer = eng::map::LayerView {m_map, 0u};
		if (!m_layer.has_data()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::MapAsset);
			return;
		}
		m_objects = eng::map::MapObjectsView {
			eng::Span<const eng::map::MapObjectView> {kMapObjects, TILED_MAP_OBJECT_COUNT},
			eng::Span<const eng::map::MapObjectImageView> {kMapObjectImages, TILED_MAP_OBJECT_IMAGE_COUNT},
			TILED_MAP_OBJECT_FOREGROUND_COUNT};
		if (!m_objects.valid() || m_objects.image_count() == 0u) {
			eng::debug::mark_failed(g_eng_run_status, Fail::MapAsset);
			return;
		}
		build_palette(m_map);
		if (!upload_actor_art(backend) || !build_actor_pair_sprites(backend) || !build_prop_sprites()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::ActorArt);
			return;
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.hud.height = 48u;
		scene_cfg.hud.planes = 4u;
		scene_cfg.hud.palette = g_hud_palette;
		scene_cfg.tile_width = kTile;
		scene_cfg.tile_height = kTile;
		scene_cfg.planes = m_map.planes();
		scene_cfg.fetch_mode = 0u;
		scene_cfg.y_mode = playfield::AxisPolicy::Ring;
		scene_cfg.direction = playfield::DirectionPolicy::Bidirectional;
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 1u;
		scene_cfg.map = m_layer;
		scene_cfg.tileset_count = m_map.tile_count();
		scene_cfg.blocks_prebuilt = g_tiled_bank;
		scene_cfg.blocks_prebuilt_size = g_tiled_bank_size;
		scene_cfg.palette = g_palette;

		if (!scene.begin(backend.memory_manager(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::SceneBegin);
			return;
		}
		m_bob_target = scene.sprite_context().targets[0];
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Fill);
			return;
		}
		if (!scene.pre_scroll(backend, plan, kCameraX, kCameraY)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Scroll);
			return;
		}
		scene.bg().set_camera(kCameraX, kCameraY);
		// Explorador de guardia: se pinta una vez en el anillo, fuera del orden
		// dinámico; el actor que cruza el árbol y la farola es el móvil.
		plan.clear();
		const eng::s32 guard_x = 300 % static_cast<eng::s32>(m_bob_target.width);
		const eng::s32 guard_y = 170 % static_cast<eng::s32>(m_bob_target.height);
		if (!m_actor_pair_sprites[0].draw(plan, m_bob_target, 0u,
				static_cast<eng::s16>(guard_x), static_cast<eng::s16>(guard_y))) {
			eng::debug::mark_failed(g_eng_run_status, Fail::ActorArt);
			return;
		}
		if (plan.blit_job_count() != 0u && !backend.execute_frame_plan(plan)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
		plan.clear();

		m_walkers[0] = Walker {300, 170, 0u, 0u, 0u, 0u, 0, 0, false, false};
		m_walkers[1] = Walker {272, 58, 4u, 0u, 3u, 0u, 0, 0, false, true};
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::ComposeInit);
			return;
		}
		scene.takeover(backend);
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, kReadyDetail);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!ready) return;
		for (auto& walker : m_walkers) {
			if (walker.active) walker.update();
		}

		plan.clear();
		plan.set_blit_budget_limits({16384, 65536, 120, 512});
		for (eng::u8 slot = 0u; slot < 2u; ++slot) {
			if (!restore_actor_under(plan, slot)) {
				ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::ObjectDraw);
				return;
			}
		}
		// Orden de pintado: props antes del actor cuyo pie es menor; después el
		// actor; el resto de props; y por último los occluders (siempre encima).
		eng::u8 order[2] = {0u, 1u};
		if (m_walkers[order[0]].foot_y() > m_walkers[order[1]].foot_y()) {
			const eng::u8 t = order[0];
			order[0] = order[1];
			order[1] = t;
		}
		eng::u16 cursor = 0u;
		const eng::u16 fg_count = m_objects.foreground_count();
		for (eng::u8 k = 0u; k < 2u; ++k) {
			Walker& walker = m_walkers[order[k]];
			if (!walker.active) continue;
			while (cursor < fg_count &&
			       m_objects.object(cursor).sort_key() <= walker.foot_y()) {
				if (!draw_prop(plan, m_objects.object(cursor))) {
					ready = false;
					eng::debug::mark_failed(g_eng_run_status, Fail::ObjectDraw);
					return;
				}
				++cursor;
			}
			if (!draw_actor(plan, walker, order[k])) {
				ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::ObjectDraw);
				return;
			}
		}
		while (cursor < fg_count) {
			if (!draw_prop(plan, m_objects.object(cursor))) {
				ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::ObjectDraw);
				return;
			}
			++cursor;
		}
		for (eng::u16 i = fg_count; i < m_objects.count(); ++i) {
			if (!draw_prop(plan, m_objects.object(i))) {
				ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::ObjectDraw);
				return;
			}
		}
		if (plan.blit_job_count() != 0u && !backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::ComposeFrame);
			return;
		}
		eng::util::TextLine<64> stats;
		stats << "F=" << context.frame.frame_index << " SPLIT=" << cursor
		      << " A0=" << m_walkers[0].foot_y() << " A1=" << m_walkers[1].foot_y();
		host_hud.draw(backend.debug(), "208 TILED OCCLUSION", kVersion,
			      context.frame.frame_index, eng::debug::prof_clock(), stats.c_str());
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (ready) scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);
	return 0;
}
