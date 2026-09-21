#include <eng/api/api.hpp>
#include <eng/hw/info.hpp>
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
// Demo 205 — inventario de hardware (eng::hw::probe)
// -----------------------------------------------------------------------------
// Llama a `eng::hw::probe` UNA vez y muestra el `HwInfo` en el overlay del
// depurador: modelo, chipset, CPU/FPU, Kickstart, RAM por tipo, display y
// puertos de entrada. El display vigente lo declara la app/el engine con
// `hw::set_display` (los registros de vídeo son de solo escritura); aquí se
// declara el modo que la demo usaría (320x256x5). Evidencia de la API pública:
// `docs/engine/architecture/HARDWARE_INVENTORY.md`.
//
//   bash ./tools/build/build-demo.sh demos/amiga/205_hw_probe --debug --clean
//   bash ./tools/run/run-demo.sh demos/amiga/205_hw_probe
// -----------------------------------------------------------------------------

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 5;

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

char* append_kb(char* p, const char* label, eng::u32 bytes) {
	p = append(p, label);
	p = append_u32(p, bytes / 1024u);
	p = append(p, " KB");
	return p;
}

const char* yes_no(bool v) {
	return v ? "yes" : "no";
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 16u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020501u);
			return;
		}

		m_probed = eng::hw::probe(m_hw);
		// El engine declara el modo de display que programa (aquí, el que usaría
		// la demo). `probe` solo da una estimación del arranque, no fiable.
		if (m_probed) {
			eng::hw::set_display(m_hw, kWidth, kHeight, kPlanes);
		}
		// Saneado mínimo: con Exec presente el chip RAM y la CPU deben conocerse.
		m_ok = m_probed && m_hw.chip_ram_bytes > 0u && m_hw.cpu != eng::hw::CpuKind::Unknown;

		KPrintF("AMG205 model=%s chipset=%s cpu=%s chip=%ld fast=%ld slow=%ld kick=%ld.%ld\n",
			eng::hw::model_name(m_hw.model),
			eng::hw::chipset_name(m_hw.chipset),
			eng::hw::cpu_name(m_hw.cpu),
			static_cast<eng::u32>(m_hw.chip_ram_bytes),
			static_cast<eng::u32>(m_hw.fast_ram_bytes),
			static_cast<eng::u32>(m_hw.slow_ram_bytes),
			static_cast<eng::u32>(m_hw.kick_major),
			static_cast<eng::u32>(m_hw.kick_minor));

		if (m_ok) {
			eng::debug::mark_ready(g_eng_run_status, 0x00020500u);
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00020502u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		const eng::u16 pulse = static_cast<eng::u16>((context.frame.frame_index >> 2) & 0x0f);
		backend.set_color(0, static_cast<eng::u16>((pulse << 8) | 0x004));
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		auto& d = backend.debug();
		d.clear();
		d.filled_rect(40, 40, 720, 440, m_ok ? 0x00082030 : 0x00502020);
		d.rect(40, 40, 720, 440, 0x00ffffff);
		d.text(64, 60, "AMG demo 205 - eng::hw hardware inventory", 0x00ffffff);
		d.text(64, 86, m_ok ? "probe: OK" : "probe: FAIL", m_ok ? 0x0000ff80 : 0x00ff6060);

		char line[64];
		eng::s16 y = 118;

		{
			char* p = append(line, "Model: ");
			p = append(p, eng::hw::model_name(m_hw.model));
			p = append(p, "   Chipset: ");
			p = append(p, eng::hw::chipset_name(m_hw.chipset));
			p = append(p, "   CPU: ");
			p = append(p, eng::hw::cpu_name(m_hw.cpu));
			*p = '\0';
			d.text(64, y, line, 0x00ffff00);
		}
		y += 26;
		{
			char* p = append(line, "Kickstart: ");
			p = append_u32(p, m_hw.kick_major);
			p = append(p, ".");
			p = append_u32(p, m_hw.kick_minor);
			p = append(p, "   NTSC: ");
			p = append(p, yes_no(m_hw.caps.ntsc));
			p = append(p, "   FPU: ");
			p = append(p, yes_no(m_hw.caps.has_fpu));
			*p = '\0';
			d.text(64, y, line, 0x00ffffff);
		}
		y += 26;
		{
			char* p = append_kb(line, "Chip RAM: ", m_hw.chip_ram_bytes);
			*p = '\0';
			d.text(64, y, line, 0x000080ff);
		}
		y += 26;
		{
			char* p = append_kb(line, "Fast RAM: ", m_hw.fast_ram_bytes);
			*p = '\0';
			d.text(64, y, line, 0x0000ff80);
		}
		y += 26;
		{
			char* p = append_kb(line, "Slow RAM: ", m_hw.slow_ram_bytes);
			*p = '\0';
			d.text(64, y, line, 0x00ff8000);
		}
		y += 26;
		{
			char* p = append_kb(line, "Total RAM: ", m_hw.total_ram_bytes);
			p = append(p, "   regions: ");
			p = append_u32(p, m_hw.mem_count);
			*p = '\0';
			d.text(64, y, line, 0x00ffffff);
		}
		y += 26;
		{
			char* p = append(line, "Display: ");
			p = append_u32(p, m_hw.display.width);
			p = append(p, "x");
			p = append_u32(p, m_hw.display.height);
			p = append(p, "x");
			p = append_u32(p, m_hw.display.depth);
			p = append(p, "   colors: ");
			p = append_u32(p, m_hw.display.max_colors);
			p = append(p, "   (declared)");
			*p = '\0';
			d.text(64, y, line, 0x00ffffff);
		}
		y += 26;
		{
			char* p = append(line, "hires: ");
			p = append(p, yes_no(m_hw.display.hires));
			p = append(p, "   lace: ");
			p = append(p, yes_no(m_hw.display.lace));
			p = append(p, "   HAM: ");
			p = append(p, yes_no(m_hw.display.ham));
			p = append(p, "   EHB: ");
			p = append(p, yes_no(m_hw.display.extrahalfbrite));
			*p = '\0';
			d.text(64, y, line, 0x00aaaaaa);
		}
		y += 26;
		{
			char* p = append(line, "Caps: ecs=");
			p = append(p, yes_no(m_hw.caps.ecs_denise));
			p = append(p, " aga=");
			p = append(p, yes_no(m_hw.caps.aga));
			p = append(p, " akiko=");
			p = append(p, yes_no(m_hw.caps.akiko));
			p = append(p, " c2p_hw=");
			p = append(p, yes_no(m_hw.caps.c2p_hw));
			p = append(p, " mmu=");
			p = append(p, yes_no(m_hw.caps.has_mmu));
			*p = '\0';
			d.text(64, y, line, 0x00ffffff);
		}
		y += 26;
		{
			char* p = append(line, "Port1: mouse   Port2: joystick   (assumed, not detected)");
			*p = '\0';
			d.text(64, y, line, 0x00aaaaaa);
		}

		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	eng::hw::HwInfo m_hw {};
	bool m_probed = false;
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
