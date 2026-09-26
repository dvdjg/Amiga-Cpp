// ============================================================================
// Test HOST-328: carga tipada desde fichero (eng::res::load_file<Tag>).
// ============================================================================
//
// Respalda la variante de fichero de `eng/res/load.hpp`. Como la E/S (`os::file_*`) la
// aporta el backend, el test **implementa un backend falso** sobre un "sistema de ficheros"
// en memoria y valida la composición: abrir -> medir -> reservar -> leer -> cerrar, con los
// caminos de error (no existe, lectura corta, no cabe). No se toca hardware.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/328_load_file

#include <cstdio>
#include <cstring>

#include <eng/memory/arena.hpp>
#include <eng/res/load.hpp>

namespace {

const eng::u8* g_file = nullptr;
eng::u32 g_file_bytes = 0;

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

// --- Backend falso de `eng::os::file_*` (solo se definen las funciones que usa `load_file`) ---
namespace eng::os {

FileHandle file_open(const char* path, FileMode) {
	if (path != nullptr && std::strcmp(path, "mem://ok") == 0) {
		return 1u;
	}
	if (path != nullptr && std::strcmp(path, "mem://short") == 0) {
		return 2u;
	}
	return 0u;
}
void file_close(FileHandle) {}
eng::u32 file_size(FileHandle) { return g_file_bytes; }
eng::s32 file_read_sync(FileHandle h, eng::Span<eng::u8> dst, eng::u32) {
	eng::u32 n = static_cast<eng::u32>(dst.size());
	if (h == 2u && n > 4u) {
		n = 4u; // lectura corta simulada
	}
	for (eng::u32 i = 0; i < n; ++i) {
		dst[i] = g_file[i];
	}
	return static_cast<eng::s32>(n);
}

} // namespace eng::os

int main() {
	std::printf("== HOST-328 load_file ==\n");

	eng::u8 file_data[100] {};
	for (eng::u32 i = 0; i < sizeof(file_data); ++i) {
		file_data[i] = static_cast<eng::u8>(i + 1u);
	}
	g_file = file_data;
	g_file_bytes = sizeof(file_data);

	eng::u8 chip_buf[1024] {};
	eng::MemorySystem ms {
		eng::ChipArena {chip_buf, sizeof(chip_buf), eng::MemoryKind::Chip},
		eng::ChipArena {},
		eng::ChipArena {},
	};

	// --- Fichero existente: se carga y el tamaño útil es el real ------------
	{
		eng::u32 bytes = 0u;
		const auto b =
			eng::res::load_file<eng::PlaneTag>(ms, "mem://ok", bytes);
		check(b.valid(), "bloque valido");
		check(bytes == sizeof(file_data), "tamano util = tamano del fichero");
		check(b.kind == eng::MemoryKind::Chip, "vive en Chip");
		check(reinterpret_cast<eng::uintptr>(b.view.data()) % 16u == 0u, "alineado a 16");
		bool same = true;
		for (eng::u32 i = 0; i < sizeof(file_data); ++i) {
			if (b.view.data()[i] != file_data[i]) {
				same = false;
			}
		}
		check(same, "datos copiados del fichero");
	}

	// --- Fichero inexistente -> invalido ------------------------------------
	{
		eng::u32 bytes = 123u;
		const auto b = eng::res::load_file<eng::PlaneTag>(ms, "mem://missing", bytes);
		check(!b.valid(), "no existe -> bloque invalido");
		check(bytes == 0u, "no existe -> out_bytes = 0");
	}

	// --- Lectura corta -> invalido (no se acepta parcial) -------------------
	{
		eng::u32 bytes = 0u;
		const auto b = eng::res::load_file<eng::PlaneTag>(ms, "mem://short", bytes);
		check(!b.valid(), "lectura corta -> bloque invalido");
	}

	// --- No cabe en la arena -> invalido ------------------------------------
	{
		eng::u8 tiny[8] {};
		eng::MemorySystem ms2 {
			eng::ChipArena {tiny, sizeof(tiny), eng::MemoryKind::Chip},
			eng::ChipArena {},
			eng::ChipArena {},
		};
		eng::u32 bytes = 0u;
		const auto b = eng::res::load_file<eng::PlaneTag>(ms2, "mem://ok", bytes);
		check(!b.valid(), "no cabe -> bloque invalido");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: res::load_file (abrir/medir/copiar, no existe, corta, overflow) validado.\n");
	return 0;
}
