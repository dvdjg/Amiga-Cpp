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
//   - **Fine-scroll** del playfield (`BPLCON1`, tabla `sinus15`) que "menea" el logo.
//   - Música P61 por `app.audio()`.

#include <eng/api/api.hpp>
#include <eng/api/assets.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/telemetry.hpp>
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
constexpr eng::u16 kBobCount = 16u;
constexpr eng::u16 kBobSpacing = 16u;
constexpr eng::u16 kGameBandTop = 200u;
constexpr eng::u16 kGameBandHeight = kHeight - kGameBandTop;
constexpr eng::u16 kHorizontalWaveModulo = 51u;
constexpr eng::u8 kVerticalWaveMask = 63u;
constexpr eng::u8 kWaveFrequency = 2u;
constexpr eng::u8 kWaveAmplitudeScale = 2u;
constexpr eng::u8 kWaveVerticalScale = 2u;

constexpr eng::u16 kBobW = 32u;
constexpr eng::u16 kBobH = 16u;
constexpr eng::u32 kBobFrameStride = kBobH * kPlanes * kBytesPerWord *
					    (kBobW / (kBytesPerWord * 8u)) * kBytesPerWord; // 640 B

// Ondas generadas en compilación (`eng::ct_array`), aproximación entera de seno (Bhaskara I),
// como la referencia de `Amiga-Cpp-Optimizations`: reparto horizontal y seno vertical del
// enjambre de 16 BOBs. (El `main.c` original usa `sinus40`/`sinus32`; aquí se generan.)
constexpr eng::u8 kWaveY[kVerticalWaveMask + 1u] {
	20,22,24,26,28,30,31,33,34,36,37,38,39,39,40,40,
	40,40,39,39,38,37,36,35,34,32,30,29,27,25,23,21,
	19,17,15,13,11,10,8,6,5,4,3,2,1,1,0,0,
	0,0,1,1,2,3,4,6,7,9,10,12,14,16,18,20};
constexpr eng::u8 kWaveX[kHorizontalWaveModulo] {
	16,18,20,22,24,25,27,28,30,30,31,32,32,32,32,31,
	30,30,28,27,25,24,22,20,18,16,14,12,10,8,7,5,
	4,2,2,1,0,0,0,0,1,2,2,4,5,7,8,10,12,14,16};

// Tabla `sinus15` de `BartmanBasic/main.c` (amplitud 0..15): alimenta el **fine-scroll** del
// playfield por `BPLCON1`, que "menea" toda la imagen del fondo (logo abyss incluido, sobre el
// que van los BOBs).
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
		// **Fine-scroll** del playfield (tabla `sinus15`): desplaza TODO el fondo —imagen
		// "abyss" incluida— con `BPLCON1`, sin tocar los BOBs del plan del frame. Se aplica en
		// el VBlank (lo hace el engine) para no partir scanlines.
		app.set_fine_scroll(kFineScroll[frame & 63u]);
		// Limpia la banda de juego (blit `D=0` encolado en el plan, en orden con los sprites).
		s.clear_box(eng::Box {0, kGameBandTop, kWidth, kGameBandHeight});
		// Enjambre de 16 BOBs: reparto horizontal + seno vertical, con el frame de la hoja
		// ciclando 0..5 (cada frame colorea el glifo con un plano/planos distintos). Desfase
		// horizontal en módulo 51 con un contador que envuelve (sin `%` por frame).
		eng::u32 phase = m_phase51;
		if (++m_phase51 >= kHorizontalWaveModulo) {
			m_phase51 = 0u;
		}
		eng::u8 fi = 0u;
		for (eng::u16 i = 0u; i < kBobCount; ++i) {
			const eng::s16 x = static_cast<eng::s16>(
				static_cast<eng::u32>(i) * kBobSpacing +
				static_cast<eng::u32>(kWaveX[phase]) * kWaveAmplitudeScale);
			const eng::s16 y = static_cast<eng::s16>(
				kGameBandTop + static_cast<eng::u32>(
					kWaveY[((frame + i) * kWaveFrequency) & kVerticalWaveMask]) /
					kWaveVerticalScale);
			s.sprite(m_sprite, x, y, fi);
			if (++phase >= kHorizontalWaveModulo) {
				phase = 0u;
			}
			if (++fi >= kSpriteFrameCount) {
				fi = 0u;
			}
		}
		app.present();
		// --- Overlay de depuración de WinUAE (no aparece en las capturas de gameplay) -----
		// La original pinta estado por `debug_rect`/`debug_text`; aquí se usa el overlay del
		// engine (`app.debug()`), con el panel de telemetría estándar. Ver
		// `docs/tools/PROFILING_FROM_AGENT.md` y `eng/debug/telemetry.hpp`.
		if constexpr (requires { app.debug(); }) {
			auto& d = app.debug();
			d.clear();
			d.text(4, 4, "213 bartman abyss - fine-scroll + 16 bobs", 0x00ffffffu);
			eng::debug::Telemetry t = app.telemetry();
			t.frames = app.frame();
			t.fps_x100 = m_fps_x100;
			eng::debug::draw_telemetry(d, t, 4, 16, 8, 0x0000ff80u);
		}
		if (m_ready) {
			eng::debug::mark_ready(g_eng_run_status, kRunDetailReady);
		}
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

	eng::graphics::composition::Scene m_scene {};
	eng::graphics::Sprite m_sprite {};
	eng::Assets m_assets {};
	eng::u32 m_phase51 = 0u; ///< desfase de onda horizontal (módulo 51) sin división por frame
	eng::u32 m_vblank_msgs = 0u; ///< mensajes `VBlank` drenados del puerto del mini-SO
	eng::u32 m_fps_x100 = 0u; ///< fps*100 para el panel de telemetría (lo fija el runner/probe)
	bool m_ready = false;
};

} // namespace

ENG_APP_MAIN(AbyssDemo);
