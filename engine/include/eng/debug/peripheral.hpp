#pragma once

/// \file peripheral.hpp
/// Periférico de depuración (WinUAE-DBG v2.x, e9k "Amiga Debug Peripherals").
///
/// Es el canal de telemetría "dentro del Amiga": el programa emulado escribe en
/// una región de memoria fija y el emulador captura los eventos (consola,
/// checkpoints, contador de ciclos, debug args, breakpoints auto-dirigidos).
/// Complementa a `run_status.hpp` (estado booleano/ligero) con trazas más ricas
/// para depurar comportamiento temporal y coste de ciclos.
///
/// Mapa de registros (base `0xB70000`):
///   0xB70000  byte write  -> carácter a consola (0, \n o \r flushean la línea)
///   0xB70004  long write  -> solicita breakpoint en esa dirección
///   0xB70008/0C/10 long write -> bases .text/.data/.bss (resolución de símbolos)
///   0xB70020  long write  -> checkpoint slot 0-63 (host registra ciclos+frame)
///   0xB7E900..E924 long read -> debug args 0-9 (host: winuae_debugperiph arg)
///   0xB7E928  long read  -> contador de ciclos de CPU
///
/// El host consulta todo con `monitor debugperiph` / `winuae_debugperiph`
/// (consola, checkpoints, args) y `winuae_side_read` (lecturas).
///
/// API homogénea entre modos gráficos: esta clase es el contrato de alto nivel.
/// Lo que falta para otros backends/máquinas (y que se debe añadir poco a poco):
///   - la base `0xB70000` es la del backend `amiga_minimal` (A500); otros
///     backends deben mapear estos registros a su transporte de depuración;
///   - una salida con formato (printf-like) para la consola;
///   - un búfer de consola legible por el programa (hoy es write-only y el host
///     la vuelca);
///   - más slots de checkpoint/counters configurables por el programa.

#include <eng/core/types.hpp>

namespace eng::debug {

/// Acceso al periférico de depuración (write-only desde el programa).
///
/// Todas las funciones son inline y de coste constante; no usan libc ni
/// asignación. El host (WinUAE-DBG) traduce estas escrituras a su log/consola.
class DebugPeripheral {
public:
	/// Escribe un carácter a la consola del host. `0`, `\n` o `\r` fuerzan el
	/// volcado de la línea acumulada.
	static inline void console_char(u8 c) {
		*reinterpret_cast<volatile u8*>(kBase + kConsoleOff) = c;
	}

	/// Escribe una cadena (sin terminador; escribe todos los caracteres).
	static inline void console_text(const char* text) {
		while (*text != 0) {
			console_char(static_cast<u8>(*text));
			++text;
		}
	}

	/// Escribe una línea completa y la flushea.
	static inline void console_line(const char* text) {
		console_text(text);
		console_char(0);
	}

	/// Registra un checkpoint (slot 0-63). El host anota ciclos + frame y los
	/// expone en `winuae_debugperiph checkpoints`.
	static inline void checkpoint(u8 slot) {
		*reinterpret_cast<volatile u32*>(kBase + kCheckpointOff) = static_cast<u32>(slot);
	}

	/// Lee el contador de ciclos de CPU del emulador.
	static inline u32 cycle_counter() {
		return *reinterpret_cast<volatile u32*>(kBase + kCyclesOff);
	}

	/// Lee un debug arg (0-9) previamente fijado por el host
	/// (`winuae_debugperiph arg <n> <valor>`).
	static inline u32 debug_arg(u8 n) {
		return *reinterpret_cast<volatile u32*>(kBase + kArgsOff + static_cast<u32>(n & 7u) * 4u);
	}

	/// Solicita un breakpoint del depurador en una dirección.
	static inline void request_break(u32 address) {
		*reinterpret_cast<volatile u32*>(kBase + kBreakOff) = address;
	}

	/// Reporta las bases runtime de .text/.data/.bss para resolución de símbolos.
	static inline void set_section_bases(u32 text_base, u32 data_base, u32 bss_base) {
		*reinterpret_cast<volatile u32*>(kBase + kTextOff) = text_base;
		*reinterpret_cast<volatile u32*>(kBase + kDataOff) = data_base;
		*reinterpret_cast<volatile u32*>(kBase + kBssOff) = bss_base;
	}

	static constexpr u32 base_address = 0xB70000u;

private:
	static constexpr u32 kBase = 0xB70000u;
	static constexpr u32 kConsoleOff = 0x0000u;
	static constexpr u32 kBreakOff = 0x0004u;
	static constexpr u32 kTextOff = 0x0008u;
	static constexpr u32 kDataOff = 0x000Cu;
	static constexpr u32 kBssOff = 0x0010u;
	static constexpr u32 kCheckpointOff = 0x0020u;
	static constexpr u32 kArgsOff = 0xE900u;
	static constexpr u32 kCyclesOff = 0xE928u;
};

} // namespace eng::debug
