// ============================================================================
// Demo 060: self-check de eng::core (isqrt, sort, crc32, random) en hardware.
// ============================================================================
//
// Purpose: validar en WinUAE (evidencia viva de hardware) los cuatro ports de
// libmisc/libc que ya cubre el test host HOST-000. La demo ejecuta las mismas
// comprobaciones por FASES y publica el resultado en `g_eng_run_status.detail`:
//
//   detalle | fase
//   --------|----------------------------------------------
//   0x06000101  isqrt OK (muestras autenticadas del C original)
//   0x06000102  crc32 OK (muestras autenticadas)
//   0x06000103  random OK (xoroshiro64++)
//   0x06000104  quick_sort / sort_items OK
//   0x060100FF  todas las fases OK (self-check completo)
//
// Si una fase falla, `g_eng_run_status.state = Failed` y `detail` codifica la
// fase, de modo que el runner detecta el fallo sin analisis visual. En el
// overlay se dibuja un resumen (identico a las lineas del test host).
//
// Build/run/analyze:
//   tools/build/build-demo.sh demos/amiga/060_eng_core_selfcheck --clean
//   tools/run/run-demo.sh       demos/amiga/060_eng_core_selfcheck
//   tools/analyze/analyze-demo.sh demos/amiga/060_eng_core_selfcheck

#include <eng/core/crc32.hpp>
#include <eng/core/isqrt.hpp>
#include <eng/core/random.hpp>
#include <eng/core/sort.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

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

// Resultado del self-check por fases. `detail()` publica 0x060100FF si todas
// las fases pasan, o codifica la primera fase fallida para depuracion.
struct SelfCheck {
	bool isqrt_ok = false;
	bool crc32_ok = false;
	bool random_ok = false;
	bool sort_ok = false;

	bool ok() const { return isqrt_ok && crc32_ok && random_ok && sort_ok; }
	eng::u32 detail() const {
		if (ok()) {
			return 0x060100FFu;
		}
		if (!isqrt_ok) return 0x06000201u;
		if (!crc32_ok) return 0x06000202u;
		if (!random_ok) return 0x06000203u;
		return 0x06000204u;
	}
};

SelfCheck g_check;

// --- Fase 1: isqrt ----------------------------------------------------------
// Mismas muestras autenticadas del C original que el test host.
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

// Etiquetas legibles para el overlay (const char* estaticos).
const char* kLabelOk = "OK";
const char* kLabelFail = "FAIL";

struct CoreSelfcheckDemo {
	static consteval int language_level_marker() { return 23; }

	void init(eng::amiga::MinimalBackend&, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		static_assert(language_level_marker() == 23);

		// Ejecuta las cuatro fases una sola vez en init.
		check_isqrt();
		check_crc32();
		check_random();
		check_sort();

		if (g_check.ok()) {
			eng::debug::mark_ready(g_eng_run_status, g_check.detail());
		} else {
			eng::debug::mark_failed(g_eng_run_status, g_check.detail());
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		auto& debug = backend.debug();
		debug.clear();

		// Fondo: verde si todo bien, rojo oscuro si falla (detectable por el
		// analizador: verde dominante / no-azul / oscuro).
		const bool all_ok = g_check.ok();
		debug.filled_rect(0, 0, 700, 540, all_ok ? 0x00206010 : 0x00600010);

		debug.text(20, 20, "Demo 060 - eng::core self-check (isqrt/sort/crc32/random)", 0x00ffffff);
		debug.text(20, 56, "isqrt  : ", 0x00ffffff);
		debug.text(100, 56, g_check.isqrt_ok ? kLabelOk : kLabelFail, g_check.isqrt_ok ? 0x0000ff80 : 0x00ff4040);
		debug.text(20, 92, "crc32  : ", 0x00ffffff);
		debug.text(100, 92, g_check.crc32_ok ? kLabelOk : kLabelFail, g_check.crc32_ok ? 0x0000ff80 : 0x00ff4040);
		debug.text(20, 128, "random : ", 0x00ffffff);
		debug.text(100, 128, g_check.random_ok ? kLabelOk : kLabelFail, g_check.random_ok ? 0x0000ff80 : 0x00ff4040);
		debug.text(20, 164, "sort   : ", 0x00ffffff);
		debug.text(100, 164, g_check.sort_ok ? kLabelOk : kLabelFail, g_check.sort_ok ? 0x0000ff80 : 0x00ff4040);

		debug.rect(20, 190, 560, 220, 0x00ffff80);
		debug.text(30, 200, all_ok ? "SELF-CHECK: ALL PHASES OK" : "SELF-CHECK: FAILED", 0x00ffff00);

		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	CoreSelfcheckDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}