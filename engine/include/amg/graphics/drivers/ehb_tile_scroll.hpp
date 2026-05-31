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

} // namespace amg::graphics::drivers
