// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/features/engine/amiga/060_eng_core_selfcheck --debug   && bash ./tools/run/run-demo.sh demos/features/engine/amiga/060_eng_core_selfcheck --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/features/engine/amiga/060_eng_core_selfcheck --release && bash ./tools/run/run-demo.sh demos/features/engine/amiga/060_eng_core_selfcheck --keep-running

// ============================================================================
// Demo 060: self-check de eng::core (isqrt, sort, crc32, random) en hardware.
// ============================================================================
//
// Valida en WinUAE los ports de libmisc/libc que cubre el test host HOST-000,
// dibujando el resultado en BITPLANES REALES visibles en la ventana del Amiga.
//
// Arranque de alto nivel (`run_demo`): `main` declara el display (320x256, 6 planos con la
// paleta del self-check) y `run_demo` pone memoria, `start` y bucle; la demo no incluye
// soporte de plataforma ni escribe `SysBase`/`g_eng_run_status`. El texto se dibuja por la
// fachada (`screen.text`, Font8).
//
// Build (Windows nativo):
//   bash tools/build/build-demo.sh demos/features/engine/amiga/060_eng_core_selfcheck --clean
//   <Node de Windows> dist/tools/run/run-demo.js demos\features\engine\amiga\060_eng_core_selfcheck --warp

#include <eng/api/api.hpp>
#include <eng/core/data/crc32.hpp>
#include <eng/core/data/sort.hpp>
#include <eng/core/math/isqrt.hpp>
#include <eng/core/math/random.hpp>
#include <eng/core/types/span.hpp>
#include <eng/platform/amiga/entry.hpp> // run_demo: plataforma/memoria/display/bucle

namespace {

// Índices de la paleta base (0..31): texto blanco 31, amarillo 30, rojo 26, cian 20 (pie).
constexpr eng::u8 kTextWhite = 31u;
constexpr eng::u8 kTextYellow = 30u;
constexpr eng::u8 kTextFail = 26u;

/// Paleta del self-check (32 entradas de 12 bits).
constexpr eng::Palette32 kPalette {{
	0x000, 0x06a, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x0aa, 0x000, 0xf00, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0xff0, 0xfff,
}};

/// Versión del HUD; el build la inyecta `build-demo.sh`.
constexpr eng::debug::Version kVersion {1u, 0u};

// Resultado del self-check.
struct SelfCheck {
	bool isqrt_ok = false;
	bool crc32_ok = false;
	bool random_ok = false;
	bool sort_ok = false;

	bool ok() const { return isqrt_ok && crc32_ok && random_ok && sort_ok; }
};

SelfCheck g_check;

// --- Fase 1: isqrt ----------------------------------------------------------
void check_isqrt() {
	const struct { eng::u32 n; eng::u32 expect; } cases[] = {
		{ 0u, 0u }, { 1u, 1u }, { 4u, 2u }, { 9u, 2u }, { 16u, 4u },
		{ 100u, 10u }, { 32768u, 181u }, { 65535u, 253u }, { 65536u, 256u },
		{ 100000u, 313u }, { 1000000u, 999u }, { 2147483647u, 45977u },
	};
	g_check.isqrt_ok = true;
	for (const auto& c : cases) {
		if (eng::isqrt(c.n) != c.expect) {
			g_check.isqrt_ok = false;
			return;
		}
	}
}

// --- Fase 2: crc32 ----------------------------------------------------------
void check_crc32() {
	const eng::u8 b1[] = { '1','2','3','4','5','6','7','8','9' };
	const eng::u8 b2[] = { 0x00 };
	const eng::u8 b3[] = { 0x00, 0x00, 0x00, 0x00 };
	const eng::u8 b4[] = { 'a','b','c','d','e','f','g','h','i','j','k','l','m',
	                       'n','o','p','q','r','s','t','u','v','w','x','y','z' };
	const eng::u8 b5[] = { 0x41 };
	const struct { const eng::u8* data; eng::u32 len; eng::u32 expect; } cases[] = {
		{ b1, sizeof(b1), 0xCBF43926u },
		{ b2, sizeof(b2), 0xD202EF8Du },
		{ b3, sizeof(b3), 0x2144DF1Cu },
		{ b4, sizeof(b4), 0x4C2750BDu },
		{ b5, sizeof(b5), 0xD3D99E8Bu },
	};
	g_check.crc32_ok = true;
	for (const auto& c : cases) {
		if (eng::crc32(c.data, c.len) != c.expect) {
			g_check.crc32_ok = false;
			return;
		}
	}
}

// --- Fase 3: random ---------------------------------------------------------
void check_random() {
	eng::Xoroshiro64pp rng{1u, 0u};
	const eng::u32 expect[] = {
		0x48020a01u, 0x81662931u, 0xcd2b5253u, 0xd3e6cbe6u, 0xcd5af43du,
		0x860aa4bau, 0xb7bea7fbu, 0x63dcaff3u, 0x762d74c9u, 0x3e7d7e8fu,
	};
	g_check.random_ok = true;
	for (const eng::u32 e : expect) {
		if (rng.next() != e) {
			g_check.random_ok = false;
			return;
		}
	}
}

// --- Fase 4: sort -----------------------------------------------------------
void check_sort() {
	eng::s32 a[] = { 9, 4, 7, 1, 5, 3, 8, 2, 6, 0, 10, -3 };
	const eng::s32 n = static_cast<eng::s32>(sizeof(a) / sizeof(a[0]));
	eng::quick_sort(eng::Span<eng::s32>{a}, [](eng::s32 x, eng::s32 y) {
		return x <= y;
	});
	g_check.sort_ok = true;
	for (eng::s32 i = 1; i < n; ++i) {
		if (a[i - 1] > a[i]) {
			g_check.sort_ok = false;
			return;
		}
	}
	if (a[0] != -3 || a[n - 1] != 10) {
		g_check.sort_ok = false;
	}
}

const char* ok_fail(bool ok) {
	return ok ? "OK" : "FAIL";
}

struct CoreSelfcheckDemo {
	static consteval int language_level_marker() { return 23; }

