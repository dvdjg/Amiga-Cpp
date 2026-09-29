#pragma once

/// \file asset_backend.hpp
/// **Backend Amiga de `res::AssetCache`**: memoria de las arenas del `MemorySystem` y E/S del
/// mini-SO (`os::file_*`). Con él, la caché de assets (`eng/res/asset_cache.hpp`) funciona en
/// hardware: `declare` + `prefetch` reservan en Chip/Slow y lanzan la lectura asíncrona; la
/// finalización llega como `FileDone`/`FileError` y se enruta con `res::route_io` ->
/// `cache.on_load_done(id, result)` (`eng/res/resources.hpp`).
///
/// El contrato del cache es **duck-typed** (`alloc`/`free`/`load`); este backend lo cumple:
///   - `alloc(bytes, bank)`: reserva en la arena Chip (o Slow para `MemBank::Fast`);
///   - `free(...)`: no-op (las arenas son *bump*; el desalojo libera el slot, no la memoria);
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
	constexpr AssetCacheBackend() = default;
	explicit constexpr AssetCacheBackend(eng::MemoryManager& memory) noexcept : m_memory(memory) {}

	/// Reserva `bytes` en el banco (Chip, o Slow para `Fast`). El cache usa el tamaño real del
	/// destino; el pool alinea la base, así que no hace falta margen.
	[[nodiscard]] eng::Span<eng::u8> alloc(eng::u32 bytes, eng::res::MemBank bank) noexcept {
		if (bank == eng::res::MemBank::Chip) {
			eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> b =
				m_memory->chip().reserve<eng::PlaneTag>(bytes, 4u);
			return b.valid() ? b.view.raw() : eng::Span<eng::u8> {};
		}
		eng::Block<eng::PlaneTag> b = eng::fast_or_slow<eng::PlaneTag>(*m_memory, bytes, 4u);
		return b.valid() ? b.view.raw() : eng::Span<eng::u8> {};
	}

	/// No-op: el cache reutiliza el slot (el `Block` lo posee el pool hasta el `reset_phase`).
	void free(eng::Span<eng::u8>, eng::res::MemBank) noexcept {}

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
	eng::Ref<eng::MemoryManager> m_memory {};
};

} // namespace eng::amiga
