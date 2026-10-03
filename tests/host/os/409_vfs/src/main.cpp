// ============================================================================
// Test HOST-409: fachada Vfs (R6.1) con backend simulado en memoria
// ============================================================================
//
// Verifica `eng::os::Vfs` (paths normalizados + exists/size/read/read_all) sobre un backend
// en memoria. Sin E/S real.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/os/409_vfs

#include <cstdio>

#include <eng/os/vfs.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

eng::usize slen(const char* s) {
	eng::usize n = 0u;
	while (s[n] != '\0') {
		++n;
	}
	return n;
}
bool eq(const char* a, const char* b) {
	for (eng::usize i = 0u;; ++i) {
		if (a[i] != b[i]) return false;
		if (a[i] == '\0') return true;
	}
}

struct FakeFile {
	const char* path;
	const char* data;
};

/// Backend en memoria: una tabla de ficheros. Cumple el concepto que espera `Vfs`.
struct FakeFs {
	const FakeFile* files = nullptr;
	eng::usize count = 0u;

	const FakeFile* find(const char* p) const {
		for (eng::usize i = 0u; i < count; ++i) {
			if (eq(files[i].path, p)) return &files[i];
		}
		return nullptr;
	}
	bool exists(const char* p) const { return find(p) != nullptr; }
	eng::u32 size(const char* p) const {
		const FakeFile* f = find(p);
		return f != nullptr ? static_cast<eng::u32>(slen(f->data)) : 0u;
	}
	eng::s32 read(const char* p, eng::Span<eng::u8> dst, eng::u32 off) const {
		const FakeFile* f = find(p);
		if (f == nullptr) return -1;
		const eng::u32 len = static_cast<eng::u32>(slen(f->data));
		if (off > len) return -1;
		eng::u32 k = len - off;
		if (k > dst.size()) k = static_cast<eng::u32>(dst.size());
		for (eng::u32 i = 0u; i < k; ++i) dst[i] = static_cast<eng::u8>(f->data[off + i]);
		return static_cast<eng::s32>(k);
	}
};

constexpr FakeFile kFiles[] = {
	{"/assets/hero.bpl", "HELLO"},
	{"data/level1.map", "MAPDATA!"},
};

FakeFs make_fs() {
	FakeFs fs {};
	fs.files = kFiles;
	fs.count = sizeof(kFiles) / sizeof(kFiles[0]);
	return fs;
}

void test_access() {
	FakeFs fs = make_fs();
	eng::os::Vfs<FakeFs> vfs {fs};
	check(vfs.exists("/assets/hero.bpl"), "exists path absoluto");
	check(vfs.exists("/assets//./hero.bpl"), "exists con . y separador repetido (normalizado)");
	check(vfs.exists("/assets/../assets/hero.bpl"), "exists con .. (normalizado)");
	check(!vfs.exists("/assets/missing.bpl"), "no existe");
	check(vfs.size("data/level1.map") == 8u, "size");
	check(!vfs.exists("/a/../../b"), "escape → path inválido → false");
}

void test_read() {
	FakeFs fs = make_fs();
	eng::os::Vfs<FakeFs> vfs {fs};
	eng::u8 buf[16] {};
	const eng::s32 n = vfs.read("/assets/hero.bpl", eng::Span<eng::u8> {buf, sizeof(buf)});
	check(n == 5, "read 5 bytes");
	check(buf[0] == 'H' && buf[4] == 'O', "contenido leído");
	check(vfs.read("/nope", eng::Span<eng::u8> {buf, sizeof(buf)}) == -1, "read inexistente → -1");

	// read_all
	eng::u8 all[16] {};
	const auto r = vfs.read_all("data/level1.map", eng::Span<eng::u8> {all, sizeof(all)});
	check(r.has_value() && *r == 8u && all[0] == 'M', "read_all completo");
	eng::u8 tiny[2] {};
	check(vfs.read_all("data/level1.map", eng::Span<eng::u8> {tiny, sizeof(tiny)}).error() ==
		      eng::os::VfsError::OutOfMemory,
	      "read_all con destino pequeño → OutOfMemory");
	check(vfs.read_all("/missing", eng::Span<eng::u8> {all, sizeof(all)}).error() ==
		      eng::os::VfsError::NotFound,
	      "read_all inexistente → NotFound");
}

} // namespace

int main() {
	test_access();
	test_read();
	if (failures == 0) {
		std::printf("OK: fachada Vfs (paths normalizados + read) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
