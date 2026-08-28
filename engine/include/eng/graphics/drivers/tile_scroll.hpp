#pragma once

/// \file tile_scroll.hpp
/// Driver generico de scroll por tiles, independiente del modo grafico.
///
/// Este es el nucleo que sustituye a `ehb_tile_scroll.hpp`: la misma logica de
/// scroll (copperlist, `BPLCON1`, punteros `BPLxPT`, modulos y prefetch de tiles
/// por Blitter) se compila para cualquier modo:
///
/// - single playfield de 4, 5 o 6 bitplanes (6 = EHB);
/// - dual playfield 2+3 y 3+3, con scroll fino/coarse independiente por playfield.
///
/// La clase es un template sobre el modo (`TileScrollScene<Mode>`) para que todos
/// los limites (numero de planos, bytes por plano, tamaño de tile, modulos) sean
/// `constexpr` y auditables por demo, igual que en el resto del engine. El juego
/// solo decide una posicion de camara por playfield (`TileScrollInput`); el driver
/// traduce esa intencion a BPLCON0/1/2, DIW/DDF, modulos y punteros.
///
/// Convenciones de hardware usadas (HRM 3rd ed.):
///
/// - `BPLCON1` es un *delay*: valores mayores desplazan el playfield a la derecha.
///   En single playfield los dos nibbles son iguales; en dual, el nibble bajo es
///   PF1 y el alto PF2.
/// - En dual playfield los bitplanes impares (1,3,5) forman PF1 y los pares
///   (2,4,6) PF2; el color 0 de cada playfield es transparente. `BPL2PRI`
///   (bit 6 de `BPLCON2`) decide cual va delante.
/// - Con `DDFSTRT=$30` se fetcha un word extra a la izquierda de la ventana; cada
///   playfield apunta a su propio coarse `(scroll_x - 1) & ~15` y programa su fine
///   `(16 - fine) & 15`. Asi `display_start == scroll_x` es continuo en todo el
///   rango, sin salto en el cruce de tile (fine 15 -> 0).
///
/// Para efectos tipo RoboCod (un bitplane de fondo con scroll propio distinto a los
/// demas, conseguido por Blitter), `TileScrollInput::plane[]` permite anadir un
/// offset coarse por bitplane a los punteros `BPLxPT` sin cambiar la formula de los
/// demas planos.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/tilemap/tile_scroll.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::drivers {

/// Posicion de camara en pixels para un playfield.
struct ScrollPosition2 {
	u16 x = 0;
	u16 y = 0;
};

/// Offset coarse extra por bitplane (parallax tipo RoboCod).
///
/// Se suma al offset del playfield al calcular el puntero `BPLxPT`. Normalmente
/// `coarse_x_pixels` es multiplo de 16 (word-aligned); el fine scroll sigue siendo
/// por playfield, no por plano.
struct PlaneScrollOffset {
	s16 coarse_x_pixels = 0;
	s16 y_rows = 0;
};

/// Input de scroll de un frame.
///
/// `playfield[0]` es la camara de PF1 (planos impares), `playfield[1]` la de PF2
/// (pares) y se ignora en single playfield. `plane[i]` anade un offset coarse al
/// bitplane i (para parallax dentro de un playfield).
struct TileScrollInput {
	ScrollPosition2 playfield[2] {};
	PlaneScrollOffset plane[6] {};
};

/// Modo de display para la escena de scroll.
///
/// Tipo estructural de C++20/23, usable como parametro de template no de tipo:
/// `TileScrollScene<Mode>`.
struct TileScrollMode {
	u8 pf1_planes = 6;              ///< Planos de playfield 1 (impares 1,3,5).
	u8 pf2_planes = 0;              ///< Planos de playfield 2 (pares 2,4,6); 0 => single.
	bool foreground_is_pf2 = false; ///< Dual: PF2 delante (`PF2PRI`).

	constexpr u8 total_planes() const {
		return static_cast<u8>(pf1_planes + pf2_planes);
	}

	constexpr bool dual() const {
		return pf2_planes != 0;
	}

