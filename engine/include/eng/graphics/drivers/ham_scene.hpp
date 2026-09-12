#pragma once

/// \file ham_scene.hpp
/// Driver reutilizable para displays **HAM/planos con repeticion de filas**.
///
/// Nace del porte 1:1 de `effects/fire-rgb`: el efecto vive en un buffer *chunky*
/// que un C2P pasa a 4 bitplanes, y el display es HAM6 con **cuadruplicado de
/// lineas** por Copper (cada fila logica se repite 4 veces variando `BPL1MOD`/
/// `BPL2MOD`) y un `BPLCON1` alterno que desplaza la mitad de las lineas.
///
/// El driver encapsula esa parte de *frontera* (la que el engine debe abstraer):
///
/// - El efecto **no** calcula DIW/DDF ni punteros BPLx ni palabras de Copper.
/// - El efecto pide una superficie planar de N planos con una geometria y un
///   factor de repeticion; el driver reserva Chip RAM y construye la copperlist.
/// - El efecto rellena `bitplanes()` (o la pasa al C2P) y hace `install()`.
///
/// Todo es **parametrico** (ancho de fila, planos, BPLCON0, DIW/DDF, factor de
/// repeticion, paleta, linea base): no hay un caso "320x256 HAM6" cableado, de
/// modo que sirve igual para otras resoluciones/modos planares.

#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/driver.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::drivers {

/// Configuracion de una escena planar con repeticion de filas.
struct HamSceneConfig {
	/// Geometria del display (por defecto: 320x256 PAL lowres, DDF/DIW de la 080).
	u16 diwstrt = 0x2c81;
	u16 diwstop = 0x2cc1;
	u16 ddfstrt = 0x0038;
	u16 ddfstop = 0x00d0;
	u16 bytes_per_row = 40;

	/// Filas logicas del bitmap (antes de la repeticion).
	u16 rows = 256;

	/// Planos de bitplane (4 = 16 colores/HAM4, 6 = HAM6/EHB...).
	u8 planes = 6;

	/// `BPLCON0` (BPU + bits de modo). HAM6 = `0x7a00` (BPU=7, COLOR, HAM).
	u16 bplcon0 = 0x7a00;

	/// Linea VPOS del primer `WAIT` visible (`CopWaitSafe(Y(i))` del original).
	u16 first_line = 0x2c;

	/// Veces que se repite cada fila logica (1 = sin repeticion, 4 = cuadruplicado).
	u8 row_repeat = 4;

	/// Valor de `BPLCON1` en las lineas impares (0 en las pares). El original usa
	/// `0x0022` para su dither de HAM; 0 lo desactiva.
	u16 bplcon1_shift = 0x0022;

	/// Paleta opcional cargada al principio de la lista (HAM base o paleta plana).
	const u16* palette = nullptr;
	u8 palette_first = 0;
	u8 palette_count = 0;

	/// Si es true, reordena `BPLxPT` en orden inverso (bpl[N-1]..bpl[0]), como pide
	/// el original de fire-rgb para encajar con el orden en que el C2P escribe.
	bool reverse_plane_ptrs = false;

	/// Tamano del bloque Chip de la copperlist.
	u32 copper_bytes = 8192;
};

/// Superficie planar con display de repeticion de filas y copperlist propia.
///
/// No reserva memoria al sistema: toma bloques de Chip RAM de la `MemorySystem`
/// del backend (bitplanes y copperlist **deben** estar en Chip RAM para el DMA).
/// Para doble buffer se crean dos instancias (una por buffer), cada una con su
/// propia copperlist.
class HamScene {
public:
	static constexpr GraphicsDriverId id = GraphicsDriverId::HamScene;

	/// Reserva bitplanes + copperlist en Chip RAM y construye la lista.
	bool init(MemorySystem& memory, const HamSceneConfig& config) {
		m_config = config;

		const u32 plane_bytes = plane_bytes_for(config);
		const u32 bitplane_bytes = plane_bytes * static_cast<u32>(config.planes);
		// +16 de headroom por el peyote de alineacion de la arena (ver arena.hpp).
		m_bitplane_block = memory.chip.allocate(bitplane_bytes + 16u, 16);
		m_copper_block = memory.chip.allocate(config.copper_bytes, 16);
		m_bitplanes = static_cast<u8*>(m_bitplane_block.data);
		m_plane_bytes = plane_bytes;

		if (!m_bitplane_block.valid() || !m_copper_block.valid() || config.planes == 0u || plane_bytes == 0u) {
			m_ok = false;
			return false;
		}
		return rebuild_copper(config);
	}

