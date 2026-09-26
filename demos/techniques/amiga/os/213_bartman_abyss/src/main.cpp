// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/os/213_bartman_abyss --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/os/213_bartman_abyss --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --keep-running

// Demo 213 - "Bartman Abyss": port de la demo clásica de Bartman/vscode-amiga-debug al engine,
// escrita con la **fachada de juego** (`eng::App`/`Screen`, `Scene` + `effects::Gradient` +
// `app.audio()`). No hay `main`, `SysBase`, punteros crudos, registros ni `BlitJob`: el juego
// describe QUÉ quiere y el engine decide CÓMO. Ver `docs/engine/architecture/GAME_API_TWO_LEVELS.md`.
//
// Reproduce la demo original:
//   - Escena 320x256 de 5 planos con la imagen "abyss".
//   - Degradado de `COLOR00` por raster (efecto de alto nivel, sin listar `COLOR`).
//   - 16 BOB enmascarados (cookie-cut) movidos por senos; el juego solo pinta sprites.
//   - Música P61 por `app.audio()`.

#include <eng/api/api.hpp>
#include <eng/api/effects.hpp>
#include <eng/platform/amiga/entry.hpp>

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

constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u8 kPlanes = 5u;
constexpr eng::u32 kImageBytes = static_cast<eng::u32>(kWidth / 8u) * kPlanes * kHeight;

constexpr eng::u16 kBobW = 32u;
constexpr eng::u16 kBobH = 16u;
constexpr eng::u32 kBobFrameStride = kBobH * kPlanes * 2u * (kBobW / 16u) * 2u; // 640 B

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
constexpr auto kWaveY = make_wave<64u, 40u>();
constexpr auto kWaveX = make_wave<51u, 32u>();

struct AbyssDemo {
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!app.configure_memory({96u * 1024u, 8u * 1024u, 4u * 1024u, 0u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021301u);
			return;
		}

		// --- Display de alto nivel: escena planar de 5 planos (interleaved) + paleta --------
		comp::SceneResources res = comp::planar(kWidth, kHeight, kPlanes);
		res.layout = comp::SceneLayout::Interleaved; // el bitmap abyss es interleaved
		const eng::u16* pal = reinterpret_cast<const eng::u16*>(abyss_pal);
		if (!comp::compose(m_scene, app.device().memory(), res, comp::ocs_a500,
				   comp::display(res, 0x5200u), // 5 planos + COLOR
				   comp::palette(eng::PaletteWords {pal, 32u}, 0u, 32u))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021302u);
			return;
		}
		// Vuelca la imagen (una vez) al bitmap de la escena; a partir de aquí el juego dibuja
		// con primitivas de `Screen`, no tocando memoria.
		eng::Span<eng::u8> dst = m_scene.bitplanes().raw();
		const eng::u8* src = reinterpret_cast<const eng::u8*>(abyss_img);
		for (eng::u32 i = 0u; i < kImageBytes && i < dst.size(); ++i) {
			dst[i] = src[i];
		}
		app.bind_scene(m_scene);

		// --- Los assets a Chip (carga tipada: medio y alineación por dominio) ---------------
		m_bob_block = eng::res::load<eng::BobTag>(
			app.memory_manager(),
			eng::Span<const eng::u8> {reinterpret_cast<const eng::u8*>(abyss_bob),
						  INCBIN_SIZE(abyss_bob)});
		const eng::u32 mod_bytes = INCBIN_SIZE(abyss_mod);
		m_mod_block = eng::res::load<eng::MusicTag>(
			app.memory_manager(), eng::Span<const eng::u8> {
						      reinterpret_cast<const eng::u8*>(abyss_mod), mod_bytes});
		if (!m_bob_block.valid() || !m_mod_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021303u);
			return;
		}

		// --- El objeto como asset de juego -------------------------------------------------
		eng::graphics::Bob bob {};
		bob.sheet = m_bob_block.view.data();
		bob.width = kBobW;
		bob.height = kBobH;
		bob.planes = kPlanes;
		bob.frame_count = 6u;
		bob.frame_stride = kBobFrameStride;
		bob.layout = eng::graphics::BobLayout::Interleaved;
		bob.draw = eng::graphics::BobDraw::CookieCut;
		bob.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
		m_sprite = eng::graphics::Sprite {bob, INCBIN_SIZE(abyss_bob)};

		// --- Efecto de alto nivel: degradado de COLOR00 (líneas 0x41..0x4f) ----------------
		eng::u16 keys[15] {};
		for (eng::u8 i = 0u; i < 15u; ++i) {
			keys[i] = static_cast<eng::u16>(0x0111u * (i + 1u));
		}
		(void)m_sky.attach({.first_line = 0x41u, .band_height = 1u, .bands = 15u, .first = 0u},
				   eng::Span<const eng::u16> {keys, 15u}, false);

		// --- Música por la fachada de audio ------------------------------------------------
		const eng::Span<const eng::u8> mod {m_mod_block.view.data(), mod_bytes};
		eng::Span<eng::u8> sbuf {};
		if (eng::audio::p61_needs_sample_buffer(mod)) {
			const eng::u32 need = eng::audio::p61_sample_buffer_size(mod);
			m_sample = app.memory_manager().chip().template reserve<eng::AudioTag>(need, 4u);
			if (!m_sample.valid()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00021304u);
				return;
			}
			sbuf = eng::Span<eng::u8> {m_sample.view.data(), need};
		}
		(void)app.audio().play_music(eng::audio::MusicModule {mod},
					     eng::audio::MusicFormat::P61, sbuf);

		app.takeover();
		m_ready = true;
	}

	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
		app.audio().update_music(); // música P61: avanza una vez por frame
		m_sky.set_phase(static_cast<eng::u16>(app.frame()));
	}

	void render(auto& app) {
		auto s = app.screen();
		// Limpia la banda de juego (blit `D=0` encolado en el plan, en orden con los sprites) y
		// dibuja los 16 BOB; el juego solo pinta y limpia con primitivas de `Screen`.
		s.clear_box(eng::Box {0, 200, kWidth, 56u});
		eng::u32 phase = app.frame() % 51u;
		eng::u8 fi = 0u;
		for (eng::u16 i = 0u; i < 16u; ++i) {
			const eng::s16 x = static_cast<eng::s16>(
				static_cast<eng::u32>(i) * 16u + static_cast<eng::u32>(kWaveX[phase]) * 2u);
			const eng::s16 y = static_cast<eng::s16>(
				200u + static_cast<eng::u32>(kWaveY[((app.frame() + i) * 2u) & 63u]) / 2u);
			const eng::u8 frame = fi;
			if (++phase >= 51u) {
				phase = 0u;
			}
			if (++fi >= 6u) {
				fi = 0u;
			}
			s.sprite(m_sprite, x, y, frame);
		}
		m_sky.frame(m_scene); // aporta el degradado al plan del frame
		app.present();

		if (m_ready && app.frame() < 2u) {
			eng::debug::mark_ready(g_eng_run_status, 0x00021300u);
		}
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

	eng::graphics::composition::Scene m_scene {};
	eng::graphics::Sprite m_sprite {};
	eng::effects::Gradient m_sky {};
	eng::Block<eng::BobTag> m_bob_block {};
	eng::Block<eng::MusicTag> m_mod_block {};
	eng::Block<eng::AudioTag> m_sample {};
	bool m_ready = false;
};

} // namespace

ENG_APP_MAIN(AbyssDemo);
