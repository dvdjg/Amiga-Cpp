#pragma once

/// \file heap_alloc.hpp
/// `eng::util::HeapAlloc`: asignador de **heap del anfitrion** que cumple el
/// contrato `Allocator` del engine, para las **herramientas host** (transpiladores,
/// conversores, generadores de codigo) que construyen contenedores dinamicos de
/// tamano no acotado (`Vector`, `DynamicHashMap`, `DynamicString`).
///
/// No forma parte del runtime Amiga: en `m68k` no existe `malloc` freestanding y
/// este header lo rechaza en compilacion. En el engine embebido se usan
/// `BumpAlloc`/`InlineAlloc`/`ArenaAlloc` (sin heap, coste visible). Aqui el
/// proposito es que una herramienta de PC reutilice las mismas estructuras y el
/// mismo estilo sin caer en la STL.
///
/// Alineacion: `malloc` garantiza la alineacion natural maxima (16 en x86-64),
/// asi que por debajo no hace falta nada; para `align` mayores se sobre-reserva y
/// se guarda el puntero original justo antes del bloque alineado.
///
/// Uso:
///   eng::util::Vector<eng::u8, eng::util::HeapAlloc> rom;
///   rom.push_back(0x4Cu);

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

#if defined(__m68k__)
#error "HeapAlloc es solo para herramientas host; el runtime Amiga no tiene heap"
#endif

namespace eng::util {

struct HeapAlloc {
	/// Entrega `bytes` con la alineacion pedida; vacio si no hay memoria.
	[[nodiscard]] Span<u8> allocate(usize bytes, usize align) noexcept {
		if (bytes == 0u) {
			return {};
		}
		const usize a = align == 0u ? 1u : align;
		// Hueco extra para el puntero original + el desalineado hasta `a`.
		void* raw = __builtin_malloc(bytes + a + sizeof(void*));
		if (raw == nullptr) {
			return {};
		}
		const uintptr base = reinterpret_cast<uintptr>(raw) + sizeof(void*);
		const uintptr aligned = eng::align_up_ptr(base, a);
		// Guarda el puntero original inmediatamente antes del bloque alineado.
		reinterpret_cast<void**>(aligned)[-1] = raw;
		return Span<u8> {reinterpret_cast<u8*>(aligned), bytes};
	}

	/// Libera un bloque entregado por `allocate`. Ignora el vacio (no-op).
	void deallocate(Span<u8> block) noexcept {
		if (block.empty()) {
			return;
		}
		void* raw = reinterpret_cast<void**>(block.data())[-1];
		__builtin_free(raw);
	}
};

} // namespace eng::util
