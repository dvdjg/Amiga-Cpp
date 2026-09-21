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
// Estrategia y mapper (en el engine): `eng::field::FlatScrollPlayfield` (bitmap
// flat interleaved + `BigBufferScroll` por eje) sobre `eng::field::map_flat_scroll`
// (cámara→BPLxPT/BPLCON1/BPLMOD, geometría del fetch ancho DDF $30, HOST-061). La
// demo NO conoce registros: mueve la cámara y pide la vista. Fórmula verificada en
// `engine/include/eng/graphics/drivers/tile_scroll.hpp`.
//
// Contenido (art del tileset compartido): el mundo se rellena UNA vez en `init`
// copiando tiles 16x16 del atlas "Beginning Fields" a 8 colores (3 planos,
// `out/assets/beginning-fields/8c`). El banco X-Limited interleaved (3 planos,
// 320 px de ancho) se incrusta en `.MEMF_CHIP` y se copia por CPU (palabra a
// palabra) al bitmap; después no se toca memoria. El mapa es el real (40x40) del
// atlas, repetido toroidalmente para cubrir el mundo.
//
// Presupuesto Chip RAM: bitmap 56*1684*3 = 276 KB + banco 111 KB + copper 3 KB.
// Referencias: `docs/guides/roadmap/SCROLL_DEMOS_CLEANUP.md`,
// `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §2/§5.

#include <eng/api/api.hpp>
#include <eng/field/flat_playfield.hpp>
#include <eng/field/xlimited.hpp>
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

// Datos del atlas a 8 colores (paleta 8 RGB + mapa 40x40 de ids de tile).
#include "../../../../out/assets/beginning-fields/8c/tilebank_8c_t16_mediancut_none_640x640.h"

// Banco de bloques X-Limited YA interleaved (3 planos, layout 320 px de ancho,
// tile t -> (t%20, t/20)) producido en el HOST por amiga-tiles.mjs --xlimited y
// cargado en `.MEMF_CHIP` (elf2hunk -> HUNKF_CHIP): la demo lo copia por CPU al
// bitmap en `init` y no lo vuelve a usar.
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tilebank_xlimited\ng_tilebank_xlimited:\n"
	".align 2\n"
	".incbin \"out/assets/beginning-fields/8c/tilebank_xlimited_8c_t16_mediancut_none.bin\"\n"
	".globl g_tilebank_xlimited_size\ng_tilebank_xlimited_size:\n"
	".long . - g_tilebank_xlimited");
extern "C" const unsigned char g_tilebank_xlimited[];
extern "C" const unsigned int g_tilebank_xlimited_size;

namespace {

namespace field = eng::field;

constexpr eng::u16 kWorldW = 448;              // ancho del mundo (múltiplo de 16)
constexpr eng::u16 kWorldH = 1684;             // alto del mundo
constexpr eng::u16 kViewW = 320;               // ventana visible
constexpr eng::u16 kViewH = 256;
constexpr eng::u8  kPlanes = 3;                // 8 colores (atlas 8c)
constexpr eng::u32 kRowBytes = kWorldW / 8u;   // 56 B por fila (y por plano)
constexpr eng::u32 kCellsX = kWorldW / 16u;    // celdas de 16 px por fila (28)
constexpr eng::u32 kChipNeed =
	static_cast<eng::u32>(kRowBytes) * kWorldH * kPlanes + 2u * 1536u + 64u;

struct DemoGame {
	field::FlatScrollPlayfield m_pf {};
	field::XlimitedDisplayComposer m_comp {};
	eng::u16 m_palette[8] {};   // paleta del atlas, convertida a palabras Amiga
	eng::s32 m_dir_x = 1;
	eng::s32 m_dir_y = 1;
	bool m_ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({kChipNeed + 16u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012001u);
			return;
		}
		if (!m_pf.begin(backend.memory(), {kWorldW, kWorldH, kViewW, kViewH, kPlanes, 42u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012002u);
			return;
		}
		build_palette();
		blit_world();

		field::XlimitedDisplayComposer::Config cfg {};
		cfg.palette = eng::PaletteWords { m_palette, 8u };
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
		sweep(m_pf.cam_x(), m_dir_x);
		sweep(m_pf.cam_y(), m_dir_y);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_ready) {
			if (m_comp.compose(m_pf.hardware_view())) {
				m_comp.install(backend); // swap de COP1LC (doble buffer de copper)
				g_eng_run_status.detail = 0x12000000u |
					((static_cast<eng::u32>(m_pf.cam_x().position) & 0xffu) << 12) |
					(static_cast<eng::u32>(m_pf.cam_y().position) & 0xfffu);
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

	/// Convierte los 8 colores RGB del atlas (kPalette[24], bytes) a palabras
	/// Amiga 0x0RGB (4 bits por componente).
	void build_palette() {
		for (eng::u32 i = 0; i < 8u; ++i) {
			const eng::u8 r = ::kPalette[i * 3u];
			const eng::u8 g = ::kPalette[i * 3u + 1u];
			const eng::u8 b = ::kPalette[i * 3u + 2u];
			m_palette[i] = static_cast<eng::u16>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}
	}

	/// Rellena el mundo UNA sola vez (init) copiando tiles del banco X-Limited.
	/// El mapa (kTileIndexedMap, 40x40) se repite toroidalmente para cubrir el
	/// mundo. A partir de aquí no se toca memoria.
	void blit_world() {
		const eng::u8* const bank = g_tilebank_xlimited;
		eng::u8* const dst = m_pf.bitmap().bytes().data();
		constexpr eng::u32 kBankRowBytes = 320u / 8u;        // 40 B por planelínea del banco
		constexpr eng::u32 kBlocksPerRow = 320u / 16u;       // 20 tiles por fila del banco
		const eng::u32 block_rows =
			g_tilebank_xlimited_size / (16u * kPlanes * kBankRowBytes);
		const eng::u32 tile_count = block_rows * kBlocksPerRow;
		for (eng::u32 cy = 0; cy * 16u < kWorldH; ++cy) {
			for (eng::u32 cx = 0; cx < kCellsX; ++cx) {
				eng::u32 t = kTileIndexedMap[cy % 40u][cx % 40u];
				if (t >= tile_count) t = 0u;
				const eng::u32 bx = t % kBlocksPerRow;
				const eng::u32 by = t / kBlocksPerRow;
				for (eng::u32 r = 0; r < 16u; ++r) {
					const eng::u32 dy = cy * 16u + r;
					if (dy >= kWorldH) break;
					for (eng::u32 p = 0; p < kPlanes; ++p) {
						const eng::u32 src = (by * 16u * kPlanes + r * kPlanes + p) * kBankRowBytes + bx * 2u;
						const eng::u32 d = (dy * kPlanes + p) * kRowBytes + cx * 2u;
						dst[d] = bank[src];
						dst[d + 1u] = bank[src + 1u];
					}
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
