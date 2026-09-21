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
//   - adaptado: la escena es modo `CopperChunky` (`scene::compose`) y la lista la emite
//     `composition::CopperChunkyLayer`; doble buffer por el `copper::Plan` (flip + present);
//     vblank via el engine.
#include <eng/api/api.hpp>
#include <eng/graphics/composition/copper_chunky.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"
#include "data/plasma_colors.hpp"
#include "data/plasma_tables.hpp"

// Numero de buffers de display: 1 = sin doble buffer, 2 = doble, 3 = triple.
// Configurable sin tocar codigo: EXTRA_DEFINES="-DK_082_BUFFERS=1".
#ifndef K_082_BUFFERS
#define K_082_BUFFERS 2
#endif
static_assert(K_082_BUFFERS >= 1 && K_082_BUFFERS <= 4, "K_082_BUFFERS fuera de rango");

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};

// Bucle caliente del plasma en ASM (ver support/plasma_chunky.s). Argumentos por
// memoria: [0]=dst [1]=xbuf [2]=ybuf[y] [3]=cmap [4]=cols.
eng::u32 g_plasma_chunky_args[5];
void plasma_chunky_row();
}

namespace {

namespace amiga = eng::amiga;
namespace scene = eng::graphics::composition;
namespace comp = eng::graphics::composition;

constexpr eng::u8 kCols = 36; // HTILES = WIDTH/8
constexpr eng::u8 kRows = 64; // VTILES = HEIGHT/4

/// Simulacion del plasma (verbatim de `UpdateXBUF`/`UpdateYBUF`/`UpdateChunky`).
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
};

struct PlasmaDemo {
	void init(amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({64u * 1024u, 4u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008201u);
			return;
		}
		comp::CopperChunkyConfig cfg {};
		cfg.cols = kCols;
		cfg.rows = kRows;
		// Escena en modo copper chunky (sin bitplanes): reserva solo la copperlist. El doble
		// buffer lo da el `copper::Plan` (dos bloques; `end_build` hace flip y `present`
		// publica la lista nueva con COP1LC).
		scene::SceneResources res = scene::planar(288u, 256u, 0u);
		res.mode = scene::SceneMode::CopperChunky;
		res.copper_bytes = 12288u; // ~11 KB para 36x64 bloques
		if (!scene::compose(m_scene, backend.memory(), res, scene::ocs_a500)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008202u);
			return;
		}
		if (!m_layer.attach(m_scene, cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008203u);
			return;
		}
		// Primer frame + toma del display (la estructura ya está en ambos bloques; aquí solo
		// se escriben los colores y se publica).
		draw_frame(backend);
		m_init_ok = true;
		eng::debug::mark_ready(g_eng_run_status, 0x0082u);
	}

	void update(amiga::MinimalBackend& backend, eng::GameContext&) {
		if (!m_init_ok) return;
		m_plasma.advance();
		draw_frame(backend);
	}

	void render(amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Un frame: colores en el bloque inactivo + flip + install.
	void draw_frame(amiga::MinimalBackend& backend) {
		m_layer.begin_frame(m_scene);
		fill_colors();
		m_layer.end_frame(m_scene, backend);
	}

	/// Escribe los colores del plasma en el bloque del frame (coste: `cols*rows` words).
	void fill_colors() {
		g_plasma_chunky_args[1] = reinterpret_cast<eng::u32>(m_plasma.xbuf);
		g_plasma_chunky_args[3] = reinterpret_cast<eng::u32>(plasma_data::kColors);
		g_plasma_chunky_args[4] = kCols;
		for (eng::u8 y = 0; y < kRows; ++y) {
			eng::u16* p = m_layer.row(y);
			if (p == nullptr) {
				return;
			}
			g_plasma_chunky_args[0] = reinterpret_cast<eng::u32>(p);
			g_plasma_chunky_args[2] = m_plasma.ybuf[y];
			plasma_chunky_row();
		}
	}

	Plasma m_plasma {};
	scene::Scene m_scene {};
	comp::CopperChunkyLayer<kCols, kRows> m_layer {};
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
