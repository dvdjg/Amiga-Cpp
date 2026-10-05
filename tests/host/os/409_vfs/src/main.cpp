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
	/// Enumeración: hijos directos de `dir` (paths que empiezan por `dir + '/'`).
	template <class Fn>
	bool list(const char* dir, Fn&& fn) const {
		const eng::usize dl = slen(dir);
		bool any = false;
		for (eng::usize i = 0u; i < count; ++i) {
			const char* p = files[i].path;
			bool match = (dl != 0u);
			for (eng::usize k = 0u; k < dl; ++k) {
				if (p[k] != dir[k]) {
					match = false;
					break;
				}
			}
			if (!match || p[dl] != '/') continue;
			const eng::os::DirEntry e {p + dl + 1u,
						   static_cast<eng::u32>(slen(files[i].data)), false};
			any = true;
			fn(e);
		}
		return any;
	}
	/// Handles: `open_file` devuelve índice+1 (0 = inválido).
	eng::u32 open_file(const char* p) const {
		for (eng::usize i = 0u; i < count; ++i) {
			if (eq(files[i].path, p)) return static_cast<eng::u32>(i + 1u);
		}
		return 0u;
	}
	eng::s32 read_file(eng::u32 h, eng::Span<eng::u8> dst, eng::u32 off) const {
		if (h == 0u || h > count) return -1;
		return read(files[h - 1u].path, dst, off);
	}
	void close_file(eng::u32) const {}
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

void test_mounts() {
	FakeFs fs = make_fs();
	eng::os::Vfs<FakeFs> vfs {fs};
	// Sin montaje ni raíz: el path va tal cual (plano) — ya cubierto en test_access.
	check(vfs.mount("assets", "/assets"), "mount assets → /assets");
	check(vfs.exists("assets/hero.bpl"), "mount: existe (lógico → backend)");
	check(eq(vfs.last_path(), "/assets/hero.bpl"), "mount resuelve a /assets/hero.bpl");
	check(vfs.exists("/assets/hero.bpl"), "mount: path absoluto también");
	check(vfs.mount("data", "DH1:map"), "mount data → DH1:map");
	check(vfs.mount_count() == 2u, "2 montajes");
	(void)vfs.exists("data/level1.map");
	check(eq(vfs.last_path(), "DH1:map/level1.map"), "mount data resuelve");
	// Raíz por defecto (para lo que no encaja en un montaje) + desmontaje.
	vfs.set_root("vol");
	(void)vfs.exists("/other/x");
	check(eq(vfs.last_path(), "vol/other/x"), "raíz por defecto resuelve relativos");
	check(vfs.umount("data") && vfs.mount_count() == 1u, "umount");
}

void test_list() {
	FakeFs fs = make_fs();
	eng::os::Vfs<FakeFs> vfs {fs};
	int n = 0;
	const char* name = nullptr;
	const bool ok = vfs.list("/assets",
				 [&](const eng::os::DirEntry& e) { ++n; name = e.name; });
	check(ok && n == 1 && name != nullptr && eq(name, "hero.bpl"), "list /assets → hero.bpl");
}

void test_handles() {
	FakeFs fs = make_fs();
	eng::os::Vfs<FakeFs> vfs {fs};
	auto f = vfs.open("/assets/hero.bpl");
	check(f.valid(), "open → handle válido");
	eng::u8 buf[8] {};
	check(f.read(eng::Span<eng::u8> {buf, 5u}) == 5 && buf[0] == 'H', "read por handle");
	check(f.read(eng::Span<eng::u8> {buf, 3u}, 2u) == 3 && buf[0] == 'L', "read por offset");
	f.close();
	check(!f.valid(), "close invalida el handle");
	check(!vfs.open("/missing").valid(), "open inexistente → inválido");
}

} // namespace

int main() {
	test_access();
	test_read();
	test_mounts();
	test_list();
	test_handles();
	if (failures == 0) {
		std::printf("OK: fachada Vfs (paths normalizados + read) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
