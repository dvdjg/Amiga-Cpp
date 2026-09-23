#pragma once

/// \file zobrist.hpp
/// Generador **determinista** de claves Zobrist y acumulador XOR, genéricos de
/// cualquier juego de tablero.
///
/// El motor de búsqueda hashea posiciones con Zobrist para la tabla de
/// transposición y la detección de repeticiones. El 68000 no tiene aritmética
/// nativa de 64 bits, así que las claves son `u32` y el generador es un xorshift32
/// evaluado en `constexpr` (la tabla acaba en `.rodata`, sin coste en runtime).
///
/// `zobrist_step` da una secuencia reproducible en cualquier plataforma; cada juego
/// construye su tabla (pieza×casilla, turno, enroques, al paso…) con ella.
///
/// Uso:
///   eng::u32 state = 0x9e3779b9u;
///   const eng::u32 key = eng::board::zobrist_step(state);
///
/// Verificación: HOST-138.

#include <eng/core/types/types.hpp>

namespace eng::board {

/// Siguiente valor de la secuencia xorshift32. El estado no puede ser 0.
[[nodiscard]] constexpr u32 zobrist_step(u32& state) noexcept {
	state ^= state << 13u;
	state ^= state >> 17u;
	state ^= state << 5u;
	return state;
}

/// Acumulador XOR de componentes de una clave. Permite aplicar y **deshacer**
/// (`toggle` es su propia inversa) sin recalcular la posición entera.
class ZobristKey {
public:
	constexpr ZobristKey() noexcept = default;

	/// Aplica/deshace una componente (XOR).
	constexpr void toggle(u32 value) noexcept { m_key ^= value; }

	constexpr void reset() noexcept { m_key = 0u; }

	[[nodiscard]] constexpr u32 value() const noexcept { return m_key; }

	[[nodiscard]] friend constexpr bool operator==(const ZobristKey&, const ZobristKey&) noexcept = default;

private:
	u32 m_key = 0u;
};

} // namespace eng::board
