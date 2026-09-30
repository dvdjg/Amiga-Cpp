#pragma once

/// \file asset_backend.hpp
/// **Backend Amiga de `res::AssetCache`**: memoria de los bancos de `MemoryManager` y E/S del
/// mini-SO (`os::file_*`). Con él, la caché de assets (`eng/res/asset_cache.hpp`) funciona en
/// hardware: `declare` + `prefetch` reservan en Chip/Slow y lanzan la lectura asíncrona; la
/// finalización llega como `FileDone`/`FileError` y se enruta con `res::route_io` ->
/// `cache.on_load_done(id, result)` (`eng/res/resources.hpp`).
///
/// El contrato del cache es **duck-typed** (`alloc`/`free`/`load`); este backend lo cumple:
///   - `alloc(bytes, bank)`: devuelve un `MemoryBlock` con el banco efectivo;
///   - `free(...)`: devuelve el bloque tipado al banco que conserva el owner;
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

/// Backend de `AssetCache` sobre los bancos Chip/Fast/Slow de `MemoryManager` y la E/S del mini-SO.
class AssetCacheBackend {
public:
	constexpr AssetCacheBackend() = default;
	explicit constexpr AssetCacheBackend(eng::MemoryManager& memory) noexcept : m_memory(memory) {}

	/// Reserva `bytes` en el banco solicitado (con Fast→Slow como fallback). El cache usa el tamaño real del
	/// destino; el pool alinea la base, así que no hace falta margen.
	[[nodiscard]] eng::MemoryBlock alloc(eng::u32 bytes, eng::res::MemBank bank) noexcept {
		eng::MemoryBlock block {};
		if (bank == eng::res::MemBank::Chip) {
			block = m_memory->chip().pool().allocate(bytes, 4u);
		} else if (bank == eng::res::MemBank::Slow) {
			block = m_memory->slow().pool().allocate(bytes, 4u);
		} else {
			block = m_memory->fast().pool().allocate(bytes, 4u);
			if (!block.valid()) block = m_memory->slow().pool().allocate(bytes, 4u);
		}
		return block;
	}

	/// Devuelve la reserva física al banco efectivo conservado por el bloque. Esto cubre también
	/// Fast→Slow sin una tabla paralela que pueda perder el owner.
	void free(const eng::MemoryBlock& block) noexcept {
		if (!block.valid()) return;
		release(block);
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
	void release(const eng::MemoryBlock& block) noexcept {
		if (block.kind == eng::MemoryKind::Chip) {
			m_memory->chip().pool().free(block.data);
		} else if (block.kind == eng::MemoryKind::Fast) {
			m_memory->fast().pool().free(block.data);
		} else if (block.kind == eng::MemoryKind::Slow) {
			m_memory->slow().pool().free(block.data);
		}
	}

	eng::Ref<eng::MemoryManager> m_memory {};
};

} // namespace eng::amiga
