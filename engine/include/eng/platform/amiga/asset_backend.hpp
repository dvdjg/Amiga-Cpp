#pragma once

/// \file asset_backend.hpp
/// **Backend Amiga de `res::AssetCache`**: memoria de los bancos de `MemoryManager` y E/S del
/// mini-SO (`os::file_*`). Con él, la caché de assets (`eng/res/asset_cache.hpp`) funciona en
/// hardware: `declare` + `prefetch` reservan en Chip/Slow y lanzan la lectura asíncrona; la
/// finalización llega como `FileDone`/`FileError` y se enruta con `res::route_io` ->
/// `cache.on_load_done(id, result)` (`eng/res/resources.hpp`).
///
/// El contrato del cache es **duck-typed** (`alloc`/`free`/`load`); este backend lo cumple:
///   - `alloc(bytes, bank)`: reserva en el banco tipado (Chip o Fast→Slow);
///   - `free(...)`: devuelve la reserva física usando una tabla fija de owners por puntero;
///   - `load(id, path, dst)`: abre y lanza `file_read_async` con el cookie `IoUser{'A', id}`.
///
/// Ver `docs/engine/architecture/RESOURCE_SYSTEM.md` y `PUBLIC_GAME_API.md` §2.1.4.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/os/file.hpp>
#include <eng/res/asset_cache.hpp>

namespace eng::amiga {

/// Backend de `AssetCache` sobre los **bancos tipados** (`MemoryManager`) + E/S del mini-SO.
class AssetCacheBackend {
public:
	static constexpr eng::u8 kMaxAllocations = 8u;

	constexpr AssetCacheBackend() = default;
	explicit constexpr AssetCacheBackend(eng::MemoryManager& memory) noexcept : m_memory(memory) {}

	/// Reserva `bytes` en el banco (Chip, o Slow para `Fast`). El cache usa el tamaño real del
	/// destino; el pool alinea la base, así que no hace falta margen.
	[[nodiscard]] eng::Span<eng::u8> alloc(eng::u32 bytes, eng::res::MemBank bank) noexcept {
		if (m_allocation_count >= kMaxAllocations) {
			return {};
		}
		eng::Block<eng::PlaneTag> block {};
		if (bank == eng::res::MemBank::Chip) {
			eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> b =
				m_memory->chip().reserve<eng::PlaneTag>(bytes, 4u);
			if (!b.valid()) return {};
			block = eng::Block<eng::PlaneTag> {
				eng::Bytes<eng::PlaneTag> {b.view.data(), b.view.size()}, eng::MemoryKind::Chip};
		} else {
			block = eng::fast_or_slow<eng::PlaneTag>(*m_memory, bytes, 4u);
		}
		if (!block.valid()) return {};
		m_allocations[m_allocation_count++] = Allocation {block.view.data(), block.kind};
		return block.view.raw();
	}

	/// Devuelve la reserva física al banco que realmente la entregó. El `MemBank` del slot puede
	/// ser `Fast` aunque el fallback efectivo haya sido `Slow`, por eso se usa el registro local.
	void free(eng::Span<eng::u8> view, eng::res::MemBank) noexcept {
		for (eng::u8 i = 0u; i < m_allocation_count; ++i) {
			if (m_allocations[i].ptr != view.data()) continue;
			release(m_allocations[i]);
			for (eng::u8 j = i; j + 1u < m_allocation_count; ++j) {
				m_allocations[j] = m_allocations[j + 1u];
			}
			--m_allocation_count;
			return;
		}
	}

	/// Abre el fichero y lanza la lectura asíncrona a `dst` (0..`dst.size()`). El resultado
	/// llega por `FileDone`/`FileError` con el cookie `IoUser{'A', id}`. `false` si no abre.
	bool load(eng::res::AssetId id, const char* path, eng::Span<eng::u8> dst) noexcept {
		const eng::os::FileHandle h = eng::os::file_open(path, eng::os::FileMode::Read);
		if (h == 0u) {
			return false;
		}
		eng::os::IoNotify n {};
		n.cookie = eng::os::IoUser {static_cast<eng::u8>('A'), id}.encode();
		return eng::os::file_read_async(h, dst, 0u, n);
	}

private:
	struct Allocation {
		const eng::u8* ptr = nullptr;
		eng::MemoryKind kind = eng::MemoryKind::Any;
	};

	void release(const Allocation& allocation) noexcept {
		if (allocation.kind == eng::MemoryKind::Chip) {
			m_memory->chip().release(allocation.ptr);
		} else if (allocation.kind == eng::MemoryKind::Fast) {
			m_memory->fast().release(allocation.ptr);
		} else if (allocation.kind == eng::MemoryKind::Slow) {
			m_memory->slow().release(allocation.ptr);
		}
	}

	eng::Ref<eng::MemoryManager> m_memory {};
	Allocation m_allocations[kMaxAllocations] {};
	eng::u8 m_allocation_count = 0u;
};

} // namespace eng::amiga
