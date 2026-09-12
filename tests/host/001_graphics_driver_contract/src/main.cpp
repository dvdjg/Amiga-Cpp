// ============================================================================
// Test HOST-001: contratos de driver grafico (GraphicsDriver / DisplayDriver)
// ============================================================================
//
// Valida, en host (g++ nativo, sin WinUAE), que los drivers y compositores del
// engine cumplen los conceptos compile-time definidos en `driver.hpp`:
//
//   - eng::graphics::DisplayDriver<Driver, Backend> : takeover + install
//     (ciclo de instalacion del display: toma de control una sola vez + swap
//     de copperlist por frame).
//   - eng::graphics::GraphicsDriver<Driver, Backend> : refina DisplayDriver y
//     anade Driver::id + begin_frame/end_frame (solo drivers graficos completos,
//     no compositores de campo).
//
// La validacion es exclusivamente en COMPILE-TIME: los `static_assert` fuerzan
// que las expresiones `requires` se satisfagan por cada tipo concreto. No se
// instancia ni ejecuta ningun driver (no hay hardware ni RAM Amiga en juego),
// por eso puede correr en host de forma rapida y determinista.
//
// Backend de prueba: un `MockBackend` que solo declara los dos metodos que el
// contrato necesita (`takeover_display` e `install_copper_list`). El concepto se
// limita a comprobar que el driver puede invocarlos; no ejecuta nada.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/001_graphics_driver_contract   (solo este)
//   bash tools/run-host-tests.sh                                            (todos)

#include <cstdio>

#include <eng/graphics/driver.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/drivers/ehb_tile_scroll.hpp>
#include <eng/graphics/drivers/tile_scroll.hpp>
#include <eng/field/xlimited.hpp>
#include <eng/field/xlimited_scene.hpp>

namespace {

/// Backend de pega que solo expone los dos metodos del ciclo de display.
/// El contrato es agnostico del backend real (Amiga, Mega Drive, PC...): pide
/// que el driver sepa invocar `takeover_display` e `install_copper_list`. Aqui
/// no se ejecutan, solo se declaran como validas las llamadas.
struct MockBackend {
	void takeover_display(const eng::u16*) {}
	void install_copper_list(const eng::u16*) {}
};

using eng::DisplayDriver;
using eng::GraphicsDriver;

// 1) Driver grafico completo: StaticEhbScene tiene id + begin/end_frame.
static_assert(DisplayDriver<eng::graphics::drivers::StaticEhbScene, MockBackend>);
static_assert(GraphicsDriver<eng::graphics::drivers::StaticEhbScene, MockBackend>);

// 2) Driver de scroll (single/dual): solo ciclo de display (no tiene id/hooks).
static_assert(DisplayDriver<eng::graphics::drivers::EhbTileScrollScene, MockBackend>);
static_assert(DisplayDriver<eng::graphics::drivers::TileScrollScene<eng::graphics::drivers::TileScrollMode::ehb()>, MockBackend>);
static_assert(DisplayDriver<eng::graphics::drivers::TileScrollScene<eng::graphics::drivers::TileScrollMode::dual(3, 3)>, MockBackend>);

// 3) Compositores de campo (X-Limited/XYLimited): solo ciclo de display.
static_assert(DisplayDriver<eng::field::XlimitedDisplayComposer, MockBackend>);
static_assert(DisplayDriver<eng::field::XlimitedDualComposer, MockBackend>);

// 4) Escena X-Limited (wrapper) y sus constantes de scroll de ejemplo.
static_assert(DisplayDriver<eng::field::XlimitedScene<eng::field::ScrollConsts{16, 16, 256, 768, 3}>, MockBackend>);

} // namespace

int main() {
	// Todo el trabajo es la evaluacion de los static_assert de arriba: si este
	// binario compila y enlaza, los contratos se cumplen. Nada que ejecutar.
	std::printf("OK: contratos GraphicsDriver/DisplayDriver validados en compilacion.\n");
	return 0;
}
