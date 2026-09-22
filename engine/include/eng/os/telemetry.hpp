#pragma once

/// \file telemetry.hpp
/// **Telemetría de saturación del mini-SO** (`eng::os`): acumula los descartes por cola llena
/// (`overflows`), los VBlank pisados (`missed`) y las **marcas de agua** (profundidad máxima del
/// puerto, total y por prioridad). Sirve para **avisar de saturación sin fallo silencioso**: si el
/// productor (ISR) encola más de lo que el hilo principal drena, o si el bucle no consume el
/// VBlank, queda registrado aquí. Ver `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` y
/// `docs/guides/roadmap/ROADMAP_MINI_OS.md` (M9).

#include <eng/core/types.hpp>
#include <eng/os/port.hpp>

namespace eng::os {

/// Contadores de saturación del mini-SO. Se muestrea **una vez por frame** con `sample` (antes de
/// `take_vblank`, para capturar `missed`) o con `note_vblank` (con el `missed` del mensaje); usa
/// **uno** de los dos, no ambos. `reset` reinicia el periodo de medida.
struct IrqTelemetry {
	eng::u32 queue_overflows = 0u; ///< descartes acumulados (cola llena)
	eng::u32 vblank_missed = 0u;   ///< VBlank pisados acumulados
	eng::u16 peak_depth = 0u;      ///< profundidad máxima observada del puerto (marca de agua)
	eng::u16 peak_high = 0u;       ///< profundidad máxima observada de la cola `High`

	/// Muestrea **solo el puerto**: acumula los descartes (`overflows`, en delta) y actualiza las
	/// marcas de agua. Úsalo cuando el VBlank se contabiliza por otra vía (`note_vblank`); lo llama
	/// `MessagePumpGame` cada frame.
	template <eng::u16 N>
	void sample_port(const MsgPort<N>& port) noexcept {
		const eng::u16 of = port.queue.overflows();
		queue_overflows += static_cast<eng::u32>(static_cast<eng::u16>(of - m_last_overflows));
		m_last_overflows = of;
		observe(port);
	}

	/// Muestrea el puerto **y el latch**: como `sample_port` más los VBlank pisados
	/// (`latch.missed`). Llamar **antes** de `take_vblank`.
	template <eng::u16 N>
	void sample(const MsgPort<N>& port, const VBlankLatch& latch) noexcept {
		sample_port(port);
		vblank_missed += latch.missed;
	}

	/// Actualiza solo las marcas de agua (sin tocar overflows/missed). Útil si el llamador ya
	/// contabilizó el VBlank por otra vía.
	template <eng::u16 N>
	void observe(const MsgPort<N>& port) noexcept {
		const eng::u16 d = port.queue.depth_total();
		if (d > peak_depth) {
			peak_depth = d;
		}
		const eng::u16 h = port.queue.depth(MsgPrio::High);
		if (h > peak_high) {
			peak_high = h;
		}
	}

	/// Suma los VBlank pisados de un mensaje `VBlank` (`payload.vblank.missed`) si el llamador ya
	/// consumió el latch con `take_vblank` (en ese caso no uses `sample`).
	void note_vblank(eng::u16 missed) noexcept {
		vblank_missed += static_cast<eng::u32>(missed);
	}

	/// ¿Hubo saturación desde el último `reset` (descartes o VBlank pisados)?
	[[nodiscard]] bool saturated() const noexcept {
		return queue_overflows != 0u || vblank_missed != 0u;
	}

	/// Reinicia el periodo de medida: contadores y marcas de agua a cero.
	void reset() noexcept {
		queue_overflows = 0u;
		vblank_missed = 0u;
		peak_depth = 0u;
		peak_high = 0u;
		m_last_overflows = 0u;
	}

private:
	eng::u16 m_last_overflows = 0u; ///< último `overflows()` leído, para acumular el delta
};

} // namespace eng::os
