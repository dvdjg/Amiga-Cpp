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

#include <eng/core/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/timeline.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::copper {

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
	u8 unhandled_intents = 0;   // intents que el scheduler base no materializa (ver emit_copper_intents)
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

	/// Carga un puntero BPLxPT desde una intención de display. Mantiene los dos
	/// MOVEs del puntero en el scheduler, también para los splits verticales.
	void move_bitplane_pointer(u8 plane, const void* address) {
		m_builder.move_bitplane_pointer(plane, address);
		m_report.display_moves = static_cast<u16>(m_report.display_moves + 2u);
	}

	/// Emite un WAIT de raster sin asociarlo a una paleta.
	void wait_line(u8 line) {
		m_builder.wait_line(line);
		m_timeline.reserve_wait(line);
		++m_report.waits;
	}

	/// Emite un WAIT a una linea PAL completa (0..311) manejando el overflow.
	///
	/// Delega en `ListBuilder::wait_line_pal` (port de `CopWaitSafe` de libgfx). El
	/// timeline solo reserva las lineas 0..255 (zona visible); las lineas del borde
	/// inferior/VBlank no compiten por H-BLANK y no se presupuestan aqui.
	void wait_line_safe(u16 line) {
		m_builder.wait_line_pal(line);
		if (line <= 255u) {
			m_timeline.reserve_wait(static_cast<u8>(line & 0xffu));
		}
		++m_report.waits;
	}

	/// Emite un WAIT a una posicion concreta (V y H): para "copper bars" a mitad de
	/// scanline.
	void wait_position(u8 line, u8 hpos) {
		m_builder.wait_position(line, hpos);
		m_timeline.reserve_wait(line);
		++m_report.waits;
	}

	/// Configura una pantalla de PLANOS EHB/plana genérica (paramétrica).
	///
	/// No asume tamaño: el llamador decide la geometría (DIW/DDF) y la anchura de
	/// fila (bytes_per_row); el scheduler solo traduce la intención a BPLCON,
	/// módulos y punteros BPL. Para una ventana 320x256 lowres PAL la demo pasa
	/// diwstrt=0x2c81, diwstop=0x2cc1, ddfstrt=0x0038, ddfstop=0x00d0.
	void emit_planes_display(
		u16 diwstrt, u16 diwstop, u16 ddfstrt, u16 ddfstop,
		u16 bytes_per_row, u16 bplcon0, u8 planes,
		const u8* bitplanes, u32 plane_bytes
	) {
		move(
			Register::DMACON,
			static_cast<u16>(DmaSetClear | DmaMaster | DmaCopper | DmaBitplane)
		);

		move(Register::BPLCON0, bplcon0);            // BPU + bits modo (EHB/HAM/DPF…)
		move(Register::BPLCON1, 0x0000);
		move(Register::BPLCON2, 0x0000);
		move(Register::BPL1MOD, 0x0000);
		move(Register::BPL2MOD, 0x0000);
		move(Register::DIWSTRT, diwstrt);
		move(Register::DIWSTOP, diwstop);
		move(Register::DDFSTRT, ddfstrt);
		move(Register::DDFSTOP, ddfstop);

		for (u8 plane = 0; plane < planes; ++plane) {
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

	/// Emite una lista de `CopperIntent` (vocabulario portable de la escena).
	///
	/// Es el punto de entrada que materializa el vocabulario de `raster_intent.hpp`:
	/// la escena/efecto describe qué quiere (un cambio de paleta en una franja, un
	/// shift, un split...), y el scheduler lo traduce a WAIT/MOVE de su copperlist.
	///
	/// Contrato:
	///   - `intents` DEBE venir ordenado por `top` ascendente (el llamador garantiza
	///     el orden; aqui no hay heap para ordenar). Se emite cada franja en su linea.
	///   - Soportados: `PaletteLine` y `PaletteSpan`. Los de layout (`ShiftLines`,
	///     `BitplaneSplit`) los materializa `emit_copper_intents_full` (que conoce el
	///     display); `SpriteRearm`/`Priority` aún requieren contexto de canal/prioridad
	///     y se cuentan en `report().unhandled_intents`.
	void emit_copper_intents(const graphics::CopperIntent* intents, u8 count) {
		if (intents == nullptr) {
			return;
		}
		for (u8 i = 0; i < count; ++i) {
			emit_single_intent(intents[i], nullptr, 0, 0);
		}
	}

	/// Igual que `emit_copper_intents`, pero con el layout del display para
	/// materializar también `BitplaneSplit` (re-pointa los bitplanes a
	/// `intent.bitplanes`) y `ShiftLines` (re-pointa a `bitplane_base + shift_x`
	/// bytes). `plane_bytes` es el stride entre planos y `planes` cuántos re-pointar.
	void emit_copper_intents_full(
		const graphics::CopperIntent* intents, u8 count,
		const u8* bitplane_base, u32 plane_bytes, u8 planes
	) {
		if (intents == nullptr) {
			return;
		}
		for (u8 i = 0; i < count; ++i) {
			emit_single_intent(intents[i], bitplane_base, plane_bytes, planes);
		}
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
	/// Materializa UNA intent. `bitplane_base != nullptr` habilita los intents de
	/// layout (BitplaneSplit/ShiftLines); si es null, se marcan como sin manejar.
	void emit_single_intent(
		const graphics::CopperIntent& intent, const u8* bitplane_base, u32 plane_bytes, u8 planes
	) {
		switch (intent.kind) {
			case graphics::CopperIntentKind::PaletteLine:
				wait_line_safe(intent.top);
				if (intent.top <= 255u) {
					m_timeline.reserve_moves(static_cast<u8>(intent.top & 0xffu), intent.count);
				}
				emit_palette(intent.colors, intent.first, intent.count);
				break;
			case graphics::CopperIntentKind::PaletteSpan:
				wait_position(static_cast<u8>(intent.top & 0xffu), static_cast<u8>(intent.hpos & 0xfeu));
				if (intent.top <= 255u) {
					m_timeline.reserve_moves(static_cast<u8>(intent.top & 0xffu), intent.count);
				}
				emit_palette(intent.colors, intent.first, intent.count);
				break;
			case graphics::CopperIntentKind::BitplaneSplit:
				if (bitplane_base == nullptr || intent.bitplanes == nullptr) {
					m_report.unhandled_intents = static_cast<u8>(m_report.unhandled_intents + 1u);
					break;
				}
				wait_line_safe(intent.top);
				for (u8 p = 0; p < planes; ++p) {
					move_bitplane_pointer(p, intent.bitplanes + static_cast<u32>(p) * plane_bytes);
				}
				break;
			case graphics::CopperIntentKind::ShiftLines:
				if (bitplane_base == nullptr) {
					m_report.unhandled_intents = static_cast<u8>(m_report.unhandled_intents + 1u);
					break;
				}
				wait_line_safe(intent.top);
				for (u8 p = 0; p < planes; ++p) {
					const s32 offset = static_cast<s32>(static_cast<u32>(p) * plane_bytes) + static_cast<s32>(intent.shift_x);
					move_bitplane_pointer(p, bitplane_base + offset);
				}
				break;
			default:
				m_report.unhandled_intents = static_cast<u8>(m_report.unhandled_intents + 1u);
				break;
		}
	}

	ListBuilder m_builder {};
	Timeline m_timeline {};
	ScheduleReport m_report {};
};

} // namespace eng::copper
