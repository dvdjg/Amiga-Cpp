#pragma once

/// \file render_pacing.hpp
/// **Pacing de render para presentación con frames intermedios**: convierte el coste
/// medido de la **pasada completa** (en ciclos de CPU) en el número de **Fake frames**
/// que conviene intercalar antes del siguiente Full, con dos objetivos de cadencia:
///
/// - **base** (Fulls por segundo) no debe bajar de `base_hz` (p. ej. 12,5 Hz para escenas
///   3D caras);
/// - la **presentación con interpolados** no debe bajar de `display_hz` (p. ej. 25 Hz).
///
/// Modelo (todo enteros):
///
/// - un tick de presentación dura `tick_cycles` (1 campo PAL = 141 876 ciclos a 50 Hz);
/// - el Full del frame ocupa `F = ceil(full / tick)` campos (su trabajo no se rebanada
///   todavía: bloquea su tick completo) y cada Fake ~1 campo;
/// - con `K` Fakes entre Fulls: ciclo = `F + K` campos → Fulls = `50 / (F + K)` Hz y
///   actualizaciones de pantalla = `50·(K+1) / (F + K)` Hz.
///
/// Regla: `K = max(0, ceil(F) − 2)` — el mínimo que lleva las actualizaciones a ≥25 Hz
/// cuando el Full ocupa ≥2 campos —, acotado por `cap` (el llamador lo limita además por
/// la tolerancia visual y el margen de la ventana). Con `F ≤ 2` no hacen falta Fakes;
/// con `F = 3` un Fake da ciclo de 4 campos = **12,5 Hz de base y 25 Hz de presentación**.
///
/// La medida se suaviza con una media móvil de 1/4 (el coste de un Full depende del
/// contenido y del sitio del framebuffer); `observe()` hace **una** división por Full
/// (fuera del bucle caliente) y el resto son comparaciones.

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Estado y cálculo del pacing de render (una instancia por escena/presentador).
struct RenderPacing {
	/// Ciclos de un tick de presentación (1 campo PAL = 141 876 a 50 Hz).
	eng::u32 tick_cycles = 141876u;
	/// Media móvil del coste de la pasada completa (ciclos). 0 = sin muestra.
	eng::u32 full_cycles = 0u;
	/// Coste del Full en campos con 8 bits de fracción (`full·256/tick`, redondeado).
	eng::u32 full_fields_q8 = 0u;

	/// `true` si ya hay una medida de la pasada completa.
	[[nodiscard]] constexpr bool valid() const noexcept { return full_cycles != 0u; }

	/// Registra el coste medido de una pasada completa (media móvil 1/4 + campo Q8).
	/// Coste: una división u32 (fuera del bucle caliente; se llama una vez por Full).
	void observe(eng::u32 cycles) noexcept {
		if (cycles == 0u) {
			return;
		}
		full_cycles = valid() ? ((3u * full_cycles + cycles) >> 2u) : cycles;
		full_fields_q8 = (full_cycles * 256u + tick_cycles / 2u) / tick_cycles;
	}

	/// Nº de campos (redondeado al más cercano) que ocupa la pasada completa medida.
	[[nodiscard]] constexpr eng::u8 full_fields() const noexcept {
		return static_cast<eng::u8>((full_fields_q8 + 128u) >> 8u);
	}

	/// Fakes asequibles entre Fulls, acotados por `cap`.
	/// `K = max(0, campos − 2)` (presentación ≥25 Hz); sin medida, 0 (Full por tick).
	[[nodiscard]] constexpr eng::u8 affordable_fakes(eng::u8 cap) const noexcept {
		const eng::u8 fields = full_fields();
		const eng::u8 k = (fields > 2u) ? static_cast<eng::u8>(fields - 2u) : 0u;
		return (k < cap) ? k : cap;
	}
};

} // namespace eng::graphics
