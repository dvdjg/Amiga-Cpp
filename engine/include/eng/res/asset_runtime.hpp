#pragma once

/// \file asset_runtime.hpp
/// **Runtime de assets** (`eng::res`): junta la **caché** (`AssetCache`) con el **enrutado de
/// E/S** (`route_io`) para que el bucle del juego cargue de disco de forma **asíncrona** sin
/// conocer el backend de memoria ni de ficheros. Es la pieza que faltaba entre `AssetCache`
/// (HOST-254) y el bucle: `load(path, size)` declara y lanza; los `FileDone`/`FileError` que
/// drena el bucle se completan con `on_msg(...)`.
///
/// ```cpp
/// // init (una vez):
/// eng::res::AssetRuntime<eng::amiga::AssetCacheBackend, 8> assets;
/// assets.init(backend, {.chip_budget = 64u * 1024u, .fast_budget = 64u * 1024u});
/// // al pedir:
/// auto id = assets.load("data/music.mod", mod_bytes, eng::res::MemBank::Chip);
/// // en el bucle, por cada mensaje de E-S:
/// if (assets.on_msg(m)) { /* la caché ya sabe lo que se cargó */ }
/// // cuando `state(id) == Ready`:
/// eng::Span<eng::u8> data = assets.get(id);
/// ```
///
/// Ver `docs/engine/architecture/RESOURCE_SYSTEM.md` §1/§4 y `PUBLIC_GAME_API.md` §2.1.4.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/os/message.hpp>
#include <eng/res/asset_cache.hpp>
#include <eng/res/resources.hpp>

namespace eng::res {

/// Caché de assets + enrutado de E/S. El `CacheBackend` es el contrato *duck-typed* de
/// `AssetCache` (`alloc`/`free`/`load`); en Amiga, `eng::amiga::AssetCacheBackend`.
template <class CacheBackend, eng::u16 MaxAssets = 8u>
class AssetRuntime {
public:
	using Cache = AssetCache<CacheBackend, MaxAssets>;

	/// Inicializa la caché con su backend y presupuesto. El runtime **posee** el backend
	/// (la caché solo guarda una referencia), así que debe sobrevivir a la caché.
	bool init(CacheBackend backend, const CacheConfig& cfg) noexcept {
		m_backend = backend;
		return m_cache.init(m_backend, cfg);
	}

	/// Declara el asset y **lanza** su carga (asíncrona). `id` = 0 si no cabe (consulta el
	/// presupuesto antes con `eng::res::Budget`). El tamaño debe conocerse de antemano.
	AssetId load(const char* path, eng::u32 size, MemBank bank = MemBank::Any,
		     eng::u8 prio = 128u) noexcept {
		const AssetId id = m_cache.declare(path, size, bank, prio);
		if (id != 0u) {
			(void)m_cache.prefetch(id);
		}
		return id;
	}

	/// Enruta un mensaje de E/S (`FileDone`/`FileError`) a la caché. Devuelve `true` si lo
	/// consumió (tag `'A'`); así el bucle reenvía sin clasificar.
	bool on_msg(const eng::os::Msg& m) noexcept { return route_io(m, m_cache, m_libs); }

	[[nodiscard]] Cache& cache() noexcept { return m_cache; }
	[[nodiscard]] const Cache& cache() const noexcept { return m_cache; }
	[[nodiscard]] AssetState state(AssetId id) const noexcept { return m_cache.state(id); }
	[[nodiscard]] eng::Span<eng::u8> get(AssetId id) noexcept { return m_cache.get(id); }

	/// **Vista tipada** de un asset ya cargado (`Tag` de dominio). Vacía mientras no esté
	/// `Ready`. La "decodificación" es la reinterpretación al dominio (los bytes se cargan
	/// tal cual); para formatos con decoders (música, imágenes cocinadas) se añadirá encima.
	template <class Tag>
	[[nodiscard]] eng::ByteView<Tag> bytes(AssetId id) noexcept {
		const eng::Span<eng::u8> s = m_cache.get(id);
		return eng::ByteView<Tag> {s.data(), s.size()};
	}

	[[nodiscard]] eng::u32 used_chip() const noexcept { return m_cache.used_chip(); }
	[[nodiscard]] eng::u32 used_fast() const noexcept { return m_cache.used_fast(); }

private:
	/// `route_io` exige un objeto con `on_file_done`; el runtime de assets solo usa el tag
	/// `'A'` (el loader de código se enganchará aquí cuando se integre `DynLoader`).
	struct LibsStub {
		void on_file_done(eng::u16, eng::s32) noexcept {}
	};

	Cache m_cache {};
	LibsStub m_libs {};
	CacheBackend m_backend {};
};

} // namespace eng::res
