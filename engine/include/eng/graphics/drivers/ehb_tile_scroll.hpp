#pragma once

/// \file ehb_tile_scroll.hpp
/// Driver EHB con superficie lineal de scroll por tiles.
///
/// Este driver es el primer paso entre el MVP retenido y el Amiga real. La demo
/// 100 pintaba un viewport de forma didactica; esta clase ya reserva una
/// superficie mayor que la ventana visible y construye una copperlist que desplaza
/// los punteros de bitplane en X/Y y `BPLCON1` para el fine scroll horizontal.
///
/// La politica general sigue siendo la misma:
///
/// - la escena/camara decide `scroll_x/scroll_y`;
/// - `TileMap16` y `ProgressiveTileScheduler` deciden que tiles offscreen preparar;
/// - este driver convierte updates aceptados a `TileBlockCopy`;
/// - el backend ejecuta los blits y el Copper muestra la ventana desplazada.

#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/tilemap/tile_scroll.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::drivers {

struct EhbTileScrollConfig {
	const EhbPalette* base_palette = nullptr;
	const EhbPaletteZone* zones = nullptr;
	u8 zone_count = 0;
	u32 copper_bytes = 1536;
};

/// Superficie EHB 480x416 que muestra una ventana 320x256.
///
/// Los 160 pixels extra horizontales y verticales son margen de prefetch: diez
/// columnas y diez filas de tiles de 16px. Esta demo usa ese margen para moverse
/// derecha/izquierda/arriba/abajo y despues recorrer una orbita de cuatro tiles
/// de radio sin que los uploads del Blitter entren en el area visible.
///
/// Un driver mas avanzado alternara buffers o superficies circulares; esta
/// version mantiene un bloque lineal para que los punteros, modulos y blits sean
/// faciles de auditar mientras se valida el contrato de alto nivel.
class EhbTileScrollScene {
public:
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
	static constexpr u8 plane_count = 6;
	static constexpr u32 plane_bytes = static_cast<u32>(surface_bytes_per_row) * surface_height;
	static constexpr u32 bitplane_bytes = plane_bytes * plane_count;
	static constexpr u16 display_modulo = surface_bytes_per_row - fetch_bytes_per_row;

	bool init(MemorySystem& memory, const EhbTileScrollConfig& config) {
		m_bitplane_block = memory.chip.allocate(bitplane_bytes, 16);
		m_copper_block = memory.chip.allocate(config.copper_bytes, 16);
		m_bitplanes = static_cast<u8*>(m_bitplane_block.data);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || config.base_palette == nullptr) {
			m_ok = false;
			return false;
		}

