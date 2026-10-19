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
// En el engine eso es `app.screen().circle(...)`: el contexto de dibujo resuelve el mapeo del
// playfield (aquí 1 plano), el recorte y la escritura. `main` declara el display de forma
// **imperativa** (qué se quiere ver) y `run_demo` pone el resto (memoria, `start`, bucle,
// telemetría del runner); la demo no incluye soporte de plataforma ni escribe `SysBase`.
// La figura se construye a razón de un radio por frame (el estado estable es estático, coste
// cero) y `eng::debug::draw_hud` rotula la franja inferior (nombre + versión + frames).
#include <eng/api/api.hpp>              // fachada: App/Screen/paleta + HUD de depuración
#include <eng/platform/amiga/entry.hpp> // run_demo: arranque de plataforma

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
	void init(auto& app) { (void)app; }
	void update(auto& app) { (void)app; }

	void render(auto& app) {
		auto screen = app.screen();
		if (m_radius == kFirstRadius) {
			screen.clear(0u); // fondo negro (COLOR00) la primera vez
		}
		// La misma figura del original (círculos concéntricos de radio par). El original la
		// pinta entera cada frame; aquí se **construye progresivamente** (un radio por frame)
		// para no meter ~150 campos de coste en un solo frame de debug: el estado estable es
		// la figura completa y el coste por frame vuelve a cero.
		if (m_radius <= kMaxRadius) {
			(void)screen.circle(kCenterX, kCenterY, m_radius, kInk);
			m_radius += kRadiusStep;
		}
		// HUD de depuración en una línea: nombre + versión + contador de frames, por palabras.
		// Se reescribe ~1 vez/segundo (no cada frame): el contador se ve avanzar y el coste
		// del formateo decimal queda fuera del bucle normal (norma de HUD barato del repo).
		const eng::u32 tick = app.frame() / 50u;
		if (tick != m_hud_tick) {
			m_hud_tick = tick;
			(void)eng::debug::draw_hud(screen, "CIRCLES", kVersion, app.frame(), kInk);
		}
		app.present();
		if (m_radius > kMaxRadius && !m_ready) {
			m_ready = true;
			eng::amiga::mark_ready(); // la figura está completa: el runner puede medir/capturar
		}
	}

private:
	eng::s16 m_radius = kFirstRadius;
	eng::u32 m_hud_tick = 0xffffffffu; ///< sentinela: fuerza el primer pintado del HUD
	bool m_ready = false;
};

} // namespace

int main() {
	// **Qué quiere ver el juego** (imperativo, sin conocer el chipset): 320×256×1, un buffer
	// y esta paleta. El engine decide el cómo (composición, copperlist, DMA).
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = kPlanes;
	display.buffers = kBuffers;
	display.palette = kPalette;
	CirclesDemo demo {};
	// Espera de VBlank **activa** (familia 212/DT-011): el bucle por senal pierde el
	// despertar bajo el SO vivo (y el guard 0xE212 lo rechaza); ver el doc 212.
	return eng::amiga::run_demo(demo, display, eng::amiga::VblankWait::Active);
}
