#pragma once

/// \file chunk_cache.hpp
/// Cache de chunks residentes para un `WorldMap` disperso: mantiene `Capacity`
/// chunks en un pool de Chip RAM aportado por el llamador y carga el resto bajo
/// demanda con un **loader estático** (concept `ChunkLoader`), p. ej. desde una tarea
/// de fondo. Evita tener todo el mundo en memoria: sólo lo visitado.
///
/// No posee el pool (es del llamador) -> el coste de Chip RAM entra en el modelo de
/// recursos. Ver `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2.
///
/// El `Loader` es un **tipo** con `load(cx, cy, TileBankBuffer) -> LoadResult`, no un
/// puntero a función: el compilador verifica la interfaz y `ChunkCache` se
/// especializa por loader. Devuelve `LoadResult`:
/// `Ready` (datos escritos), `Empty` (chunk ausente de verdad; el loader rellena
/// `empty_tile` y queda residente para no reintentar) o `Pending` (carga asíncrona
/// aún no lista: NO se marca residente y se reintentará). El detalle se diseña en
/// `docs/engine/architecture/STREAMING_LOADER.md`.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/hash_map.hpp>

namespace eng::field {

/// Resultado de una petición de carga al `Loader`.
enum class LoadResult : eng::u8 {
	Ready = 0,   ///< chunk escrito en el destino; se marca residente.
	Empty = 1,   ///< chunk ausente; el `Loader` rellenó `empty_tile`; residente.
	Pending = 2, ///< aún no disponible; no se marca residente (se reintenta).
};

/// Contrato de un cargador de chunks. Estático (templates), sin punteros.
template <class Loader>
concept ChunkLoader = requires(Loader& loader, eng::s32 cx, eng::s32 cy, eng::TileBankBuffer dst) {
	{ loader.load(cx, cy, dst) };
};

/// Clave exacta `(cx, cy)` de un chunk residente. No se empaqueta a 16 bits: los
/// índices de chunk pueden ser negativos o grandes y una colisión daría un falso hit.
struct ChunkKey {
	eng::s32 cx = 0;
	eng::s32 cy = 0;
	[[nodiscard]] constexpr bool operator==(const ChunkKey& other) const noexcept {
		return cx == other.cx && cy == other.cy;
	}
};

} // namespace eng::field

namespace eng::util {
/// Hash de la clave de chunk (xor de dos avalanchas + rotación; sin multiplicar 32×32).
template <>
struct Hash<eng::field::ChunkKey> {
	[[nodiscard]] eng::u32 operator()(const eng::field::ChunkKey& k) const noexcept {
		return hash_u32(static_cast<eng::u32>(k.cx)) ^
		       rotl(hash_u32(static_cast<eng::u32>(k.cy)), 16u);
	}
};
} // namespace eng::util

namespace eng::field {

template <eng::u16 ChunkSize, eng::u8 Capacity, ChunkLoader Loader>
class ChunkCache {
public:
	static constexpr eng::u32 kCells = static_cast<eng::u32>(ChunkSize) * ChunkSize;
	static constexpr eng::u32 kPoolCells = static_cast<eng::u32>(Capacity) * kCells;

	/// `loader` debe vivir más que la caché (referencia no propietaria); `pool` debe
	/// tener al menos `kPoolCells` words (Chip RAM del llamador).
	bool init(Loader& loader, eng::TileBankBuffer pool) {
		if (pool.size() < kPoolCells) return false;
		m_loader = &loader;
		m_pool = pool;
		for (eng::u8 i = 0; i < Capacity; ++i) m_slots[i] = Slot {};
		m_index.clear();
		m_clock = 0;
		m_loads = 0;
		m_evictions = 0;
		m_hits = 0;
		m_empties = 0;
		m_pendings = 0;
		return true;
	}

	/// Devuelve las celdas del chunk si ESTÁ RESIDENTE (sin cargar ni tocar stats).
	/// El índice hash lleva `(cx,cy) -> ranura` en `O(1)`.
	const eng::u16* find(eng::s32 cx, eng::s32 cy) const {
		const eng::u8* slot = m_index.find(ChunkKey {cx, cy});
		if (slot == nullptr) {
			return nullptr;
		}
		return m_pool.data() + static_cast<eng::u32>(*slot) * kCells;
	}

	/// Devuelve las celdas del chunk (residente o recién cargado). `nullptr` si el
	/// `Loader` devolvió `Pending` (no queda residente) o si no hay fuente.
	const eng::u16* get(eng::s32 cx, eng::s32 cy) {
		if (const eng::u8* slot = m_index.find(ChunkKey {cx, cy})) {
			Slot& s = m_slots[*slot];
			s.stamp = ++m_clock;
			++m_hits;
			return m_pool.data() + static_cast<eng::u32>(*slot) * kCells;
		}
		eng::u8 victim = 0;
		bool any_free = false;
		eng::u32 oldest = 0xffffffffu;
		for (eng::u8 i = 0; i < Capacity; ++i) {
			if (!m_slots[i].valid) { victim = i; any_free = true; break; }
			if (m_slots[i].stamp < oldest) { oldest = m_slots[i].stamp; victim = i; }
		}
		Slot& s = m_slots[victim];
		eng::TileBankBuffer dst = m_pool.subspan(static_cast<eng::u32>(victim) * kCells, kCells);
		const LoadResult r = m_loader->load(cx, cy, dst);
		if (r == LoadResult::Pending) {
			++m_pendings; // no se toca el slot: se reintentará en la próxima petición
			return nullptr;
		}
		if (s.valid && !any_free) {
			++m_evictions;
			m_index.erase(ChunkKey {s.cx, s.cy});
		}
		s.cx = cx;
		s.cy = cy;
		s.valid = true;
		s.stamp = ++m_clock;
		++m_loads;
		if (r == LoadResult::Empty) ++m_empties;
		m_index.insert_or_assign(ChunkKey {cx, cy}, victim);
		return dst.data();
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
	eng::Ref<Loader> m_loader {}; ///< loader de chunks (referencia no propietaria)
	eng::TileBankBuffer m_pool {};
	eng::util::Array<Slot, Capacity> m_slots {};
	/// Índice `(cx,cy) -> ranura` para no recorrer los slots en cada `find`/`get`.
	eng::util::HashMap<ChunkKey, eng::u8, Capacity> m_index {};
	eng::u32 m_clock = 0;
	eng::u32 m_loads = 0;
	eng::u32 m_evictions = 0;
	eng::u32 m_hits = 0;
	eng::u32 m_empties = 0;
	eng::u32 m_pendings = 0;
};

} // namespace eng::field
