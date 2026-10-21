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
/// La política usa el **máximo de los dos Fulls más recientes**: responde al coste actual
/// y no reduce la racha por un Full afortunadamente barato aislado. `observe()` mide y
/// clasifica fuera del bucle caliente.

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Estado y cálculo del pacing de render (una instancia por escena/presentador).
struct RenderPacing {
	/// Ciclos de un tick de presentación (1 campo PAL = 141 876 a 50 Hz).
	eng::u32 tick_cycles = 141876u;
	/// Coste de referencia: máximo del último y el penúltimo Full (ciclos).
	eng::u32 full_cycles = 0u;
	/// Dos muestras más recientes (ciclos), para filtrar solo un descenso aislado.
	eng::u32 previous_full_cycles = 0u;
	eng::u32 last_full_cycles = 0u;
	/// Techo entero de `full_cycles / tick_cycles` (0 = aún sin medida).
	eng::u8 full_fields_count = 0u;

	/// `true` si ya hay una medida de la pasada completa.
	[[nodiscard]] constexpr bool valid() const noexcept { return last_full_cycles != 0u; }

	/// Registra el coste medido de un Full y conserva el máximo de las dos últimas muestras.
	/// Coste: una división u32 (fuera del bucle caliente; se llama una vez por Full).
	void observe(eng::u32 cycles) noexcept {
		if (cycles == 0u) {
			return;
		}
		previous_full_cycles = last_full_cycles;
		last_full_cycles = cycles;
		full_cycles = (previous_full_cycles > cycles) ? previous_full_cycles : cycles;
		const eng::u32 fields = full_cycles / tick_cycles;
		const eng::u32 remainder = full_cycles % tick_cycles;
		const eng::u32 fields_ceil = fields + ((remainder != 0u) ? 1u : 0u);
		full_fields_count = static_cast<eng::u8>((fields_ceil > 255u) ? 255u : fields_ceil);
	}

	/// Nº de campos completos que cubre la medida (cualquier ciclo parcial cuenta como otro campo).
	[[nodiscard]] constexpr eng::u8 full_fields() const noexcept {
		return full_fields_count;
	}

	/// Fakes asequibles entre Fulls, acotados por `cap`.
	/// `K = max(0, campos − 2)` para mantener presentación ≥25 Hz; el caller limita a 1–3.
	[[nodiscard]] constexpr eng::u8 affordable_fakes(eng::u8 cap) const noexcept {
		const eng::u8 fields = full_fields();
		const eng::u8 k = (fields > 2u) ? static_cast<eng::u8>(fields - 2u) : 0u;
		return (k < cap) ? k : cap;
	}
};

} // namespace eng::graphics
