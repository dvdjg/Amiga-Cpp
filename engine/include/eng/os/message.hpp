#pragma once

/// \file message.hpp
/// **Vocabulario de mensajes del mini-SO** (`eng::os`): `MsgType` (contiguo desde 0), `Signal`
/// (máscara de bits) y `Msg` (unión trivial de payloads). Ver
/// `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md`.
///
/// El `Msg` es un **valor pequeño y copiable** (sin punteros propietarios): cabe en una entrada de
/// la cola y se copia en la ISR sin coste apreciable. Nada de `variant` ni constructores virtuales
/// (no son IRQ-aptos y arrastran código).
///
/// `MsgType` es **contiguo desde 0** para que el `switch`/la tabla de despacho sean un índice
/// directo, y se agrupa por rango (entrada / tiempo / E-S / app) para priorizar por comparación.

#include <eng/core/types.hpp>

namespace eng::os {

/// Tipo de mensaje. Contiguo desde 0 (índice de tabla) y agrupado por rango.
enum class MsgType : eng::u8 {
	None = 0,

	// Entrada (flancos / estado)
	KeyDown, KeyUp,
	MouseMove, MouseButton,
	Joystick, ///< joystick digital (1 botón)
	Gamepad,  ///< CD32 / multi-botón

	// Tiempo
	VBlank,
	Timer,

	// Sistema / E-S
	FileDone, FileError, DiskChange,

	// Aplicación
	User, Quit,

	COUNT, ///< nº de tipos (índice máximo + 1): tamaño de la tabla de despacho
};

/// Máscara de señales (modelo Exec): cada subsistema tiene su bit. La cola lleva los mensajes;
/// `signalled` es el aviso barato de «hay algo de este tipo».
enum Signal : eng::u32 {
	SigNone   = 0u,
	SigVBlank = 1u << 0,
	SigInput  = 1u << 1,
	SigFile   = 1u << 2,
	SigTimer  = 1u << 3,
	SigUser   = 1u << 4,
	SigQuit   = 1u << 5,
	SigHigh   = 1u << 6, ///< hay un mensaje de alta prioridad (entrada/quit)
	SigAll    = 0x7fu,
};

/// Payload de un mensaje. Unión **trivial** (sin constructores salvo el por defecto): se escribe el
/// campo del tipo que toca. Todos los miembros son copiables en la ISR.
union MsgPayload {
	struct { eng::u16 code; eng::u16 qual; } key;      ///< scancode Amiga + modificadores
	struct { eng::s16 x, y; eng::s8 dx, dy; eng::u8 buttons; } mouse;
	struct { eng::u8 port; eng::u8 dirs; eng::u8 fire; } joy; ///< direcciones (bits) + fuego
	struct { eng::u8 port; eng::u16 buttons; } pad;    ///< CD32: bitmask de botones
	struct { eng::u32 sequence; eng::u16 missed; } vblank; ///< secuencia + frames perdidos
	struct { eng::u16 id; } timer;
	struct { eng::u16 handle; eng::s32 result; eng::u8 op; eng::u32 cookie; } file;
	struct { eng::u32 code; eng::u32 a; eng::u32 b; } user;

	constexpr MsgPayload() noexcept : user {} {}
};

/// Mensaje del mini-SO.
struct Msg {
	MsgType type = MsgType::None;
	eng::u16 flags = 0u;      ///< flags por tipo (p. ej. coalescido/atrasado)
	eng::u32 time_stamp = 0u; ///< frames o ticks desde el arranque
	MsgPayload payload {};
};

static_assert(static_cast<eng::u8>(MsgType::COUNT) <= 32u, "MsgType cabe en una tabla de bits");
static_assert(sizeof(MsgPayload) <= 16u, "MsgPayload debe ser pequeno (copia barata en la ISR)");

} // namespace eng::os
