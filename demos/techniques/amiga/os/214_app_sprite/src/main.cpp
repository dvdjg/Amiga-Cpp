// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/os/214_app_sprite --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/os/214_app_sprite --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/os/214_app_sprite --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/os/214_app_sprite --keep-running

// ============================================================================
// Demo 214 — sprite de juego por la fachada `App`/`Screen`
// ============================================================================
//
// Tutorial (§1.12 de AGENTS.md): como pintar un objeto de juego por la fachada de ALTO NIVEL,
// sin armar `BlitJob`s ni `BobTarget`. El juego describe su sprite
// (`eng::graphics::Sprite`) y lo pinta con `app.screen().sprite(...)`; la geometría del
// destino la prepara la escena (`Scene::bob_target()` -> `DrawTarget`) y `app.present()`
// ejecuta el plan de Blitter. La segunda vía (mundo retenido: `add_layer`/`add_actor` +
// `draw_world`) muestra que el MISMO descriptor (`actor_desc_from_sprite`) elige representación
// sin que el juego la fije: si mañana el sprite cabe en un canal de hardware, cambia solo.
//
// Qué mirar para aprender: (1) que NO aparece ningún `BlitJob`, puntero ni registro; (2) que la
// transición sprite<->BOB es transparente porque el `Visual` conserva su identidad; (3) que el
// orden por `z` lo resuelve el engine, no el juego.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/os/214_app_sprite --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/os/214_app_sprite --warp
// ============================================================================

#include <eng/api/api.hpp>
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
/// Dos buffers mantienen visible el último frame completo mientras la CPU/Blitter dibuja el siguiente.
constexpr scene::SceneResources make_scene_resources() {
	scene::SceneResources res = scene::planar(kWidth, kHeight, kPlanes);
	res.buffers = 2u;
	return res;
}
constexpr scene::SceneResources kRes = make_scene_resources();

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

		// Mundo retenido: una capa de fondo con su cámara; el sprite sigue su scroll.
		auto fondo = app.world().add_layer("fondo", 0u);
		if (!fondo) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021404u);
			return;
		}
		fondo->camera().reset(eng::scene::WorldRect {0u, 0u, 2048u, 256u},
				      eng::Size2u {kWidth, kHeight});

		m_sheet = app.device().memory_manager().chip().template reserve<eng::BobTag>(kSheetBytes, 16u);
		if (!m_sheet.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021402u);
			return;
		}
		build_sheet();

		// El juego describe **su** sprite: geometría + política. No ve planos ni strides.
		const eng::ChipView<eng::BobTag> sheet = m_sheet.mem_view_chip();
		m_bob.sheet = sheet;
		m_bob.mask = sheet.subview(kObjData, sheet.size() - kObjData);
		m_bob.width = kObjW;
		m_bob.height = kObjH;
		m_bob.planes = kObjPlanes;
		m_bob.layout = graphics::BobLayout::Planar;
		m_bob.draw = graphics::BobDraw::CookieCut;
		m_bob.erase = graphics::BobErase::None; // se repinta el fondo entero cada frame
		m_sprite = graphics::Sprite {m_bob, kObjData, kObjMask};

		// Segundo objeto por el **mundo retenido**: el engine elige la representación (aquí
		// BOB). Usa el **mismo descriptor** que `screen.sprite` (`actor_desc_from_sprite`).
		app.world().reset_actors(0, 60000u, 0u);
		m_actor = app.world().add_actor(eng::scene::actor_desc_from_sprite(m_sprite, 0, 0, 10u));
		if (!m_actor.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021405u);
			return;
		}

		// READY se retrasa hasta que el display haya completado varios ciclos de doble buffer:
		// el primer render puede ocurrir antes de que la primera captura vea el buffer publicado.
	}

	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
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
		// READY tras varios frames completos para que el runner capture un buffer ya visible.
		if (app.frame() == 4u) {
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
	const auto memory = eng::amiga::game_memory_custom(
		eng::MemoryConfig {96u * 1024u, 8u * 1024u, 4u * 1024u, 0u}, "Demo 214");
	if (!backend.configure_game_memory(memory)) {
		eng::debug::mark_failed(g_eng_run_status, 0x00021403u);
		return 0;
	}
	AppSpriteDemo game {};
	eng::App app {backend, game, backend.memory_manager()};
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = kPlanes;
	display.buffers = kRes.buffers;
	display.palette = kPalette;
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00021401u);
		return 0;
	}
	app.run(0xffffu);

	return 0;
}
