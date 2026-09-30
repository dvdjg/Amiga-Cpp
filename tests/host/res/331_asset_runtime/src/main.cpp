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

#include <eng/memory/memory_manager.hpp>
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
eng::u16 file_close_count = 0u;
FileHandle file_open(const char* path, FileMode) { return (path != nullptr) ? 1u : 0u; }
void file_close(FileHandle) { ++file_close_count; }
eng::u32 file_size(FileHandle) { return 0u; }
bool file_read_async(FileHandle, eng::Span<eng::u8>, eng::u32, const IoNotify&) { return true; }
} // namespace eng::os

int main() {
	std::printf("== HOST-331 asset_runtime ==\n");

	eng::u8 chip_buf[4096] {};
	eng::MemoryManager ms {};
	ms.configure(chip_buf, sizeof(chip_buf), nullptr, 0u, nullptr, 0u, 16u);
	eng::amiga::AssetCacheBackend amiga_backend {ms};
	eng::res::AssetRuntime<eng::amiga::AssetCacheBackend, 4u> assets;
	check(assets.init(amiga_backend, eng::res::CacheConfig {4096u, 0u, 4u}),
	      "init");

	// --- load (declara + lanza) ---------------------------------------------
	const eng::res::AssetId id = assets.load("mem://mod", 200u, eng::res::MemoryRequest::Chip, 200u);
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
	check(eng::os::file_close_count == 1u, "AssetRuntime cierra el handle asociado a FileDone");

	// --- un mensaje ajeno no se consume -------------------------------------
	{
		eng::os::Msg other {};
		other.type = eng::os::MsgType::VBlank;
		check(!assets.on_msg(other), "VBlank no es de E/S -> no se consume");
	}
	check(eng::os::file_close_count == 1u, "un mensaje ajeno no cierra handles");

	// Un backend sin `finish` conserva el contrato duck-typed usado por tests/consumidores host.
	struct HostBackend {
		eng::u8 bytes[128] {};
		eng::MemoryBlock alloc(eng::u32 n, eng::MemoryKind k) noexcept {
			return n <= sizeof(bytes) ? eng::MemoryBlock {bytes, n, k} : eng::MemoryBlock {};
		}
		void free(const eng::MemoryBlock&) noexcept {}
		bool load(eng::res::AssetId, const char*, eng::Span<eng::u8>) noexcept { return true; }
	};
	eng::res::AssetRuntime<HostBackend, 2u> host_assets;
	check(host_assets.init(HostBackend {}, {.chip_budget = 128u, .max_assets = 2u}),
	      "backend host sin finish inicializa");
	const eng::res::AssetId host_id = host_assets.load("host", 32u, eng::res::MemoryRequest::Chip);
	eng::os::Msg host_done {};
	host_done.type = eng::os::MsgType::FileDone;
	host_done.payload.file.cookie = eng::os::IoUser {static_cast<eng::u8>('A'), host_id}.encode();
	host_done.payload.file.result = 32;
	check(host_assets.on_msg(host_done), "backend host sin finish enruta carga");
	check(host_assets.shutdown(), "backend host sin finish libera cache");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: AssetRuntime (load -> on_msg -> get) validado.\n");
	return 0;
}
