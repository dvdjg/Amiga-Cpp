// ============================================================================
// Demo 120 — Virtual playfield: bitmap FLAT contiguo + scroll SOLO por punteros
// ============================================================================
//
// Técnica: un mundo grande (448x1684) YA DIBUJADO en un único bitmap continuo;
// el scroll no redibuja ni un píxel, solo mueve los punteros BPLxPT y el fine
// BPLCON1 desde el Copper. Es el contraste exacto del corkscrew/XYLimited (107):
// allí el bitmap es un anillo pequeño y el Blitter pinta la banda entrante; aquí
// el coste por frame es CERO blits y la CPU solo reprograma registros.
//
// Estrategia: `eng::field::BigBufferScroll` (una instancia por eje; el struct
// modela un solo eje). La cámara se satura en los límites y reverde.
//
// Superficie: `CanvasPlayfield` (layout interleaved, Chip RAM) con un
// `hardware_view()` reescrito: el de base asume `ancho == viewport` y su BPLMOD
// es el del fetch estándar (DDF $38). Aquí se usa la geometría del fetch ancho
// (DDF $30, 42 B/fila), la misma que el corkscrew y el driver lineal validado
// `tile_scroll.hpp`: `planeaddx = ((cam_x-1) & ~15)/8`, `BPLCON1=(16-fine)&15`,
// `BPLMOD = row_bytes*planes - 42`.
//
// Referencias: `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §2
// (BigBufferScroll), `docs/guides/roadmap/SCROLL_DEMOS_CLEANUP.md` (matriz de
// demos de algoritmo) y `engine/include/eng/graphics/drivers/tile_scroll.hpp`
// (fórmula cámara→puntero verificada).
//
// Presupuesto Chip RAM del mundo 448x1684: 276 KB (3 pl) / 368 KB (4 pl) /
// 460 KB (5 pl). Se usa 4 planos (16 colores) para que quepa en un A500.

#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/scroll_engine.hpp>
#include <eng/field/xlimited.hpp>
#include <eng/memory/arena.hpp>
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

namespace field = eng::field;

constexpr eng::u16 kWorldW = 448;              // ancho del mundo (múltiplo de 16)
constexpr eng::u16 kWorldH = 1684;             // alto del mundo
constexpr eng::u16 kViewW = 320;               // ventana visible
constexpr eng::u16 kViewH = 256;
constexpr eng::u8  kPlanes = 4;                // 16 colores -> 368 KB de bitmap
constexpr eng::u32 kRowBytes = kWorldW / 8u;   // 56 B por fila (y por plano)
constexpr eng::u32 kChipNeed =
	static_cast<eng::u32>(kRowBytes) * kWorldH * kPlanes + 2u * 1536u + 32u;

// Paleta de 16 colores (0x0RGB, 12 bits) con aire de aldea: verdes/tierras/cielo.
constexpr eng::u16 kPalette[16] {
	0x021, 0x042, 0x063, 0x0a4, 0x2c5, 0x5e7, 0x7fb, 0x58a,
	0x740, 0x960, 0xb90, 0xcb0, 0xed0, 0x620, 0xd40, 0xfff,
};

/// Superficie flat con cámara. `CanvasPlayfield` aporta el bitmap interleaved y
/// las primitivas; solo se reescribe la vista de hardware para (a) usar la
/// geometría del fetch ancho y (b) exponer el viewport 320x256 real.
struct FlatPlayfield : field::CanvasPlayfield {
	field::BigBufferScroll cam_x {};
	field::BigBufferScroll cam_y {};

	field::PlayfieldHardwareView hardware_view() const override {
		field::PlayfieldHardwareView v = CanvasPlayfield::hardware_view();
		const eng::s32 cx = cam_x.position; // 1..(world-view)
		const eng::s32 cy = cam_y.position; // 0..(world-view)
		// Fórmula canónica superficie lineal + DDF $30 (tile_scroll.hpp:684-701):
		const eng::u16 fine = static_cast<eng::u16>(cx & 15);
		const eng::u16 nibble = static_cast<eng::u16>((16u - fine) & 15u);
		v.planeaddx = static_cast<eng::u32>((cx - 1) & ~15) / 8u;
		v.bplcon1 = static_cast<eng::u16>(nibble | (nibble << 4));
		// Interleaved: la planelínea siguiente está a row_bytes*planes.
		v.planeaddy = static_cast<eng::u32>(cy) * kPlanes * kRowBytes;
		v.bpl1mod = static_cast<eng::u16>(kRowBytes * kPlanes - 42u); // fetch 40+2
		v.bpl2mod = v.bpl1mod;
		v.viewport_w = kViewW;
		v.viewport_h = kViewH;
		v.display_height = kWorldH;
		v.display_offset = 0;
		v.split_active = false;
		v.videoposx = cx; v.mapposx = cx;
		v.videoposy = cy; v.mapposy = cy;
		return v;
	}
};

