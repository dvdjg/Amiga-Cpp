// ============================================================================
// Test HOST-331: runtime de assets (eng::res::AssetRuntime: caché + route_io).
// ============================================================================
//
// Respalda `eng/res/asset_runtime.hpp`: la pieza que engancha la caché (`AssetCache`) con el
// enrutado de E/S (`route_io`). Con un backend de ficheros falso valida `load` (declara +
// lanza) -> `on_msg(FileDone)` (completa) -> `get` (datos), y el descarte de mensajes ajenos.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/331_asset_runtime

#include <cstdio>

#include <eng/memory/arena.hpp>
#include <eng/platform/amiga/asset_backend.hpp>
#include <eng/res/asset_runtime.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

namespace eng::os {
FileHandle file_open(const char* path, FileMode) { return (path != nullptr) ? 1u : 0u; }
void file_close(FileHandle) {}
eng::u32 file_size(FileHandle) { return 0u; }
bool file_read_async(FileHandle, eng::Span<eng::u8>, eng::u32, const IoNotify&) { return true; }
} // namespace eng::os

int main() {
	std::printf("== HOST-331 asset_runtime ==\n");

	eng::u8 chip_buf[4096] {};
	eng::MemorySystem ms {
		eng::LinearArena {chip_buf, sizeof(chip_buf), eng::MemoryKind::Chip},
		eng::LinearArena {},
		eng::LinearArena {},
	};
	eng::res::AssetRuntime<eng::amiga::AssetCacheBackend, 4u> assets;
	check(assets.init(eng::amiga::AssetCacheBackend {ms}, eng::res::CacheConfig {4096u, 0u, 4u}),
	      "init");

	// --- load (declara + lanza) ---------------------------------------------
	const eng::res::AssetId id = assets.load("mem://mod", 200u, eng::res::MemBank::Chip, 200u);
	check(id != 0u, "load devuelve id");
	check(assets.state(id) == eng::res::AssetState::Loading, "estado Loading tras load");

	// --- on_msg(FileDone) completa la carga ---------------------------------
	{
		eng::os::Msg m {};
		m.type = eng::os::MsgType::FileDone;
		m.payload.file.cookie = eng::os::IoUser {static_cast<eng::u8>('A'), id}.encode();
		m.payload.file.result = 200;
		check(assets.on_msg(m), "on_msg consume el FileDone del asset");
		check(assets.state(id) == eng::res::AssetState::Ready, "estado Ready");
		check(assets.get(id).size() == 200u, "datos disponibles");
		check(assets.used_chip() >= 200u, "presupuesto Chip");
	}

	// --- un mensaje ajeno no se consume -------------------------------------
	{
		eng::os::Msg other {};
		other.type = eng::os::MsgType::VBlank;
		check(!assets.on_msg(other), "VBlank no es de E/S -> no se consume");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: AssetRuntime (load -> on_msg -> get) validado.\n");
	return 0;
}
