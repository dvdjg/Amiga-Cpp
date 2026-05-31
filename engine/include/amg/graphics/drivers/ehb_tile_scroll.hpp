#pragma once

/// \file ehb_tile_scroll.hpp
/// Driver EHB con superficie de scroll horizontal.
///
/// Este driver es el primer paso entre el MVP retenido y el Amiga real. La demo
/// 100 pintaba un viewport de forma didactica; esta clase ya reserva una superficie
/// mas ancha que la ventana visible y construye una copperlist que desplaza los
/// punteros de bitplane y `BPLCON1`.
///
/// La politica general sigue siendo la misma:
///
/// - la escena/camara decide `scroll_x`;
/// - `TileMap16` y `ProgressiveTileScheduler` deciden que tiles offscreen preparar;
/// - este driver convierte updates aceptados a `TileBlockCopy`;
/// - el backend ejecuta los blits y el Copper muestra la ventana desplazada.

#include <amg/core/types.hpp>
#include <amg/graphics/copper/scheduler.hpp>
#include <amg/graphics/drivers/ehb_scene.hpp>
#include <amg/graphics/frame_plan.hpp>
#include <amg/graphics/tilemap/tile_scroll.hpp>
#include <amg/memory/arena.hpp>

namespace amg::graphics::drivers {

struct EhbTileScrollConfig {
	const EhbPalette* base_palette = nullptr;
	const EhbPaletteZone* zones = nullptr;
	u8 zone_count = 0;
	u32 copper_bytes = 1536;
};

/// Superficie EHB 384x256 que muestra una ventana 320x256.
///
/// Los 64 pixels extra son margen horizontal de prefetch: cuatro columnas de
/// tiles de 16px. Esto evita que el motor tenga que pintar una columna completa
/// justo en el fotograma anterior a hacerse visible. La escena puede ir
/// encolando tiles sueltos y el scheduler los reparte entre varios VBLANKs.
///
/// Un driver mas avanzado alternara buffers o superficies circulares; esta
/// version mantiene un bloque lineal para que los punteros, modulos y blits sean
/// faciles de auditar mientras se valida el contrato de alto nivel.
class EhbTileScrollScene {
public:
	static constexpr u16 visible_width = 320;
	static constexpr u16 visible_height = 256;
	static constexpr u16 tile_size = 16;
	static constexpr u8 prefetch_columns = 4;
	static constexpr u16 prefetch_width = tile_size * prefetch_columns;
	static constexpr u16 surface_width = visible_width + prefetch_width;
	static constexpr u16 visible_bytes_per_row = visible_width / 8;
	static constexpr u16 surface_bytes_per_row = surface_width / 8;
	static constexpr u8 plane_count = 6;
	static constexpr u32 plane_bytes = static_cast<u32>(surface_bytes_per_row) * visible_height;
	static constexpr u32 bitplane_bytes = plane_bytes * plane_count;
	static constexpr u16 display_modulo = surface_bytes_per_row - visible_bytes_per_row;

	bool init(MemorySystem& memory, const EhbTileScrollConfig& config) {
		m_bitplane_block = memory.chip.allocate(bitplane_bytes, 16);
		m_copper_block = memory.chip.allocate(config.copper_bytes, 16);
		m_bitplanes = static_cast<u8*>(m_bitplane_block.data);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || config.base_palette == nullptr) {
			m_ok = false;
			return false;
		}

		m_config = config;
		return rebuild_copper(0);
	}

	bool rebuild_copper(u16 scroll_x) {
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || m_config.base_palette == nullptr) {
			m_ok = false;
			return false;
		}

		const u16 coarse_x = static_cast<u16>(scroll_x & 0xfff0u);
		const u8 fine_x = static_cast<u8>(scroll_x & 15u);
		const u32 pointer_offset = coarse_x / 8u;

		copper::Scheduler scheduler { m_copper_block };
		scheduler.move(copper::Register::DMACON, static_cast<u16>(
			copper::DmaSetClear | copper::DmaMaster | copper::DmaCopper | copper::DmaBitplane
		));
		scheduler.move(copper::Register::BPLCON0, 0x6200);
		scheduler.move(copper::Register::BPLCON1, static_cast<u16>(fine_x | (fine_x << 4u)));
		scheduler.move(copper::Register::BPLCON2, 0x0000);
		scheduler.move(copper::Register::BPL1MOD, display_modulo);
		scheduler.move(copper::Register::BPL2MOD, display_modulo);
		scheduler.move(copper::Register::DIWSTRT, 0x2c81);
		scheduler.move(copper::Register::DIWSTOP, 0x2cc1);
		scheduler.move(copper::Register::DDFSTRT, 0x0038);
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
/// En las primeras demos usamos una ventana lineal de 24 columnas y mantenemos el
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
	/// que sea facil auditarla en pruebas: cada 24 columnas el slot se recicla.
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

} // namespace amg::graphics::drivers
