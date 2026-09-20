#pragma once

/// \file timeline.hpp
/// Modelo didactico de coste por linea para Copper.
///
/// El Copper no es una CPU general: comparte tiempo con el barrido de video y con
/// el resto del chipset. Un efecto que "solo" escribe 32 colores puede ser barato
/// si ocurre fuera de la zona visible, o muy agresivo si pretende hacerlo en una
/// linea con pixels activos. La `Timeline` es la pieza para que el engine razone
/// sobre esto antes de generar la copperlist final.
///
/// **Construcción barata (clave de rendimiento).** Antes reservaba e inicializaba a cero
/// `2 × 256` bytes en el constructor, y como el `Scheduler` la posee por valor y se
/// construye cada frame en la pila (Chip RAM), eso costaba **~25k ciclos/frame** solo en
/// limpiar memoria. Ahora los contadores **no se inicializan**: se marcan las líneas
/// tocadas con un bitset de 32 bytes (lo único que se pone a cero), y `finish()`/las
/// consultas ignoran las líneas no tocadas. `reset()` limpia el bitset si se reutiliza.

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
	static constexpr u16 words = (line_count + 31u) / 32u; // 8 words = 256 bits

	/// Presupuesto conservador de MOVEs "seguros" por linea visible.
	static constexpr u8 visible_hblank_move_budget = 20;

	Timeline() = default;

	/// Limpia el estado (solo el bitset + informe; los contadores no se tocan).
	void reset() {
		for (u16 w = 0; w < words; ++w) {
			m_touched[w] = 0u;
		}
		m_report = {};
	}

	/// Reserva un WAIT en una linea.
	void reserve_wait(u8 line) {
		if (!mark_touched(line)) {
			m_waits_by_line[line] = 1u;
		} else {
			++m_waits_by_line[line];
		}
		++m_report.waits;
	}

	/// Reserva uno o varios MOVEs asociados a una linea.
	void reserve_moves(u8 line, u8 count) {
		if (!mark_touched(line)) {
			m_moves_by_line[line] = count; // primer toque: se fija
		} else {
			const u16 next = static_cast<u16>(m_moves_by_line[line]) + count;
			m_moves_by_line[line] = next > 255u ? 255u : static_cast<u8>(next);
		}
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

	/// Calcula el informe final (solo líneas tocadas).
	TimelineReport finish() {
		m_report.over_budget_lines = 0;
		m_report.heaviest_line = 0;
		m_report.heaviest_line_moves = 0;
		m_report.has_visible_spill = false;

		for (u16 i = 0; i < line_count; ++i) {
			const u8 line = static_cast<u8>(i);
			if (!touched(line)) {
				continue;
			}
			const u8 moves = m_moves_by_line[line];
			if (moves > m_report.heaviest_line_moves) {
				m_report.heaviest_line = line;
				m_report.heaviest_line_moves = moves;
			}
			if (is_visible(line) && moves > visible_hblank_move_budget) {
				++m_report.over_budget_lines;
				m_report.has_visible_spill = true;
			}
		}

		m_report.ok = true;
		return m_report;
	}

	constexpr const TimelineReport& report() const { return m_report; }
	u8 moves_on_line(u8 line) const { return touched(line) ? m_moves_by_line[line] : 0u; }
	u8 waits_on_line(u8 line) const { return touched(line) ? m_waits_by_line[line] : 0u; }

	/// Ventana visible PAL lowres usada por los primeros drivers.
	static constexpr bool is_visible(u8 line) {
		return line >= 0x2cu && line <= 0xf0u;
	}

private:
	/// Marca la linea como tocada; devuelve `true` si YA estaba tocada.
	__attribute__((always_inline)) inline bool mark_touched(u8 line) {
		const u16 w = static_cast<u16>(line >> 5u);
		const u32 b = static_cast<u32>(1u) << (line & 31u);
		const bool was = (m_touched[w] & b) != 0u;
		m_touched[w] |= b;
		return was;
	}
	__attribute__((always_inline)) inline bool touched(u8 line) const {
		return (m_touched[line >> 5u] & (static_cast<u32>(1u) << (line & 31u))) != 0u;
	}

	u8 m_moves_by_line[line_count]; // sin inicializar: válido solo si `touched`
	u8 m_waits_by_line[line_count]; ///< waits por línea (sin iniciar: válido si `touched`)
	u32 m_touched[words] {}; ///< bitset de líneas tocadas (32 B a cero en construcción, no 512)
	TimelineReport m_report {}; ///< informe acumulado en `finish()`
};

} // namespace eng::copper
