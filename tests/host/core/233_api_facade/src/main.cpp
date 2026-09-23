// ============================================================================
// Test HOST-233: fachada publica del engine (eng/api/api.hpp).
// ============================================================================
//
// Valida que un solo include (`<eng/api/api.hpp>`) expone las cabeceras estables de la
// API: bucle/contrato de juego, escena, dibujo, rasterizado, paleta, entrada, tareas de
// fondo y valores preparados de Blitter. No se instancia un backend (el backend va en
// main()); se comprueba el contrato `GameModule` con un backend ficticio y se usan los
// tipos publicos.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/233_api_facade

#include <cstdio>

#include <eng/api/api.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Backend ficticio: solo prueba que el contrato `GameModule` es visible por la fachada.
struct DummyBackend {};

struct DummyGame {
	void init(DummyBackend&, eng::GameContext&) {}
	void update(DummyBackend&, eng::GameContext&) {}
	void render(DummyBackend&, eng::GameContext&) {}
};

static_assert(eng::GameModule<DummyGame, DummyBackend>, "GameModule visible por la fachada");

} // namespace

int main() {
	// Rectangulo de UI.
	const eng::Box b {0, 0, 16u, 8u};
	check(b.contains(15, 7) && !b.contains(16, 7), "eng::Box por la fachada");

	// Plan de frame y valor preparado de Blitter.
	eng::graphics::FramePlan plan {};
	check(plan.ok() && plan.blit_job_count() == 0u, "FramePlan por la fachada");
	const eng::graphics::OrBob bob {};
	const eng::graphics::LineEor line {};
	const eng::graphics::C2p4 c2p {};
	check(bob.source == nullptr && line.bltsize == 0u && c2p.phase == 0u,
	      "valores preparados de Blitter por la fachada");

	// Rasterizador CPU por defecto (visible por la fachada).
	check(sizeof(eng::field::kCpuRaster) > 0u, "kCpuRaster por la fachada");

	// Entrada y tareas de fondo.
	eng::input::InputAggregator in {};
	check(!in.any(), "InputAggregator por la fachada");
	eng::task::BackgroundQueue q {};
	check(q.live_count() == 0u, "BackgroundQueue por la fachada");

	// Paleta.
	const eng::Palette32 pal {};
	check(pal.color[0] == 0u, "Palette32 por la fachada");

	if (failures == 0) {
		std::printf("OK: fachada publica (eng/api/api.hpp) expone la API estable.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
