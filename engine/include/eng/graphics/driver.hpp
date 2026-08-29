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

/// Contrato compile-time de un driver.
///
/// El objetivo es tener polimorfismo sin vtables en las rutas calientes. Cada driver
/// concreto puede ser un tipo C++ con metodos conocidos en compilacion.
template <typename Driver>
concept GraphicsDriver = requires(Driver driver, RenderContext& context) {
	Driver::id;
	driver.begin_frame(context);
	driver.end_frame(context);
};

} // namespace eng
