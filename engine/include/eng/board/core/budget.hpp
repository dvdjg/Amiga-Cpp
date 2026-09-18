#pragma once

/// \file budget.hpp
/// Presupuesto de memoria de `eng::board`: perfiles `P20`…`P1M` y reparto
/// determinista de la RAM libre entre tabla de transposición, libro, caché externa
/// y pila de búsqueda.
///
/// Regla del engine: **ningún tamaño se fija con macros**; se calcula en `init` a
/// partir de la RAM libre real (modelo de memoria del engine o contador de arena) y
/// se expone en un `MemoryPlan`. Así el mismo binario funciona en un A500 base
/// (`P20`) y en uno ampliado (`P512`/`P1M`).
///
/// El plan elige el perfil **mayor cuyo footprint planificado cabe** en los bytes
/// libres; si no cabe ni el mínimo, devuelve `P20` como fallback (con footprint
/// sujeto a revisión por el llamador).
///
/// Verificación: HOST-139.

#include <eng/core/types.hpp>

namespace eng::board {

/// Tamaño de una entrada de TT empaquetada (clave parcial + profundidad + score +
/// jugada + flags).
inline constexpr u32 kTtEntryBytes = 12u;

/// Perfiles de footprint objetivo del motor completo.
enum class MemoryProfile : u8 {
	P20 = 0u,
	P64,
	P128,
	P256,
	P512,
	P1M,
	Count,
};

/// Reparto de memoria decidido. Todos los campos son bytes salvo `tt_entries`,
/// `multi_pv` y `null_move`.
struct MemoryPlan {
	MemoryProfile profile = MemoryProfile::P20;
	u32 free_bytes = 0u;
	u32 engine_overhead_bytes = 0u;
	u32 search_stack_bytes = 0u;
	u32 tt_entries = 0u;
	u32 book_bytes = 0u;
	u32 cache_bytes = 0u;
	u32 refutation_bytes = 0u;
	u8 multi_pv = 1u;
	bool null_move = false;

	[[nodiscard]] constexpr u32 tt_bytes() const noexcept { return tt_entries * kTtEntryBytes; }

	[[nodiscard]] constexpr u32 planned_bytes() const noexcept {
		return engine_overhead_bytes + search_stack_bytes + tt_bytes() + book_bytes + cache_bytes +
		       refutation_bytes;
	}
};

/// Valores fijos de cada perfil. Los huecos entre perfiles se cubren con la TT
/// derivada en `plan_memory` (aquí están las **capacidades**).
[[nodiscard]] constexpr MemoryPlan profile_plan(MemoryProfile profile) noexcept {
	MemoryPlan plan {};
	plan.profile = profile;
	switch (profile) {
	case MemoryProfile::P20:
		plan.engine_overhead_bytes = 8192u;
		plan.search_stack_bytes = 4096u;
		plan.tt_entries = 0u;
		plan.book_bytes = 4096u;
		plan.cache_bytes = 4096u;
		plan.refutation_bytes = 1024u;
		plan.multi_pv = 1u;
		plan.null_move = false;
		break;
	case MemoryProfile::P64:
		plan.engine_overhead_bytes = 8192u;
		plan.search_stack_bytes = 8192u;
		plan.tt_entries = 2048u;
		plan.book_bytes = 8192u;
		plan.cache_bytes = 8192u;
		plan.refutation_bytes = 2048u;
		plan.multi_pv = 1u;
		plan.null_move = false;
		break;
	case MemoryProfile::P128:
		plan.engine_overhead_bytes = 8192u;
		plan.search_stack_bytes = 16384u;
		plan.tt_entries = 6144u;
		plan.book_bytes = 12288u;
		plan.cache_bytes = 12288u;
		plan.refutation_bytes = 2048u;
		plan.multi_pv = 2u;
		plan.null_move = false;
		break;
	case MemoryProfile::P256:
		plan.engine_overhead_bytes = 8192u;
		plan.search_stack_bytes = 20480u;
		plan.tt_entries = 16384u;
		plan.book_bytes = 12288u;
		plan.cache_bytes = 12288u;
		plan.refutation_bytes = 2048u;
		plan.multi_pv = 2u;
		plan.null_move = true;
		break;
	case MemoryProfile::P512:
		plan.engine_overhead_bytes = 8192u;
		plan.search_stack_bytes = 32768u;
		plan.tt_entries = 32768u;
		plan.book_bytes = 24576u;
		plan.cache_bytes = 24576u;
		plan.refutation_bytes = 2048u;
		plan.multi_pv = 4u;
		plan.null_move = true;
		break;
	case MemoryProfile::P1M:
		plan.engine_overhead_bytes = 8192u;
		plan.search_stack_bytes = 49152u;
		plan.tt_entries = 65536u;
		plan.book_bytes = 65536u;
		plan.cache_bytes = 65536u;
		plan.refutation_bytes = 2048u;
		plan.multi_pv = 4u;
		plan.null_move = true;
		break;
	case MemoryProfile::Count:
		break;
	}
	return plan;
}

/// Elige el perfil mayor cuyo `planned_bytes()` cabe en `free_bytes` (fallback
/// `P20`). Determinista y sin dependencias de plataforma.
[[nodiscard]] constexpr MemoryPlan plan_memory(u32 free_bytes) noexcept {
	for (int index = static_cast<int>(MemoryProfile::P1M); index >= 0; --index) {
		MemoryPlan candidate = profile_plan(static_cast<MemoryProfile>(index));
		if (candidate.planned_bytes() <= free_bytes) {
			candidate.free_bytes = free_bytes;
			return candidate;
		}
	}
	MemoryPlan fallback = profile_plan(MemoryProfile::P20);
	fallback.free_bytes = free_bytes;
	return fallback;
}

} // namespace eng::board
