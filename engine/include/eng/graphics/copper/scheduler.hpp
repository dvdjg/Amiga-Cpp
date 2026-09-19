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
#include <eng/graphics/mode_switch.hpp>
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

	/// Construye desde una reserva tipada de copperlist (`Block<CopperTag>`).
	explicit Scheduler(eng::Block<eng::CopperTag> block)
		: m_builder(block) {}

	/// Emite un MOVE generico.
	///
	/// Se mantiene publico porque algunos drivers tempranos necesitan registrar
	/// movimientos concretos. A medida que aparezcan APIs mas expresivas, este metodo
	/// deberia usarse cada vez menos fuera del scheduler. Camino caliente: `always_inline`.
	__attribute__((always_inline)) inline void move(Register reg, u16 value) {
		m_builder.move(reg, value);
		++m_report.display_moves;
	}

	__attribute__((always_inline)) inline void move(u16 custom_register_offset, u16 value) {
		m_builder.move(custom_register_offset, value);
		++m_report.display_moves;
	}

	/// MOVE con escritura de 32 bits (1 store); útil en copperlists largas por línea.
	__attribute__((always_inline)) inline void move32(u16 custom_register_offset, u16 value) {
		m_builder.move32(custom_register_offset, value);
		++m_report.display_moves;
	}

	/// Emite un MOVE y devuelve un **handle** (índice de la word de instrucción) para
	/// parchear su dato despues con `patch_data`. Es la via para que un driver
	/// parchee registros por frame sin depender de offsets cableados (ver
	/// `copper::DoubleBuffer`).
	u16 move_at(Register reg, u16 value) {
		++m_report.display_moves;
		return m_builder.move_at(reg, value);
	}

	/// Igual que `move_at(Register, ...)` para registros custom por offset.
	u16 move_at(u16 custom_register_offset, u16 value) {
		++m_report.display_moves;
		return m_builder.move_at(custom_register_offset, value);
	}

	/// Sobrescribe el dato de un MOVE emitido con `move_at` (mismo handle).
	void patch_data(u16 instruction_word, u16 value) { m_builder.patch_data(instruction_word, value); }

	/// Sobrescribe el REGISTRO (destino) de un MOVE emitido con `move_at`.
	void patch_move_reg(u16 instruction_word, u16 reg) {
		m_builder.patch_move_reg(instruction_word, reg);
	}

	/// Sobrescribe la LINEA de un WAIT emitido antes (indice de su word0).
	void patch_wait(u16 instruction_word, u16 vpos, u8 hpos = 1) {
		m_builder.patch_wait(instruction_word, vpos, hpos);
	}

	/// Carga un puntero BPLxPT desde una intención de display. Mantiene los dos
	/// MOVEs del puntero en el scheduler, también para los splits verticales.
	void move_bitplane_pointer(u8 plane, eng::ChipAddress address) {
		m_builder.move_bitplane_pointer(plane, address);
		m_report.display_moves = static_cast<u16>(m_report.display_moves + 2u);
	}

	/// Emite un WAIT de raster sin asociarlo a una paleta. Camino caliente.
	__attribute__((always_inline)) inline void wait_line(u8 line) {
		m_builder.wait_line(line);
		m_timeline.reserve_wait(line);
		++m_report.waits;
	}

	/// Emite un WAIT a una linea PAL completa (0..311) manejando el overflow.
	///
	/// Delega en `ListBuilder::wait_line_pal` (port de `CopWaitSafe` de libgfx). El
	/// timeline solo reserva las lineas 0..255 (zona visible); las lineas del borde
	/// inferior/VBlank no compiten por H-BLANK y no se presupuestan aqui.
	__attribute__((always_inline)) inline void wait_line_safe(u16 line) {
		m_builder.wait_line_pal(line);
		if (line <= 255u) {
			m_timeline.reserve_wait(static_cast<u8>(line & 0xffu));
		}
		++m_report.waits;
	}

	/// Emite un WAIT a una posicion concreta (V y H): para "copper bars" a mitad de
	/// scanline. Camino caliente.
	__attribute__((always_inline)) inline void wait_position(u8 line, u8 hpos) {
		m_builder.wait_position(line, hpos);
		m_timeline.reserve_wait(line);
		++m_report.waits;
	}

	/// WAIT a una POSICION (V y H) de una linea PAL completa (0..311): port exacto de
	/// `CopWaitSafe` conservando la comparacion horizontal (`ListBuilder::wait_position_pal`).
	/// Para cambios de paleta por scanline mas alla de la linea 255 sin perder el H.
	__attribute__((always_inline)) inline void wait_position_safe(u16 line, u8 hpos) {
		m_builder.wait_position_pal(line, hpos);
		if (line <= 255u) {
			m_timeline.reserve_wait(static_cast<u8>(line & 0xffu));
		}
		++m_report.waits;
	}

	/// **Rearmado horizontal de un canal de sprite**: espera a `(vstart, hpos)` y
	/// reescribe `SPRxPOS`/`SPRxCTL`/`SPRxDATA`/`SPRxDATB` para redibujar el MISMO canal
	/// más a la derecha en la misma línea (multiplexado horizontal). NO toca `SPRxPT`.
	///
	/// Se emite **por línea** (el truco se repite en cada scanline del efecto). El
	/// llamador es responsable de la separación ≥24 px entre usos del mismo canal (carrera
	/// contra el haz). Ver `graphics::SpriteHorizontalRearm` y
	/// `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`.
	///
	/// Codificación (AHRM cap. 4): `SPRxPOS = (VSTART[7:0]<<8) | (HSTART[8:1])`;
	/// `SPRxCTL = (VSTOP[7:0]<<8) | (VSTART[8]<<3) | (VSTOP[8]<<2) | (HSTART[0]<<1) | attach`.
	/// Offsets: `SPRxPOS=0x140+x*8`, `SPRxCTL=0x142+x*8`, `SPRxDATA=0x144+x*8`,
	/// `SPRxDATB=0x146+x*8`.
	void emit_sprite_horizontal_rearm(const graphics::SpriteHorizontalRearm& r) {
		const u16 ch = r.channel & 7u;
		wait_position(static_cast<u8>(r.vstart & 0xffu), static_cast<u8>(r.hpos & 0xfeu));
		const u16 pos = static_cast<u16>(((r.vstart & 0xffu) << 8u) | ((r.hpos >> 1u) & 0xffu));
		const u16 ctl = static_cast<u16>(
			((r.vstop & 0xffu) << 8u) |
			(((r.vstart >> 8u) & 0x1u) << 3u) |
			(((r.vstop >> 8u) & 0x1u) << 2u) |
			((r.hpos & 0x1u) << 1u) |
			(r.attach ? 1u : 0u)
		);
		move(static_cast<u16>(0x140u + ch * 8u), pos);   // SPRxPOS
		move(static_cast<u16>(0x142u + ch * 8u), ctl);   // SPRxCTL
		move(static_cast<u16>(0x144u + ch * 8u), r.data_high); // SPRxDATA (arma)
		move(static_cast<u16>(0x146u + ch * 8u), r.data_low);  // SPRxDATB
	}

	/// Igual que `emit_sprite_horizontal_rearm(1)`, para una lista (misma línea).
	///
	/// La lista DEBE venir en `hpos` ascendente: el Copper ejecuta los WAIT en orden y
	/// un WAIT ya pasado espera al frame siguiente. Aquí se salta el siguiente rearm si
	/// su `hpos` es menor que el anterior (la lista la ordena el llamador).
	void emit_sprite_horizontal_rearms(const graphics::SpriteHorizontalRearm* list, u16 count) {
		if (list == nullptr) {
			return;
		}
		u16 last_hpos = 0;
		for (u16 i = 0; i < count; ++i) {
			if (i > 0u && list[i].hpos < last_hpos) {
				continue; // fuera de orden: se ignora (el llamador debe ordenar)
			}
			last_hpos = list[i].hpos;
			emit_sprite_horizontal_rearm(list[i]);
		}
	}

	/// **Reposiciona** un canal de sprite horizontalmente a media línea: espera a
	/// `(vline, hpos)` y escribe SOLO `SPRxPOS`. No toca CTL ni DATA.
	///
	/// Es el patrón real del **Risky Woods** / R-Type 2 (copperlist desensamblada): el
	/// canal se arma UNA vez por DMA (`SPRxPT` con POS/CTL/DATA válidos, VSTART/VSTOP que
	/// cubren la línea) y el Copper **solo mueve `SPRxPOS`** repetidamente mientras el haz
	/// barre; el armado del sprite persiste y el patrón (de 64 px) se repite. Es más
	/// barato que `emit_sprite_horizontal_rearm` (1 MOVE en vez de 4) y es el que cabe en
	/// el presupuesto de una línea completa.
	///
	/// `vline` es la línea del efecto y `hpos` la posición horizontal (low-res px, mismo
	/// valor que el HSTART de `SPRxPOS`). Al final de la línea hay que **resetear** el
	/// canal a la izquierda (otra llamada con el `hpos` inicial) antes de la siguiente.
	void emit_sprite_horizontal_reposition(u8 channel, u8 vline, u16 hpos) {
		const u16 ch = channel & 7u;
		wait_position(vline, static_cast<u8>(hpos & 0xfeu));
		move(static_cast<u16>(0x140u + ch * 8u),
		     static_cast<u16>((static_cast<u16>(vline) << 8u) | ((hpos >> 1u) & 0xffu)));
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
		eng::PlaneBytes bitplanes, u32 plane_bytes
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
			m_builder.move_bitplane_pointer(plane, bitplanes.address(static_cast<eng::s32>(plane) * static_cast<eng::s32>(plane_bytes)));
			m_report.display_moves += 2;
		}
	}

	/// Emite una zona de conmutación de geometría (`ModeSwitchZone`).
	///
	/// En el WAIT de `zone.top` reprograma, en orden canónico MI09:
	/// `BPLCON0` -> `DDFSTRT`/`DDFSTOP` -> `BPL1MOD`/`BPL2MOD` -> `BPLxPT` (par por
	/// plano) -> `BPLCON4`/`BPLCON1` (opcionales) -> paleta (opcional). Los registros
	/// de modo van DESPUÉS de los punteros (intercalarlos pierde el último plano).
	/// Devuelve `false` si la zona no es utilizable (DDF desalineado, planos fuera de
	/// rango o falta la base cuando `planes > 0`); en ese caso no emite nada.
	bool emit_mode_switch_zone(const graphics::ModeSwitchZone& zone) {
		if (!zone.ddf_aligned() || zone.planes > 6u) {
			return false;
		}
		if (zone.planes > 0u && zone.bitplanes.empty()) {
			return false;
		}
		wait_line_safe(zone.top);
		move(Register::BPLCON0, zone.bplcon0);
		move(Register::DDFSTRT, zone.ddfstrt);
		move(Register::DDFSTOP, zone.ddfstop);
		move(Register::BPL1MOD, zone.bpl1mod);
		move(Register::BPL2MOD, zone.bpl2mod);
		for (u8 p = 0; p < zone.planes; ++p) {
			move_bitplane_pointer(
				p,
				zone.bitplanes.address(
					static_cast<eng::s32>(static_cast<eng::u32>(p) * zone.plane_bytes)
				)
			);
		}
		// Las escrituras de modo que NO son geometría (BPLCON4, BPLCON1) van DESPUES
		// de los punteros: intercalarlas entre BPLCON0 y DDF/módulos/punteros hace
		// que el DMA pierda el ultimo plano del tramo (verificado en WinUAE-DBG con
		// 4/5 planos; ver OPTIMIZACION_GPP_68000.md). Orden canónico estricto:
		// BPLCON0 -> DDF -> módulos -> BPLxPT -> (modo/paleta).
		if (zone.set_bplcon4) {
			move(Register::BPLCON4, zone.bplcon4);
		}
		if (zone.set_bplcon1) {
			move(Register::BPLCON1, zone.bplcon1);
		}
		if (!zone.palette.empty() && zone.palette_colors != 0u) {
			emit_palette(zone.palette, 0, zone.palette_colors);
		}
		return true;
	}

	/// Emite una paleta base completa o parcial.
	///
	/// `colors` es la paleta de dominio (`PaletteWords`, con su tamaño). Los
	/// parametros `first/count` permiten que un efecto actualice solo un tramo sin
	/// que el llamador tenga que recalcular registros COLORxx. Camino caliente.
	__attribute__((always_inline)) inline void emit_palette(eng::PaletteWords colors, u8 first = 0, u8 count = 32) {
		if (colors.empty() || first >= 32) {
			return;
		}
		if (first + count > 32) {
			count = static_cast<u8>(32 - first);
		}
		if (static_cast<eng::u32>(first) + count > colors.size()) {
			count = static_cast<u8>(colors.size() - first);
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
	void emit_palette_zone(u8 line, eng::PaletteWords colors, u8 first = 0, u8 count = 32) {
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
	///
	/// NO `always_inline`: absorber el `switch` de `emit_single_intent` dentro del bucle
	/// expandia ~2,5 KB de codigo en `materialize` (medido en la 086). Es una llamada por
	/// intencion; el despacho se comparte.
	void emit_copper_intents(const graphics::CopperIntent* intents, u8 count) {
		if (intents == nullptr) {
			return;
		}
		for (u8 i = 0; i < count; ++i) {
			emit_single_intent(intents[i], {}, 0, 0);
		}
	}

	/// Ruta rápida para escenas dominadas por `PaletteLine` con **un solo color por
	/// línea** (el caso del degradado continuo: un COLORxx por scanline): evita el
	/// `switch` por elemento y el bucle de 1 de `emit_palette`, emitiendo WAIT+MOVE
	/// directamente. Cualquier otra forma cae a la ruta normal.
	///
	/// Motivo (medido en la 086): 288 intenciones costaban ~387k ciclos (~1.343/intención)
	/// por el despacho + `emit_palette`. Con el cielo a 64 bandas (4 colores por franja)
	/// esta ruta NO aplica y se usa la general.
	void emit_copper_intents_fast(const graphics::CopperIntent* intents, u16 count) {
		if (intents == nullptr) {
			return;
		}
		for (u16 i = 0; i < count; ++i) {
			const graphics::CopperIntent& it = intents[i];
			if (it.kind == graphics::CopperIntentKind::PaletteLine && it.count == 1u && !it.colors.empty()) {
				m_builder.wait_line_pal(it.top);
				if (it.top <= 255u) {
					m_timeline.reserve_wait(static_cast<u8>(it.top & 0xffu));
				}
				++m_report.waits;
				if (it.top <= 255u) {
					m_timeline.reserve_moves(static_cast<u8>(it.top & 0xffu), 1u);
				}
				m_builder.move(color_register(it.first), it.colors[it.first]);
				++m_report.palette_moves;
			} else {
				emit_single_intent(it, {}, 0, 0);
			}
		}
	}

	/// Igual que `emit_copper_intents`, pero con el layout del display para
	/// materializar también `BitplaneSplit` (re-pointa los bitplanes a
	/// `intent.bitplanes`) y `ShiftLines` (re-pointa a `bitplane_base + shift_x`
	/// bytes). `plane_bytes` es el stride entre planos y `planes` cuántos re-pointar.
	void emit_copper_intents_full(
		const graphics::CopperIntent* intents, u8 count,
		eng::PlaneBytes bitplane_base, u32 plane_bytes, u8 planes
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
	///
	/// NO `always_inline`: con el `switch` completo inlineado en el bucle de
	/// `materialize`, gcc expandia ~2,5 KB de codigo dentro del bucle (medido en la 086)
	/// en lugar de compartir el despacho. Aqui interesa una llamada por intencion.
	void emit_single_intent(
		const graphics::CopperIntent& intent, eng::PlaneBytes bitplane_base, u32 plane_bytes, u8 planes
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
				if (bitplane_base.empty() || intent.bitplanes.empty()) {
					m_report.unhandled_intents = static_cast<u8>(m_report.unhandled_intents + 1u);
					break;
				}
				wait_line_safe(intent.top);
				for (u8 p = 0; p < planes; ++p) {
					move_bitplane_pointer(p, intent.bitplanes.address(static_cast<eng::s32>(p) * static_cast<eng::s32>(plane_bytes)));
				}
				break;
			case graphics::CopperIntentKind::ShiftLines:
				if (bitplane_base.empty()) {
					m_report.unhandled_intents = static_cast<u8>(m_report.unhandled_intents + 1u);
					break;
				}
				wait_line_safe(intent.top);
				for (u8 p = 0; p < planes; ++p) {
					const s32 offset = static_cast<s32>(static_cast<u32>(p) * plane_bytes) + static_cast<s32>(intent.shift_x);
					move_bitplane_pointer(p, bitplane_base.address(offset));
				}
				break;
			case graphics::CopperIntentKind::SpriteRearm:
				if (intent.sprite_ptr == nullptr) {
					m_report.unhandled_intents = static_cast<u8>(m_report.unhandled_intents + 1u);
					break;
				}
				wait_line_safe(intent.top);
				{
					// Re-pointa el puntero de DATA del sprite (SPRxPT) para el rearm
					// vertical ("chasing the raster"). No toca SPRxPOS/CTL: el
					// `SpriteManager` los programa por separado.
					const uintptr addr = reinterpret_cast<uintptr>(intent.sprite_ptr);
					move(static_cast<u16>(0x120u + static_cast<u16>(intent.sprite_channel) * 4u), static_cast<u16>(addr >> 16));
					move(static_cast<u16>(0x122u + static_cast<u16>(intent.sprite_channel) * 4u), static_cast<u16>(addr & 0xffffu));
				}
				break;
			case graphics::CopperIntentKind::Priority:
				wait_line_safe(intent.top);
				// `shift_x` transporta el valor de BPLCON2 (bit 6 = sprites detrás
				// del playfield, 0x0040; 0 = sprites delante).
				move(Register::BPLCON2, static_cast<u16>(intent.shift_x));
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
