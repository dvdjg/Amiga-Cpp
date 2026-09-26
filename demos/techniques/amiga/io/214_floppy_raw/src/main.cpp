// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/io/214_floppy_raw --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/io/214_floppy_raw
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/io/214_floppy_raw --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/io/214_floppy_raw

#include <eng/api/api.hpp>
#include <eng/core/data/byte_order.hpp>
#include <eng/os/floppy.hpp>
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
// Demo 214 — disquete a bajo nivel (DMA crudo + decode MFM en CPU)
// -----------------------------------------------------------------------------
// Lee la pista 0 (cara 0) de DF0: con DMA crudo de Paula (DSKPT/DSKLEN doble + WORDSYNC),
// decodifica los dos primeros sectores en CPU y valida el bootblock: firma DOS\0/DOS\1 y los
// checksums propios de cada sector AmigaDOS (hck/dck). NO usa trackdisk.device ni dos.library.
//
//   node tools/fs/make-volume.mjs
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/io/214_floppy_raw --debug --clean
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/io/214_floppy_raw --disk out/fs/211_fs_test.adf --wait-ms 12000
// -----------------------------------------------------------------------------

// Una revolución completa + un sector de margen: el DMA arranca en el primer sync que ve y
// parte el sector de ese sync, que así reaparece entero en la vuelta siguiente.
constexpr eng::u16 kTrackWords = eng::os::kMfmReadWords; // dos vueltas
constexpr eng::u16 kTrackBytes = kTrackWords * 2u;

char* append(char* p, const char* s) {
	while (*s != '\0') {
		*p++ = *s++;
	}
	return p;
}

char* append_u32(char* p, eng::u32 v) {
	char tmp[10];
	eng::u8 n = 0;
	do {
		tmp[n++] = static_cast<char>('0' + (v % 10u));
		v /= 10u;
	} while (v != 0u && n < 10u);
	while (n > 0u) {
		*p++ = tmp[--n];
	}
	return p;
}

struct DemoGame {
	// Etapa actual en `detail` (para localizar el cuelgue desde el runner si no llega a READY).
	static void stage(eng::u32 code) { g_eng_run_status.detail = code; }

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		stage(0x214001u);
		if (!backend.configure_memory({ 48u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021401u);
			return;
		}
		stage(0x214002u);
		const eng::MemoryBlock mb = backend.memory().chip.allocate(kTrackBytes, 2u);
		if (!mb.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021402u);
			return;
		}
		stage(0x214003u);
		m_track = static_cast<eng::u16*>(mb.data);
		for (eng::u16 i = 0u; i < kTrackWords; ++i) {
			m_track[i] = 0u;
		}

		stage(0x214004u); // antes de motor on
		m_motor = eng::os::floppy_motor(0u, true);
		stage(m_motor ? 0x214005u : 0x214105u);
		// La fase de rotación varía entre lecturas, así que se reintenta (como `trackdisk`)
		// hasta verificar los dos sectores del bootblock.
		// Una vuelta basta (11 sectores); leer dos vueltas no ayuda (verificado: la lectura larga
		// se queda en un tramo). El sync de arranque depende de la fase rotacional, asi que se
		// reintenta con un desfase creciente hasta dar con los sectores 0/1.
		for (eng::u8 attempt = 0u; attempt < 4u; ++attempt) {
			if (attempt > 0u) {
				// Desfase creciente (la fase cambia entre intentos): sin esto cada lectura
				// arranca en la misma fase y el reintento no aporta nada.
				const eng::u32 d = 200000u * attempt;
				for (volatile eng::u32 i = 0u; i < d; ++i) {
				}
			}
			stage(0x214010u | attempt); // antes de read_track (intento)
			m_words = eng::os::floppy_read_track(
				0u, 0u, false, eng::Span<eng::u16> { m_track, kTrackWords });
			stage(m_words != 0u ? (0x214020u | attempt) : (0x214120u | attempt));
			if (m_words > m_max_words) {
				m_max_words = m_words;
			}
			if (m_words == 0u) {
				// La DMA no completo (DSKBLK). Reintentar con el desfase del bucle alarga la demo
				// mucho (cada intento agota el guard ≈ 15 s); se corta y se reporta el motivo.
				break;
			}
			eng::os::FloppySectorHeader hdr0 {};
			const bool sec0 = eng::os::floppy_find_sector(
				eng::Span<const eng::u16> { m_track, kTrackWords }, 0u,
				eng::Span<eng::u8> { m_boot, 512u }, &hdr0, true);
			const bool sec1 = eng::os::floppy_find_sector(
				eng::Span<const eng::u16> { m_track, kTrackWords }, 1u,
				eng::Span<eng::u8> { m_boot + 512u, 512u }, nullptr, true);
			m_sec_ok = sec0 && sec1;
			if (m_sec_ok) {
				m_hdr_track = hdr0.track;
				m_hdr_sector = hdr0.sector;
				m_sig = (m_boot[0] == 'D' && m_boot[1] == 'O' && m_boot[2] == 'S' &&
					 m_boot[3] <= 1u);
				m_kind = m_boot[3];
				break;
			}
		}
		(void)eng::os::floppy_motor(0u, false);

