// ============================================================================
// Demo 060: self-check de eng::core (isqrt, sort, crc32, random) en hardware.
// ============================================================================
//
// Valida en WinUAE los ports de libmisc/libc que cubre el test host HOST-000,
// dibujando el resultado en BITPLANES REALES con el API de alto nivel:
//
//   CanvasPlayfield (lienzo EHB) -> Surface (contexto de dibujo) -> draw_text
//
// El programador NO ve punteros a bitplanes ni planos: dibuja sobre una
// `Surface` (contexto de dispositivo, como un RastPort de la ROM) y el texto
// se enruta por el mapeo del playfield. Entradas en UTF-8 (fuente LATIN-1).
//
// Fases (las mismas que HOST-000) ejecutadas en init, resultado publicado en
// `g_eng_run_status.detail`:
//
//   0x060100FF  todas las fases OK (self-check completo)
//   0x06000201  isqrt fallo  ·  0x06000202  crc32 fallo
//   0x06000203  random fallo  ·  0x06000204  sort fallo
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
#include <eng/field/playfield.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/copper/scheduler.hpp>
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

namespace field = eng::field;
namespace copper = eng::copper;

constexpr eng::u16 kScreenW = 320;
constexpr eng::u16 kScreenH = 256;
constexpr eng::u8 kPlanes = 6; // EHB

// Índices EHB (0..63): fondo 1 (azul), texto blanco 31, amarillo 30, rojo 26,
// cian 20 (pie).
constexpr eng::u8 kBgIndex = 1;
constexpr eng::u8 kTextWhite = 31;
constexpr eng::u8 kTextYellow = 30;
constexpr eng::u8 kTextFail = 26;

// Resultado del self-check.
struct SelfCheck {
	bool isqrt_ok = false;
	bool crc32_ok = false;
	bool random_ok = false;
	bool sort_ok = false;

	bool ok() const { return isqrt_ok && crc32_ok && random_ok && sort_ok; }
	eng::u32 detail() const {
		if (ok()) return 0x060100FFu;
		if (!isqrt_ok) return 0x06000201u;
		if (!crc32_ok) return 0x06000202u;
		if (!random_ok) return 0x06000203u;
		return 0x06000204u;
	}
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

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		static_assert(language_level_marker() == 23);

		m_memory_ok = backend.configure_memory({
			96u * 1024u, // Chip: bitplanes EHB + copperlist
			8u * 1024u,  // Slow: metadatos
			4u * 1024u,  // Frame scratch
		});

		// Lienzo EHB 320x256, 6 planos.
		field::CanvasPlayfield::Config canvas_cfg;
		canvas_cfg.width = kScreenW;
		canvas_cfg.height = kScreenH;
		canvas_cfg.planes = kPlanes;
		m_canvas_ok = m_canvas.begin(backend.memory(), canvas_cfg);
		if (!m_memory_ok || !m_canvas_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000050u);
			return;
		}

		// Paleta EHB (32 físicas; half-brite usa el 6.º plano -> +32).
		const eng::u16 palette[32] = {
			0x000, 0x06a, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x0aa, 0x000, 0x000, 0xf00, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0xff0, 0xfff,
		};

		// Copper de display en Chip RAM.
		eng::MemoryBlock copper_block = backend.memory().chip.allocate(1024, 16);
		if (!copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000051u);
			return;
		}
		copper::Scheduler scheduler { copper_block };
		const auto& view = m_canvas.hardware_view();
		scheduler.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,   // 320x256 lowres
			m_canvas.bytes_per_row(), 0x6200, kPlanes,
			view.bitplanes, view.plane_bytes
		);
		scheduler.emit_palette(palette);
		scheduler.end();
		m_copper_ok = scheduler.ok();

		if (!m_copper_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000052u);
			return;
		}

		// Fondo azul (índice 1) en TODOS los píxeles del lienzo.
		eng::Span<eng::u8> b = m_canvas.bitmap().bytes();
		for (eng::u16 p = 0; p < kPlanes; ++p) {
			eng::u8* plane = b.data() + static_cast<eng::u32>(p) * m_canvas.bytes_per_row() * kScreenH;
			const eng::u8 value = (kBgIndex & (1u << p)) ? 0xffu : 0x00u;
			for (eng::u32 i = 0; i < static_cast<eng::u32>(m_canvas.bytes_per_row()) * kScreenH; ++i) {
				plane[i] = value;
			}
		}

		// Texto a través de la API (Surface + draw_text, UTF-8/LATIN-1).
		field::SurfaceRect full_clip { 0, 0, kScreenW, kScreenH };
		field::Surface surf { m_canvas, full_clip };

		check_isqrt();
		check_crc32();
		check_random();
		check_sort();

		surf.draw_text(16, 16, "Demo 060 - eng::core self-check", kTextWhite);
		surf.draw_text(16, 40, "isqrt  : ", kTextWhite);
		surf.draw_text(100, 40, ok_fail(g_check.isqrt_ok),
		               g_check.isqrt_ok ? kTextYellow : kTextFail);
		surf.draw_text(16, 56, "crc32  : ", kTextWhite);
		surf.draw_text(100, 56, ok_fail(g_check.crc32_ok),
		               g_check.crc32_ok ? kTextYellow : kTextFail);
		surf.draw_text(16, 72, "random : ", kTextWhite);
		surf.draw_text(100, 72, ok_fail(g_check.random_ok),
		               g_check.random_ok ? kTextYellow : kTextFail);
		surf.draw_text(16, 88, "sort   : ", kTextWhite);
		surf.draw_text(100, 88, ok_fail(g_check.sort_ok),
		               g_check.sort_ok ? kTextYellow : kTextFail);
		surf.draw_text(16, 120,
		               g_check.ok() ? "SELF-CHECK: ALL PHASES OK" : "SELF-CHECK: FAILED",
		               g_check.ok() ? kTextYellow : kTextFail);
		surf.draw_text(16, 240, "óptica: eng::core ports (Surface API)", 20);

		m_copper_words = scheduler.words_used();
		m_copper_ptr = scheduler.data();

		if (m_copper_ptr != nullptr) {
			backend.install_copper_list(m_copper_ptr);
		}

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
		// El contenido ya se escribió en init; aquí solo publicar la copperlist.
		// "commit" del engine ocurre en render (ver AGENTS: update->wait_vblank->render).
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	field::CanvasPlayfield m_canvas {};
	bool m_memory_ok = false;
	bool m_canvas_ok = false;
	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	const eng::u16* m_copper_ptr = nullptr;
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