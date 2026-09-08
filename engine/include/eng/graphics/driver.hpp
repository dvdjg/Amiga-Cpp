#pragma once

/// \file driver.hpp
/// Contratos para drivers graficos.
///
/// En este engine un driver grafico no es solo "un modo de pantalla". Es una
/// estrategia completa de composicion: numero de bitplanes, reglas de paleta,
/// sprites disponibles, uso de Copper, uso de Blitter y presupuesto por frame.
///
/// Esta separacion es la que permite que la misma logica de juego pueda ejecutarse
/// sobre `EhbScene` para una aventura grafica o sobre `DualPlayfield` para un juego
/// con parallax real.

#include <eng/core/types.hpp>

namespace eng {

/// Identificadores de drivers previstos.
///
/// No todos estan implementados. La enumeracion existe para documentar desde el
/// principio las familias de composicion que queremos soportar.
enum class GraphicsDriverId : u8 {
	EhbScene,
	Standard5,
	Standard4,
	FakeDualPlayfield,
	DualPlayfield,
	SpriteBackdrop,
	CopperHeavy,
};

/// Contadores ligeros de frame.
///
/// En Amiga los presupuestos importan tanto como la imagen final. Estos campos se
/// iran alimentando desde blitter, copper y sprites para que las demos puedan fallar
/// antes de saturar el hardware.
struct FrameStats {
	u32 frame_index = 0;
	u16 blit_jobs = 0;
	u16 copper_patches = 0;
	u16 hardware_sprites = 0;
	u16 software_sprites = 0;
};

/// Contexto entregado a un driver durante el render.
struct RenderContext {
	FrameStats* stats = nullptr;
};

/// Contrato compile-time del ciclo de instalacion del display.
///
/// Todo driver/compositor que produzca una copperlist debe exponer DOS metodos con
/// responsabilidades separadas (ver `MinimalBackend::takeover_display`):
///
/// - `takeover(Backend&)`: toma el control completo del display UNA sola vez, al
///   iniciar la demo (antes del bucle de frames). Congela el sistema que AmigaDOS
///   dejo vivo (interrupciones, sprites, DMA) y arranca la primera copperlist
///   alineada al VBlank.
/// - `install(Backend&)`: swap de la copperlist activa. Solo actualiza el puntero
///   COP1LC; el Copper lo recarga solo al comienzo del proximo VBlank. NUNCA debe
///   disparar COPJMP1 (reiniciaria el Copper a media pantalla).
///
/// Ambos son templates sobre `Backend` por la misma razon que lo es `install` hoy:
/// permitir que cada plataforma futura exponga sus metodos sin introducir una
/// interfaz virtual en las rutas calientes. El concepto no fuerza firma concreta de
/// backend, solo que existan ambos metodos invocables con el backend dado.
template <typename Driver, typename Backend>
concept DisplayDriver = requires(Driver driver, Backend& backend) {
	driver.takeover(backend);
	driver.install(backend);
};

/// Contrato compile-time de un driver grafico completo.
///
/// Refina `DisplayDriver` (un driver grafico siempre produce y sirve un display)
/// y anade la identidad del driver mas los hooks de frame que el resto del ciclo
/// de composicion necesita:
///
/// - `Driver::id`: identificador de la familia de composicion.
/// - `begin_frame(context)` / `end_frame(context)`: hooks de frame con
///   `RenderContext`.
///
/// Como `DisplayDriver`, exige `takeover`/`install`. El objetivo sigue siendo
/// polimorfismo sin vtables en las rutas calientes: cada driver concreto es un
/// tipo C++ con metodos conocidos en compilacion.
template <typename Driver, typename Backend>
concept GraphicsDriver = DisplayDriver<Driver, Backend> &&
	requires(Driver driver, RenderContext& context) {
		Driver::id;
		driver.begin_frame(context);
		driver.end_frame(context);
	};

} // namespace eng
