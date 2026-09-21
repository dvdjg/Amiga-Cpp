#include <eng/api/api.hpp>
#include <eng/core/byte_order.hpp>
#include <eng/os/floppy.hpp>
#include <eng/platform/amiga_minimal.hpp>

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
// decodifica el sector 0 en CPU y valida el bootblock: firma DOS\0/DOS\1 + checksum
// (suma de los 256 longs BE de 4..1023 == 0xFFFFFFFF). NO usa trackdisk.device ni dos.library.
//
//   node tools/fs/make-volume.mjs
//   bash ./tools/build/build-demo.sh demos/amiga/214_floppy_raw --debug --clean
//   bash ./tools/run/run-demo.sh demos/amiga/214_floppy_raw --disk out/fs/211_fs_test.adf --wait-ms 12000
// -----------------------------------------------------------------------------

constexpr eng::u16 kTrackWords = 6400u;
constexpr eng::u16 kTrackBytes = kTrackWords * 2u;

char* append(char* p, const char* s) {
	while (*s != '\0') {
		*p++ = *s++;
	}
	return p;
}

char* append_hex(char* p, eng::u16 v) {
	const char* hex = "0123456789abcdef";
	for (eng::s8 s = 12; s >= 0; s -= 4) {
		*p++ = hex[(v >> s) & 0xfu];
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
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 48u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021401u);
			return;
		}
		const eng::MemoryBlock mb = backend.memory().chip.allocate(kTrackBytes, 2u);
		if (!mb.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021402u);
			return;
		}
		m_track = static_cast<eng::u16*>(mb.data);
		for (eng::u16 i = 0u; i < kTrackWords; ++i) {
			m_track[i] = 0u;
		}

		m_motor = eng::os::floppy_motor(0u, true);
		m_words = eng::os::floppy_read_track(
			0u, 0u, false, eng::Span<eng::u16> { m_track, kTrackWords });

		if (m_words != 0u) {
			for (eng::u16 i = 0u; i + 1u < kTrackWords; ++i) {
				if (m_track[i] == eng::os::kMfmSync &&
				    m_track[i + 1u] == eng::os::kMfmSync) {
					++m_syncs;
				}
			}
			m_w0 = m_track[0];
			m_w1 = m_track[1];
			m_w2 = m_track[2];
			m_w3 = m_track[3];
			eng::os::FloppySectorHeader hdr {};
			m_sec_ok = eng::os::floppy_find_sector(
				eng::Span<const eng::u16> { m_track, kTrackWords }, 0u,
				eng::Span<eng::u8> { m_boot, 512u }, &hdr);
			m_hdr_track = hdr.track;
			m_hdr_sector = hdr.sector;
			if (m_sec_ok) {
				m_sig = (m_boot[0] == 'D' && m_boot[1] == 'O' && m_boot[2] == 'S' &&
					 m_boot[3] <= 1u);
				m_kind = m_boot[3];
				eng::u32 sum = 0u;
				for (eng::u32 i = 4u; i < 1024u; i += 4u) {
					sum += eng::read_be32(m_boot + i);
				}
				m_boot_sum = sum;
				m_sum = (sum == 0xFFFFFFFFu);
			}
		}
		(void)eng::os::floppy_motor(0u, false);

		m_ok = m_motor && m_words != 0u && m_sec_ok && m_sig && m_sum;
		const eng::u32 flags = (m_words != 0u ? 1u : 0u) | (m_sig ? 2u : 0u) |
				       (m_sum ? 4u : 0u);
		if (m_ok) {
			eng::debug::mark_ready(g_eng_run_status, 0x00021400u | flags);
		} else {
			eng::debug::mark_ready(g_eng_run_status, 0x00021410u | flags); // DIAG
		}
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
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
			char* p = append(line, "sector 0: ");
			p = append(p, m_sec_ok ? "decodificado" : "NO");
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
			p = append(p, "   checksum: ");
			p = append_u32(p, m_boot_sum);
			p = append(p, m_sum ? " OK" : " MAL");
			*p = '\0';
			d.text(64, y, line, m_sum ? 0x0000ff80 : 0x00ff6060);
		}
		y += 26;
		{
			char* p = append(line, "syncs: ");
			p = append_u32(p, m_syncs);
			p = append(p, "   w0..3: ");
			p = append_hex(p, m_w0);
			p = append(p, " ");
			p = append_hex(p, m_w1);
			p = append(p, " ");
			p = append_hex(p, m_w2);
			p = append(p, " ");
			p = append_hex(p, m_w3);
			*p = '\0';
			d.text(64, y, line, 0x00ffff00);
		}
		y += 26;
		d.text(64, y, "sin trackdisk.device ni dos.library; buffer en Chip RAM", 0x00aaaaaa);

		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	eng::u16* m_track = nullptr;
	eng::u32 m_boot_sum = 0u;
	eng::u16 m_words = 0u;
	eng::u16 m_syncs = 0u;
	eng::u16 m_w0 = 0u;
	eng::u16 m_w1 = 0u;
	eng::u16 m_w2 = 0u;
	eng::u16 m_w3 = 0u;
	eng::u8 m_boot[512] {};
	eng::u8 m_kind = 0xffu;
	eng::u8 m_hdr_track = 0xffu;
	eng::u8 m_hdr_sector = 0xffu;
	bool m_motor = false;
	bool m_sec_ok = false;
	bool m_sig = false;
	bool m_sum = false;
	bool m_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
