#pragma once

/// \file timeline.hpp
/// Modelo didactico de coste por linea para Copper.
///
/// El Copper no es una CPU general: comparte tiempo con el barrido de video y con
/// el resto del chipset. Un efecto que "solo" escribe 32 colores puede ser barato
/// si ocurre fuera de la zona visible, o muy agresivo si pretende hacerlo en una
/// linea con pixels activos. `CopperTimeline` es la primera pieza para que el
/// engine razone sobre esto antes de generar la copperlist final.
///
/// Esta version es deliberadamente conservadora y pequena:
///
/// - no reserva memoria;
/// - no usa STL;
/// - no intenta simular todos los ciclos exactos de Agnus;
/// - cuenta `WAIT` y `MOVE` por linea raster;
/// - marca lineas visibles con mas movimientos de los que cabrian comodamente en
///   H-BLANK.
///
/// Mas adelante la ajustaremos con datos del Hardware Reference Manual y del
/// profiler de WinUAE, pero el contrato de alto nivel ya queda fijado: los efectos
/// piden slots al timeline, y el scheduler decide como materializarlos.

#include <eng/core/types.hpp>

namespace eng::copper {

/// Resumen de presupuesto de una copperlist planificada.
struct TimelineReport {
	u16 waits = 0;
	u16 moves = 0;
	u16 visible_moves = 0;
	u16 over_budget_lines = 0;
	u8 heaviest_line = 0;
	u8 heaviest_line_moves = 0;
	bool ok = true;
	bool has_visible_spill = false;
};

/// Planificador de coste por linea.
class Timeline {
public:
	static constexpr u16 line_count = 256;

	/// Presupuesto conservador de MOVEs "seguros" por linea visible.
	///
	/// No significa que el Copper no pueda ejecutar mas instrucciones en una linea.
	/// Significa que, para efectos reutilizables y exportables, cualquier cosa por
	/// encima de este umbral debe quedar marcada y revisarse con captura/profiler.
	static constexpr u8 visible_hblank_move_budget = 20;

	constexpr Timeline() = default;

	/// Limpia el estado para construir un nuevo frame/lista.
	void reset() {
		for (u16 i = 0; i < line_count; ++i) {
			m_moves_by_line[i] = 0;
			m_waits_by_line[i] = 0;
		}
		m_report = {};
	}

	/// Reserva un WAIT en una linea.
	void reserve_wait(u8 line) {
		++m_waits_by_line[line];
		++m_report.waits;
	}

	/// Reserva uno o varios MOVEs asociados a una linea.
	void reserve_moves(u8 line, u8 count) {
		const u16 current = m_moves_by_line[line];
		const u16 next = current + count;
		m_moves_by_line[line] = next > 255u ? 255u : static_cast<u8>(next);
		m_report.moves = static_cast<u16>(m_report.moves + count);
		if (is_visible(line)) {
			m_report.visible_moves = static_cast<u16>(m_report.visible_moves + count);
		}
	}

	/// Reserva una zona de paleta: un WAIT y `color_count` MOVEs COLORxx.
	void reserve_palette_zone(u8 line, u8 color_count) {
		reserve_wait(line);
		reserve_moves(line, color_count);
	}

	/// Calcula el informe final.
	TimelineReport finish() {
		m_report.over_budget_lines = 0;
		m_report.heaviest_line = 0;
		m_report.heaviest_line_moves = 0;
		m_report.has_visible_spill = false;

		for (u16 i = 0; i < line_count; ++i) {
			const u8 moves = m_moves_by_line[i];
			if (moves > m_report.heaviest_line_moves) {
				m_report.heaviest_line = static_cast<u8>(i);
				m_report.heaviest_line_moves = moves;
			}
			if (is_visible(static_cast<u8>(i)) && moves > visible_hblank_move_budget) {
				++m_report.over_budget_lines;
				m_report.has_visible_spill = true;
			}
		}

		m_report.ok = true;
		return m_report;
	}

	constexpr const TimelineReport& report() const { return m_report; }
	constexpr u8 moves_on_line(u8 line) const { return m_moves_by_line[line]; }
	constexpr u8 waits_on_line(u8 line) const { return m_waits_by_line[line]; }

	/// Ventana visible PAL lowres usada por los primeros drivers.
	static constexpr bool is_visible(u8 line) {
		return line >= 0x2cu && line <= 0xf0u;
	}

private:
	u8 m_moves_by_line[line_count] {};
	u8 m_waits_by_line[line_count] {};
	TimelineReport m_report {};
};

} // namespace eng::copper