		m_config = config;
		return rebuild_copper(0, 0);
	}

	/// Reconstruye la copperlist para mostrar una ventana desplazada.
	///
	/// `scroll_x/scroll_y` son coordenadas de camara dentro de la superficie lineal
	/// en pixels. El desplazamiento vertical es directo: basta con sumar filas al
	/// puntero de cada bitplane. El horizontal se divide en:
	///
	/// - parte coarse: `scroll_x / 16`, que avanza el puntero dos bytes por tile de
	///   fetch OCS;
	/// - parte fine: resto 0..15, programado en `BPLCON1`.
	///
	/// La convencion de este MVP es que `scroll_x` crece igual que una camara de
	/// juego: valores mayores muestran columnas mas a la derecha del mundo.
	///
	/// En OCS el valor horizontal de `BPLCON1` desplaza el playfield dentro del
	/// word que ya ha sido traido por el fetch DMA. Para que el borde izquierdo no
	/// se quede sin datos durante `fine != 0`, esta demo adelanta el fetch de la
	/// ventana visible:
	///
	/// - `DDFSTRT` se adelanta de `$38` a `$30`;
	/// - el modulo se calcula con 42 bytes leidos por linea en vez de 40;
	/// - el puntero base apunta al inicio del tile coarse (`fetch_x == coarse_x`),
	///   avanzando dos bytes en cada cruce de tile;
	/// - `BPLCON1` recibe el fine scroll directo.
	///
	/// Con eso el borde izquierdo visible es continuo en todo el rango:
	/// `visible == coarse_x + fine_x == scroll_x`. Usar `coarse_x - 16` dejaba el
	/// puntero sin avanzar en el primer cruce (`scroll_x == 16`) y producia un
	/// salto de 15px al pasar de fine 15 a 0 (fix documentado en git).
	///
	/// La prueba `analyze-fine-scroll.ps1` captura `fineX=14,15,0,1` y comprueba
	/// que el borde izquierdo se mantiene estable y que el contenido avanza un pixel
	/// lowres por frame. Esta es la base que despues usaran los drivers con anillos
	/// reales de tiles offscreen.
	bool rebuild_copper(u16 scroll_x, u16 scroll_y = 0) {
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || m_config.base_palette == nullptr) {
			m_ok = false;
			return false;
		}

		const u8 fine_x = static_cast<u8>(scroll_x & 15u);
		const u16 coarse_x = static_cast<u16>(scroll_x & 0xfff0u);
		// El puntero avanza en cada cruce de tile. `coarse_x - 16` dejaba el puntero
		// sin avanzar en el primer cruce (scroll_x == 16), mostrando un salto de
		// 15px al pasar de fine 15 -> 0. Con `coarse_x` el borde visible es continuo:
		// visible = coarse_x + fine_x == scroll_x en todo el rango.
		const u16 fetch_x = coarse_x;
		const u8 bplcon1_x = fine_x;
		const u32 pointer_offset =
			static_cast<u32>(scroll_y) * surface_bytes_per_row +
			fetch_x / 8u;

		copper::Scheduler scheduler { m_copper_block };
		scheduler.move(copper::Register::DMACON, static_cast<u16>(
			copper::DmaSetClear | copper::DmaMaster | copper::DmaCopper | copper::DmaBitplane
		));
		scheduler.move(copper::Register::BPLCON0, 0x6200);
		scheduler.move(copper::Register::BPLCON1, static_cast<u16>(bplcon1_x | (bplcon1_x << 4u)));
		scheduler.move(copper::Register::BPLCON2, 0x0000);
		scheduler.move(copper::Register::BPL1MOD, display_modulo);
		scheduler.move(copper::Register::BPL2MOD, display_modulo);
		scheduler.move(copper::Register::DIWSTRT, 0x2c81);
		scheduler.move(copper::Register::DIWSTOP, 0x2cc1);
		scheduler.move(copper::Register::DDFSTRT, 0x0030);
		scheduler.move(copper::Register::DDFSTOP, 0x00d0);
		for (u8 plane = 0; plane < plane_count; ++plane) {
			scheduler.move(
				copper::bitplane_pointer_high_register(plane),
				static_cast<u16>((reinterpret_cast<u32>(m_bitplanes + static_cast<u32>(plane) * plane_bytes + pointer_offset)) >> 16)
			);
			scheduler.move(
				copper::bitplane_pointer_low_register(plane),
				static_cast<u16>(reinterpret_cast<u32>(m_bitplanes + static_cast<u32>(plane) * plane_bytes + pointer_offset) & 0xffffu)
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

		m_scroll_x = scroll_x;
		m_scroll_y = scroll_y;
		m_copper_words = scheduler.words_used();
		m_copper_words_ptr = scheduler.data();
		m_copper_report = scheduler.report();
		m_ok = scheduler.ok();
		return m_ok;
	}

	template <typename Backend>
	void install(Backend& backend) const {
		if (m_ok && m_copper_words_ptr != nullptr) {
			backend.install_copper_list(m_copper_words_ptr);
		}
	}

	/// Crea un blit de tile 16x16 hacia la superficie de scroll.
	///
	/// `tile_source` apunta al primer word del tile planar: 6 planos contiguos,
	/// cada plano con 16 words. `surface_tile_x/y` son coordenadas de tile dentro de
	/// la superficie, no del mundo virtual.
	graphics::BlitJob make_tile_upload_job(
		const u16* tile_source,
		u16 surface_tile_x,
		u16 surface_tile_y
	) const {
		return {
			graphics::BlitJobKind::TileBlockCopy,
			nullptr,
			tile_source,
			tile_destination(surface_tile_x, surface_tile_y),
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

	constexpr bool ok() const { return m_ok; }
	constexpr u8* bitplanes() const { return m_bitplanes; }
	constexpr u16 scroll_x() const { return m_scroll_x; }
	constexpr u16 scroll_y() const { return m_scroll_y; }
	constexpr u16 copper_words() const { return m_copper_words; }
	constexpr const copper::ScheduleReport& copper_report() const { return m_copper_report; }

	static constexpr u32 tile_plane_bytes() {
		return tile_size * sizeof(u16);
	}

	static constexpr u32 tile_bytes() {
		return tile_plane_bytes() * plane_count;
	}

private:
	u16* tile_destination(u16 surface_tile_x, u16 surface_tile_y) const {
		const u32 offset =
			static_cast<u32>(surface_tile_y) * tile_size * surface_bytes_per_row +
			static_cast<u32>(surface_tile_x) * sizeof(u16);
		return reinterpret_cast<u16*>(m_bitplanes + offset);
	}

	EhbTileScrollConfig m_config {};
	MemoryBlock m_bitplane_block {};
	MemoryBlock m_copper_block {};
	u8* m_bitplanes = nullptr;
	const u16* m_copper_words_ptr = nullptr;
	copper::ScheduleReport m_copper_report {};
	u16 m_scroll_x = 0;
	u16 m_scroll_y = 0;
	u16 m_copper_words = 0;
	bool m_ok = false;
};

/// Planificador de columnas para una superficie horizontal con margen oculto.
///
/// El Amiga no puede hacer que un unico fetch de bitplanes "salte" del final de
/// una fila al principio en mitad de la ventana visible. Por eso esta clase no
/// promete todavia un wrap fisico perfecto de pantalla completa. Lo que si fija es
/// el contrato que necesitamos para llegar ahi:
///
/// - la camara habla en columnas de mundo;
/// - el driver decide que slot fisico de la superficie contiene cada columna;
/// - cada slot recuerda que columna logica tiene preparada;
/// - cuando falta una columna, la demo o el engine recibe un trabajo explicito de
///   prefetch y puede convertirlo en `TileBlockCopy`.
///
/// En las primeras demos usamos una ventana lineal de superficie completa y mantenemos el
/// scroll visible dentro de un tramo seguro. Mas adelante este mismo contrato
/// podra alimentar doble superficie, copia de borde, wrap por modulo o una
/// composicion mas sofisticada sin cambiar la logica de juego.
class EhbHorizontalRingPrefetch {
public:
	static constexpr u16 visible_columns = EhbTileScrollScene::visible_width / EhbTileScrollScene::tile_size;
	static constexpr u16 surface_columns = EhbTileScrollScene::surface_width / EhbTileScrollScene::tile_size;
	static constexpr u16 prefetch_column_count = surface_columns - visible_columns;
	static constexpr u16 unknown_column = 0xffffu;

	constexpr void reset(u16 first_world_column) {
		m_first_world_column = first_world_column;
		for (u16 i = 0; i < surface_columns; ++i) {
			m_world_column_by_slot[i] = static_cast<u16>(first_world_column + i);
		}
	}

	/// Slot fisico que debe contener una columna de mundo.
	///
	/// Es modulo la superficie completa. La funcion es deliberadamente pequena para
	/// que sea facil auditarla en pruebas: cada `surface_columns` columnas el slot
	/// se recicla.
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
	///
	/// `camera_world_column` es la columna visible izquierda. La columna objetivo es
	/// la mas lejana dentro del margen de prefetch derecho; si su slot ya contiene
	/// esa columna, no hay trabajo nuevo. Si no, devuelve un job de una columna de
	/// alto completo para que el scheduler lo divida por filas y presupuesto.
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

/// Trabajo de prefetch 2D emitido por `EhbBidirectionalRingPrefetch`.
///
/// Un trabajo puede representar una columna, una fila o, mas adelante, una esquina
/// explicita. En esta primera fase columna y fila se solapan naturalmente en una
/// celda; el scheduler acepta duplicados benignos porque su coste queda acotado por
/// el presupuesto por frame.
struct EhbBidirectionalPrefetchJob {
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
/// Esta clase es el equivalente 2D del anillo horizontal: mantiene que columna y
/// fila de mundo vive en cada slot fisico y decide que franjas se deben preparar
/// cuando la camara logica avanza en X, Y o ambos. Todavia no resuelve el wrap
/// visual completo; fija el algoritmo portable que luego podra compilarse a:
///
/// - superficies circulares reales;
/// - doble buffer de tiles;
/// - copia de bordes;
/// - drivers especializados solo X, solo Y o bidireccionales.
class EhbBidirectionalRingPrefetch {
public:
	static constexpr u16 visible_columns = EhbTileScrollScene::visible_width / EhbTileScrollScene::tile_size;
	static constexpr u16 visible_rows = EhbTileScrollScene::visible_height / EhbTileScrollScene::tile_size;
	static constexpr u16 surface_columns = EhbTileScrollScene::surface_width / EhbTileScrollScene::tile_size;
	static constexpr u16 surface_rows = EhbTileScrollScene::surface_height / EhbTileScrollScene::tile_size;
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
	///
	/// `camera_world_column/row` son la esquina superior izquierda visible en tiles.
	/// `previous_camera_*` permite decidir el sentido: derecha, izquierda, abajo y
	/// arriba reciclan slots distintos. Cuando hay movimiento diagonal se pueden
	/// emitir dos jobs: una franja horizontal y otra vertical. La esquina queda
	/// cubierta por el cruce de ambas, y el presupuesto de Blitter decide en cuantos
	/// frames se materializa.
	constexpr u8 plan_prefetch(
		u16 camera_world_column,
		u16 camera_world_row,
		u16 previous_camera_column,
		u16 previous_camera_row,
		u16 map_width,
		u16 map_height,
		EhbBidirectionalPrefetchJob* out_jobs,
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
		EhbBidirectionalPrefetchJob* out_jobs,
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

} // namespace eng::graphics::drivers
