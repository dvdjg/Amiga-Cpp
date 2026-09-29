#pragma once

/// \file plane_view.hpp
/// `PlaneView`: vista de los planos de OTRO bitmap (mismo layout/stride) con
/// **doble buffer opcional** de UN bloque extra. Encapsula el patrón **soft DPF**
/// (RoboCod): el display lee el buffer DELANTERO (`display_base`, la base que
/// publica `BPLxPT`) y el Blit escribe el TRASERO (`write_base`, base con
/// `frontbase_offset`), y `flip()` conmuta ambos. No posee el bitmap principal;
/// solo el bloque extra.
///
/// Se separa del playfield de scroll (`XLimitedPlayfield`) para que la composición
/// (soft DPF) sea una pieza reutilizable. Ver
/// `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §3.2 y
/// `docs/reference/amiga/techniques/robocod-layered-scroll.md`.

#include <eng/core/types/types.hpp>
#include <eng/graphics/bitmap.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

class PlaneView {
public:
	/// Enlaza el bitmap principal (sin doble buffer): display y escritura apuntan
	/// al mismo bloque.
	void bind_single(eng::Address<eng::MemoryKind::Chip> main_real,
	                 eng::Address<eng::MemoryKind::Chip> main_front) {
		m_main_real = main_real;
		m_main_front = main_front;
		m_db = false;
		m_active = 0;
	}

	/// Enlace crudo (tests o memoria ya gestionada por el llamador).
	void bind_raw(eng::Address<eng::MemoryKind::Chip> main_real,
	              eng::Address<eng::MemoryKind::Chip> main_front,
	              eng::Address<eng::MemoryKind::Chip> extra_real,
	              eng::Address<eng::MemoryKind::Chip> extra_front) {
		bind_single(main_real, main_front);
		m_extra_real = extra_real;
		m_extra_front = extra_front;
		m_db = extra_real.valid() && extra_front.valid();
	}

	/// Reserva el bloque extra con el MISMO layout que el principal (doble buffer).
	bool enable_double_buffer(eng::MemorySystem& memory, const eng::gfx::BitmapConfig& bc) {
		if (!m_extra.init(memory, bc)) return false;
		m_extra_real = m_extra.allocation_start();
		m_extra_front = m_extra.front();
		m_db = true;
		m_active = 0;
		return true;
	}

	constexpr bool double_buffered() const { return m_db; }

	/// Base del buffer DELANTERO (la que usa `BPLxPT` del display).
	[[nodiscard]] eng::Address<eng::MemoryKind::Chip> display_base() const {
		if (!m_db) return m_main_real;
		return m_active ? m_extra_real : m_main_real;
	}
	/// Base del buffer TRASERO (destino del Blit de fondo, con `frontbase_offset`).
	[[nodiscard]] eng::Address<eng::MemoryKind::Chip> write_base() const {
		if (!m_db) return m_main_front;
		return m_active ? m_main_front : m_extra_front;
	}
	/// Conmuta delantero/trasero. Llamar tras escribir el blit y antes del display.
	void flip() { if (m_db) m_active = static_cast<eng::u8>(m_active ^ 1u); }

	constexpr eng::u32 total_bytes() const { return m_extra.total_bytes(); }

private:
	eng::gfx::Bitmap m_extra {};
	eng::Address<eng::MemoryKind::Chip> m_main_real {};
	eng::Address<eng::MemoryKind::Chip> m_main_front {};
	eng::Address<eng::MemoryKind::Chip> m_extra_real {};
	eng::Address<eng::MemoryKind::Chip> m_extra_front {};
	bool m_db = false;
	eng::u8 m_active = 0;
};

} // namespace eng::field