	constexpr u8 playfield_count() const {
		return dual() ? 2u : 1u;
	}

	/// Single playfield con `planes` bitplanes (4, 5 o 6).
	static constexpr TileScrollMode single(u8 planes) {
		return {planes, 0, false};
	}

	/// EHB: single playfield de 6 bitplanes.
	static constexpr TileScrollMode ehb() {
		return single(6);
	}

	/// Dual playfield con `front_planes` en el playfield delante (transparencia) y
	/// `back_planes` detras.
	///
	/// El hardware asigna PF1 = planos impares y PF2 = pares. Para totales impares
	/// (5 planos) PF1 se queda con un plano mas; la fabrica elige como delante el
	/// PF que tiene `front_planes` planos (en 2+3 es PF2, que tiene 2). En 3+3 los
	/// dos playfields tienen 3 planos y el frontal queda en PF1.
	static constexpr TileScrollMode dual(u8 front_planes, u8 back_planes) {
		const u8 total = static_cast<u8>(front_planes + back_planes);
		const u8 pf1 = static_cast<u8>((total + 1u) / 2u);
		const u8 pf2 = static_cast<u8>(total / 2u);
		return {pf1, pf2, front_planes == pf2 && front_planes < back_planes};
	}
};

/// Zona de paleta aplicada por copper en una linea concreta.
using PaletteZone = EhbPaletteZone;

/// Configuracion de la escena de scroll.
struct TileScrollConfig {
	const EhbPalette* base_palette = nullptr;
	const PaletteZone* zones = nullptr;
	u8 zone_count = 0;
	u32 copper_bytes = 1536;
};

/// Planificador de columnas para una superficie horizontal con margen oculto.
///
/// El Amiga no puede hacer que un unico fetch de bitplanes "salte" del final de
/// una fila al principio en mitad de la ventana visible. Esta clase fija el
/// contrato que necesitamos para el driver completo: mapear columna/fila de mundo
/// a slot fisico, recordar que franja vive en cada slot y pedir solo lo que falta.
template <u16 VisibleColumns, u16 SurfaceColumns>
class HorizontalRingPrefetch {
public:
	static constexpr u16 visible_columns = VisibleColumns;
	static constexpr u16 surface_columns = SurfaceColumns;
	static constexpr u16 prefetch_column_count = surface_columns - visible_columns;
	static constexpr u16 unknown_column = 0xffffu;

	constexpr void reset(u16 first_world_column) {
		m_first_world_column = first_world_column;
		for (u16 i = 0; i < surface_columns; ++i) {
			m_world_column_by_slot[i] = static_cast<u16>(first_world_column + i);
		}
	}

	constexpr u16 slot_for_world_column(u16 world_column) const {
		return static_cast<u16>(world_column % surface_columns);
	}

	constexpr u16 world_column_in_slot(u16 slot) const {
		return slot < surface_columns ? m_world_column_by_slot[slot] : unknown_column;
	}

	constexpr bool slot_contains(u16 slot, u16 world_column) const {
		return slot < surface_columns && m_world_column_by_slot[slot] == world_column;
	}

	constexpr void mark_slot_ready(u16 slot, u16 world_column) {
		if (slot < surface_columns) {
			m_world_column_by_slot[slot] = world_column;
		}
	}

	/// Devuelve la siguiente columna derecha que conviene preparar.
	constexpr bool next_right_prefetch(
		u16 camera_world_column,
		u16 map_height,
		tilemap::TileRect& out_rect,
		u16& out_surface_slot
	) const {
		const u16 world_column = static_cast<u16>(camera_world_column + surface_columns - 1u);
		const u16 slot = slot_for_world_column(world_column);
		if (slot_contains(slot, world_column)) {
			return false;
		}
		out_rect = {world_column, 0, 1, map_height};
		out_surface_slot = slot;
		return true;
	}

	constexpr u16 first_world_column() const { return m_first_world_column; }

private:
	u16 m_first_world_column = 0;
	u16 m_world_column_by_slot[surface_columns] {};
};

