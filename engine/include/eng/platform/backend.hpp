#pragma once

/// \file backend.hpp
/// Contrato de BACKEND del engine (anillo 2, docs/engine/architecture/PLATFORM_LAYERS.md).
///
/// Un backend es un tipo que satisface estos concepts; `eng::Engine` y los drivers lo
/// usan por *duck typing*. No hay clase base ni jerarquía: un backend con menos
/// capacidades simplemente omite los servicios que no ofrece (p. ej. un backend host sin
/// Blitter ni IRQ de VBlank). Añadir una plataforma (Atari ST, Megadrive) es implementar
/// el contrato, no copiar el backend Amiga.
///
/// Los concepts se comprueban con un tipo-sonda para no fijar firmas concretas de funtor.
/// El contrato mínimo de un backend es `BackendCore`; el resto son capacidades opcionales
/// que el engine detecta con `if constexpr (requires { … })`.

#include <eng/core/types/types.hpp>
#include <eng/field/raster.hpp>

namespace eng {

/// Sonda usada para comprobar servicios que reciben un funtor tipado `(C&, u16)`.
struct BackendServiceProbe {
	static void run(BackendServiceProbe&, u16);
};

/// Contrato mínimo: arrancar y esperar el VBlank.
template <class B>
concept BackendCore = requires(B& b) {
	b.boot();
	b.wait_vblank();
};

/// Capacidad: ejecutar servicios en la IRQ de VBlank (latido del juego interrupt-driven).
template <class B>
concept InterruptBackend = requires(B& b, BackendServiceProbe probe) {
	{ b.set_vblank_service(&BackendServiceProbe::run, probe) };
	b.clear_vblank_service();
};

/// Capacidad: reportar la línea de raster actual (presupuesto del tick).
template <class B>
concept RasterLineBackend = requires(B& b) {
	{ b.current_raster_line() };
};

/// Capacidad: declarar las capacidades de rasterizado (Blitter/CPU, ancho de bus).
template <class B>
concept RasterCapsBackend = requires(const B& b) {
	{ b.raster_caps() };
};

/// Capacidad: exponer el sistema de memoria (arenas Chip/Slow/Frame).
template <class B>
concept MemoryBackend = requires(B& b) {
	{ b.memory() };
};

} // namespace eng
