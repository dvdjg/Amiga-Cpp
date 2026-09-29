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
#include <eng/os/file.hpp>
#include <eng/res/asset_cache.hpp>

namespace eng::amiga {

/// Backend de `AssetCache` sobre `MemorySystem` + E/S del mini-SO.
class AssetCacheBackend {
public:
	constexpr AssetCacheBackend() = default;
	explicit constexpr AssetCacheBackend(eng::MemorySystem& memory) noexcept : m_memory(memory) {}

	/// Reserva `bytes` en la arena del banco (Chip, o Slow para `Fast`). El cache usa el
	/// tamaño real del destino; aquí se añade margen de alineación interno de la arena.
	[[nodiscard]] eng::Span<eng::u8> alloc(eng::u32 bytes, eng::res::MemBank bank) noexcept {
		eng::LinearArena& arena =
			(bank == eng::res::MemBank::Fast) ? m_memory->slow : m_memory->chip;
		const auto block = arena.template allocate_block<eng::PlaneTag>(bytes, 4u);
		return block.valid() ? block.view.raw() : eng::Span<eng::u8> {};
	}

	/// No-op: las arenas son *bump* (sin liberación por bloque). El cache reutiliza el slot.
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
	eng::Ref<eng::MemorySystem> m_memory {};
};

} // namespace eng::amiga
