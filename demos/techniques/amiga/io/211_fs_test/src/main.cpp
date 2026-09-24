#include <eng/api/api.hpp>
#include <eng/audio/pcm_stream.hpp>
#include <eng/os/file.hpp>
#include <eng/os/file_stream.hpp>
#include <eng/os/os.hpp>
#include <eng/res/dynloader.hpp>
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

// -----------------------------------------------------------------------------
// Demo 211 — sistema de archivos (dos.library) + carga dinamica de codigo
// -----------------------------------------------------------------------------
// Lee un volumen DH1: generado por `tools/fs/make-volume.mjs` (texto, imagen, sonido y
// un `.englib`), carga el codigo relocatable y lo EJECUTA, y prueba la ESCRITURA
// (crea `out/result.txt`, lo escribe y lo relee). Todo por `eng::os::file_*`.
//
//   node tools/fs/make-volume.mjs
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/io/211_fs_test --debug --clean
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/io/211_fs_test --wait-ms 8000
// -----------------------------------------------------------------------------

constexpr eng::u32 kLibMax = 128u;

// --- streaming desde disquete (M8) ---
constexpr eng::u8 kStreamBufs = 2u;
constexpr eng::u32 kStreamChunk = 4096u;

/// Fuente del `FileChunkFeeder`: envuelve `file_read_async` (política de plantilla, sin punteros a
/// función). El `idx` del buffer viaja como `IoUser::id` en el cookie del `FileDone`.
struct FileSource {
	eng::os::FileHandle h = 0u;
	bool read_async(eng::u8 idx, eng::u32 offset, eng::u8* dst, eng::u32 bytes) noexcept {
		const eng::os::IoUser user {static_cast<eng::u8>(0u), idx}; // tag 0 = stream
		return eng::os::file_read_async(h, eng::Span<eng::u8> {dst, bytes}, offset,
						eng::os::IoNotify {true, eng::os::MsgPrio::Low,
								   user.encode()});
	}
};

struct DemoGame {
	eng::res::DynLoader m_dl {};
	eng::u8 m_lib[kLibMax] {};
	eng::u8 m_hunk[kLibMax] {};
	eng::u8 m_pool_buf[256] {};
	eng::LinearArena m_pool {m_pool_buf, sizeof(m_pool_buf), eng::MemoryKind::Any};

	bool m_text_ok = false;
	eng::u32 m_text_len = 0;
	char m_text[48] {};
	eng::u32 m_img_len = 0;
	eng::u32 m_snd_len = 0;
	bool m_lib_ok = false;
	eng::s32 m_answer = -1;
	bool m_hunk_ok = false;
	eng::s32 m_hunk_answer = -1;
	bool m_write_ok = false;
	eng::u32 m_readback = 0;
	bool m_dir_ok = false;
	// streaming (M8): lee el fichero de 512 KB por rebanadas con `FileChunkFeeder`.
	eng::u8 m_stream_bufs[kStreamBufs * kStreamChunk] {};
	eng::u32 m_stream_total = 0;
	eng::u32 m_stream_bytes = 0;
	eng::u32 m_stream_chunks = 0;
	bool m_stream_ok = false;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		(void)backend.configure_memory({16u * 1024u, 8u * 1024u, 4u * 1024u});

		// --- texto ---
		{
			const eng::os::FileHandle h =
				eng::os::file_open("data/text/hello.txt", eng::os::FileMode::Read);
			if (h != 0u) {
				eng::u8 buf[48] {};
				const eng::s32 n = eng::os::file_read_sync(
					h, eng::Span<eng::u8> {buf, 47u}, 0u);
				if (n > 0) {
					m_text_len = static_cast<eng::u32>(n);
					for (eng::u32 i = 0u; i < m_text_len && i < 47u; ++i) {
						m_text[i] = static_cast<char>(buf[i]);
					}
					m_text_ok = true;
				}
				eng::os::file_close(h);
			}
		}

		// --- imagen y sonido (tamano) ---
		{
			const eng::os::FileHandle hi =
				eng::os::file_open("data/images/logo.raw", eng::os::FileMode::Read);
			if (hi != 0u) {
				m_img_len = eng::os::file_size(hi);
				eng::os::file_close(hi);
			}
			const eng::os::FileHandle hs =
				eng::os::file_open("data/audio/beep.raw", eng::os::FileMode::Read);
			if (hs != 0u) {
				m_snd_len = eng::os::file_size(hs);
				eng::os::file_close(hs);
			}
		}

