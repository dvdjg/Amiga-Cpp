// Plantilla de juego del engine (Amiga): un juego 2D cerrado **sin bajar al metal**.
//
// La lógica de juego usa solo la fachada (`eng/api/api.hpp`): `App`, `Screen`, `Anim`, input. No
// hay registros, `FramePlan`, `SceneResources`, `compose`, memoria ni copperlist.
//
//   bash ./tools/build/build-demo.sh games/000_template --debug
//   bash ./tools/run/run-demo.sh games/000_template

#include <eng/api/api.hpp>
#include <eng/platform/amiga/entry.hpp>

namespace {

constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u16 kBoxPx = 32u;
/// La escena de título cede el control tras este nº de frames (3 s a 50 fps) o al pulsar fuego.
constexpr eng::u32 kTitleFrames = 150u;

/// Versión mostrada por el HUD; el build la inyecta `build-demo.sh`.
constexpr eng::debug::Version kVersion {1u, 0u};

/// Franja inferior reservada al HUD (norma del repo): los rellenos de juego no la pisan y el
/// helper la limpia una sola vez antes de rotular.
constexpr eng::u16 kHudH = 8u;

/// HUD de la plantilla: `draw_hud` no borra fondo, así que la franja (memoria Chip sin
/// inicializar) se limpia una vez con un `fill` inmediato; después se reescribe ~1 vez/segundo.
void draw_template_hud(eng::Screen& s, eng::u32 frame, eng::u32& tick, bool& cleared) {
	if (!cleared) {
		(void)s.fill(eng::Box {0, static_cast<eng::s16>(kHeight - kHudH), kWidth, kHudH}, 1u);
		cleared = true;
	}
	if (tick == frame / 50u) {
		return;
	}
	tick = frame / 50u;
	(void)eng::debug::draw_hud(s, "TEMPLATE", kVersion, frame, 2u);
}

/// **Escena de juego**: describe *qué* quiere (dibujar, mover, animar); el engine decide *cómo*.
struct PlayScene {
	eng::s16 m_x = 144;
	eng::s16 m_y = 112;
	// Animación de juego (sin assets): 4 frames a 6 frames de juego cada uno.
	static constexpr eng::u8 kFrames[] = {0u, 1u, 2u, 3u};
	static constexpr eng::u8 kDurations[] = {6u, 6u, 6u, 6u};
	eng::graphics::Anim m_anim {eng::Span<const eng::u8> {kFrames, 4u},
				    eng::Span<const eng::u8> {kDurations, 4u}};
	eng::u32 m_hud_tick = ~0u; ///< fuerza el primer HUD en el frame inicial
	bool m_hud_cleared = false;

	void update(auto& app) {
		// Entrada: la escena lee el estado del pad; no sondea hardware. Paso 16 = alineado a palabra
		// (el relleno `fill_box` es exacto y sin recorte de borde con x/w múltiplos de 16).
		auto& in = app.input();
		if (in.pad0.left) m_x -= 16;
		if (in.pad0.right) m_x += 16;
		if (in.pad0.up) m_y -= 16;
		if (in.pad0.down) m_y += 16;
		m_anim.update();
	}

	void render(auto& app) {
		auto s = app.screen();
		// Todo por el **plan del frame** (Blitter), en orden. `fill_box` es el relleno de color
		// **diferido** (D = A con A constante; sin tocar el rasterizador inmediato). La franja
		// del HUD queda fuera del fondo: la pinta (una vez) el helper del HUD.
		s.fill_box(eng::Box {0, 0, kWidth, static_cast<eng::u16>(kHeight - kHudH)}, 1u); // fondo azul
		s.fill_box(eng::Box {m_x, m_y, kBoxPx, kBoxPx}, 3u); // objeto rojo que se mueve con el pad
		draw_template_hud(s, app.frame(), m_hud_tick, m_hud_cleared);
		app.present();
		eng::amiga::mark_ready();
	}
};
constexpr eng::u8 PlayScene::kFrames[];
constexpr eng::u8 PlayScene::kDurations[];

/// **Escena de título**: espera fuego (o un tiempo) y cede a la de juego con `set_scene` (§6).
struct TitleScene {
	PlayScene* play = nullptr;
	eng::u32 m_frames = 0u;
	eng::u32 m_hud_tick = ~0u;
	bool m_hud_cleared = false;
	bool m_drawn = false;

	void update(auto& app) {
		if (++m_frames >= kTitleFrames || app.input().pad0.fire) {
			// Vacía la pila (exit) y empuja la escena de juego (enter); si no cabe, fallo de init.
			if (!app.set_scene(*play)) {
				eng::amiga::mark_failed(eng::debug::BootFail::Game);
			}
		}
	}

	void render(auto& app) {
		auto s = app.screen();
		// El título es **estático**: se dibuja una sola vez (el buffer persiste y no se
		// reencola el plan cada frame), así la captura no pilla un relleno a medias.
		if (!m_drawn) {
			m_drawn = true;
			// La franja del HUD queda fuera del fondo (la pinta `draw_template_hud`).
			s.fill_box(eng::Box {0, 0, kWidth, static_cast<eng::u16>(kHeight - kHudH)}, 1u); // fondo azul
			s.fill_box(eng::Box {48, 112, 224u, 32u}, 4u);    // "logo" verde (sin fuente en esta ruta)
			draw_template_hud(s, app.frame(), m_hud_tick, m_hud_cleared);
		} else if (m_hud_cleared && m_hud_tick != app.frame() / 50u) {
			// Solo el contador del HUD se refresca (~1 vez/segundo).
			draw_template_hud(s, app.frame(), m_hud_tick, m_hud_cleared);
		}
		app.present();
		eng::amiga::mark_ready();
	}
};

/// **El juego**: *composition root*. Empuja la escena de título; las escenas conducen el bucle.
struct TemplateGame {
	PlayScene m_play {};
	TitleScene m_title {};

	void init(auto& app) {
		m_title.play = &m_play;
		if (!app.push_scene(m_title)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
		}
	}

	void update(auto&) {} // no se llama mientras haya escena activa
	void render(auto&) {}
};

} // namespace

int main() {
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
	// `run_demo` configura la memoria del perfil de juego, arranca el display y conduce el bucle.
	return eng::amiga::run_demo(game, display);
}
