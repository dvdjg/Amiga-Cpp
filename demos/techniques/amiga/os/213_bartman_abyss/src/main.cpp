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
constexpr eng::u8 kSpriteFrameCount = 6u;
// El original dibuja **un solo** BOB (`for (i = 0; i < 1; i++)`) en `(100, 0)` y deja que el
// **fine-scroll** del playfield "meneé" toda la escena (logo incluido). Las tablas `sinus40`/
// `sinus32` de `main.c` están sin usar; solo `sinus15` alimenta `BPLCON1`.
constexpr eng::u16 kBobX = 100u;
constexpr eng::u16 kBobY = 0u;
constexpr eng::u16 kGameBandTop = 200u;
constexpr eng::u16 kGameBandHeight = kHeight - kGameBandTop;

constexpr eng::u16 kBobW = 32u;
constexpr eng::u16 kBobH = 16u;
constexpr eng::u32 kBobFrameStride = kBobH * kPlanes * kBytesPerWord *
					    (kBobW / (kBytesPerWord * 8u)) * kBytesPerWord; // 640 B

// Tabla `sinus15` de `BartmanBasic/main.c` (amplitud 0..15): alimenta el **fine-scroll** del
// playfield por `BPLCON1`, que "menea" toda la imagen del fondo (logo abyss incluido) con el
// BOB, que va en el mismo bitmap.
constexpr eng::u8 kFineScroll[64] {
	8,8,9,10,10,11,12,12,
	13,13,14,14,14,15,15,15,
	15,15,15,15,14,14,14,13,
	13,12,12,11,10,10,9,8,
	8,7,6,5,5,4,3,3,
	2,2,1,1,1,0,0,0,
	0,0,0,0,1,1,1,2,
	2,3,3,4,5,5,6,7};

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
		// El `bob.bpl` original ya trae el layout que consume el motor: por cada fila de cada
		// plano, `[imagen `w/16` palabras][máscara `w/16` palabras]` (ver
		// `docs/reference/amiga/techniques/interleaved-bob-single-blit.md`). No se reempaqueta:
		// el engine lo dibuja con **un** blit cookie-cut `$CA` por BOB.
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
		// **Cola de mensajes del mini-SO** (`ENG_APP_MAIN` la deja lista): el latido de VBlank
		// del `App` publica `MsgType::VBlank` en `app.port()` y el juego lo drena aquí. Sin
		// drenar, el puerto se llena y descarta; un juego real consume además la entrada.
		eng::os::Msg m;
		while (app.port().pop(m)) {
			if (m.type == eng::os::MsgType::VBlank) {
				++m_vblank_msgs;
			}
		}
		eng::debug::mark_frame(g_eng_run_status, app.frame());
	}

	void render(auto& app) {
		auto s = app.screen();
		const eng::u32 frame = app.frame();
		// **Fine-scroll** del playfield (tabla `sinus15` del original): desplaza TODO el fondo
		// —imagen "abyss" y BOB, que comparten bitmap— con `BPLCON1`. Se aplica en el VBlank
		// (lo hace el engine) para no partir scanlines.
		app.set_fine_scroll(kFineScroll[frame & 63u]);
		// Limpia la caja del BOB (filas de sus 5 planos) antes de repintarlo: su caja es
		// pequeña, así que no se borra toda la banda.
		s.clear_box(eng::Box {static_cast<eng::s16>(kBobX), static_cast<eng::s16>(kGameBandTop + kBobY),
				      kBobW, kBobH});
		// Un solo BOB (frame 0) en `(100, 0)` de la banda, como el original. El movimiento
		// aparente lo da el fine-scroll del playfield.
		s.sprite(m_sprite, static_cast<eng::s16>(kBobX),
			 static_cast<eng::s16>(kGameBandTop + kBobY), 0u);
		app.present();
		if (m_ready) {
			eng::debug::mark_ready(g_eng_run_status, kRunDetailReady);
		}
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

	eng::graphics::composition::Scene m_scene {};
	eng::graphics::Sprite m_sprite {};
	eng::Assets m_assets {};
	eng::u32 m_vblank_msgs = 0u; ///< mensajes `VBlank` drenados del puerto del mini-SO
	bool m_ready = false;
};

} // namespace

ENG_APP_MAIN(AbyssDemo);