		// --- codigo relocatable: cargar y ejecutar ---
		{
			const eng::os::FileHandle h =
				eng::os::file_open("data/code/answer.englib", eng::os::FileMode::Read);
			if (h != 0u) {
				const eng::s32 n = eng::os::file_read_sync(
					h, eng::Span<eng::u8> {m_lib, kLibMax}, 0u);
				eng::os::file_close(h);
				const eng::res::LibHandle lib = m_dl.declare("answer");
				if (n > 0 && m_dl.load(lib, eng::Span<eng::u8> {m_lib,
						static_cast<eng::usize>(n)})) {
					using Fn = eng::s32 (*)();
					auto fn = reinterpret_cast<Fn>(m_dl.symbol(lib, "answer"));
					if (fn != nullptr) {
						m_answer = fn();
						m_lib_ok = (m_answer == 42);
					}
				}
			}
		}

		// --- codigo nativo HUNK: cargar (deteccion de formato) y ejecutar ---
		{
			const eng::os::FileHandle h =
				eng::os::file_open("data/code/answer.hunk", eng::os::FileMode::Read);
			if (h != 0u) {
				const eng::s32 n = eng::os::file_read_sync(
					h, eng::Span<eng::u8> {m_hunk, kLibMax}, 0u);
				eng::os::file_close(h);
				const eng::res::LibHandle lib = m_dl.declare("answer.hunk");
				if (n > 0 && m_dl.load(lib,
						eng::Span<eng::u8> {m_hunk, static_cast<eng::usize>(n)},
						&m_pool)) {
					using Fn = eng::s32 (*)();
					auto fn = reinterpret_cast<Fn>(m_dl.symbol(lib, "answer"));
					if (fn != nullptr) {
						m_hunk_answer = fn();
						m_hunk_ok = (m_hunk_answer == 42);
					}
				}
			}
		}

		// --- escritura: crear out/, escribir y releer ---
		{
			m_dir_ok = eng::os::file_make_dir("out");
			const eng::os::FileHandle h =
				eng::os::file_open("out/result.txt", eng::os::FileMode::Create);
			if (h != 0u) {
				const char* msg = "demo 211: escritura OK";
				const eng::u32 len = 23u;
				const eng::s32 w = eng::os::file_write_sync(
					h, eng::Span<const eng::u8> {
						   reinterpret_cast<const eng::u8*>(msg), len},
					0u);
				eng::os::file_close(h);
				if (w == static_cast<eng::s32>(len)) {
					const eng::os::FileHandle hr = eng::os::file_open(
						"out/result.txt", eng::os::FileMode::Read);
					if (hr != 0u) {
						eng::u8 rb[32] {};
						const eng::s32 r = eng::os::file_read_sync(
							hr, eng::Span<eng::u8> {rb, 32u}, 0u);
						eng::os::file_close(hr);
						m_readback = static_cast<eng::u32>(r);
						m_write_ok = (r == static_cast<eng::s32>(len));
					}
				}
			}
		}

		// --- streaming: leer el fichero de 512 KB por rebanadas (M8) ---
		{
			const eng::os::FileHandle h =
				eng::os::file_open("data/audio/tone_8k_512k.raw", eng::os::FileMode::Read);
			if (h != 0u) {
				m_stream_total = eng::os::file_size(h);
				FileSource src {h};
				// Composición M8: el feeder alimenta el `ChunkStream` del `PcmStream` con PCM crudo
				// (`Codec::None`), es decir, lee del disquete y lo deja listo para reproducir.
				eng::audio::PcmStream<kStreamBufs> stream;
				const eng::u16 num_chunks = static_cast<eng::u16>(
					(m_stream_total + kStreamChunk - 1u) / kStreamChunk);
				const eng::audio::PcmStream<kStreamBufs>::Config pcfg {
					8000u, static_cast<eng::u16>(kStreamChunk), num_chunks,
					static_cast<eng::u8>(eng::audio::pcm_codec::Codec::None)};
				const eng::Span<eng::u8> pbufs[kStreamBufs] = {
					eng::Span<eng::u8> {m_stream_bufs, kStreamChunk},
					eng::Span<eng::u8> {m_stream_bufs + kStreamChunk, kStreamChunk}};
				stream.begin(pcfg, pbufs);
				eng::os::FileChunkFeeder<kStreamBufs, FileSource> feeder;
				feeder.init(stream.state(),
					    eng::Span<eng::u8> {m_stream_bufs, sizeof(m_stream_bufs)},
					    kStreamChunk, m_stream_total, src);
				feeder.pump();
				bool error = false;
				for (eng::u32 guard = 0u; guard < 100000u && !stream.eof() && !error; ++guard) {
					// "Reproduce" (libera buffers) para dejar sitio al siguiente chunk.
					if (stream.state().play_ready()) {
						(void)stream.advance();
					}
					// El backend resuelve la asíncrona como **diferida**: `file_pump`
					// ejecuta una operación y postea `FileDone` al puerto del sistema.
					(void)eng::os::file_pump();
					eng::os::Msg m;
					while (eng::os::system_port().pop(m)) {
						if (m.type == eng::os::MsgType::FileDone &&
						    m.payload.file.result > 0) {
							const eng::os::IoUser u =
								eng::os::IoUser::decode(m.payload.file.cookie);
							const eng::u32 got = static_cast<eng::u32>(
								m.payload.file.result);
							m_stream_bytes += got;
							++m_stream_chunks;
							(void)feeder.on_done(u.id, got);
						} else if (m.type == eng::os::MsgType::FileError) {
							error = true;
						}
					}
					feeder.pump();
				}
				eng::os::file_close(h);
				m_stream_ok = !error && (m_stream_bytes == m_stream_total) &&
					      (m_stream_chunks > 1u);
			}
		}

