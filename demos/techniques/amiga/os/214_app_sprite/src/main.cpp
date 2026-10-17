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
// orden por `z` lo resuelve el engine, no el juego; (4) que un fondo indexado se **pre-renderiza
// a planar** una vez (`c2p_1x1_naive`) y el Blitter copia la ventana visible cada frame con
// `screen.bitmap(asset, destino, palabra_origen, shift)` — sin re-materializar por CPU (DT-030).
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/os/214_app_sprite --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/os/214_app_sprite --warp
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/graphics/c2p.hpp>         // c2p_1x1_naive: fondo indexado -> planar (una vez)
#include <eng/platform/amiga/entry.hpp> // run_demo: plataforma/memoria/display/bucle

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
constexpr eng::u16 kBackdropWidth = 640u;
constexpr eng::u16 kBackdropHeight = kHeight;
constexpr eng::u32 kBackdropPlaneBytes = static_cast<eng::u32>(kBackdropWidth / 8u) * kBackdropHeight;
constexpr eng::u32 kBackdropBytes = kBackdropPlaneBytes * kPlanes;
eng::u8 g_backdrop[kBackdropWidth * kBackdropHeight] {};

void build_backdrop() {
	for (eng::u16 y = 0u; y < kBackdropHeight; ++y) {
		for (eng::u16 x = 0u; x < kBackdropWidth; ++x) {
			const bool ridge = y > 196u + ((x >> 5u) & 7u);
			const bool marker = (((x >> 4u) ^ (y >> 3u)) & 7u) == 0u;
			g_backdrop[static_cast<eng::u32>(y) * kBackdropWidth + x] =
				ridge ? static_cast<eng::u8>(1u + (marker ? 2u : 0u)) : 0u;
		}
	}
}

struct AppSpriteDemo {
	void init(auto& app) {
		// El fondo se **pre-renderiza a planar una vez** (c2p CPU) y cada frame el Blitter copia
		// solo la ventana visible (palabra de origen + shift fino): re-materializar 320x256 px
		// por CPU no cabe en un frame (DT-030).
		build_backdrop();
		m_planar = app.device().memory_manager().chip().template reserve<eng::PlaneTag>(kBackdropBytes, 16u);
		if (!m_planar.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		eng::graphics::c2p_1x1_naive(kBackdropWidth, kBackdropHeight, kPlanes, kBackdropPlaneBytes,
					     eng::ChunkyView {g_backdrop, sizeof(g_backdrop)},
					     eng::PlaneBytes {m_planar.view.data(), kBackdropBytes});
		m_backdrop.planes = m_planar.mem_view();
		m_backdrop.width = kBackdropWidth;
		m_backdrop.height = kBackdropHeight;
		m_backdrop.row_bytes = static_cast<eng::u16>(kBackdropWidth / 8u);
		m_backdrop.plane_count = kPlanes;
		m_backdrop.layout = graphics::PlaneLayout::Contiguous;
		m_backdrop.plane_step = kBackdropPlaneBytes;

		m_sheet = app.device().memory_manager().chip().template reserve<eng::BobTag>(kSheetBytes, 16u);
		if (!m_sheet.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
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
		m_bob.erase = graphics::BobErase::None; // App materializa el bitmap antes de los BOBs
		m_sprite = graphics::Sprite {m_bob, kObjData, kObjMask};

		// Segundo objeto por el **mundo retenido**: el engine elige la representación (aquí
		// BOB). Usa el **mismo descriptor** que `screen.sprite` (`actor_desc_from_sprite`).
		app.world().reset_actors(0, 60000u, 0u);
		m_actor = app.world().add_actor(eng::scene::actor_desc_from_sprite(m_sprite, 0, 0, 10u));
		if (!m_actor.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}

		// READY se retrasa hasta que el display haya completado varios ciclos de doble buffer:
		// el primer render puede ocurrir antes de que la primera captura vea el buffer publicado.
	}

	void update(auto&) {
		// Scroll del fondo a 2 px/frame; al agotar la ventana extra del asset, vuelve al inicio.
		m_scroll = static_cast<eng::u16>((m_scroll + 2u) % (kBackdropWidth - kWidth));
	}

	void render(auto& app) {
		auto s = app.screen();
		// Ventana visible del fondo planar: palabra de origen (16 px) + shift fino del Blitter.
		const eng::u16 coarse = static_cast<eng::u16>(m_scroll & ~15u);
		const eng::u8 fine = static_cast<eng::u8>(m_scroll & 15u);
		const eng::u16* src_words =
			reinterpret_cast<const eng::u16*>(m_planar.view.data() + static_cast<eng::u32>(coarse) / 8u +
										(fine != 0u ? 2u : 0u)); // la palabra extra del barrel shift
		// Ventana visible (320 px) de un asset de 640: palabra de origen + shift fino del Blitter.
		// El engine deriva el modulo del stride real del asset.
		eng::graphics::ChipBitmapView<eng::PlaneTag> window = m_backdrop;
		window.width = kWidth;
		const eng::u16 source_x = static_cast<eng::u16>(m_scroll & ~15u);
		const eng::u8 source_shift = static_cast<eng::u8>(m_scroll & 15u);
		const bool copy_ok = s.bitmap(window, eng::Box {0, 0, kWidth, kHeight}, source_x, source_shift);
		if (!copy_ok) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		const eng::s16 x = static_cast<eng::s16>(16u + m_scroll % (kWidth - kObjW));
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
			eng::amiga::mark_ready();
		}
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
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_planar {};
	graphics::ChipBitmapView<eng::PlaneTag> m_backdrop {};
	eng::u16 m_scroll = 0u;
	graphics::Bob m_bob {};
	graphics::Sprite m_sprite {};
};

} // namespace

int main() {
	// Display imperativo (qué se quiere ver); `run_demo` pone memoria, `start`, bucle y
	// telemetría del runner. La demo no ve `SysBase`, soporte ni `g_eng_run_status`.
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = kPlanes;
	display.buffers = kRes.buffers;
	display.palette = kPalette;
	AppSpriteDemo game {};
	return eng::amiga::run_demo(game, display);
}