	/// Reconstruye la copperlist sobre la memoria Chip ya reservada (misma geometria).
	bool rebuild_copper(const HamSceneConfig& config) {
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			m_ok = false;
			return false;
		}

		copper::Scheduler scheduler { m_copper_block };
		scheduler.emit_planes_display(
			config.diwstrt, config.diwstop, config.ddfstrt, config.ddfstop,
			config.bytes_per_row, config.bplcon0, config.planes, m_bitplanes, m_plane_bytes);

		if (config.reverse_plane_ptrs) {
			for (u8 plane = 0; plane < config.planes; ++plane) {
				scheduler.move_bitplane_pointer(
					plane, m_bitplanes + static_cast<u32>(config.planes - 1u - plane) * m_plane_bytes);
			}
		}

		if (config.palette != nullptr && config.palette_count != 0u) {
			scheduler.emit_palette(config.palette, config.palette_first, config.palette_count);
		}

		// Repeticion de filas: por cada fila logica se emiten `row_repeat` lineas de
		// display. `BPL1MOD/BPL2MOD` retroceden una fila (modulo negativo = -ancho de
		// fila) salvo en la ultima linea del grupo, que avanza (modulo 0). El original
		// alterna `BPLCON1` (dither fino).
		const u16 repeat = (config.row_repeat == 0u) ? 1u : config.row_repeat;
		const u16 row_back = static_cast<u16>(0u - config.bytes_per_row); // -bytes_per_row
		const u32 total_lines = static_cast<u32>(config.rows) * static_cast<u32>(repeat);
		for (u32 i = 0; i < total_lines; ++i) {
			scheduler.wait_line_safe(static_cast<u16>(config.first_line + i));
			const bool last_of_group = ((i % repeat) == (repeat - 1u));
			const u16 mod = last_of_group ? 0u : row_back;
			scheduler.move(copper::Register::BPL1MOD, mod);
			scheduler.move(copper::Register::BPL2MOD, mod);
			scheduler.move(copper::Register::BPLCON1, ((i & 1u) != 0u) ? config.bplcon1_shift : 0u);
		}

		scheduler.end();

		m_copper_words = scheduler.words_used();
		m_copper_words_ptr = scheduler.data();
		m_report = scheduler.report();
		m_ok = scheduler.ok();
		return m_ok;
	}

	/// Toma el control del display e instala la primera copperlist (una vez).
	template <typename Backend>
	void takeover(Backend& backend) const {
		if (m_ok && m_copper_words_ptr != nullptr) {
			backend.takeover_display(m_copper_words_ptr);
		}
	}

	/// Instala la copperlist del driver (swap de puntero; no toma el control).
	template <typename Backend>
	void install(Backend& backend) const {
		if (m_ok && m_copper_words_ptr != nullptr) {
			backend.install_copper_list(m_copper_words_ptr);
		}
	}

	/// Hooks de driver para encajar con el contrato `GraphicsDriver`.
	void begin_frame(RenderContext&) {}
	void end_frame(RenderContext&) {}

	constexpr bool ok() const { return m_ok; }
	constexpr u8* bitplanes() const { return m_bitplanes; }
	constexpr u8* plane(u8 index) const {
		return (index < m_config.planes) ? (m_bitplanes + static_cast<u32>(index) * m_plane_bytes) : nullptr;
	}
	constexpr u32 plane_bytes() const { return m_plane_bytes; }
	constexpr u8 plane_count() const { return m_config.planes; }
	constexpr u16 copper_words() const { return m_copper_words; }
	constexpr const u16* copper_words_ptr() const { return m_copper_words_ptr; }
	constexpr const copper::ScheduleReport& copper_report() const { return m_report; }

private:
	static constexpr u32 plane_bytes_for(const HamSceneConfig& config) {
		return static_cast<u32>(config.bytes_per_row) * static_cast<u32>(config.rows);
	}

	HamSceneConfig m_config {};
	MemoryBlock m_bitplane_block {};
	MemoryBlock m_copper_block {};
	u8* m_bitplanes = nullptr;
	u32 m_plane_bytes = 0;
	const u16* m_copper_words_ptr = nullptr;
	copper::ScheduleReport m_report {};
	u16 m_copper_words = 0;
	bool m_ok = false;
};

} // namespace eng::graphics::drivers