/// Trabajo de prefetch 2D emitido por `BidirectionalRingPrefetch`.
struct BidirectionalPrefetchJob {
	tilemap::TileRect world_rect {};
	tilemap::TileUpdateEdge edge = tilemap::TileUpdateEdge::Interior;
	u16 surface_x = 0;
	u16 surface_y = 0;

	constexpr bool valid() const {
		return world_rect.valid();
	}
};

/// Planificador de prefetch para scroll horizontal y vertical.
///
/// Mantiene que columna y fila de mundo viven en cada slot fisico y decide que
/// franjas se deben preparar cuando la camara avanza en X, Y o ambos. El algoritmo
/// es portable: podra compilarse a superficies circulares, doble buffer, copia de
/// bordes o drivers especializados sin cambiar la logica de juego.
template <u16 VisibleColumns, u16 VisibleRows, u16 SurfaceColumns, u16 SurfaceRows>
class BidirectionalRingPrefetch {
public:
	static constexpr u16 visible_columns = VisibleColumns;
	static constexpr u16 visible_rows = VisibleRows;
	static constexpr u16 surface_columns = SurfaceColumns;
	static constexpr u16 surface_rows = SurfaceRows;
	static constexpr u16 unknown_index = 0xffffu;

	constexpr void reset(u16 first_world_column, u16 first_world_row) {
		for (u16 x = 0; x < surface_columns; ++x) {
			m_world_column_by_slot[x] = static_cast<u16>(first_world_column + x);
		}
		for (u16 y = 0; y < surface_rows; ++y) {
			m_world_row_by_slot[y] = static_cast<u16>(first_world_row + y);
		}
	}

	constexpr u16 slot_for_world_column(u16 world_column) const {
		return static_cast<u16>(world_column % surface_columns);
	}

	constexpr u16 slot_for_world_row(u16 world_row) const {
		return static_cast<u16>(world_row % surface_rows);
	}

	constexpr void mark_column_ready(u16 slot, u16 world_column) {
		if (slot < surface_columns) {
			m_world_column_by_slot[slot] = world_column;
		}
	}

	constexpr void mark_row_ready(u16 slot, u16 world_row) {
		if (slot < surface_rows) {
			m_world_row_by_slot[slot] = world_row;
		}
	}

	constexpr bool column_ready(u16 slot, u16 world_column) const {
		return slot < surface_columns && m_world_column_by_slot[slot] == world_column;
	}

	constexpr bool row_ready(u16 slot, u16 world_row) const {
		return slot < surface_rows && m_world_row_by_slot[slot] == world_row;
	}

	/// Calcula las franjas de prefetch necesarias para la camara actual.
	constexpr u8 plan_prefetch(
		u16 camera_world_column,
		u16 camera_world_row,
		u16 previous_camera_column,
		u16 previous_camera_row,
		u16 map_width,
		u16 map_height,
		BidirectionalPrefetchJob* out_jobs,
		u8 max_jobs
	) const {
		u8 count = 0;

		if (camera_world_column > previous_camera_column) {
			const u16 right_world_column = static_cast<u16>(camera_world_column + surface_columns - 1u);
			if (count < max_jobs && right_world_column < map_width) {
				const u16 slot = slot_for_world_column(right_world_column);
				if (!column_ready(slot, right_world_column)) {
					out_jobs[count++] = {
						{right_world_column, camera_world_row, 1, surface_rows},
						tilemap::TileUpdateEdge::Right,
						slot,
						0,
					};
				}
			}
		} else if (camera_world_column < previous_camera_column) {
			const u16 left_world_column = camera_world_column;
			if (count < max_jobs) {
				const u16 slot = slot_for_world_column(left_world_column);
				if (!column_ready(slot, left_world_column)) {
					out_jobs[count++] = {
						{left_world_column, camera_world_row, 1, surface_rows},
						tilemap::TileUpdateEdge::Left,
						slot,
						0,
					};
				}
			}
		}

		if (camera_world_row > previous_camera_row) {
			const u16 bottom_world_row = static_cast<u16>(camera_world_row + surface_rows - 1u);
			if (count < max_jobs && bottom_world_row < map_height) {
				const u16 slot = slot_for_world_row(bottom_world_row);
				if (!row_ready(slot, bottom_world_row)) {
					out_jobs[count++] = {
						{camera_world_column, bottom_world_row, surface_columns, 1},
						tilemap::TileUpdateEdge::Bottom,
						0,
						slot,
					};
				}
			}
		} else if (camera_world_row < previous_camera_row) {
			const u16 top_world_row = camera_world_row;
			if (count < max_jobs) {
				const u16 slot = slot_for_world_row(top_world_row);
				if (!row_ready(slot, top_world_row)) {
					out_jobs[count++] = {
						{camera_world_column, top_world_row, surface_columns, 1},
						tilemap::TileUpdateEdge::Top,
						0,
						slot,
					};
				}
			}
		}

		return count;
	}

