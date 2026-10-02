// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/os/213_bartman_abyss --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/os/213_bartman_abyss --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --keep-running

// Demo 213 - "Bartman Abyss": port de la demo clásica de Bartman/vscode-amiga-debug al engine,
// escrita con la **fachada de juego** (`eng::App`/`Screen`, `Scene` + `app.audio()`). No hay
// `main`, `SysBase`, punteros crudos, registros ni `BlitJob`: el juego describe QUÉ quiere y el
// engine decide CÓMO. Ver `docs/engine/architecture/GAME_API_TWO_LEVELS.md`.
//
// Reproduce la demo original:
//   - Escena 320x256 de 5 planos con la imagen "abyss" sobre fondo claro.
//   - 16 BOB enmascarados (cookie-cut) movidos por senos; el juego solo pinta sprites.
//   - Música P61 por `app.audio()`.

#include <eng/api/api.hpp>
#include <eng/api/assets.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/memory_profile.hpp>

#include "support/gcc8_c_support.h"

// --- Assets incrustados (ámbito global para que casen los símbolos `incbin_*`) ----------------
// `INCBIN` deja el blob en `.rodata` (sección estándar; sin hunks extra que descuadren la
// reubicación del runner); el engine lo copia a **Chip** con `res::load` (DMA: Blitter/música).
INCBIN(abyss_img, "assets/amiga/sprites/abyss/abyss.bpl");
INCBIN(abyss_bob, "assets/amiga/sprites/abyss/bob.bpl");
INCBIN(abyss_mod, "assets/amiga/audio/testmod.p61");
INCBIN(abyss_pal, "assets/amiga/sprites/abyss/abyss.pal");

