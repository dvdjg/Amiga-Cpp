#pragma once

/// \file scheduler.hpp
/// Scheduler central minimo para Copper.
///
/// `ListBuilder` sabe escribir instrucciones `MOVE/WAIT`. Este `Scheduler` empieza
/// a ser una abstraccion de engine: recibe intenciones de varios sistemas y produce
/// una unica copperlist final con metricas de coste.
///
/// La version actual todavia no ordena ni resuelve conflictos entre muchos efectos;
/// eso llegara con `CopperTimeline`. Aun asi, mover el setup EHB y las zonas de
/// paleta aqui ya impide que cada demo/driver escriba registros a mano. Esa regla
/// sera vital cuando convivan:
///
/// - display setup del driver;
/// - color cycling;
/// - splits de parallax;
/// - fondos por sprites;
/// - cambios de prioridad;
/// - efectos raster de demoscene.

#include <amg/core/types.hpp>
#include <amg/graphics/copper/copper.hpp>
#include <amg/graphics/copper/timeline.hpp>

namespace amg::copper {

/// Informe de una copperlist generada.
///
/// Estos contadores son deliberadamente simples. Sirven para que las demos y
/// futuras herramientas UAF-R puedan decir "esto cabe pero es caro" antes de que
/// aparezcan corrupciones visuales dificiles de depurar.
struct ScheduleReport {
	u16 words_used = 0;
	u16 display_moves = 0;
	u16 palette_moves = 0;
	u16 waits = 0;
	u16 heavy_palette_zones = 0;
	u16 timeline_over_budget_lines = 0;
	u8 heaviest_line = 0;
	u8 heaviest_line_moves = 0;
	bool ok = false;
	bool has_visible_heavy_palette_zone = false;
	bool has_visible_timeline_spill = false;
};

/// Compositor central de Copper para las primeras escenas.
class Scheduler {
public:
	constexpr Scheduler() = default;

	explicit Scheduler(MemoryBlock block)
		: m_builder(block) {}

	/// Emite un MOVE generico.
	///
	/// Se mantiene publico porque algunos drivers tempranos necesitan registrar
	/// movimientos concretos. A medida que aparezcan APIs mas expresivas, este metodo
	/// deberia usarse cada vez menos fuera del scheduler.
	void move(Register reg, u16 value) {
		m_builder.move(reg, value);
		++m_report.display_moves;
	}

	void move(u16 custom_register_offset, u16 value) {
		m_builder.move(custom_register_offset, value);
		++m_report.display_moves;
	}

	/// Emite un WAIT de raster sin asociarlo a una paleta.
	void wait_line(u8 line) {
		m_builder.wait_line(line);
		m_timeline.reserve_wait(line);
		++m_report.waits;
	}

	/// Configura una pantalla EHB PAL lowres 320x256.
	///
	/// Este metodo encapsula las palabras magicas que antes vivian en la demo. El
	/// driver sigue decidiendo que quiere una escena EHB; el scheduler traduce esa
	/// intencion a BPLCON, DIW/DDF, modulos y punteros BPL.
	void emit_ehb_320x256_display(const u8* bitplanes, u32 plane_bytes) {
		move(
			Register::DMACON,
			static_cast<u16>(DmaSetClear | DmaMaster | DmaCopper | DmaBitplane)
		);

		move(Register::BPLCON0, 0x6200);
		move(Register::BPLCON1, 0x0000);
		move(Register::BPLCON2, 0x0000);
		move(Register::BPL1MOD, 0x0000);
		move(Register::BPL2MOD, 0x0000);
		move(Register::DIWSTRT, 0x2c81);
		move(Register::DIWSTOP, 0x2cc1);
		move(Register::DDFSTRT, 0x0038);
		move(Register::DDFSTOP, 0x00d0);

		for (u8 plane = 0; plane < 6; ++plane) {
			m_builder.move_bitplane_pointer(plane, bitplanes + static_cast<u32>(plane) * plane_bytes);
			m_report.display_moves += 2;
		}
	}

	/// Emite una paleta base completa o parcial.
	///
	/// `colors` apunta siempre a una paleta fisica completa de 32 entradas. Los
	/// parametros `first/count` permiten que un efecto futuro actualice solo un
	/// tramo sin que el llamador tenga que recalcular registros COLORxx.
	void emit_palette(const u16* colors, u8 first = 0, u8 count = 32) {
		if (colors == nullptr || first >= 32) {
			return;
		}
		if (first + count > 32) {
			count = static_cast<u8>(32 - first);
		}
		for (u8 i = 0; i < count; ++i) {
			m_builder.move(color_register(static_cast<u8>(first + i)), colors[first + i]);
			++m_report.palette_moves;
		}
	}

	/// Espera a una linea y aplica una paleta.
	///
	/// Un cambio de 32 colores en una linea visible es caro: no lo prohibimos porque
	/// muchas escenas EHB lo necesitan en zonas seleccionadas, pero dejamos un aviso
	/// medible para que el exportador y las pruebas puedan razonar sobre el coste.
	void emit_palette_zone(u8 line, const u16* colors, u8 first = 0, u8 count = 32) {
		wait_line(line);
		m_timeline.reserve_moves(line, count);
		if (count >= 16) {
			++m_report.heavy_palette_zones;
			if (line >= 0x2c && line <= 0xf0) {
				m_report.has_visible_heavy_palette_zone = true;
			}
		}
		emit_palette(colors, first, count);
	}

	/// Finaliza la lista y congela el informe.
	void end() {
		m_builder.end();
		const TimelineReport timeline = m_timeline.finish();
		m_report.words_used = m_builder.words_used();
		m_report.ok = m_builder.ok();
		m_report.timeline_over_budget_lines = timeline.over_budget_lines;
		m_report.heaviest_line = timeline.heaviest_line;
		m_report.heaviest_line_moves = timeline.heaviest_line_moves;
		m_report.has_visible_timeline_spill = timeline.has_visible_spill;
	}

	constexpr bool ok() const { return m_builder.ok(); }
	constexpr u16* data() const { return m_builder.data(); }
	constexpr u16 words_used() const { return m_builder.words_used(); }
	constexpr const ScheduleReport& report() const { return m_report; }

private:
	ListBuilder m_builder {};
	Timeline m_timeline {};
	ScheduleReport m_report {};
};

} // namespace amg::copper