struct DemoGame {
	FlatPlayfield m_pf {};
	field::XlimitedDisplayComposer m_comp {};
	eng::s32 m_dir_x = 1;
	eng::s32 m_dir_y = 1;
	bool m_ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({kChipNeed + 8u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012001u);
			return;
		}
		if (!m_pf.begin(backend.memory(), {kWorldW, kWorldH, kPlanes})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012002u);
			return;
		}
		fill_world();

		m_pf.cam_x.min_pos = 1;                                   // mínimo obligatorio (DDF $30)
		m_pf.cam_x.max_pos = static_cast<eng::s32>(kWorldW - kViewW);
		m_pf.cam_y.min_pos = 0;
		m_pf.cam_y.max_pos = static_cast<eng::s32>(kWorldH - kViewH);

		field::XlimitedDisplayComposer::Config cfg {};
		cfg.palette = eng::PaletteWords { kPalette, 16u };
		cfg.copper_bytes = 1536u;
		cfg.planes = kPlanes;
		cfg.ddfstrt = 0x0030u; // fetch ancho: 42 B/fila (coincide con BPLMOD)
		cfg.ddfstop = 0x00D0u;
		if (!m_comp.init(backend.memory(), cfg) || !m_comp.compose(m_pf.hardware_view())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012003u);
			return;
		}
		m_comp.takeover(backend); // congela el sistema y arranca la copperlist
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x12000000u);
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) return;
		// SOLO se mueve la cámara: BigBufferScroll satura y reveer en los límites.
		sweep(m_pf.cam_x, m_dir_x);
		sweep(m_pf.cam_y, m_dir_y);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_ready) {
			// Recompone la copperlist con la cámara nueva (punteros + BPLCON1).
			if (m_comp.compose(m_pf.hardware_view())) {
				m_comp.install(backend); // swap de COP1LC (doble buffer de copper)
				g_eng_run_status.detail = 0x12000000u |
					((static_cast<eng::u32>(m_pf.cam_x.position) & 0xffu) << 12) |
					(static_cast<eng::u32>(m_pf.cam_y.position) & 0xfffu);
			} else {
				m_ready = false;
				eng::debug::mark_failed(g_eng_run_status, 0x00012010u);
			}
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Avanza la cámara 1 px; si ya está en el límite (saturada), invierte.
	static void sweep(field::BigBufferScroll& cam, eng::s32& dir) {
		const eng::s32 before = cam.position;
		cam.step(dir);
		if (cam.position == before) dir = -dir;
	}

	/// Rellena el mundo UNA sola vez (init). A partir de ahí no se toca memoria.
	/// Patrón procedural de celdas de 16 px para que el movimiento sea inequívoco.
	void fill_world() {
		eng::u8* const base = m_pf.bitmap().bytes().data();
		for (eng::u32 y = 0; y < kWorldH; ++y) {
			const eng::u32 cell_y = y >> 4;
			for (eng::u32 xb = 0; xb < kRowBytes; ++xb) {
				const eng::u32 cell_x = xb >> 1;
				eng::u8 idx = static_cast<eng::u8>(((cell_x * 7u) ^ (cell_y * 3u)) & 15u);
				if (cell_x < 1u) idx = 15u;                        // columna 0: marcador de origen
				else if ((cell_x % 10u) == 0u) idx = 14u;          // rejilla vertical marcada
				for (eng::u8 p = 0; p < kPlanes; ++p) {
					base[(y * kPlanes + p) * kRowBytes + xb] =
						(idx & (1u << p)) ? 0xffu : 0x00u;
				}
			}
		}
	}
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
