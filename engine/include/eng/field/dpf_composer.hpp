#pragma once

/// \file dpf_composer.hpp
/// Compositor de display para uno o dos campos (DPF).
///
/// Es la ÚNICA capa que toca los registros compartidos del chipset:
///
/// - `BPLCON0` (bit 10 DPF + número de bitplanes);
/// - `BPLCON1` (nibble bajo = PF1, alto = PF2; en single los dos iguales);
/// - `BPL1MOD`/`BPL2MOD` (módulos de las filas del framebuffer);
/// - `BPLCON2` (prioridad PF1/PF2);
/// - los punteros `BPLxPT`, intercalados por playfield (PF1 = planos 1,3,5,
///   PF2 = 2,4,6; en single consecutivos).
///
/// El compositor no decide cómo scrollea cada campo: recibe dos
/// `FieldHardwareView` (una por campo, opcional la segunda para single
/// playfield) y solo las une a la copperlist.
///
/// Uso (mismo patrón que `TileScrollScene`):
/// - `init` solo reserva los dos bloques del doble buffer de la copperlist;
/// - el primer `compose` emite la lista completa en AMBOS bloques (registros
///   estáticos + paleta + punteros reales de las dos vistas);
/// - los siguientes `compose` parchean solo las words que dependen de la
///   cámara (BPLCON1 + los punteros), que es lo que cambia cada frame.
/// - `install` activa el bloque inactivo.
///
/// Convención de hardware (igual que `TileScrollScene`): con `DDFSTRT=$30` se
/// fetchean 42 bytes por fila (40 visibles + 2 de margen) y el driver usa la
/// fórmula canónica `BPLCON1=(16-fine)&15` con puntero = `(scroll-1)&~15`, que
/// da `display_start == scroll` continuo. La vista del campo ya trae
/// `display_byte_offset`, `fine_x` y `bpl1mod` calculados; aquí solo se colocan.

#include <eng/field/tile_field.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

/// Configuración del compositor.
struct DpfComposerConfig {
	const eng::u16* palette = nullptr;   // 32 colores
	eng::u32 copper_bytes = 1536;        // por bloque del doble buffer
	bool dual = false;                   // DPF: dos playfields
	bool foreground_is_pf2 = false;      // BPL2PRI: PF2 delante en dual
	eng::u8 plane_count = 6;             // total de bitplanes de hardware
	eng::u16 diwstrt = 0x2c81;
	eng::u16 diwstop = 0x2cc1;
	eng::u16 ddfstrt = 0x0030;
	eng::u16 ddfstop = 0x00d0;
};

/// Compositor de display para campos de tiles (único dueño de los registros
/// compartidos del chipset).
class DpfDisplayComposer {
public:
	DpfDisplayComposer() = default;

	bool init(eng::MemorySystem& memory, const DpfComposerConfig& config) {
		m_config = config;
		m_copper_blocks[0] = memory.chip.allocate(config.copper_bytes, 16);
		m_copper_blocks[1] = memory.chip.allocate(config.copper_bytes, 16);
		if (!m_copper_blocks[0].valid() || !m_copper_blocks[1].valid() ||
			config.palette == nullptr) {
			m_ok = false;
			return false;
		}
		m_initialized = true;
		return true;
	}

	/// Actualiza la copperlist para mostrar las dos vistas.
	///
	/// El primer frame emite la lista completa (con los punteros reales de ambas
	/// vistas) en los dos bloques; los siguientes parchean las words que
	/// dependen de la cámara (BPLCON1 + punteros).
	bool compose(const FieldHardwareView& pf1, const FieldHardwareView& pf2) {
		if (!m_initialized) {
			return false;
		}
		if (!m_copper_initialized) {
			if (!emit_full(0, pf1, pf2) || !emit_full(1, pf1, pf2)) {
				return false;
			}
			m_copper_initialized = true;
			m_active_copper = 0;
			return true;
		}
		const u8 inactive = static_cast<u8>(m_active_copper ^ 1u);
		if (!emit_full(inactive, pf1, pf2)) return false;
		m_active_copper = inactive;
		return true;
	}

