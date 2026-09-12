// Demo 082 - plasma (PORTE 1:1 de demoscene-repo-orig/effects/plasma/plasma.c).
//
// Plasma 2D (suma separable de tres senoides 1D) sobre un display **copper chunky**
// 288x256: SIN bitplanes, el Copper escribe COLOR00 por bloque de 8x4 (36x64 bloques),
// re-ejecutando la linea de color 4 veces por fila (COP2LC/COPJMP2) y saliendo con SKIP.
// La CPU solo parchea las instrucciones COLOR00 de la rejilla (~2304 words/frame).
//
// Verbatim vs adaptado:
//   - verbatim: punteros por memoria (a0-a4), tablas (data/plasma_tables.hpp), update
//     separable (xbuf/ybuf + incrementos) y el mapeo `cmap[v]`.
//   - adaptado: la copperlist la genera `drivers::CopperChunkyScene` (frontera display);
//     doble buffer con dos instancias; vblank via el engine.
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/drivers/copper_chunky.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"
#include "data/plasma_colors.hpp"
#include "data/plasma_tables.hpp"

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

namespace amiga = eng::amiga;
namespace drivers = eng::graphics::drivers;

constexpr eng::u8 kCols = 36; // HTILES = WIDTH/8
constexpr eng::u8 kRows = 64; // VTILES = HEIGHT/4

/// Simulacion del plasma (verbsatim de `UpdateXBUF`/`UpdateYBUF`/`UpdateChunky`).
struct Plasma {
	eng::u8 a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0;
	eng::u8 xbuf[kCols] {};
	eng::u8 ybuf[kRows] {};
	const plasma_data::PlasmaTables& t = plasma_data::kTables;

	void advance() {
		eng::u8 _a0 = a0, _a1 = a1, _a2 = a2;
		for (eng::u8 x = 0; x < kCols; ++x) {
			xbuf[x] = static_cast<eng::u8>(t.tab1[_a0] + t.tab2[_a1] + t.tab3[_a2]);
			_a0 += 1; _a1 += 2; _a2 += 3;
		}
		eng::u8 _a3 = a3, _a4 = a4;
		for (eng::u8 y = 0; y < kRows; ++y) {
			ybuf[y] = static_cast<eng::u8>(t.tab1[_a3] + t.tab2[_a4]);
			_a3 += 2; _a4 += 3;
		}
		a0 += 1; a1 += 3; a2 += 2; a3 += 1; a4 -= 1;
	}

	eng::u16 color(eng::u8 x, eng::u8 y) const {
		const eng::u8 v = static_cast<eng::u8>(xbuf[x] + ybuf[y]);
		return plasma_data::kColors[v];
	}
};

struct PlasmaDemo {
	void init(amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({64u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008201u);
			return;
		}
		drivers::CopperChunkyConfig cfg {};
		cfg.cols = kCols;
		cfg.rows = kRows;
		for (eng::u8 b = 0; b < 2; ++b) {
			if (!m_scene[b].init(backend.memory(), cfg)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00008202u);
				return;
			}
		}
		// Rellena el primer frame antes de tomar el display (evita basura inicial).
		draw_into(m_scene[0]);
		m_scene[0].takeover(backend);
		m_active = 1;
		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, 0x0082u);
	}

	void update(amiga::MinimalBackend& backend, eng::GameContext&) {
		if (!m_init_ok) return;
		m_plasma.advance();
		draw_into(m_scene[m_active]);
		m_scene[m_active].install(backend);
		m_active ^= 1;
	}

	void render(amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void draw_into(drivers::CopperChunkyScene& scene) {
		for (eng::u8 y = 0; y < kRows; ++y) {
			for (eng::u8 x = 0; x < kCols; ++x) {
				scene.set(y, x, m_plasma.color(x, y));
			}
		}
	}

	Plasma m_plasma {};
	drivers::CopperChunkyScene m_scene[2] {};
	eng::u8 m_active = 0;
	bool m_init_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	amiga::MinimalBackend backend {};
	PlasmaDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffff); // modo por defecto: interrupt-driven

	return 0;
}