	/// Variante abreviada para camaras que solo avanzan hacia derecha/abajo.
	constexpr u8 plan_forward_prefetch(
		u16 camera_world_column,
		u16 camera_world_row,
		u16 map_width,
		u16 map_height,
		BidirectionalPrefetchJob* out_jobs,
		u8 max_jobs
	) const {
		return plan_prefetch(
			camera_world_column,
			camera_world_row,
			static_cast<u16>(camera_world_column == 0 ? 0 : camera_world_column - 1u),
			static_cast<u16>(camera_world_row == 0 ? 0 : camera_world_row - 1u),
			map_width,
			map_height,
			out_jobs,
			max_jobs
		);
	}

private:
	u16 m_world_column_by_slot[surface_columns] {};
	u16 m_world_row_by_slot[surface_rows] {};
};

/// Superficie de tiles con scroll, compilada para un modo concreto.
///
/// Reserva una superficie lineal por plano (`surface_width x surface_height`),
/// mayor que la ventana visible para que el prefetch pueda escribir fuera de
/// pantalla, y reconstruye cada frame una copperlist que muestra la ventana
/// desplazada por playfield.
template <TileScrollMode Mode>
class TileScrollScene {
public:
	static constexpr TileScrollMode mode = Mode;
	static constexpr u8 plane_count = Mode.total_planes();
	static constexpr bool dual_playfield = Mode.dual();
	static constexpr u16 visible_width = 320;
	static constexpr u16 visible_height = 256;
	static constexpr u16 fine_scroll_fetch_margin_bytes = 2;
	static constexpr u16 tile_size = 16;
	static constexpr u8 prefetch_columns = 10;
	static constexpr u8 prefetch_rows = 10;
	static constexpr u16 prefetch_width = tile_size * prefetch_columns;
	static constexpr u16 prefetch_height = tile_size * prefetch_rows;
	static constexpr u16 surface_width = visible_width + prefetch_width;
	static constexpr u16 surface_height = visible_height + prefetch_height;
	static constexpr u16 visible_bytes_per_row = visible_width / 8;
	static constexpr u16 fetch_bytes_per_row = visible_bytes_per_row + fine_scroll_fetch_margin_bytes;
	static constexpr u16 surface_bytes_per_row = surface_width / 8;
	static constexpr u32 plane_bytes = static_cast<u32>(surface_bytes_per_row) * surface_height;
	static constexpr u32 bitplane_bytes = plane_bytes * plane_count;
	static constexpr u16 display_modulo = surface_bytes_per_row - fetch_bytes_per_row;

	/// Planos de un playfield (PF1 o PF2).
	static constexpr u8 playfield_planes(u8 playfield) {
		return playfield == 0 ? Mode.pf1_planes : Mode.pf2_planes;
	}

	/// Playfield al que pertenece un bitplane por su indice de hardware (0..).
	///
	/// En single playfield todo pertenece a PF1; en dual, los indices pares (planos
	/// 1,3,5) son PF1 y los impares (2,4,6) PF2.
	static constexpr u8 playfield_of_plane(u8 plane) {
		return dual_playfield && (plane & 1u) != 0u ? 1u : 0u;
	}