#define INCBIN_SIZE(name) \
	static_cast<eng::u32>(reinterpret_cast<const char*>(&incbin_##name##_end) - incbin_##name##_start)

namespace {

namespace comp = eng::graphics::composition;

/// Códigos que el runner interpreta para localizar fallos de init y el estado Ready.
constexpr eng::u32 kRunDetailBase = 0x00021300u;
constexpr eng::u32 kRunDetailReady = kRunDetailBase;
constexpr eng::u32 kRunDetailMemorySetupFailed = kRunDetailBase + 1u;
constexpr eng::u32 kRunDetailSceneSetupFailed = kRunDetailBase + 2u;
constexpr eng::u32 kRunDetailBitmapAssetFailed = kRunDetailBase + 3u;
constexpr eng::u32 kRunDetailBitmapCopyFailed = kRunDetailBase + 4u;
constexpr eng::u32 kRunDetailSpriteOrMusicAssetFailed = kRunDetailBase + 5u;

constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u8 kPlanes = 5u;
constexpr eng::u32 kBytesPerWord = sizeof(eng::u16);
constexpr eng::MemoryConfig kMemoryBudget {
	160u * eng::amiga::kBytesPerKiB, 8u * eng::amiga::kBytesPerKiB,
	4u * eng::amiga::kBytesPerKiB, 0u};
constexpr eng::u16 kPaletteColorCount = eng::kPaletteEntries;
constexpr eng::u16 kPaletteBytes = kPaletteColorCount * sizeof(eng::u16);
constexpr eng::u8 kSpriteFrameCount = 6u;
constexpr eng::u16 kBobCount = 8u;
constexpr eng::u16 kBobSpacing = 32u;
constexpr eng::u16 kGameBandTop = 200u;
constexpr eng::u16 kGameBandHeight = kHeight - kGameBandTop;
constexpr eng::u16 kHorizontalWaveModulo = 51u;
constexpr eng::u8 kVerticalWaveMask = 63u;
constexpr eng::u8 kHorizontalWaveMax = 32u;
constexpr eng::u8 kVerticalWaveMax = 40u;
constexpr eng::u8 kWaveFrequency = 2u;
constexpr eng::u8 kWaveAmplitudeScale = 2u;
constexpr eng::u8 kWaveVerticalScale = 2u;

constexpr eng::u16 kBobW = 32u;
constexpr eng::u16 kBobH = 16u;
constexpr eng::u16 kBobRightLimit = kWidth - kBobW - 16u;
constexpr eng::u32 kBobFrameStride = kBobH * kPlanes * kBytesPerWord *
					    (kBobW / (kBytesPerWord * 8u)) * kBytesPerWord; // 640 B

// Ondas generadas en compilación (`eng::ct_array`), aproximación entera de seno (Bhaskara I).
template <eng::u16 N, eng::u8 Max>
[[nodiscard]] constexpr eng::ct_array<eng::u8, N> make_wave() noexcept {
	return eng::ct_array<eng::u8, N> {[](eng::usize i) -> eng::u8 {
		const eng::s32 deg = static_cast<eng::s32>((360u * i) / N) % 360;
		const bool neg = deg > 180;
		const eng::s32 x = neg ? deg - 180 : deg;
		const eng::s32 p = x * (180 - x);
		const eng::s32 s = (4 * p * 256) / (40500 - p);
		const eng::s32 v = neg ? -s : s;
		return static_cast<eng::u8>(((v + 256) * Max) / 512);
	}};
}
constexpr auto kWaveY = make_wave<kVerticalWaveMask + 1u, kVerticalWaveMax>();
constexpr auto kWaveX = make_wave<kHorizontalWaveModulo, kHorizontalWaveMax>();

struct AbyssDemo {
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!app.configure_memory(kMemoryBudget)) {
			eng::debug::mark_failed(g_eng_run_status, kRunDetailMemorySetupFailed);
			return;
		}

		// --- Display de alto nivel: escena planar de 5 planos (interleaved) + paleta --------
		comp::SceneResources res = comp::planar(kWidth, kHeight, kPlanes);
		res.layout = comp::SceneLayout::Interleaved; // el bitmap abyss es interleaved
		const eng::u16* pal = reinterpret_cast<const eng::u16*>(abyss_pal);

		// El motor elige perfil y `BPLCON0` (sin `ocs_a500`/`0x5200` en el código de juego).
		if (!comp::compose(m_scene, app.device().memory_manager(), res,
				   eng::PaletteWords {pal, kPaletteColorCount})) {
			eng::debug::mark_failed(g_eng_run_status, kRunDetailSceneSetupFailed);
			return;
		}
		app.bind_scene(m_scene);
		if (!app.screen().valid()) {
			eng::debug::mark_failed(g_eng_run_status, kRunDetailSceneSetupFailed);
			return;
		}

		// --- Assets de juego por nombre (el engine copia a Chip y resuelve el dominio) ------
		m_assets.bind(app.device().memory_manager());
		const bool bitmap_added = m_assets.add_bitmap("abyss", reinterpret_cast<const eng::u8*>(abyss_img),
					       INCBIN_SIZE(abyss_img), kWidth, kHeight, kPlanes,
					       eng::graphics::PlaneLayout::Interleaved);
		if (!bitmap_added) {
			eng::debug::mark_failed(g_eng_run_status, kRunDetailBitmapAssetFailed);
			return;
		}
		const auto background = m_assets.bitmap("abyss");
		const bool background_queued = app.screen().bitmap(background, eng::Box {0, 0, kWidth, kHeight});
		if (!background_queued) {
			eng::debug::mark_failed(g_eng_run_status, kRunDetailBitmapCopyFailed);
			return;
		}
		app.present();
		const bool sprite_added = m_assets.add<eng::SpriteTag>("bob",
						  reinterpret_cast<const eng::u8*>(abyss_bob), INCBIN_SIZE(abyss_bob));
		const bool music_added = m_assets.add<eng::MusicTag>("mod",
						 reinterpret_cast<const eng::u8*>(abyss_mod), INCBIN_SIZE(abyss_mod));
		if (!sprite_added || !music_added) {
			eng::debug::mark_failed(g_eng_run_status, kRunDetailSpriteOrMusicAssetFailed);
			return;
		}

		// --- El objeto como asset de juego (geometría declarada; el sheet lo pone `Assets`) --
		eng::graphics::Bob desc {};
		desc.width = kBobW;
		desc.height = kBobH;
		desc.planes = kPlanes;
		desc.frame_count = kSpriteFrameCount;
		desc.frame_stride = kBobFrameStride;
		desc.layout = eng::graphics::BobLayout::Interleaved;
		desc.draw = eng::graphics::BobDraw::CookieCut;
		desc.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
		m_sprite = m_assets.sprite("bob", desc);

		// --- Música por la fachada de audio ------------------------------------------------
		// El engine resuelve **formato** y **buffer** (§4/§2) y **conduce** la música en su VBlank:
		// el juego solo la arranca por nombre.
		(void)app.audio().play_music(m_assets.music("mod"));

		app.takeover();
		m_ready = true;
	}

	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
	}

	void render(auto& app) {
		auto s = app.screen();
		// Limpia la banda anterior y reparte ocho BOB de 32 px sin solaparlos.
		s.clear_box(eng::Box {0, kGameBandTop, kWidth, kGameBandHeight});
		// Desfase de onda **sin división por frame** (regla de coste ~cero en el bucle): se mantiene
		// el módulo 51 con un contador que envuelve, en vez de `app.frame() % 51u`.
		eng::u32 phase = m_phase51;
		if (++m_phase51 >= kHorizontalWaveModulo) {
			m_phase51 = 0u;
		}
		eng::u8 fi = 0u;
		for (eng::u16 i = 0u; i < kBobCount; ++i) {
			eng::u16 bob_x = static_cast<eng::u16>(i * kBobSpacing +
				static_cast<eng::u16>(kWaveX[phase]) * kWaveAmplitudeScale);
			// El desplazamiento del Blitter lee y escribe una palabra adicional al final.
			if (bob_x > kBobRightLimit) bob_x = kBobRightLimit;
			const eng::s16 x = static_cast<eng::s16>(bob_x);
			const eng::s16 y = static_cast<eng::s16>(
				kGameBandTop + static_cast<eng::u32>(
					kWaveY[((app.frame() + i) * kWaveFrequency) & kVerticalWaveMask]) /
					kWaveVerticalScale);
			const eng::u8 frame = fi;
			if (++phase >= kHorizontalWaveModulo) {
				phase = 0u;
			}
			if (++fi >= kSpriteFrameCount) {
				fi = 0u;
			}
			s.sprite(m_sprite, x, y, frame);
		}
		app.present();
		if (m_ready) {
			eng::debug::mark_ready(g_eng_run_status, kRunDetailReady);
		}
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

	eng::graphics::composition::Scene m_scene {};
	eng::graphics::Sprite m_sprite {};
	eng::Assets m_assets {};
	eng::u32 m_phase51 = 0u; ///< desfase de onda (módulo 51) sin división por frame
	bool m_ready = false;
};

} // namespace

ENG_APP_MAIN(AbyssDemo);