		m_ok = m_motor && m_words != 0u && m_sec_ok && m_sig;
		if (m_ok) {
			eng::debug::mark_ready(g_eng_run_status, 0x00021400u);
		} else {
			// Diagnostico del fallo: mascara de sectores (0..10) presentes en el buffer leido
			// (bit `s` = encontrado). Con la lectura de **una vuelta** la ventana capturada
			// depende de la fase rotacional; los fallos tipicos son mascaras contiguas que dejan
			// fuera el 0/1 (que caen en el "wrap" de la pista).
			eng::u32 mask = 0u;
			for (eng::u8 s = 0u; s < 11u; ++s) {
				eng::u8 tmp[512];
				if (eng::os::floppy_find_sector(
					eng::Span<const eng::u16> { m_track, kTrackWords }, s,
					eng::Span<eng::u8> { tmp, 512u }, {}, true)) {
					mask |= (1u << s);
				}
			}
			eng::debug::mark_failed(g_eng_run_status, mask);
		}
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		auto& d = backend.debug();
		d.clear();
		d.filled_rect(40, 40, 720, 420, m_ok ? 0x00082030 : 0x00502020);
		d.rect(40, 40, 720, 420, 0x00ffffff);
		d.text(64, 60, "AMG demo 214 - floppy DMA crudo + MFM", 0x00ffffff);

		char line[64];
		eng::s16 y = 96;
		{
			char* p = append(line, "motor: ");
			p = append(p, m_motor ? "OK" : "FALLO");
			p = append(p, "   palabras DMA: ");
			p = append_u32(p, m_words);
			*p = '\0';
			d.text(64, y, line, m_words != 0u ? 0x0000ff80 : 0x00ff6060);
		}
		y += 26;
		{
			char* p = append(line, "sectores bootblock 0/1: ");
			p = append(p, m_sec_ok ? "OK (hck/dck)" : "NO");
			p = append(p, "   hdr track=");
			p = append_u32(p, m_hdr_track);
			p = append(p, " sector=");
			p = append_u32(p, m_hdr_sector);
			*p = '\0';
			d.text(64, y, line, m_sec_ok ? 0x0000ff80 : 0x00ff6060);
		}
		y += 26;
		{
			char* p = append(line, "bootblock: ");
			p = append(p, m_sig ? "DOS" : "?");
			p = append(p, m_kind <= 1u ? (m_kind == 0u ? "0 (OFS)" : "1 (FFS)") : "?");
			p = append(p, "   firma: ");
			p = append(p, m_sig ? "OK" : "MAL");
			*p = '\0';
			d.text(64, y, line, m_sig ? 0x0000ff80 : 0x00ff6060);
		}
		y += 26;
		d.text(64, y, "sin trackdisk.device ni dos.library; buffer en Chip RAM", 0x00aaaaaa);

		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	eng::u16* m_track = nullptr;
	eng::u16 m_words = 0u;
	eng::u16 m_max_words = 0u; ///< mejor `words` visto (diagnostico)
	eng::u8 m_boot[1024] {}; // bootblock completo (2 sectores)
	eng::u8 m_kind = 0xffu;
	eng::u8 m_hdr_track = 0xffu;
	eng::u8 m_hdr_sector = 0xffu;
	bool m_motor = false;
	bool m_sec_ok = false;
	bool m_sig = false;
	bool m_ok = false;
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