		const eng::u32 flags = (m_text_ok ? 1u : 0u) | (m_lib_ok ? 2u : 0u) |
				       (m_write_ok ? 4u : 0u) | (m_hunk_ok ? 8u : 0u) |
				       (m_stream_ok ? 16u : 0u);
		eng::debug::mark_ready(g_eng_run_status, 0x00021100u | flags);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		auto& d = backend.debug();
		d.clear();
		d.filled_rect(40, 40, 720, 420, 0x00082030);
		d.rect(40, 40, 720, 420, 0x00ffffff);
		d.text(64, 60, "AMG demo 211 - filesystem (dos.library) + dynload", 0x00ffffff);
		d.text(64, 92, m_text_ok ? "texto: OK" : "texto: FALLO", m_text_ok ? 0x0000ff80 : 0x00ff6060);
		d.text(64, 118, m_text, 0x00ffff00);

		char line[64];
		char* p = append(line, "imagen bytes: ");
		p = append_u32(p, m_img_len);
		p = append(p, "   sonido bytes: ");
		p = append_u32(p, m_snd_len);
		*p = '\0';
		d.text(64, 150, line, 0x00ffffff);

		{
			char* q = append(line, "englib: ");
			q = append(q, m_lib_ok ? "OK (answer=" : "FALLO (answer=");
			q = append_u32(q, static_cast<eng::u32>(m_answer));
			q = append(q, ")");
			*q = '\0';
			d.text(64, 182, line, m_lib_ok ? 0x0000ff80 : 0x00ff6060);
		}
		{
			char* q = append(line, "hunk: ");
			q = append(q, m_hunk_ok ? "OK (answer=" : "FALLO (answer=");
			q = append_u32(q, static_cast<eng::u32>(m_hunk_answer));
			q = append(q, ")");
			*q = '\0';
			d.text(64, 214, line, m_hunk_ok ? 0x0000ff80 : 0x00ff6060);
		}
		{
			char* q = append(line, "escritura: ");
			q = append(q, m_write_ok ? "OK" : "FALLO");
			q = append(q, "   releidos: ");
			q = append_u32(q, m_readback);
			q = append(q, "   mkdir out: ");
			q = append(q, m_dir_ok ? "si" : "ya existia");
			*q = '\0';
			d.text(64, 246, line, m_write_ok ? 0x0000ff80 : 0x00ff6060);
		}
		{
			char* q = append(line, "stream 512 KB: ");
			q = append(q, m_stream_ok ? "OK" : "FALLO");
			q = append(q, "   bytes: ");
			q = append_u32(q, m_stream_bytes);
			q = append(q, "   chunks: ");
			q = append_u32(q, m_stream_chunks);
			*q = '\0';
			d.text(64, 278, line, m_stream_ok ? 0x0000ff80 : 0x00ff6060);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static char* append(char* p, const char* s) {
		while (*s != '\0') { *p++ = *s++; }
		return p;
	}
	static char* append_u32(char* p, eng::u32 v) {
		char tmp[10];
		eng::u8 n = 0;
		do { tmp[n++] = static_cast<char>('0' + (v % 10u)); v /= 10u; } while (v != 0u && n < 10u);
		while (n > 0u) { *p++ = tmp[--n]; }
		return p;
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
