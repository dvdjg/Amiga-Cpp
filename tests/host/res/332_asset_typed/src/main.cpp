// ============================================================================
// Test HOST-332: vista tipada de assets (AssetRuntime::bytes<Tag>).
// ============================================================================
//
// Respalda la decodificación "de frontera" de `eng/res/asset_runtime.hpp`: un asset cargado
// (bytes) se expone como vista de **dominio** (`eng::ByteView<Tag>`) sin copiar. Comprueba
// que antes de `Ready` la vista está vacía y que tras completarse tiene el tamaño y dominio.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/332_asset_typed

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
	std::printf("== HOST-332 asset_typed ==\n");

	eng::u8 chip_buf[2048] {};
	eng::MemorySystem ms {
		eng::LinearArena {chip_buf, sizeof(chip_buf), eng::MemoryKind::Chip},
		eng::LinearArena {},
		eng::LinearArena {},
	};
	eng::res::AssetRuntime<eng::amiga::AssetCacheBackend, 4u> assets;
	(void)assets.init(eng::amiga::AssetCacheBackend {ms}, eng::res::CacheConfig {2048u, 0u, 4u});

	const eng::res::AssetId id = assets.load("mem://mod", 512u, eng::res::MemBank::Chip);
	check(id != 0u, "load");

	// Antes de completar: la vista tipada esta vacia.
	check(assets.bytes<eng::MusicTag>(id).empty(), "no Ready -> vista vacia");

	// Completa la carga (lo que en hardware enruta `route_io`).
	eng::os::Msg m {};
	m.type = eng::os::MsgType::FileDone;
	m.payload.file.cookie = eng::os::IoUser {static_cast<eng::u8>('A'), id}.encode();
	m.payload.file.result = 512;
	check(assets.on_msg(m), "on_msg");

	const eng::MusicModule mod = assets.bytes<eng::MusicTag>(id);
	check(mod.size() == 512u, "vista con el tamano cargado");
	check(mod.data() != nullptr, "vista con datos");
	check(reinterpret_cast<eng::uintptr>(mod.data()) % 4u == 0u, "vista alineada a 4 (musica)");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: AssetRuntime::bytes<Tag> (vista tipada) validado.\n");
	return 0;
}