	template <typename Backend>
	void install(Backend& backend) const {
		if (m_ok && m_initialized && m_copper_initialized) {
			backend.install_copper_list(static_cast<const u16*>(m_copper_blocks[m_active_copper].data));
		}
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u16 copper_words() const { return m_copper_words; }

private:
	/// Offsets en words de la lista (layout fijo):
	///   0-1 DMACON   2-3 BPLCON0   4-5 BPLCON1
	///   6-7 BPLCON2  8-9 BPL1MOD   10-11 BPL2MOD
	///   12-13 DIWSTRT 14-15 DIWSTOP 16-17 DDFSTRT 18-19 DDFSTOP
	///   20+4p por plano: [BPLxPTH, val, BPLxPTL, val]
	///   después: paleta, wait, fin.
	static constexpr u16 pointer_high_word_offset(u8 plane) {
		return static_cast<u16>(21u + static_cast<u16>(plane) * 4u);
	}
	static constexpr u16 pointer_low_word_offset(u8 plane) {
		return static_cast<u16>(23u + static_cast<u16>(plane) * 4u);
	}

	u16 bplcon0() const {
		return static_cast<u16>(
			0x0200u |
			(static_cast<u16>(m_config.plane_count) << 12u) |
			(m_config.dual ? 0x0400u : 0x0000u)
		);
	}

	/// BPLCON1 contiene dos nibbles independientes: el bajo controla PF1 y el
	/// alto PF2. El valor no es el desplazamiento directo: el hardware desplaza
	/// hacia la izquierda, por eso TileFieldController entrega `(16-fine)&15`.
	/// Mantener esta conversión en la lista Copper permite que la CPU solo cambie
	/// una word por frame y evita mover píxeles con CPU o Blitter.
	u16 bplcon1(const FieldHardwareView& pf1, const FieldHardwareView& pf2) const {
		const eng::u16 fine1 = static_cast<eng::u16>(pf1.fine_x & 15u);
		const eng::u16 fine2 = static_cast<eng::u16>(pf2.fine_x & 15u);
		return m_config.dual
			? static_cast<u16>((fine2 << 4u) | fine1)
			: static_cast<u16>(fine1 | (fine1 << 4u));
	}

	/// Dirección del bitplane `plane_in_field` de una vista.
	u32 plane_address(const FieldHardwareView& v, eng::u8 plane_in_field) const {
		return reinterpret_cast<eng::u32>(v.bitplanes) +
			static_cast<eng::u32>(plane_in_field) * v.plane_stride +
			v.display_byte_offset;
	}

	bool valid_view(const FieldHardwareView& v) const {
		if (!v.bitplanes || !v.plane_count || v.plane_count > 6u || !v.row_bytes || !v.plane_bytes ||
			v.fetch_bytes > v.row_bytes || v.bpl1mod + v.fetch_bytes != v.row_bytes ||
			v.display_byte_offset + v.fetch_bytes > v.plane_bytes ||
			v.fetch_bytes != static_cast<u16>((m_config.ddfstop - m_config.ddfstrt) / 4u + 2u)) return false;
		return !v.split_active || (v.guard_line + 1u == v.surface_h && v.split_line != 0 && v.split_line < v.surface_h &&
			v.split_line < v.visible_h &&
			v.split_display_byte_offset + static_cast<u32>(v.visible_h - v.split_line - 1u) * v.row_bytes + v.fetch_bytes <= v.plane_bytes);
	}

	/// Índice de hardware del bitplane `plane_in_field` de una vista.
	eng::u8 hardware_plane(const FieldHardwareView& v, eng::u8 plane_in_field) const {
		if (m_config.dual) {
			return static_cast<eng::u8>(v.first_hardware_plane + plane_in_field * 2u);
		}
		return plane_in_field;
	}

	/// Escribe los punteros de una vista en el bloque dado (parchea PF1 o PF2).
	void patch_pointers(u8 block, const FieldHardwareView& v) {
		u16* const words = static_cast<u16*>(m_copper_blocks[block].data);
		for (eng::u8 i = 0; i < v.plane_count; ++i) {
			const eng::u8 hw = hardware_plane(v, i);
			if (hw >= 6u) continue;
			const eng::u32 addr = plane_address(v, i);
			words[pointer_high_word_offset(hw)] = static_cast<u16>(addr >> 16);
			words[pointer_low_word_offset(hw)] = static_cast<u16>(addr & 0xffffu);
		}
	}

	/// Emite la lista completa en el bloque dado con las dos vistas reales.
	bool emit_full(u8 block, const FieldHardwareView& pf1, const FieldHardwareView& pf2) {
		if (!valid_view(pf1) || (m_config.dual && !valid_view(pf2))) return false;
		if ((!m_config.dual && pf1.plane_count != m_config.plane_count) ||
			(m_config.dual && pf1.plane_count + pf2.plane_count != m_config.plane_count) ||
			(m_config.dual && pf1.split_active != pf2.split_active) ||
			(m_config.dual && pf1.split_active && pf1.split_line != pf2.split_line)) return false;
		copper::Scheduler scheduler { m_copper_blocks[block] };
		const eng::u16 dmacon = static_cast<eng::u16>(
			copper::DmaSetClear | copper::DmaMaster | copper::DmaCopper | copper::DmaBitplane
		);
		scheduler.move(copper::Register::DMACON, dmacon);
		scheduler.move(copper::Register::BPLCON0, bplcon0());
		scheduler.move(copper::Register::BPLCON1, bplcon1(pf1, pf2));
		scheduler.move(copper::Register::BPLCON2, m_config.foreground_is_pf2 ? 0x0040u : 0x0000u);
		scheduler.move(copper::Register::BPL1MOD, pf1.bpl1mod);
		scheduler.move(copper::Register::BPL2MOD, m_config.dual ? pf2.bpl1mod : pf1.bpl1mod);
		scheduler.move(copper::Register::DIWSTRT, m_config.diwstrt);
		scheduler.move(copper::Register::DIWSTOP, m_config.diwstop);
		scheduler.move(copper::Register::DDFSTRT, m_config.ddfstrt);
		scheduler.move(copper::Register::DDFSTOP, m_config.ddfstop);
		for (eng::u8 plane = 0; plane < m_config.plane_count; ++plane) {
			const bool first = !m_config.dual ? true : ((plane & 1u) == 0u);
			const FieldHardwareView& view = first ? pf1 : pf2;
			const eng::u8 local = m_config.dual ? static_cast<eng::u8>(plane / 2u) : plane;
			const eng::u8 hardware = m_config.dual ? hardware_plane(view, local) : plane;
			scheduler.move_bitplane_pointer(hardware, reinterpret_cast<const void*>(plane_address(view, local)));
		}
		const FieldHardwareView& split_view = pf1.split_active ? pf1 : pf2;
		if (split_view.split_active) {
			const eng::u16 raster = static_cast<eng::u16>((m_config.diwstrt >> 8u) + split_view.split_line);
			if (raster > 0xffu) return false;
			scheduler.wait_line(static_cast<eng::u8>(raster));
			for (eng::u8 plane = 0; plane < m_config.plane_count; ++plane) {
				const bool first = !m_config.dual ? true : ((plane & 1u) == 0u);
				const FieldHardwareView& view = first ? pf1 : pf2;
				const eng::u8 local = m_config.dual ? static_cast<eng::u8>(plane / 2u) : plane;
				const eng::u32 address = plane_address(view, local) +
					(view.split_display_byte_offset - view.display_byte_offset);
				const eng::u8 hardware = m_config.dual ? hardware_plane(view, local) : plane;
				scheduler.move_bitplane_pointer(hardware, reinterpret_cast<const void*>(address));
			}
		}
		scheduler.emit_palette(m_config.palette);
		scheduler.wait_line(0xf8);
		scheduler.move(copper::Register::COLOR00, 0x0000);
		scheduler.end();
		m_copper_words = scheduler.words_used();
		m_ok = scheduler.ok();
		if (!m_ok) {
			return false;
		}
		return true;
	}

	/// Parchea en el bloque inactivo BPLCON1 + los punteros de ambas vistas.
	bool patch(const FieldHardwareView& pf1, const FieldHardwareView& pf2) {
		u16* const words = static_cast<u16*>(m_copper_blocks[m_active_copper ^ 1u].data);
		const eng::u8 fine1 = pf1.fine_x;
		const eng::u8 fine2 = pf2.fine_x;
		words[5] = m_config.dual
			? static_cast<eng::u16>((static_cast<eng::u16>(fine2 & 15u) << 4u) | (fine1 & 15u))
			: static_cast<eng::u16>((fine1 & 15u) | ((fine1 & 15u) << 4u));
		patch_pointers(static_cast<u8>(m_active_copper ^ 1u), pf1);
		if (m_config.dual) {
			patch_pointers(static_cast<u8>(m_active_copper ^ 1u), pf2);
		}
		m_active_copper = static_cast<u8>(m_active_copper ^ 1u);
		return true;
	}

	DpfComposerConfig m_config {};
	eng::MemoryBlock m_copper_blocks[2] {};
	eng::u16 m_copper_words = 0;
	u8 m_active_copper = 0;
	bool m_copper_initialized = false;
	bool m_initialized = false;
	bool m_ok = false;
};

} // namespace eng::field
