#pragma once

/// \file double_buffer.hpp
/// **Doble buffer de copperlist**: dos bloques de instrucciones; la CPU escribe
/// siempre el **inactivo** (reemitiéndolo o parcheando sus words de valor) y `flip()`
/// lo deja listo para publicar. El Copper ejecuta el **activo**.
///
/// Publicar es solo el swap de `COP1LC` (`install`), nunca `COPJMP1`: el Copper recarga
/// el puntero al comienzo del siguiente VBlank, así que el display no ve una lista a
/// medio escribir. Ver `docs/engine/architecture/DISPLAY_COMPOSITION.md`.
///
/// Es la implementación única del patrón que hoy repiten `TileScrollScene` y los
/// compositores de field (`Xlimited*Composer`): en ambos, el bloque inactivo se rellena
/// con un `copper::Scheduler` (política `Reemit`) o se parchean las words que dependen
/// de la cámara con los handles devueltos por `Scheduler::move_at` (política `Patch`).
///
/// Uso típico (política Patch, como `TileScrollScene`):
///
///   copper::DoubleBuffer copper;
///   copper.begin(memory, config.copper_bytes);
///   // primer frame: emitir la lista completa en el bloque inactivo y voltear
///   { copper::Scheduler s { copper.inactive_block() }; ...; m_handle = s.move_at(...); }
///   copper.flip();
///   // por frame: parchear el inactivo y publicar
///   copper.inactive_words()[m_handle + 1u] = nuevo_valor;
///   copper.flip();
///   copper.install(backend);
///
/// ```text
///   CPU (escribe)                              Copper (ejecuta)
///   ─────────────                              ────────────────
///   inactive_block() ──► [ bloque INACTIVO ]   [ bloque ACTIVO ] ──► salida de vídeo
///   (reemitir con Scheduler o parchear             ▲
///    inactive_words()[handle])                     │ COP1LC (se recarga al inicio del VBlank)
///        │                                         │
///        └──────────── flip() (intercambia) ───────┘
///
///   install(backend) = swap de COP1LC; NUNCA COPJMP1 (no se ve una lista a medio escribir)
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/copper/template.hpp>
#include <eng/memory/arena.hpp>

namespace eng::copper {

class DoubleBuffer {
public:
	/// Reserva los dos bloques (mismo tamaño y alineación) en Chip RAM.
	/// `active` arranca en 1 para que el primer bloque que se escribe sea el 0.
	bool begin(eng::MemorySystem& memory, u32 bytes_per_block, u8 alignment = 16) {
		m_blocks[0] = memory.chip.allocate_block<eng::CopperTag>(bytes_per_block, alignment);
		m_blocks[1] = memory.chip.allocate_block<eng::CopperTag>(bytes_per_block, alignment);
		m_active = 1;
		m_ok = m_blocks[0].valid() && m_blocks[1].valid() && bytes_per_block >= 4u;
		return m_ok;
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u8 active_index() const { return m_active; }

	constexpr eng::Block<eng::CopperTag> active_block() const { return m_blocks[m_active]; }
	constexpr eng::Block<eng::CopperTag> inactive_block() const { return m_blocks[m_active ^ 1u]; }

	/// Words del bloque que el Copper está ejecutando (solo lectura en la práctica).
	constexpr u16* active_words() const {
		return reinterpret_cast<u16*>(m_blocks[m_active].view.data());
	}
	/// Words del bloque que la CPU debe escribir este frame.
	constexpr u16* inactive_words() const {
		return reinterpret_cast<u16*>(m_blocks[m_active ^ 1u].view.data());
	}

	/// Un `Scheduler` ligado al bloque inactivo (para reemitir o emitir+MOVE inicial).
	constexpr Scheduler inactive_scheduler() const { return Scheduler { inactive_block() }; }

	/// Un `Template` ligado al bloque inactivo: construye la ESTRUCTURA una vez y deja
	/// *slots* para parchear por frame solo las palabras que cambian (ver `template.hpp`).
	constexpr Template inactive_template() const { return Template { inactive_block() }; }

	/// El inactivo pasa a ser el activo: se publicará en el próximo `install`.
	constexpr void flip() { m_active = static_cast<u8>(m_active ^ 1u); }

	/// Toma el control del display mostrando el bloque activo (una vez).
	template <typename Backend>
	void takeover(Backend& backend) const {
		if (m_ok) {
			backend.takeover_display(active_words());
		}
	}

	/// Publica el bloque activo (swap de `COP1LC`). Llamar tras VBlank.
	template <typename Backend>
	void install(Backend& backend) const {
		if (m_ok) {
			backend.install_copper_list(active_words());
		}
	}

private:
	eng::Block<eng::CopperTag> m_blocks[2] {};
	u8 m_active = 1;
	bool m_ok = false;
};

} // namespace eng::copper
