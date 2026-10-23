// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/setup/010_chip_slow_memory --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/setup/010_chip_slow_memory --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/setup/010_chip_slow_memory --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/setup/010_chip_slow_memory --keep-running

// Demo 010 - modelo de memoria Chip/Slow con el arranque de alto nivel (`run_demo`).
//
// `main` declara el display y el **presupuesto por banco** (`MemoryConfig`); `run_demo` pone
// memoria, arranque y bucle. Los recursos persistentes se reservan por **banco tipado**
// (`MemoryManager::chip()/slow()` → `Block<Tag, Banco>`), la scratch de frame la gestiona el
// engine (se muestra con `app.telemetry()`) y una reserva que no cabe devuelve un bloque
// **inválido** (sin excepciones). La demo no ve `SysBase`, soporte ni `g_eng_run_status`.
#include <eng/api/api.hpp>
#include <eng/platform/amiga/entry.hpp>

namespace {

struct HexBuffer {
	char text[11] = {'0', 'x', '0', '0', '0', '0', '0', '0', '0', '0', 0};
};

constexpr char hex_digit(eng::u8 value) {
	return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + value - 10);
}

HexBuffer hex32(eng::u32 value) {
	HexBuffer out {};
	for (eng::u8 i = 0; i < 8; ++i) {
		const eng::u8 shift = static_cast<eng::u8>((7 - i) * 4);
		out.text[2 + i] = hex_digit(static_cast<eng::u8>((value >> shift) & 0x0f));
	}
	return out;
}

eng::s16 bar_width(eng::u32 used, eng::u32 capacity, eng::s16 max_width) {
	if (capacity == 0) {
		return 0;
	}
	const eng::u32 scaled = (used * static_cast<eng::u32>(max_width)) / capacity;
	return static_cast<eng::s16>(scaled);
}

void draw_bar(eng::amiga::DebugOverlay& debug, eng::s16 x, eng::s16 y, eng::s16 width,
	      eng::s16 height, eng::u32 used, eng::u32 capacity, eng::u32 color) {
	debug.rect(x, y, static_cast<eng::s16>(x + width), static_cast<eng::s16>(y + height), 0x00ffffff);
	const eng::s16 fill = bar_width(used, capacity, static_cast<eng::s16>(width - 2));
	if (fill > 0) {
		debug.filled_rect(static_cast<eng::s16>(x + 1), static_cast<eng::s16>(y + 1),
				  static_cast<eng::s16>(x + 1 + fill),
				  static_cast<eng::s16>(y + height - 1), color);
	}
}

/// Versión del HUD; el build la inyecta `build-demo.sh`.
constexpr eng::debug::Version kVersion {1u, 0u};

/// Paleta mínima: COLOR00 azul oscuro (el HUD pinta con tinta 1 = blanco).
constexpr eng::Palette32 kPalette {{
	0x002, 0xfff, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

struct DemoGame {
	void init(auto& app) {
		eng::MemoryManager& mm = app.memory_manager();

		// Persistente (setup): por **banco** (`MemBank`), con `free` real. El dominio va en el Tag.
		m_copper_template = mm.chip().reserve<eng::CopperTag>(1024u, 16u);
		m_bitplane_budget = mm.chip().reserve<eng::PlaneTag>(12u * 1024u, 64u);
		m_entity_pool = mm.slow().reserve<eng::WorkTag>(4096u, 16u);
		m_script_blob = mm.slow().reserve<eng::WorkTag>(8192u, 16u);
		// No cabe en el banco Chip: reserva inválida (sin excepciones).
		m_expected_fail = mm.chip().reserve<eng::PlaneTag>(64u * 1024u, 16u);

		m_memory_ok = m_copper_template.valid() && m_bitplane_budget.valid() &&
			      m_entity_pool.valid() && m_script_blob.valid() && !m_expected_fail.valid();
		if (!m_memory_ok) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
		}
	}

	void update(auto&) {}

	void render(auto& app) {
		auto& debug = app.debug();
		const eng::debug::Telemetry t = app.telemetry(); // bancos + scratch de frame del engine

		debug.clear();
		debug.filled_rect(48, 70, 700, 286, m_memory_ok ? 0x00082030 : 0x00502020);
		debug.rect(48, 70, 700, 286, 0x00ffffff);
		debug.text(76, 96, "AMG demo 010 - Chip/Slow memory model", 0x00ffffff);
		debug.text(76, 128, "Profile: A500_1MB_Slow (Slow is capacity, not true Fast)", 0x0000ff80);
		debug.text(76, 160, m_memory_ok ? "Arena checks: OK" : "Arena checks: FAIL", 0x00ffff00);

		draw_bar(debug, 76, 194, 460, 22, t.chip_used, t.chip_capacity, 0x000080ff);
		debug.text(552, 198, "Chip DMA", 0x00ffffff);

		draw_bar(debug, 76, 226, 460, 22, t.slow_used, t.slow_capacity, 0x0000ff80);
		debug.text(552, 230, "Slow metadata", 0x00ffffff);

		draw_bar(debug, 76, 258, 460, 22, t.frame_used, t.frame_capacity, 0x00ffff00);
		debug.text(552, 262, "Frame scratch", 0x00ffffff);

		const HexBuffer chip_cap = hex32(t.chip_capacity);
		const HexBuffer slow_cap = hex32(t.slow_capacity);
		debug.text(76, 314, "Chip cap:", 0x00ffffff);
		debug.text(196, 314, chip_cap.text, 0x000080ff);
		debug.text(76, 342, "Slow cap:", 0x00ffffff);
		debug.text(196, 342, slow_cap.text, 0x0000ff80);

		// HUD de depuración del host: nombre + número + versión + frame + fps.
		m_host_hud.draw(app.debug(), "010 CHIP/SLOW", kVersion, app.frame(),
				eng::debug::prof_clock(), nullptr, false);
		app.present();
		if (m_memory_ok && !m_ready) {
			m_ready = true;
			eng::amiga::mark_ready();
		}
	}

	eng::Block<eng::CopperTag, eng::MemoryKind::Chip> m_copper_template {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_bitplane_budget {};
	eng::Block<eng::WorkTag> m_entity_pool {};
	eng::Block<eng::WorkTag> m_script_blob {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_expected_fail {};
	bool m_memory_ok = false;
	bool m_ready = false;
	eng::debug::HostHud m_host_hud {}; ///< HUD del host (no pisa la telemetría de memoria)
};

} // namespace

int main() {
	// Display imperativo (320x256x4 por defecto): la demo rotula el HUD en la franja inferior.
	eng::GameDisplay display {};
	display.palette = kPalette;
	DemoGame game {};
	// Presupuesto explícito: 64K chip (display + bancos DMA), 32K slow (datos de CPU), 8K scratch.
	// Espera de VBlank **activa** (familia 212/DT-011): el bucle por senal pierde el
	// despertar bajo el SO vivo (y el guard 0xE212 lo rechaza); ver el doc 212.
	return eng::amiga::run_demo(game, display, eng::MemoryConfig {64u * 1024u, 32u * 1024u,
								      8u * 1024u, 0u},
				    eng::amiga::VblankWait::Active);
}
