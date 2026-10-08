#pragma once

/// \file custom_regs.hpp
/// **Bloque de registros custom** (`$DFF000`): el espacio de registros del chipset. No es RAM,
/// así que no se describe con `Address<MemoryKind::*>` (que tipan bancos de memoria) sino con un
/// tipo propio que **solo** puede proceder de dos sitios: el bloque real del hardware
/// (`instance()`) o la frontera explícita `from_storage` (mocks de tests host). Un
/// `volatile u16*` suelto ya no viaja por la API: quien programa hardware pide `CustomRegs`.
///
/// Coste cero: envoltorio de un puntero, trivialmente copiable; los accesos son una lectura de
/// miembro. Ver `INTERNAL_TYPE_SYSTEM.md` §3.5 y §4.3.

#include <eng/core/types/types.hpp>

namespace eng::amiga {

class CustomRegs {
public:
	constexpr CustomRegs() noexcept = default;

	/// Bloque de registros del hardware real (`$DFF000`). Único en la máquina.
	[[nodiscard]] static CustomRegs instance() noexcept {
		return CustomRegs {reinterpret_cast<volatile eng::u16*>(0x00dff000u)};
	}

	/// **Frontera explícita**: trata un buffer de RAM como bloque de registros. Solo para los
	/// mocks de tests host (en un Amiga no hay motivo para usarla); se nombra para que el acto
	/// se lea como tal, igual que `Address<Chip>::from_storage`.
	[[nodiscard]] static CustomRegs from_storage(volatile eng::u16* regs) noexcept {
		return CustomRegs {regs};
	}

	/// Registro por **índice de palabra** (offset en bytes / 2), como en los mapas de registros.
	[[nodiscard]] constexpr volatile eng::u16& word(eng::u16 word_index) const noexcept {
		return m_regs[word_index];
	}

private:
	explicit constexpr CustomRegs(volatile eng::u16* regs) noexcept : m_regs(regs) {}
	volatile eng::u16* m_regs = nullptr;
};

} // namespace eng::amiga
