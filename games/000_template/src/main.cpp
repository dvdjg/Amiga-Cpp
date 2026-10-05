// Plantilla de juego del engine (Amiga): un juego 2D cerrado **sin bajar al metal**.
//
// La lógica de juego usa solo la fachada (`eng/api/api.hpp`): `App`, `Screen`, `Anim`, input. No
// hay registros, `FramePlan`, `SceneResources`, `compose`, memoria ni copperlist.
//
//   bash ./tools/build/build-demo.sh games/000_template --debug
//   bash ./tools/run/run-demo.sh games/000_template

#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic, eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0,
};
}

namespace {

constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u16 kBoxPx = 32u;
/// La escena de título cede el control tras este nº de frames (3 s a 50 fps) o al pulsar fuego.
constexpr eng::u32 kTitleFrames = 150u;

/// **Escena de juego**: describe *qué* quiere (dibujar, mover, animar); el engine decide *cómo*.
struct PlayScene {
	eng::s16 m_x = 144;
	eng::s16 m_y = 112;
	// Animación de juego (sin assets): 4 frames a 6 frames de juego cada uno.
	static constexpr eng::u8 kFrames[] = {0u, 1u, 2u, 3u};
	static constexpr eng::u8 kDurations[] = {6u, 6u, 6u, 6u};
	eng::graphics::Anim m_anim {eng::Span<const eng::u8> {kFrames, 4u},
				    eng::Span<const eng::u8> {kDurations, 4u}};

	void update(auto& app) {
		// Entrada: la escena lee el estado del pad; no sondea hardware. Paso 16 = alineado a palabra
		// (el relleno `fill_box` es exacto y sin recorte de borde con x/w múltiplos de 16).
		auto& in = app.input();
		if (in.pad0.left) m_x -= 16;
		if (in.pad0.right) m_x += 16;
		if (in.pad0.up) m_y -= 16;
		if (in.pad0.down) m_y += 16;
		m_anim.update();
		eng::debug::mark_frame(g_eng_run_status, app.frame());
	}

	void render(auto& app) {
		auto s = app.screen();
		// Todo por el **plan del frame** (Blitter), en orden. `fill_box` es el relleno de color
		// **diferido** (D = A con A constante; sin tocar el rasterizador inmediato).
		s.fill_box(eng::Box {0, 0, kWidth, kHeight}, 1u);    // fondo azul
		s.fill_box(eng::Box {m_x, m_y, kBoxPx, kBoxPx}, 3u); // objeto rojo que se mueve con el pad
		app.present();
		eng::debug::mark_ready(g_eng_run_status, 0u);
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}
};
constexpr eng::u8 PlayScene::kFrames[];
constexpr eng::u8 PlayScene::kDurations[];

/// **Escena de título**: espera fuego (o un tiempo) y cede a la de juego con `set_scene` (§6).
struct TitleScene {
	PlayScene* play = nullptr;
	eng::u32 m_frames = 0u;

	void update(auto& app) {
		if (++m_frames >= kTitleFrames || app.input().pad0.fire) {
			app.set_scene(*play); // vacía la pila (exit) y empuja la escena de juego (enter)
		}
		eng::debug::mark_frame(g_eng_run_status, app.frame());
	}

	void render(auto& app) {
		auto s = app.screen();
		s.fill_box(eng::Box {0, 0, kWidth, kHeight}, 1u); // fondo azul
		s.fill_box(eng::Box {48, 112, 224u, 32u}, 4u);    // "logo" verde (sin fuente en esta ruta)
		app.present();
		eng::debug::mark_ready(g_eng_run_status, 0u);
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}
};

/// **El juego**: *composition root*. Empuja la escena de título; las escenas conducen el bucle.
struct TemplateGame {
	PlayScene m_play {};
	TitleScene m_title {};

	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_title.play = &m_play;
		app.push_scene(m_title);
	}

	void update(auto&) {} // no se llama mientras haya escena activa
	void render(auto&) {}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	// Memoria automática del perfil de hardware: el juego no elige pools.
	if (!backend.configure_game_memory()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00000101u);
		return 0;
	}

	// Display declarativo: geometría + paleta. Sin registros ni copperlist en el juego.
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 4u;
	display.palette.color[1] = 0x00fu; // azul
	display.palette.color[2] = 0xfffu; // blanco
	display.palette.color[3] = 0xf00u; // rojo
	display.palette.color[4] = 0x0f0u; // verde

	TemplateGame game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00000102u);
		return 0;
	}
	app.run(0xffffu);
	return 0;
}