	/// Indice de hardware de un plano dentro de un playfield.
	static constexpr u8 hardware_plane_of(u8 playfield, u8 plane_in_pf) {
		if (!dual_playfield) {
			return plane_in_pf;
		}
		return playfield == 0
			? static_cast<u8>(plane_in_pf * 2u)
			: static_cast<u8>(plane_in_pf * 2u + 1u);
	}

	/// Numero de bitplanes que caben en un playfield para el modo actual.
	static constexpr u8 playfield_count() {
		return Mode.playfield_count();
	}

	/// Rango de scroll horizontal representable por la superficie lineal.
	///
	/// El minimo es 1 (no 0): con `DDFSTRT` adelantado el puntero necesitaria
	/// apuntar 16px antes del arranque del buffer, algo imposible sin reservar un
	/// margen fisico a la izquierda.
	static constexpr u16 max_scroll_x() {
		return surface_width - visible_width;
	}

	/// Rango de scroll vertical representable por la superficie lineal.
	static constexpr u16 max_scroll_y() {
		return surface_height - visible_height;
	}

	static constexpr u32 tile_plane_bytes() {
		return tile_size * sizeof(u16);
	}

	static constexpr u32 tile_bytes() {
		return tile_plane_bytes() * plane_count;
	}

	static constexpr u32 playfield_tile_bytes(u8 playfield) {
		return tile_plane_bytes() * playfield_planes(playfield);
	}

	bool init(MemorySystem& memory, const TileScrollConfig& config) {
		m_bitplane_block = memory.chip.allocate(bitplane_bytes, 16);
		m_copper_block = memory.chip.allocate(config.copper_bytes, 16);
		m_bitplanes = static_cast<u8*>(m_bitplane_block.data);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || config.base_palette == nullptr) {
			m_ok = false;
			return false;
		}

