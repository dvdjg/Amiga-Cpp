#pragma once

/// \file memory_kind.hpp
/// Clasificacion logica de la memoria desde el punto de vista del engine.
///
/// Vive en `core/` (y no en `memory/`) porque forma parte de la identidad de una
/// reserva: `eng::Block<Tag>` lleva su `MemoryKind` junto al dominio, de modo que
/// una lista de Copper sabe a la vez que **es** una copperlist y **de que** memoria
/// procede. Separar dominio (que es el dato) de medio (donde vive) permite, por
/// ejemplo, construir una lista de Copper en Fast RAM y copiarla a Chip con el
/// Blitter antes de instalarla.

#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng {

/// Tipo logico de memoria desde el punto de vista del engine.
///
/// No describe necesariamente el flag exacto de Exec. Por ejemplo, la Slow RAM del
/// A500 puede aparecer como MEMF_FAST para AmigaOS, pero el engine la etiqueta como
/// `Slow` porque sigue sin ser Fast RAM CPU-privada.
enum class MemoryKind : u8 {
	Chip,
	Slow,
	Fast,
	Any,
};

/// **Dirección de un banco concreto** (rol tipado): `Address<MemoryKind::Chip>` y
/// `Address<MemoryKind::Fast>` son tipos **distintos** y no se mezclan; una API que exige DMA
/// (Chip) no compila si recibe otra. Etiqueta vacía: **coste cero**. Ver
/// `INTERNAL_TYPE_SYSTEM.md` §3.2/§3.6.
///
/// Aritmética de dirección: `address + offset -> address` (conserva el banco) y
/// `address - address -> offset` (desplazamiento en bytes). Nunca se pierde el tipo al sumar
/// enteros puros, así el resultado se pasa tal cual a una API DMA sin `cast`.
template <MemoryKind K>
struct Address {
	uintptr value = 0u;

	constexpr Address() noexcept = default;
	explicit constexpr Address(uintptr v) noexcept : value(v) {}
	/// Frontera explícita: interpreta una **dirección de almacenamiento** como `Address<K>`.
	/// Solo es lícito cuando la procedencia del búfer ya garantiza el medio `K` (banco/arena,
	/// `gfx::Bitmap`, o un búfer en la sección `.MEMF_CHIP`). Se nombra para que el acto se lea como tal;
	/// no hay ctor implícito desde `void*`. Ver regla del cast en `CODING_STYLE.md`.
	[[nodiscard]] static constexpr Address from_storage(const void* p) noexcept {
		return Address { reinterpret_cast<uintptr>(p) };
	}

	[[nodiscard]] constexpr bool valid() const noexcept { return value != 0u; }
	/// Puntero mutable a la dirección (escape explícito solo en la frontera con API de punteros).
	/// `const` como un puntero-miembro: la dirección no cambia, el almacén sí puede escribirse.
	[[nodiscard]] constexpr u8* ptr() const noexcept { return reinterpret_cast<u8*>(value); }
	/// Puntero constante a la dirección (escape explícito en la frontera).
	[[nodiscard]] constexpr const u8* cptr() const noexcept {
		return reinterpret_cast<const u8*>(value);
	}

	/// Suma un **offset entero** (bytes): el resultado sigue siendo `Address<K>` (mismo banco).
	/// El offset conserva su tipo dentro de la expresión (no se fuerza a `uintptr`), así GCC puede
	/// usar direccionamiento indexado de 16 bits (`.w`) cuando cabe en `s16`/`s8`.
	template <class Off>
		requires eng::util::is_integral_v<Off>
	[[nodiscard]] constexpr Address operator+(Off off) const noexcept {
		return Address { static_cast<uintptr>(value + off) };
	}
	/// Resta un **offset entero** (bytes): el resultado sigue siendo `Address<K>` (mismo banco).
	template <class Off>
		requires eng::util::is_integral_v<Off>
	[[nodiscard]] constexpr Address operator-(Off off) const noexcept {
		return Address { static_cast<uintptr>(value - off) };
	}
	/// Avanza la dirección en sitio sumando un offset entero de bytes.
	template <class Off>
		requires eng::util::is_integral_v<Off>
	constexpr Address& operator+=(Off off) noexcept {
		value = static_cast<uintptr>(value + off);
		return *this;
	}
	/// Retrocede la dirección en sitio restando un offset entero de bytes.
	template <class Off>
		requires eng::util::is_integral_v<Off>
	constexpr Address& operator-=(Off off) noexcept {
		value = static_cast<uintptr>(value - off);
		return *this;
	}
	/// Distancia en bytes entre dos direcciones **del mismo banco**.
	[[nodiscard]] constexpr uintptr operator-(Address o) const noexcept {
		return static_cast<uintptr>(value - o.value);
	}
	/// Igualdad de dirección (mismo banco).
	[[nodiscard]] constexpr bool operator==(Address o) const noexcept { return value == o.value; }
	/// Desigualdad de dirección (mismo banco).
	[[nodiscard]] constexpr bool operator!=(Address o) const noexcept { return value != o.value; }
	/// Orden por dirección (mismo banco).
	[[nodiscard]] constexpr bool operator<(Address o) const noexcept { return value < o.value; }
};

} // namespace eng
