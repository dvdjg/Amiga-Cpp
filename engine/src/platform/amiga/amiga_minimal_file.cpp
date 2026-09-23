#include <eng/os/file.hpp>
#include <eng/os/os.hpp>

#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dos.h>

/// \file amiga_minimal_file.cpp
/// Implementación Amiga de la E/S del mini-SO sobre **`dos.library`** (OFS/FFS, directorios):
/// `Open`/`Read`/`Write`/`Seek`/`Close`, `CreateDir`, `DeleteFile`, `Rename`. Abre `dos.library`
/// la primera vez. La "asíncrona" se resuelve como operación **diferida**: se encola y `file_pump`
/// la ejecuta (una por llamada) posteando `FileDone`/`FileError` con el `IoUser` cookie.
/// Ver `docs/engine/architecture/MINI_OS_IO.md`.

namespace eng::os {
namespace {

/// Base de `dos.library` (la abre `ensure_dos`). El SDK la declara `extern`; aqui la definimos.
struct DosLibrary* DOSBase = nullptr;

constexpr eng::u16 kMaxFiles = 8u;
constexpr eng::u16 kMaxPending = 4u;

struct Slot {
	BPTR fh = 0;
	bool used = false;
};
Slot g_slots[kMaxFiles] {};

struct Pending {
	bool active = false;
	bool write = false;
	FileHandle h = 0;
	eng::u8* dst = nullptr;
	const eng::u8* src = nullptr;
	eng::u32 len = 0;
	eng::u32 off = 0;
	eng::u32 cookie = 0;
};
Pending g_pending[kMaxPending] {};

bool g_dos = false;

bool ensure_dos() {
	if (!g_dos) {
		DOSBase = reinterpret_cast<struct DosLibrary*>(
			OpenLibrary(reinterpret_cast<const char*>("dos.library"), 0L));
		g_dos = DOSBase != nullptr;
	}
	return g_dos;
}

Slot* slot_of(FileHandle h) {
	if (h == 0u || h > kMaxFiles) {
		return nullptr;
	}
	Slot& s = g_slots[h - 1u];
	return s.used ? &s : nullptr;
}

void post_file(FileHandle h, eng::s32 result, eng::u8 op, eng::u32 cookie) {
	Msg m {};
	m.type = (result < 0) ? MsgType::FileError : MsgType::FileDone;
	m.payload.file = {h, result, op, cookie};
	(void)system_port().post(m);
}

const char* cpath(const char* path) { return path; }

} // namespace

FileHandle file_open(const char* path, FileMode mode) {
	if (!ensure_dos()) {
		return 0u;
	}
	const LONG amode = (mode == FileMode::Read) ? MODE_OLDFILE : MODE_NEWFILE;
	const BPTR fh = Open(const_cast<char*>(cpath(path)), amode);
	if (fh == 0) {
		return 0u;
	}
	for (eng::u16 i = 0u; i < kMaxFiles; ++i) {
		if (!g_slots[i].used) {
			g_slots[i].used = true;
			g_slots[i].fh = fh;
			return static_cast<FileHandle>(i + 1u);
		}
	}
	Close(fh);
	return 0u;
}

void file_close(FileHandle h) {
	Slot* s = slot_of(h);
	if (s == nullptr) {
		return;
	}
	Close(s->fh);
	s->fh = 0;
	s->used = false;
}

bool file_make_dir(const char* path) {
	if (!ensure_dos()) {
		return false;
	}
	return CreateDir(const_cast<char*>(cpath(path))) != 0;
}

eng::s32 file_read_sync(FileHandle h, eng::Span<eng::u8> dst, eng::u32 offset) {
	Slot* s = slot_of(h);
	if (s == nullptr || dst.empty()) {
		return -1;
	}
	if (Seek(s->fh, static_cast<LONG>(offset), OFFSET_BEGINNING) < 0) {
		return -1;
	}
	return Read(s->fh, dst.data(), static_cast<LONG>(dst.size()));
}

eng::s32 file_write_sync(FileHandle h, eng::Span<const eng::u8> src, eng::u32 offset) {
	Slot* s = slot_of(h);
	if (s == nullptr) {
		return -1;
	}
	if (Seek(s->fh, static_cast<LONG>(offset), OFFSET_BEGINNING) < 0) {
		return -1;
	}
	return Write(s->fh, const_cast<eng::u8*>(src.data()), static_cast<LONG>(src.size()));
}

bool file_read_async(FileHandle h, eng::Span<eng::u8> dst, eng::u32 offset, const IoNotify& n) {
	for (Pending& p : g_pending) {
		if (!p.active) {
			p = Pending {};
			p.active = true;
			p.h = h;
			p.dst = dst.data();
			p.len = static_cast<eng::u32>(dst.size());
			p.off = offset;
			p.cookie = n.cookie;
			return true;
		}
	}
	return false;
}

bool file_write_async(FileHandle h, eng::Span<const eng::u8> src, eng::u32 offset,
		      const IoNotify& n) {
	for (Pending& p : g_pending) {
		if (!p.active) {
			p = Pending {};
			p.active = true;
			p.write = true;
			p.h = h;
			p.src = src.data();
			p.len = static_cast<eng::u32>(src.size());
			p.off = offset;
			p.cookie = n.cookie;
			return true;
		}
	}
	return false;
}

bool file_pump() {
	for (Pending& p : g_pending) {
		if (!p.active) {
			continue;
		}
		const eng::s32 res =
			p.write ? file_write_sync(p.h, eng::Span<const eng::u8> {p.src, p.len}, p.off)
				: file_read_sync(p.h, eng::Span<eng::u8> {p.dst, p.len}, p.off);
		post_file(p.h, res, p.write ? 1u : 0u, p.cookie);
		p.active = false;
		return true;
	}
	return false;
}

eng::u32 file_size(FileHandle h) {
	Slot* s = slot_of(h);
	if (s == nullptr) {
		return 0u;
	}
	(void)Seek(s->fh, 0, OFFSET_END);                        // mueve al final
	const LONG size = Seek(s->fh, 0, OFFSET_BEGINNING);     // devuelve la posicion previa (final)
	return (size < 0) ? 0u : static_cast<eng::u32>(size);
}

bool file_is_busy(FileHandle h) {
	for (const Pending& p : g_pending) {
		if (p.active && p.h == h) {
			return true;
		}
	}
	return false;
}

bool file_delete(const char* path) {
	if (!ensure_dos()) {
		return false;
	}
	return DeleteFile(const_cast<char*>(cpath(path))) != 0;
}

bool file_rename(const char* old_path, const char* new_path) {
	if (!ensure_dos()) {
		return false;
	}
	return Rename(const_cast<char*>(cpath(old_path)), const_cast<char*>(cpath(new_path))) != 0;
}

} // namespace eng::os