		m_config = config;
		TileScrollInput initial {};
		return rebuild_copper(initial);
	}

	/// Reconstruye la copperlist para mostrar la ventana desplazada por playfield.
	///
	/// Para cada playfield p:
	/// - fine = `(16 - (scroll_x & 15)) & 15`, programado en su nibble de
	///   `BPLCON1` (bajo = PF1, alto = PF2);
	/// - coarse = `(scroll_x - 1) & ~15`, usado como offset del puntero `BPLxPT`
	///   de los planos de ese playfield, mas el offset opcional por plano.
	///
	/// Con `DDFSTRT=$30` se cumple `display_start == scroll_x` de forma continua en
	/// todo el rango (ver cabecera del archivo).
	bool rebuild_copper(const TileScrollInput& input) {
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || m_config.base_palette == nullptr) {
			m_ok = false;
			return false;
		}

		// Scroll recortado por playfield.
		const u16 cam_x[2] = {
			clamp_scroll(input.playfield[0].x, max_scroll_x(), 1u),
			clamp_scroll(input.playfield[1].x, max_scroll_x(), 1u),
		};
		const u16 cam_y[2] = {
			clamp_scroll(input.playfield[0].y, max_scroll_y(), 0u),
			clamp_scroll(input.playfield[1].y, max_scroll_y(), 0u),
		};
		const u8 fine[2] = {
			static_cast<u8>(cam_x[0] & 15u),
			static_cast<u8>(cam_x[1] & 15u),
		};
		const u8 bplcon1_nibble[2] = {
			static_cast<u8>((16u - fine[0]) & 15u),
			static_cast<u8>((16u - fine[1]) & 15u),
		};
		const u16 fetch_x[2] = {
			static_cast<u16>((cam_x[0] - 1u) & 0xfff0u),
			static_cast<u16>((cam_x[1] - 1u) & 0xfff0u),
		};
		const u32 pointer_offset[2] = {
			static_cast<u32>(cam_y[0]) * surface_bytes_per_row + fetch_x[0] / 8u,
			static_cast<u32>(cam_y[1]) * surface_bytes_per_row + fetch_x[1] / 8u,
		};

		const u16 bplcon1 = dual_playfield
			? static_cast<u16>((bplcon1_nibble[1] << 4u) | bplcon1_nibble[0])
			: static_cast<u16>(bplcon1_nibble[0] | (bplcon1_nibble[0] << 4u));
		const u16 bplcon0 = static_cast<u16>(
			0x0200u |
			(static_cast<u16>(plane_count) << 12u) |
			(dual_playfield ? 0x0400u : 0x0000u)
		);
		const u16 bplcon2 = dual_playfield && Mode.foreground_is_pf2 ? 0x0040u : 0x0000u;

		copper::Scheduler scheduler { m_copper_block };
		scheduler.move(copper::Register::DMACON, static_cast<u16>(
			copper::DmaSetClear | copper::DmaMaster | copper::DmaCopper | copper::DmaBitplane
		));
		scheduler.move(copper::Register::BPLCON0, bplcon0);
		scheduler.move(copper::Register::BPLCON1, bplcon1);
		scheduler.move(copper::Register::BPLCON2, bplcon2);
		scheduler.move(copper::Register::BPL1MOD, display_modulo);
		scheduler.move(copper::Register::BPL2MOD, display_modulo);
		scheduler.move(copper::Register::DIWSTRT, 0x2c81);
		scheduler.move(copper::Register::DIWSTOP, 0x2cc1);
		scheduler.move(copper::Register::DDFSTRT, 0x0030);
		scheduler.move(copper::Register::DDFSTOP, 0x00d0);
		for (u8 plane = 0; plane < plane_count; ++plane) {
			const u8 pf = playfield_of_plane(plane);
			const u32 extra = plane_extra_offset(plane, input);
			const u32 plane_offset = pointer_offset[pf] + extra;
			scheduler.move(
				copper::bitplane_pointer_high_register(plane),
				static_cast<u16>((reinterpret_cast<u32>(m_bitplanes + static_cast<u32>(plane) * plane_bytes + plane_offset)) >> 16)
			);
			scheduler.move(
				copper::bitplane_pointer_low_register(plane),
				static_cast<u16>(reinterpret_cast<u32>(m_bitplanes + static_cast<u32>(plane) * plane_bytes + plane_offset) & 0xffffu)
			);
		}
		scheduler.emit_palette(m_config.base_palette->color);
		for (u8 i = 0; i < m_config.zone_count; ++i) {
			if (m_config.zones[i].palette != nullptr) {
				scheduler.emit_palette_zone(m_config.zones[i].line, m_config.zones[i].palette->color);
			}
		}
		scheduler.wait_line(0xf8);
		scheduler.move(copper::Register::COLOR00, 0x0000);
		scheduler.end();

		m_scroll[0] = cam_x[0];
		m_scroll[1] = cam_x[1];
		m_scroll_y[0] = cam_y[0];
		m_scroll_y[1] = cam_y[1];
		m_copper_words = scheduler.words_used();
		m_copper_words_ptr = scheduler.data();
		m_copper_report = scheduler.report();
		m_ok = scheduler.ok();
		return m_ok;
	}

	/// Conveniencia para single playfield: un solo scroll.
	bool rebuild_copper(u16 scroll_x, u16 scroll_y = 0) {
		TileScrollInput input {};
		input.playfield[0] = {scroll_x, scroll_y};
		return rebuild_copper(input);
	}

	template <typename Backend>
	void install(Backend& backend) const {
		if (m_ok && m_copper_words_ptr != nullptr) {
			backend.install_copper_list(m_copper_words_ptr);
		}
	}

	/// Crea un blit de tile 16x16 hacia la superficie de scroll (single playfield).
	///
	/// Escribe los `plane_count` planos contiguos del tile. Para dual playfield usa
	/// `make_playfield_upload_jobs`, porque sus planos no son contiguos.
	graphics::BlitJob make_tile_upload_job(
		const u16* tile_source,
		u16 surface_tile_x,
		u16 surface_tile_y
	) const {
		return {
			graphics::BlitJobKind::TileBlockCopy,
			nullptr,
			tile_source,
			plane_tile_destination(0, surface_tile_x, surface_tile_y),
			1,
			tile_size,
			0,
			static_cast<s16>(surface_bytes_per_row - sizeof(u16)),
			plane_count,
			0,
			tile_plane_bytes(),
			plane_bytes,
		};
	}

	/// Genera los blits de un tile de `playfield` (uno por plano).
	///
	/// El tile debe estar en formato planar contiguo con solo los planos de ese
	/// playfield. Devuelve el numero de jobs escritos en `out` (maximo 3).
	u8 make_playfield_upload_jobs(
		u8 playfield,
		const u16* tile_planes,
		u16 surface_tile_x,
		u16 surface_tile_y,
		graphics::BlitJob out[3]
	) const {
		const u8 count = playfield_planes(playfield);
		const u32 plane_stride_words = tile_plane_bytes() / sizeof(u16);
		for (u8 i = 0; i < count; ++i) {
			const u8 hw_plane = hardware_plane_of(playfield, i);
			out[i] = {
				graphics::BlitJobKind::TileBlockCopy,
				nullptr,
				tile_planes + static_cast<u32>(i) * plane_stride_words,
				plane_tile_destination(hw_plane, surface_tile_x, surface_tile_y),
				1,
				tile_size,
				0,
				static_cast<s16>(surface_bytes_per_row - sizeof(u16)),
				1,
				0,
				tile_plane_bytes(),
				plane_bytes,
			};
		}
		return count;
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u8* bitplanes() const { return m_bitplanes; }
	constexpr u16 scroll_x() const { return m_scroll[0]; }
	constexpr u16 scroll_y() const { return m_scroll_y[0]; }
	constexpr u16 copper_words() const { return m_copper_words; }
	constexpr const copper::ScheduleReport& copper_report() const { return m_copper_report; }

	/// Vista mutable de todos los bytes de los bitplanes.
	[[nodiscard]] Span<u8> bitplane_span() const {
		return {m_bitplanes, bitplane_bytes};
	}

	/// Vista mutable de un plano completo en words de 16 bits.
	[[nodiscard]] Span<u16> plane_words(u8 plane) const {
		return {
			reinterpret_cast<u16*>(m_bitplanes + static_cast<u32>(plane) * plane_bytes),
			plane_bytes / sizeof(u16),
		};
	}

private:
	/// Recorta un scroll al rango representable de la superficie.
	static constexpr u16 clamp_scroll(u16 value, u16 max_value, u16 min_value) {
		return value < min_value ? min_value : (value > max_value ? max_value : value);
	}

	/// Offset extra de un plano (parallax tipo RoboCod).
	static constexpr u32 plane_extra_offset(u8 plane, const TileScrollInput& input) {
		const PlaneScrollOffset& off = input.plane[plane];
		return
			static_cast<u32>(off.coarse_x_pixels < 0 ? 0 : off.coarse_x_pixels) / 8u +
			static_cast<u32>(off.y_rows) * surface_bytes_per_row;
	}

	u16* plane_tile_destination(u8 plane, u16 surface_tile_x, u16 surface_tile_y) const {
		const u32 offset =
			static_cast<u32>(plane) * plane_bytes +
			static_cast<u32>(surface_tile_y) * tile_size * surface_bytes_per_row +
			static_cast<u32>(surface_tile_x) * sizeof(u16);
		return reinterpret_cast<u16*>(m_bitplanes + offset);
	}

	TileScrollConfig m_config {};
	MemoryBlock m_bitplane_block {};
	MemoryBlock m_copper_block {};
	u8* m_bitplanes = nullptr;
	const u16* m_copper_words_ptr = nullptr;
	copper::ScheduleReport m_copper_report {};
	u16 m_scroll[2] {};
	u16 m_scroll_y[2] {};
	u16 m_copper_words = 0;
	bool m_ok = false;
};

} // namespace eng::graphics::drivers
