#pragma once

/// \file chunk_cache.hpp
/// Cache de chunks residentes para un `WorldMap` disperso: mantiene `Capacity`
/// chunks en un pool de Chip RAM aportado por el llamador y carga el resto bajo
/// demanda con un `Loader` (p. ej. desde una tarea de fondo). Evita tener todo el
/// mundo en memoria: sólo lo visitado.
///
/// No posee memoria (el pool es del llamador) -> el coste de Chip RAM entra en el
/// modelo de recursos. Ver `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2.
///
/// El `Loader` devuelve `LoadResult`: `Ready` (datos escritos), `Empty` (chunk
/// ausente de verdad; el `Loader` rellena `empty_tile` y queda residente para no
/// reintentar) o `Pending` (carga asíncrona aún no lista: NO se marca residente y
/// se reintentará). El detalle se diseña en `docs/engine/architecture/STREAMING_LOADER.md`.

#include <eng/core/domains.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::field {

/// Resultado de una petición de carga al `Loader`.
enum class LoadResult : eng::u8 {
	Ready = 0,   ///< chunk escrito en el destino; se marca residente.
	Empty = 1,   ///< chunk ausente; el `Loader` rellenó `empty_tile`; residente.
	Pending = 2, ///< aún no disponible; no se marca residente (se reintenta).
};

template <eng::u16 ChunkSize, eng::u8 Capacity>
class ChunkCache {
public:
	static constexpr eng::u32 kCells = static_cast<eng::u32>(ChunkSize) * ChunkSize;
	static constexpr eng::u32 kPoolCells = static_cast<eng::u32>(Capacity) * kCells;

	/// Carga el chunk `(cx,cy)` en `dst` (kCells words con dominio tilebank).
	/// Ver `LoadResult`.
	struct Loader {
		LoadResult (*load)(void* user, eng::s32 cx, eng::s32 cy, eng::TileBankBuffer dst) = nullptr;
		void* user = nullptr;
	};

	/// `pool` debe tener al menos `kPoolCells` u16 (Chip RAM del llamador).
	bool init(Loader loader, eng::Span<eng::u16> pool) {
		if (loader.load == nullptr || pool.size() < kPoolCells) return false;
		m_loader = loader;
		m_pool = pool;
		for (eng::u8 i = 0; i < Capacity; ++i) m_slots[i] = Slot {};
		m_clock = 0;
		m_loads = 0;
		m_evictions = 0;
		m_hits = 0;
		m_empties = 0;
		m_pendings = 0;
		return true;
	}

	/// Devuelve las celdas del chunk si ESTÁ RESIDENTE (sin cargar ni tocar stats).
	const eng::u16* find(eng::s32 cx, eng::s32 cy) const {
		for (eng::u8 i = 0; i < Capacity; ++i) {
			if (m_slots[i].valid && m_slots[i].cx == cx && m_slots[i].cy == cy) {
				return m_pool.data() + static_cast<eng::u32>(i) * kCells;
			}
		}
		return nullptr;
	}

	/// Devuelve las celdas del chunk (residente o recién cargado). `nullptr` si el
	/// `Loader` devolvió `Pending` (no queda residente) o si no hay fuente.
	const eng::u16* get(eng::s32 cx, eng::s32 cy) {
		for (eng::u8 i = 0; i < Capacity; ++i) {
			if (m_slots[i].valid && m_slots[i].cx == cx && m_slots[i].cy == cy) {
				m_slots[i].stamp = ++m_clock;
				++m_hits;
				return m_pool.data() + static_cast<eng::u32>(i) * kCells;
			}
		}
		eng::u8 victim = 0;
		bool any_free = false;
		eng::u32 oldest = 0xffffffffu;
		for (eng::u8 i = 0; i < Capacity; ++i) {
			if (!m_slots[i].valid) { victim = i; any_free = true; break; }
			if (m_slots[i].stamp < oldest) { oldest = m_slots[i].stamp; victim = i; }
		}
		Slot& s = m_slots[victim];
		eng::u16* dst = m_pool.data() + static_cast<eng::u32>(victim) * kCells;
		const LoadResult r = m_loader.load(m_loader.user, cx, cy, eng::TileBankBuffer { dst, kCells });
		if (r == LoadResult::Pending) {
			++m_pendings; // no se toca el slot: se reintentará en la próxima petición
			return nullptr;
		}
		if (s.valid && !any_free) ++m_evictions;
		s.cx = cx;
		s.cy = cy;
		s.valid = true;
		s.stamp = ++m_clock;
		++m_loads;
		if (r == LoadResult::Empty) ++m_empties;
		return dst;
	}

	constexpr eng::u32 loads() const { return m_loads; }
	constexpr eng::u32 evictions() const { return m_evictions; }
	constexpr eng::u32 hits() const { return m_hits; }
	constexpr eng::u32 empties() const { return m_empties; }
	constexpr eng::u32 pendings() const { return m_pendings; }

private:
	struct Slot {
		eng::s32 cx = 0, cy = 0;
		eng::u32 stamp = 0;
		bool valid = false;
	};
	Loader m_loader {};
	eng::Span<eng::u16> m_pool {};
	Slot m_slots[Capacity] {};
	eng::u32 m_clock = 0;
	eng::u32 m_loads = 0;
	eng::u32 m_evictions = 0;
	eng::u32 m_hits = 0;
	eng::u32 m_empties = 0;
	eng::u32 m_pendings = 0;
};

} // namespace eng::field
