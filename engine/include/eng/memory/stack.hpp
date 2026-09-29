#pragma once

/// \file stack.hpp
/// **Pila de CPU** (`eng::Stack`): memoria de un banco concreto y su **tope** alineado (el valor
/// que se carga en `SP`). Sirve para el hilo principal, servicios/IRQs o tareas.
///
/// El banco es un parámetro: `Fast` por defecto (CPU-privada; las IRQs y las tareas van más
/// rápidas y no compiten con el bus de Agnus), `Slow`/`Chip` cuando haga falta (p. ej. una pila
/// que deba ser DMA-visible, uso muy limitado). La variante de runtime elige **Fast si la hay,
/// si no Slow**.
///
/// ```cpp
/// eng::Stack s = eng::fast_or_slow_stack(mm, 8192u);
/// if (s.valid()) { /* cargar s.top en SP */ }
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/memory/memory_manager.hpp>

namespace eng {

/// Pila de CPU: bloque de un banco + **tope** alineado (valor para `SP`). `valid()` = hay memoria.
struct Stack {
	Block<StackTag> block {};
	uintptr top = 0u; ///< tope de pila alineado (base + tamaño), para cargar en `SP`
	[[nodiscard]] constexpr bool valid() const noexcept { return top != 0u; }
};

/// Reserva una pila de `bytes` en el banco `Bank` (compile-time). El tope queda alineado a
/// `align` (potencia de dos). Inválida si el banco no tiene hueco.
template <MemoryKind Bank>
[[nodiscard]] inline Stack stack_from(MemBank<Bank>& bank, u32 bytes, u32 align = 8u) noexcept {
	if (align == 0u) {
		align = 1u;
	}
	if (bytes < align) {
		bytes = align;
	}
	const auto blk = bank.template reserve<StackTag>(bytes, align);
	Stack s {};
	s.block = blk;
	if (blk.valid()) {
		s.top = (blk.address().value + bytes) & ~static_cast<uintptr>(align - 1u);
	}
	return s;
}

/// Reserva una pila **Fast si la hay, si no Slow** (la decisión de medio en runtime).
[[nodiscard]] inline Stack fast_or_slow_stack(MemoryManager& mm, u32 bytes,
					      u32 align = 8u) noexcept {
	return mm.has_fast() ? stack_from(mm.fast(), bytes, align)
			     : stack_from(mm.slow(), bytes, align);
}

} // namespace eng
