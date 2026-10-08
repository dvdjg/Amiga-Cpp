#pragma once

/// \file hw_regs.hpp
/// **Bloque de registros del hardware** (`$DFF000`): el espacio de registros del chipset (los
/// «custom chips» en la jerga del Amiga, pero son *los* registros de la máquina: no hay otros).
/// No es RAM, así que no se describe con `Address<MemoryKind::*>` (que tipan bancos de memoria).
///
/// La **ubicación está fijada por el hardware**: en builds de Amiga `HwRegs::instance()` no
/// guarda ninguna dirección configurable — `word()` calcula `$DFF000 + offset` directamente —,
/// de modo que es imposible «rebindear» el bloque a memoria de pila, Fast RAM o cualquier otra
/// dirección. Solo los builds de host (tests) exponen `for_test()`, que existe únicamente para
/// observar la secuencia de registros que programa el motor en un buffer local.
///
/// Coste cero: en Amiga la clase es vacía (sin miembro) y `word()` es una lectura de registro
/// con desplazamiento constante. Ver `INTERNAL_TYPE_SYSTEM.md` §3.5 y §4.3.

#include <eng/core/types/types.hpp>

namespace eng::amiga {

class HwRegs {
public:
#if defined(ENG_AMIGA)
	/// Construye un bloque «vacío»: en Amiga no hay miembro que guardar (la dirección es fija), así que este constructor no aporta ninguna ubicación y es seguro tenerlo público (`HwRegs regs_ {};` de los lotes).
	constexpr HwRegs() noexcept = default;

	/// Bloque de registros del hardware real. En Amiga no hay nada que configurar: la
	/// dirección `$DFF000` es fija y esta función no transporta ninguna.
	[[nodiscard]] static HwRegs instance() noexcept { return HwRegs {}; }

	/// Dirección base del bloque de registros custom en el mapa de memoria del Amiga.
	static constexpr eng::uintptr kBase = 0x00dff000u;

	/// Registro por **índice de palabra** (offset en bytes / 2), como en los mapas de
	/// registros. La dirección se calcula desde `kBase`: no hay miembro que pueda apuntar
	/// a otra memoria.
	[[nodiscard]] volatile eng::u16& word(eng::u16 word_index) const noexcept {
		return *reinterpret_cast<volatile eng::u16*>(kBase + static_cast<eng::uintptr>(word_index) * 2u);
	}
#else
	/// (Host/tests) No hay hardware: el bloque «real» no existe. Los tests de la capa de
	/// plataforma usan `for_test()` con un buffer local; esta instancia solo evita que el
	/// código común (backend) deje de compilar en host.
	[[nodiscard]] static HwRegs instance() noexcept { return HwRegs {s_placeholder}; }

	/// (Host/tests) Construye un bloque «vacío» que apunta al buffer de relleno: mantiene
	/// `HwRegs regs_ {};` de los lotes compilando sin hardware.
	constexpr HwRegs() noexcept = default;

	/// **Solo tests de host**: trata `buffer` como si fuera el bloque de registros, para
	/// observar la secuencia de escrituras del motor. No existe en builds de Amiga (allí el
	/// bloque está fijado por el hardware).
	[[nodiscard]] static HwRegs for_test(volatile eng::u16* buffer) noexcept {
		return HwRegs {buffer};
	}

	/// Registro por índice de palabra sobre el buffer del test.
	[[nodiscard]] volatile eng::u16& word(eng::u16 word_index) const noexcept {
		return m_buffer[word_index];
	}
#endif

private:
#if defined(ENG_AMIGA)
#else
	explicit constexpr HwRegs(volatile eng::u16* buffer) noexcept : m_buffer(buffer) {}

	/// Buffer de relleno para `instance()` en host (los tests usan `for_test`).
	inline static volatile eng::u16 s_placeholder[256] {};

	/// Buffer que hace de bloque de registros en el test (solo host).
	volatile eng::u16* m_buffer = s_placeholder;
#endif
};

} // namespace eng::amiga
