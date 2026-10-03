// Demo 212 — **recursos de zona**: cargar un overlay por el VFS y `.engz`.
// ----------------------------------------------------------------------------
// Tutorial (R6.1/R6.4/R6.6/R6.7 de `ROADMAP_RESOURCES.md`): una **transición de zona** carga el
// código de la zona desde disco **de forma asíncrona** (`res::AsyncRead` + `os::file_pump`, el frame
// no se bloquea), decodifica el contenedor **`.engz`** y **carga/ejecuta/descarga** el overlay
// (HUNK). El juego no ve `dos.handles`, punteros de memoria ni el formato del recurso:
//
//   AsyncRead::begin("...engz") → [file_pump por frame] → on_done → decode_engz → DynLoader::load
//                                                                      → symbol("answer")() → unload
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/io/212_zone_resources --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/io/212_zone_resources --warp

#include <eng/api/api.hpp>
#include <eng/os/file.hpp>
#include <eng/os/os.hpp>
#include <eng/os/vfs.hpp>
#include <eng/res/async_overlay.hpp>
#include <eng/res/dynloader.hpp>
#include <eng/res/engz.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

/// Backend VFS sobre `os::file_*` (dos.library): existencia, tamaño y lectura por offset.
struct FileBackend {
	bool exists(const char* path) const noexcept {
		const eng::os::FileHandle h = eng::os::file_open(path, eng::os::FileMode::Read);
		if (h == 0u) return false;
		eng::os::file_close(h);
		return true;
	}
	eng::u32 size(const char* path) const noexcept {
		const eng::os::FileHandle h = eng::os::file_open(path, eng::os::FileMode::Read);
		if (h == 0u) return 0u;
		const eng::u32 n = eng::os::file_size(h);
		eng::os::file_close(h);
		return n;
	}
	eng::s32 read(const char* path, eng::Span<eng::u8> dst, eng::u32 off) const noexcept {
		const eng::os::FileHandle h = eng::os::file_open(path, eng::os::FileMode::Read);
		if (h == 0u) return -1;
		const eng::s32 n = eng::os::file_read_sync(h, dst, off);
		eng::os::file_close(h);
		return n;
	}
};

struct ZoneGame {
	FileBackend m_fs {};
	eng::os::Vfs<FileBackend> m_vfs {m_fs};
	eng::res::DynLoader m_dl {};
	/// Carga asíncrona del overlay (R6.4/R6.6/R6.7): `AsyncRead` + `decode_engz` + `DynLoader`.
	eng::res::AsyncOverlay m_overlay {m_dl};
	eng::u8 m_engz[512] {};    ///< búfer del contenedor `.engz` (lectura asíncrona)
	eng::u8 m_decoded[512] {}; ///< salida del decode (bytes del HUNK)
	bool m_ok = false;
	eng::s32 m_answer = -1;
	int m_ready_status = 0; ///< 0 = en curso, 1 = OK, -1 = fallo

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({64u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021201u);
			return;
		}
		// --- **transición de zona**: prefetch asíncrono del overlay `.engz` ---
		if (!m_vfs.exists("data/code/answer.engz")) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021210u);
			return;
		}
		// **La lectura NO bloquea el frame** (R6.7): se lanza aquí; la E/S (`file_pump`) y la
		// cadena decode+HUNK avanzan en `update` mientras el juego sigue (pantalla de carga).
		if (!m_overlay.begin(backend.memory_manager(), "data/code/answer.engz",
				     eng::Span<eng::u8> {m_engz, sizeof(m_engz)},
				     eng::Span<eng::u8> {m_decoded, sizeof(m_decoded)})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021211u);
			return;
		}
	}

	/// **Sondea la E/S por frame** (no bloquea): `file_pump` resuelve la operación diferida y postea
	/// el `FileDone`; al llegar, `AsyncOverlay` decodifica el `.engz` y carga el HUNK por segmento.
	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_ready_status != 0 || !m_overlay.reading()) {
			return;
		}
		(void)eng::os::file_pump();
		eng::os::Msg m;
		while (eng::os::system_port().pop(m)) {
			(void)m_overlay.on_done(m);
		}
		if (m_overlay.ready()) {
			exec_overlay();
		} else if (m_overlay.failed()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021212u);
			m_ready_status = -1;
		}
	}

	/// Con el overlay ya cargado, resuelve el símbolo, ejecuta y **descarga** (la zona termina).
	void exec_overlay() {
		using Fn = eng::s32 (*)();
		auto fn = reinterpret_cast<Fn>(m_dl.symbol(m_overlay.handle(), "answer"));
		if (fn != nullptr) {
			m_answer = fn();
			m_ok = (m_answer == 42);
		}
		m_overlay.unload();
		eng::debug::mark_ready(g_eng_run_status, 0x21200000u);
		m_ready_status = 1;
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		auto& d = backend.debug();
		d.text(48, 56, "AMG demo 212 - zona: VFS -> .engz -> overlay", 0x00ffffff);
		d.text(48, 96, m_ok ? "zona 1 (.engz): OK (answer=42)" : "zona 1 (.engz): FALLO",
		       m_ok ? 0x0000ff80 : 0x00ff6060);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	ZoneGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);
	return 0;
}
