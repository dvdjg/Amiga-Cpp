// ============================================================================
// Test HOST-330: backend Amiga de `AssetCache` (eng/platform/amiga/asset_backend.hpp).
// ============================================================================
//
// Respalda el backend con el que la caché de assets funciona en hardware. Como la E/S la
// aporta `eng::os`, el test **implementa un backend de ficheros falso** (registra la
// petición y completa a mano) y valida:
//   - `alloc` en Chip y en Slow (para `MemBank::Fast`);
//   - `load`: abre y lanza `file_read_async` con el cookie `IoUser{'A', id}`;
//   - integración con `res::AssetCache`: `declare` -> `get` (lanza carga) -> `on_load_done`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/platform/330_asset_backend

#include <cstdio>

#include <eng/memory/arena.hpp>
#include <eng/platform/amiga/asset_backend.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

bool g_opened = false;
eng::res::AssetId g_loaded_id = 0u;
eng::u32 g_loaded_len = 0u;
eng::u8 g_loaded_tag = 0u;

} // namespace

// --- Ficheros falsos (`eng::os::file_*`) ---
namespace eng::os {

FileHandle file_open(const char* path, FileMode) { return (path != nullptr) ? 1u : 0u; }
void file_close(FileHandle) {}
eng::u32 file_size(FileHandle) { return 0u; }
bool file_read_async(FileHandle, eng::Span<eng::u8> dst, eng::u32, const IoNotify& n) {
	g_opened = true;
	const IoUser u = IoUser::decode(n.cookie);
	g_loaded_tag = u.tag;
	g_loaded_id = u.id;
	g_loaded_len = static_cast<eng::u32>(dst.size());
	return true;
}

} // namespace eng::os

int main() {
	std::printf("== HOST-330 asset_backend ==\n");

	eng::u8 chip_buf[4096] {};
	eng::u8 slow_buf[1024] {};
	eng::MemorySystem ms {
		eng::ChipArena {chip_buf, sizeof(chip_buf), eng::MemoryKind::Chip},
		eng::LinearArena {slow_buf, sizeof(slow_buf), eng::MemoryKind::Slow},
		eng::ChipArena {},
	};
	eng::amiga::AssetCacheBackend backend {ms};

	// --- alloc: Chip y Slow (Fast) ------------------------------------------
	{
		const auto c = backend.alloc(256u, eng::res::MemBank::Chip);
		check(!c.empty() && c.size() >= 256u, "alloc Chip devuelve bloque");
		check(ms.chip.used() >= 256u, "consume la arena Chip");
		const auto f = backend.alloc(128u, eng::res::MemBank::Fast);
		check(!f.empty(), "alloc Fast devuelve bloque");
		check(ms.slow.used() >= 128u, "Fast consume la arena Slow");
		backend.free(c, eng::res::MemBank::Chip); // no-op, no debe alterar
		check(ms.chip.used() >= 256u, "free es no-op (bump arena)");
	}

	// --- load: abre y lanza con el cookie correcto --------------------------
	{
		eng::u8 dst[64] {};
		check(backend.load(7u, "mem://mod", eng::Span<eng::u8> {dst, sizeof(dst)}),
		      "load lanza la lectura");
		check(g_opened, "el fichero se abrio");
		check(g_loaded_tag == static_cast<eng::u8>('A'), "cookie tag 'A' (asset)");
		check(g_loaded_id == 7u, "cookie id = id del asset");
		check(g_loaded_len == sizeof(dst), "lee hasta el tamano del destino");
	}

	// --- Integracion con AssetCache -----------------------------------------
	{
		eng::res::AssetCache<eng::amiga::AssetCacheBackend, 4u> cache;
		check(cache.init(backend, eng::res::CacheConfig {4096u, 4096u, 4u}), "init cache");
		const eng::res::AssetId id =
			cache.declare("mem://spr", 300u, eng::res::MemBank::Chip, 200u);
		check(id != 0u, "declare devuelve id");

		// `get` lanza la carga y devuelve vacio (placeholder).
		check(cache.get(id).empty(), "get mientras carga -> vacio");
		check(cache.state(id) == eng::res::AssetState::Loading, "estado Loading");

		// Finalizacion (lo que en hardware enruta `route_io`).
		cache.on_load_done(id, 300);
		check(cache.state(id) == eng::res::AssetState::Ready, "estado Ready");
		check(cache.get(id).size() == 300u, "datos tras la carga");
		check(cache.used_chip() >= 300u, "presupuesto Chip del cache");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: AssetCacheBackend (alloc Chip/Slow, load+cookie, cache) validado.\n");
	return 0;
}
