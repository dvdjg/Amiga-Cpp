// ============================================================================
// Demo 214 — sprite de juego por la fachada `App`/`Screen`
// ============================================================================
//
// Gate del camino de **alto nivel** de dibujo de objetos: el juego no arma `BlitJob`s ni
// `BobTarget`; describe su sprite (`eng::graphics::Sprite`) y lo pinta con
// `app.screen().sprite(...)`. La geometría del destino la prepara la escena
// (`Scene::bob_target()` -> `DrawTarget`) y `app.present()` ejecuta el plan de Blitter.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/os/214_app_sprite --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/os/214_app_sprite --warp
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/api/game.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

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

namespace {

namespace scene = eng::graphics::composition;
namespace graphics = eng::graphics;

constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u8 kPlanes = 4u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kWidth / 8u) * kHeight;
constexpr scene::SceneResources kRes = scene::planar(kWidth, kHeight, kPlanes);

constexpr eng::Palette32 kPalette {{
	0x013, 0xf00, 0x0f0, 0xff0, 0x333, 0x333, 0x333, 0x333,
	0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

// Sprite 32x32 de 2 planos, planar con máscara (contrato de `bob.hpp`: fila = base+guarda).
constexpr eng::u16 kObjW = 32u;
constexpr eng::u16 kObjH = 32u;
constexpr eng::u8 kObjPlanes = 2u;
constexpr eng::u32 kObjRow = ((kObjW / 16u) + 1u) * 2u; // 6 (2 palabras + guarda)
constexpr eng::u32 kObjPlane = kObjH * kObjRow;         // 192
constexpr eng::u32 kObjData = kObjPlane * kObjPlanes;   // 384
constexpr eng::u32 kObjMask = kObjPlane;                // 192
constexpr eng::u32 kSheetBytes = kObjData + kObjMask;   // 576

struct AppSpriteDemo {
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);

		if (!scene::compose(m_scene, app.device().memory(), kRes, scene::ocs_a500,
				    scene::display(scene::kPal320x256, scene::kBplcon0_4Planes),
				    scene::palette(kPalette.words()))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021401u);
			return;
		}
		app.bind_scene(m_scene);

		// Mundo retenido: una capa de fondo con su cámara; el sprite sigue su scroll.
		auto fondo = app.world().add_layer("fondo", 0u);
		if (!fondo) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021404u);
			return;
		}
		fondo->camera().reset(eng::scene::WorldRect {0u, 0u, 2048u, 256u},
				      eng::Size2u {kWidth, kHeight});

		m_sheet = app.device().memory().chip.template allocate_block<eng::BobTag>(kSheetBytes, 16u);
		if (!m_sheet.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021402u);
			return;
		}
		build_sheet();

		// El juego describe **su** sprite: geometría + política. No ve planos ni strides.
		m_bob.sheet = m_sheet.view.data();
		m_bob.mask = m_sheet.view.data() + kObjData;
		m_bob.width = kObjW;
		m_bob.height = kObjH;
		m_bob.planes = kObjPlanes;
		m_bob.layout = graphics::BobLayout::Planar;
		m_bob.draw = graphics::BobDraw::CookieCut;
		m_bob.erase = graphics::BobErase::None; // se repinta el fondo entero cada frame
		m_sprite = graphics::Sprite {m_bob};

		// Segundo objeto por el **mundo retenido**: un actor BOB (el engine elige la
		// representación; aquí BOB) que `app.draw_world()` emite al plan.
		app.world().reset_actors(0, 60000u, 0u);
		eng::scene::ActorDesc ad {};
		ad.visual.kind = graphics::VisualKind::Bob;
		ad.visual.pixels = eng::Span<const eng::u16> {
			reinterpret_cast<const eng::u16*>(m_sheet.view.data()),
			static_cast<eng::usize>(kObjData / 2u)};
		ad.visual.mask = eng::Span<const eng::u16> {
			reinterpret_cast<const eng::u16*>(m_sheet.view.data() + kObjData),
			static_cast<eng::usize>(kObjMask / 2u)};
		ad.visual.w = kObjW;
		ad.visual.h = kObjH;
		ad.visual.bitplanes = kObjPlanes;
		ad.layout = graphics::BobLayout::Planar;
		ad.transparency = eng::scene::TransparencyMode::Mask1Bit;
		ad.background = eng::scene::BackgroundPolicy::None;
		ad.surface = 0u;
		ad.z = 10u;
		ad.preferred = eng::scene::Representation::Bob;
		m_actor = app.world().add_actor(ad);
		if (!m_actor.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021405u);
			return;
		}

		app.takeover(); // instala la copperlist del camino planar
		// READY se marca en el primer `render` (con el frame ya dibujado/commitido), para que
		// la captura del runner no caiga en un frame sin publicar.
	}

	void update(auto& app) {
		// La cámara de la capa de fondo avanza; el sprite se dibuja según su scroll.
		if (auto l = app.world().layer(0u)) {
			l->camera().set_scroll_x(static_cast<eng::u16>(app.frame() * 2u));
		}
	}

	void render(auto& app) {
		auto s = app.screen();
		s.clear(0u);
		const auto fondo = app.world().layer(0u);
		const eng::u16 scroll = fondo.valid() ? fondo->camera().scroll_x() : 0u;
		const eng::s16 x = static_cast<eng::s16>(16u + scroll % (kWidth - kObjW));
		const eng::s16 y = static_cast<eng::s16>(96u + ((app.frame() >> 1u) % 64u));
		s.sprite(m_sprite, x, y); // una llamada: la geometría del destino la pone el contexto
		// Actor del mundo retenido (segunda vía): se mueve y lo emite el planner del App.
		if (auto a = app.world().actor(m_actor)) {
			a->desc.x = static_cast<eng::s16>(16u + ((app.frame() * 3u) % (kWidth - kObjW)));
			a->desc.y = static_cast<eng::s16>(176u + ((app.frame() >> 1u) % 48u));
		}
		app.draw_world();
		app.present();
		// READY en el primer frame ya publicado (el runner captura tras el primer render).
		if (app.frame() < 2u) {
			eng::debug::mark_ready(g_eng_run_status, 0x00021400u);
		}
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

private:
	void build_sheet() {
		eng::u8* sheet = m_sheet.view.data();
		for (eng::u32 i = 0; i < kSheetBytes; ++i) {
			sheet[i] = 0u;
		}
		const auto put = [&](eng::u32 plane_off, eng::s16 dx, eng::s16 dy) {
			const eng::u16 x = static_cast<eng::u16>(dx + 16);
			const eng::u16 y = static_cast<eng::u16>(dy + 16);
			const eng::u32 row = plane_off + static_cast<eng::u32>(y) * kObjRow;
			sheet[row + (x >> 3u)] = static_cast<eng::u8>(sheet[row + (x >> 3u)] |
								      (0x80u >> (x & 7u)));
		};
		for (eng::s16 dy = -16; dy < 16; ++dy) {
			for (eng::s16 dx = -16; dx < 16; ++dx) {
				const eng::s16 d2 = static_cast<eng::s16>(dx * dx + dy * dy);
				if (d2 <= 14 * 14) {
					put(kObjData, dx, dy);       // máscara (cookie-cut)
					put(0u, dx, dy);             // plano 0 -> color 1
				}
				if (d2 <= 7 * 7) {
					put(kObjPlane, dx, dy);      // plano 1 -> color 3 (centro)
				}
			}
		}
	}

	scene::Scene m_scene {};
	eng::scene::ActorId m_actor {};
	eng::Block<eng::BobTag> m_sheet {};
	graphics::Bob m_bob {};
	graphics::Sprite m_sprite {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 4u * 1024u})) {
		eng::debug::mark_failed(g_eng_run_status, 0x00021403u);
		return 0;
	}
	AppSpriteDemo game {};
	eng::App app {backend, game};
	app.run(0xffffu);

	return 0;
}