	void init(auto& app) {
		static_assert(language_level_marker() == 23);

		// Ejecuta las fases y dibuja el resultado por la **fachada** (Font8, cualquier nº de
		// planos/color): `screen.text` resuelve el mapeo y el recorte.
		check_isqrt();
		check_crc32();
		check_random();
		check_sort();

		auto s = app.screen();
		s.clear(0u); // fondo negro determinista (los planos nacen sin limpiar)
		s.text(16, 16, "Demo 060 - eng::core self-check", kTextWhite);
		s.text(16, 40, "isqrt  : ", kTextWhite);
		s.text(100, 40, ok_fail(g_check.isqrt_ok), g_check.isqrt_ok ? kTextYellow : kTextFail);
		s.text(16, 56, "crc32  : ", kTextWhite);
		s.text(100, 56, ok_fail(g_check.crc32_ok), g_check.crc32_ok ? kTextYellow : kTextFail);
		s.text(16, 72, "random : ", kTextWhite);
		s.text(100, 72, ok_fail(g_check.random_ok), g_check.random_ok ? kTextYellow : kTextFail);
		s.text(16, 88, "sort   : ", kTextWhite);
		s.text(100, 88, ok_fail(g_check.sort_ok), g_check.sort_ok ? kTextYellow : kTextFail);
		s.text(16, 120, g_check.ok() ? "SELF-CHECK: ALL PHASES OK" : "SELF-CHECK: FAILED",
		       g_check.ok() ? kTextYellow : kTextFail);
		s.text(16, 240, "optica: eng::core ports", 20u);
		(void)eng::debug::draw_hud(s, "CORE 060", kVersion, app.frame(), kTextWhite);

		if (!g_check.ok()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
		} else {
			eng::amiga::mark_ready();
		}
	}

	// Pantalla estática: no hay trabajo por frame (el HUD se rotula en init).
	void update(auto&) {}
	void render(auto&) {}
};

} // namespace

int main() {
	// Display imperativo: 320x256 con 6 planos y la paleta del self-check; `run_demo` pone
	// memoria, `start` y bucle. La pantalla es estática (se pinta en `init`).
	eng::GameDisplay display {};
	display.width = 320u;
	display.height = 256u;
	display.color_depth = 6u;
	display.palette = kPalette;
	CoreSelfcheckDemo game {};
	// Espera de VBlank **activa** (familia 212/DT-011): el bucle por senal pierde el
	// despertar bajo el SO vivo (y el guard 0xE212 lo rechaza); ver el doc 212.
	return eng::amiga::run_demo(game, display, eng::amiga::VblankWait::Active);
}
