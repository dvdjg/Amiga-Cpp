// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/122_circles --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/122_circles --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/122_circles --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/122_circles --keep-running

// ============================================================================
// Demo 122 — Círculos concéntricos (port de `demoscene-repo-orig/effects/circles`)
// ============================================================================
//
// Tutorial (§1.13 de AGENTS.md): dibujar una figura geométrica por la **fachada de juego**
// (`eng::App` + `eng::Screen`), sin planos, punteros, Blitter ni registros. El efecto es el
// port del original:
//
//   screen = NewBitmap(320, 256, 1, BM_CLEAR);          // 1 plano
//   SetupPlayfield(...); SetColor(0, 0x000); SetColor(1, 0xfff);
//   for (r = 2; r < 128; r += 2) Circle(screen, 0, 160, 128, r);
//
// En el engine eso es `app.screen().circle(...)`: el contexto de dibujo resuelve el mapeo
// del playfield (aquí 1 plano), el recorte y la escritura; el juego solo describe la figura
// y su color de paleta. La figura se construye a razón de un radio por frame (el estado
// estable es estático, coste cero) y el HUD lleva nombre, versión y contador de frames en
// la franja inferior (norma del repo).
#include <eng/api/api.hpp>          // fachada: App/Screen/paleta/run_status
#include <eng/debug/version.hpp>    // versión de demo (major.minor.build)
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

constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u8 kPlanes = 1u;
constexpr eng::u8 kBuffers = 1u; ///< un buffer: la figura se construye progresivamente
/// Centro y radio máximo como en el original (160,128,r<128); la franja inferior de 8 filas
/// es del HUD, así que el círculo mayor se queda en y=246.
constexpr eng::s16 kCenterX = 160;
constexpr eng::s16 kCenterY = 124;
constexpr eng::s16 kMaxRadius = 122;
constexpr eng::s16 kFirstRadius = 2; ///< primer radio (el original empieza en 2)
constexpr eng::s16 kRadiusStep = 2;  ///< paso par del original
constexpr eng::u8 kInk = 1u;         ///< blanco (COLOR01); el fondo es COLOR00 (negro)

/// Paleta 1 bpp: solo COLOR00/COLOR01 importan (negro/blanco del original).
constexpr eng::Palette32 kPalette {{
	0x000, 0xfff, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

constexpr eng::debug::Version kVersion {1u, 0u}; // el build lo inyecta build-demo.sh

struct CirclesDemo {
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		(void)app;
	}

	void update(auto& app) { (void)app; }

	void render(auto& app) {
		auto screen = app.screen();
		if (m_radius == kFirstRadius) {
			screen.clear(0u); // fondo negro (COLOR00) la primera vez
		}
		// La misma figura del original (círculos concéntricos de radio par). El original la
		// pinta entera cada frame; aquí se **construye progresivamente** (un radio por frame)
		// para no meter ~150 campos de coste en un solo frame de debug: el estado estable es
		// la figura completa y el coste por frame vuelve a cero. La primitiva de dominio evita
		// nombrar el plano (`Circle(screen, 0, x, y, r)` en el original).
		if (m_radius <= kMaxRadius) {
			(void)screen.circle(kCenterX, kCenterY, m_radius, kInk);
			m_radius = static_cast<eng::s16>(m_radius + kRadiusStep);
		}
		draw_hud(screen, app.frame());
		app.present();
		if (m_radius > kMaxRadius && !m_ready) {
			m_ready = true;
			eng::debug::mark_ready(g_eng_run_status, 0x00012200u);
		}
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

private:
	/// Franja inferior (1 bpp, texto por palabras): nombre + versión + contador de frames.
	/// `draw_text_words` compone dos glifos por palabra de 16 px (una escritura por fila y
	/// plano), sin bucle por píxel; el `x` va alineado a 16 y pisa el fondo con el patrón.
	void draw_hud(eng::Screen& screen, eng::u32 frame) {
		char label[24] {};
		const eng::u8 vlen = eng::debug::version_label(kVersion, label, sizeof(label));
		char line[40] = "CIRCLES ";
		eng::u8 n = 8u;
		for (eng::u8 i = 0u; i < vlen && n + 1u < sizeof(line); ++i) line[n++] = label[i];
		line[n++] = ' ';
		line[n++] = 'F';
		line[n++] = ' ';
		// Contador decimal sin stdlib (mismo criterio que `version_label`).
		char digits[12] {};
		eng::u8 d = 0u;
		eng::u32 v = frame;
		do {
			digits[d++] = static_cast<char>('0' + (v % 10u));
			v /= 10u;
		} while (v != 0u && d < sizeof(digits));
		while (d > 0u && n + 1u < sizeof(line)) line[n++] = digits[--d];
		line[n] = '\0';
		(void)screen.text_words(0, static_cast<eng::s16>(kHeight - 8u), line, kInk);
	}

	eng::s16 m_radius = kFirstRadius;
	bool m_ready = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	// Perfil de memoria del juego según el hardware detectado (Chip/Slow/Fast).
	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_game_memory()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00012201u);
		return 0;
	}
	CirclesDemo game {};
	eng::App app {backend, game, backend.memory_manager()};
	// El juego declara QUÉ display quiere (320x256x1, doble buffer); `start()` lo compone,
	// valida (planos, DMA, Chip) y lo instala. No hay registros ni copper en el juego.
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = kPlanes;
	display.buffers = kBuffers;
	display.palette = kPalette;
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00012202u);
		return 0;
	}
	app.run(0xffffu);

	return 0;
}
