// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/debug/205_hw_probe --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/debug/205_hw_probe
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/debug/205_hw_probe --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/debug/205_hw_probe

#include <eng/api/api.hpp>
#include <eng/api/effects.hpp>
#include <eng/hw/info.hpp>
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

namespace composition = eng::graphics::composition;

// -----------------------------------------------------------------------------
// Demo 205 — inventario de hardware (eng::hw::probe)
// -----------------------------------------------------------------------------
// Llama a `eng::hw::probe` UNA vez y muestra el `HwInfo` en el overlay del
// depurador: modelo, chipset, CPU/FPU, Kickstart, RAM por tipo, display y puertos.
//
// El display NO se declara a mano: la escena de composición (`composition::Scene`)
// programa el modo y lo publica en el `HwInfo` con `hw::set_display` (bind_hw_info);
// la demo solo lee `hw.display`. Es la integración del paso 4.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/debug/205_hw_probe --debug --clean
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/debug/205_hw_probe --wait-ms 8000
// -----------------------------------------------------------------------------

constexpr eng::u16 kWidth = 320;
constexpr eng::u16 kHeight = 256;
constexpr eng::u8 kPlanes = 5;
// BPLCON0 = 5 planos (BPU=5 -> 5<<12) + COLOR (bit 9).
constexpr eng::u16 kBplcon0_5Planes = 0x5200;

// Claves del degradado (registro COLOR00) y tramo que reclama el efecto.
constexpr eng::u16 kGradientKeys[8] { 0x000, 0x124, 0x246, 0x368, 0x48a, 0x5ac, 0x7df, 0xfff };
constexpr eng::u16 kFxFirstLine = 0x2cu;
constexpr eng::u8 kFxBands = 24u;
constexpr eng::u16 kFxBandHeight = 8u;

