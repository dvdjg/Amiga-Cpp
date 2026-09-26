#pragma once

/// \file mem_spec.hpp
/// **Petición de memoria en dos ejes** (`eng::MemSpec`): un **requisito** duro (contrato de
/// corrección) y una **preferencia** blanda (rendimiento). Es la forma de pedir memoria sin
/// explosión combinatoria: `3 requisitos × 3 preferencias` expresados como dos enums pequeños,
/// en vez de nueve tipos.
///
/// - `MemReq::Chip`     — debe verlo Agnus (bitplanes, copper, audio, sprites). **Compile-time**.
/// - `MemReq::NonChip`  — no debe verlo Agnus (buffers CPU que no compitan con el DMA).
/// - `MemReq::Any`      — indiferente (lo decide la política).
/// - `MemHint::Fast/Slow/None` — preferencia cuando hay elección.
///
/// `MemoryKind` sigue siendo el **medio físico** de un bloque (dato); `MemSpec` es la **petición**.
/// `resolve_bank` mapea la petición a un banco físico según la disponibilidad (`MemAvail`).

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng {

/// Requisito duro (contrato de corrección).
enum class MemReq : u8 {
	Any,     ///< indiferente
	Chip,    ///< debe ser visible por Agnus (DMA)
	NonChip, ///< **no** debe ser visible por Agnus
};

/// Preferencia blanda (rendimiento), cuando el requisito deja elegir.
enum class MemHint : u8 { None, Fast, Slow };

/// Petición de memoria: requisito + preferencia.
struct MemSpec {
	MemReq req = MemReq::Any;
	MemHint hint = MemHint::None;
};

/// Disponibilidad física de bancos (la fija el backend tras sondear el hardware).
struct MemAvail {
	bool chip = false;
	bool slow = false;
	bool fast = false;
};

/// `true` si el banco `kind` está disponible.
[[nodiscard]] constexpr bool has(const MemAvail& a, MemoryKind kind) noexcept {
	switch (kind) {
	case MemoryKind::Chip:
		return a.chip;
	case MemoryKind::Slow:
		return a.slow;
	case MemoryKind::Fast:
		return a.fast;
	default:
		return false;
	}
}

/// Primer banco disponible de `order` (o `MemoryKind::Any` si ninguno).
[[nodiscard]] constexpr MemoryKind pick(const MemAvail& a, eng::Span<const MemoryKind> order) noexcept {
	for (eng::usize i = 0u; i < order.size(); ++i) {
		if (has(a, order[i])) {
			return order[i];
		}
	}
	return MemoryKind::Any;
}

/// **Resuelve el banco físico** para `spec`. Reglas:
/// - `Chip` → Chip (o ninguno): no cae a bancos que Agnus no ve.
/// - `NonChip` → Fast/Slow según el hint, **nunca Chip**.
/// - `Any` → Fast/Slow/Chip según el hint (por defecto se preserva Chip para el DMA).
/// Devuelve `MemoryKind::Any` si no hay ningún banco válido.
[[nodiscard]] constexpr MemoryKind resolve_bank(MemSpec spec, MemAvail a) noexcept {
	switch (spec.req) {
	case MemReq::Chip:
		return a.chip ? MemoryKind::Chip : MemoryKind::Any;
	case MemReq::NonChip: {
		const MemoryKind fast_first[2] = {MemoryKind::Fast, MemoryKind::Slow};
		const MemoryKind slow_first[2] = {MemoryKind::Slow, MemoryKind::Fast};
		return pick(a, spec.hint == MemHint::Slow ? slow_first : fast_first);
	}
	default: { // Any: preservar Chip para el DMA; el hint decide Fast/Slow primero.
		const MemoryKind fast_first[3] = {MemoryKind::Fast, MemoryKind::Slow,
						  MemoryKind::Chip};
		const MemoryKind slow_first[3] = {MemoryKind::Slow, MemoryKind::Fast,
						  MemoryKind::Chip};
		return pick(a, spec.hint == MemHint::Slow ? slow_first : fast_first);
	}
	}
}

} // namespace eng