constexpr eng::u16 kPalette[32] {
	0x000, 0x00f, 0x0f0, 0x0ff, 0xf00, 0xf0f, 0xff0, 0xfff,
	0x124, 0x246, 0x368, 0x48a, 0x5ac, 0x6ce, 0x7df, 0x9ef,
	0x012, 0x024, 0x036, 0x048, 0x05a, 0x06c, 0x07e, 0x08f,
	0x210, 0x420, 0x630, 0x840, 0xa50, 0xc60, 0xe70, 0xaaa,
};

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
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 80u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020501u);
			return;
		}

		// 1) Inventario de hardware (CPU, chipset, RAM, modelo...).
		m_probed = eng::hw::probe(m_hw);
		// 2) La escena publica el display que programa en el HwInfo ligado.
		m_scene.bind_hw_info(m_hw);

		// 3) Compone un display planar 320x256x5: al inicializar, la escena programa
		//    el modo y actualiza `m_hw.display` (width/height/depth/colores).
		const bool composed = composition::compose(
			m_scene, backend.memory(), composition::planar(kWidth, kHeight, kPlanes),
			composition::ocs_a500,
			composition::display(composition::kPal320x256, kBplcon0_5Planes),
			composition::palette(eng::PaletteWords { kPalette, 32u }));

		// 4) Efecto por la **lista ordenada** de la escena: un degradado de bandas que declara
		//    su tramo y su coste al plan (se ejecuta en `Scene::tick`, cada frame).
		const eng::graphics::effects::RasterGradientRange range { kFxFirstLine, kFxBandHeight,
									  kFxBands, 0u };
		m_fx_ok = m_sky.attach(range, eng::Span<const eng::u16> { kGradientKeys, 8u });
		if (m_fx_ok) {
			m_fx_bands = m_sky.bands();
			m_effect_task.self = this;
			m_scene.add_effect(m_effect_task);
			m_fx_registered = m_scene.effect_count() == 1u;
		}

		m_ok = m_probed && composed && m_scene.ok() && m_hw.chip_ram_bytes > 0u &&
		       m_hw.cpu != eng::hw::CpuKind::Unknown && m_fx_ok && m_fx_registered;

		KPrintF("AMG205 model=%s chipset=%s cpu=%s chip=%ld fast=%ld slow=%ld kick=%ld.%ld disp=%ldx%ldx%ld\n",
			eng::hw::model_name(m_hw.model),
			eng::hw::chipset_name(m_hw.chipset),
			eng::hw::cpu_name(m_hw.cpu),
			static_cast<eng::u32>(m_hw.chip_ram_bytes),
			static_cast<eng::u32>(m_hw.fast_ram_bytes),
			static_cast<eng::u32>(m_hw.slow_ram_bytes),
			static_cast<eng::u32>(m_hw.kick_major),
			static_cast<eng::u32>(m_hw.kick_minor),
			static_cast<eng::u32>(m_hw.display.width),
			static_cast<eng::u32>(m_hw.display.height),
			static_cast<eng::u32>(m_hw.display.depth));

		if (m_ok) {
			eng::debug::mark_ready(g_eng_run_status, 0x00020500u);
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00020502u);
		}
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_fx_registered) {
			// Ciclo del plan: abrir -> efectos (`tick` = `run_effects`) -> materializar -> cerrar.
			m_scene.begin_build();
			m_scene.tick();
			m_scene.plan().materialize();
			m_fx_budget_ok = m_scene.end_build();
			m_fx_cost = m_scene.plan().cost_words();
			m_fx_capacity = m_scene.plan().words_capacity();
			m_fx_phase = static_cast<eng::u16>(m_fx_phase + 1u);
		}
		(void)backend;
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
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
			p = append(p, "   (scene)");
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
			char* p = append(line, "fx (lista): ");
			p = append(p, m_fx_registered ? "1 efecto" : "ninguno");
			p = append(p, "   bands: ");
			p = append_u32(p, m_fx_bands);
			p = append(p, "   decl: ");
			p = append_u32(p, m_fx_intents);
			p = append(p, "+");
			p = append_u32(p, m_fx_words);
			p = append(p, "   coste: ");
			p = append_u32(p, m_fx_cost);
			p = append(p, "/");
			p = append_u32(p, m_fx_capacity);
			p = append(p, m_fx_budget_ok ? " ok" : " OVER");
			*p = '\0';
			d.text(64, y, line, m_fx_budget_ok ? 0x00ff80ff : 0x00ff6060);
		}
		y += 26;
		d.text(64, y, "Port1: mouse   Port2: joystick   (assumed, not detected)", 0x00aaaaaa);

		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	/// Callable del efecto: `Scene::add_effect` guarda un `FunctionRef` **no propietario**, así
	/// que el functor debe vivir en el demo (una lambda temporal quedaría colgando).
	struct EffectTask {
		DemoGame* self = nullptr;
		void operator()(composition::Scene& s) const { self->run_effect(s); }
	};

	/// Efecto registrado en la **lista ordenada** de la escena (`EFFECT_MODEL`): declara su
	/// tramo de raster (`reserve_band`, que detecta solapes) y su coste (`note_effect_cost`,
	/// que suma al presupuesto del frame), y aporta el degradado al plan.
	void run_effect(composition::Scene& scene) {
		eng::copper::Plan& plan = scene.plan();
		const eng::u16 last = static_cast<eng::u16>(kFxFirstLine + kFxBands * kFxBandHeight);
		plan.reserve_band(kFxFirstLine, last, 0x0001u);
		m_fx_intents = static_cast<eng::u16>(m_sky.bands());
		m_fx_words = static_cast<eng::u16>(m_fx_intents * 4u);
		plan.note_effect_cost({ m_fx_intents, m_fx_words });
		m_sky.set_phase(m_fx_phase);
		m_sky.frame(scene);
	}

private:
	composition::Scene m_scene {};
	eng::effects::Gradient m_sky {};
	EffectTask m_effect_task {};
	eng::hw::HwInfo m_hw {};
	eng::u16 m_fx_phase = 0u;
	eng::u16 m_fx_cost = 0u;
	eng::u16 m_fx_capacity = 0u;
	eng::u16 m_fx_bands = 0u;
	eng::u16 m_fx_intents = 0u;
	eng::u16 m_fx_words = 0u;
	bool m_fx_registered = false;
	bool m_fx_ok = false;
	bool m_fx_budget_ok = true;
	bool m_probed = false;
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
