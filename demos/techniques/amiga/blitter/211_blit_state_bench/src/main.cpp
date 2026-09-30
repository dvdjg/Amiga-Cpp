// Lanzar:
//   Sin agrupar : bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/211_blit_state_bench --debug
//   Agrupado    : EXTRA_DEFINES="-DK_BENCH_SORT=1" bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/211_blit_state_bench --debug
//   Ejecutar    : bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/211_blit_state_bench --warp
//
// Variantes de compilación (`EXTRA_DEFINES`):
//   K_BENCH_SORT   0 | 1   llama a `FramePlan::sort_by_state()` antes de ejecutar (0 = orden de emisión).
//   K_BENCH_STATES 1 | 2   nº de estados distintos: 1 = todos los tiles del mismo estado (control);
//                          2 = tablero de dos estados intercalados (caso donde agrupar ayuda).

// ============================================================================
// Demo 211 - blit_state_bench
// ----------------------------------------------------------------------------
// BENCH de `FramePlan::sort_by_state()` sobre una escena de blits **disjuntos**.
//
// El `FramePlan` admite agrupar los `BlitJob` por **estado común del Blitter**
// (`sort_by_state()`) para que el backend omita las reprogramaciones de `BLTCON*`,
// ventanas y módulos en rachas consecutivas (ver `amiga_blitter.cpp`,
// `submit_blit_job`). Es OPT-IN porque solo es lícito si el orden no importa:
// exige que los jobs reordenados tengan **destinos disjuntos**.
//
// Este bench es el consumidor mínimo que lo justifica: una rejilla de tiles de
// 16x16 que **no se solapan** (reordenar es seguro). Cada tile se emite con uno de
// dos **estados** distintos:
//   - estado A: fuente "apretada" (1 word/fila, `source_modulo = 0`);
//   - estado B: fuente con word de guarda (`source_modulo = 2`).
// Ambos estados pintan el mismo tamaño de tile, pero difieren en el módulo de
// fuente (`BLTCMOD`), así que el backend los ve como rachas distintas.
//
// Con `K_BENCH_STATES=2` el patrón es un **tablero de ajedrez** de estados: en orden
// de emisión, cada job cambia de estado respecto al anterior -> ~0 aciertos de caché
// de racha. Al agrupar (`K_BENCH_SORT=1`) los tiles de un mismo estado quedan
// adyacentes -> ~(N - nº de estados) aciertos. El contador `blitter_common_hits()`
// (accesos de racha que evitan ~8 escrituras a custom cada uno) es la medida.
//
// Verificación: `g_eng_run_status.detail = (common_hits << 16) | blitter_starts`.
// Leerlo con el canal lateral (el runner lo imprime). `blitter_starts` no cambia
// (mismo nº de lanzamientos); `common_hits` es lo que cuantifica `sort_by_state`.
//
// Referencias: `docs/engine/architecture/RASTER.md` §"Prioridades de rendimiento",
// `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` §4, `tests/host/graphics/387`.
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/graphics/frame_plan.hpp>
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

namespace gfx = eng::graphics::composition;

using eng::s16;
using eng::u16;
using eng::u32;
using eng::u8;

#ifndef K_BENCH_SORT
#define K_BENCH_SORT 0
#endif
#ifndef K_BENCH_STATES
#define K_BENCH_STATES 2
#endif
static_assert(K_BENCH_STATES == 1 || K_BENCH_STATES == 2, "K_BENCH_STATES debe ser 1 o 2");

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kBytesPerRow = kWidth / 8u; // 40
constexpr u8 kPlanes = 4u;
constexpr u32 kPlaneBytes = static_cast<u32>(kBytesPerRow) * kHeight; // 10240

// Rejilla de tiles disjuntos 16x16 (112 jobs < FramePlan::max_blit_jobs = 128).
constexpr u16 kTilePx = 16u;
constexpr u16 kTileWordsPerRow = 1u;
constexpr u16 kTilesX = 16u;
constexpr u16 kTilesY = 7u;
constexpr u16 kJobCount = kTilesX * kTilesY;

// Banco A: tile "apretado" (1 word por fila y plano) -> source_modulo = 0.
constexpr u32 kTilePlaneBytesA = static_cast<u32>(kTileWordsPerRow) * 2u * kTilePx; // 32
constexpr u32 kBankABytes = kTilePlaneBytesA * kPlanes;                              // 128
// Banco B: tile con word de guarda (2 words por fila) -> source_modulo = 2.
constexpr u32 kTileRowBytesB = static_cast<u32>(kTileWordsPerRow) * 2u * 2u;        // 4
constexpr u32 kTilePlaneBytesB = kTileRowBytesB * kTilePx;                          // 64
constexpr u32 kBankBBytes = kTilePlaneBytesB * kPlanes;                             // 256

constexpr u8 kColorA = 14u; // verde (0x0f0): el analizador genérico exige algún píxel verde
constexpr u8 kColorB = 15u; // blanco (0xfff): ...y algún píxel blanco

constexpr eng::Palette32 kPalette {{
	0x000, 0x002, 0x004, 0x006, 0x008, 0x00f, 0x080, 0x088,
	0x800, 0x808, 0x880, 0x888, 0xf00, 0xff0, 0x0f0, 0xfff,
}};

/// Rellena un tile sólido del color `color` en `bank`, con la geometría dada
/// (`plane_stride` bytes entre planos, `row_bytes` bytes por fila). Solo escribe la
/// primera word de cada fila, así sirve tanto para el banco apretado como para el
/// de guarda (que reserva una segunda word que el Blitter salta con el módulo).
void fill_tile(u8* bank, u32 plane_stride, u32 row_bytes, u8 color) {
	for (u8 plane = 0u; plane < kPlanes; ++plane) {
		if ((color & (1u << plane)) == 0u) {
			continue;
		}
		for (u16 y = 0u; y < kTilePx; ++y) {
			auto* w = reinterpret_cast<u16*>(bank + static_cast<u32>(plane) * plane_stride +
							 static_cast<u32>(y) * row_bytes);
			*w = 0xffffu;
		}
	}
}

/// Un `TileBlockCopy` (copia opaca) de un tile 16x16 a la celda (`cx`,`cy`) del
/// bitmap planar `dst`. `src_mod`/`src_plane_stride` describen la fuente y son lo
/// único que distingue los dos estados (el resto del estado común es idéntico).
eng::graphics::BlitJob make_tile_job(const u8* src, u16 src_mod, u32 src_plane_stride, u16 cx,
				     u16 cy, u8* dst) {
	eng::graphics::BlitJob j {};
	j.kind = eng::graphics::BlitJobKind::TileBlockCopy;
	j.source = eng::graphics::BlitPtr::from_storage(reinterpret_cast<const u16*>(src));
	const u32 off = static_cast<u32>(cy) * kTilePx * kBytesPerRow +
			(static_cast<u32>(cx) * kTilePx) / 8u;
	j.destination = eng::graphics::BlitPtr::from_storage(
		reinterpret_cast<const u16*>(dst + off));
	j.words_per_row = kTileWordsPerRow;
	j.height = kTilePx;
	j.source_modulo_bytes = static_cast<s16>(src_mod);
	j.destination_modulo_bytes = static_cast<s16>(kBytesPerRow - kTileWordsPerRow * 2u); // 38
	j.bitplane_count = kPlanes;
	j.source_shift = 0u;
	j.source_plane_stride_bytes = src_plane_stride;
	j.destination_plane_stride_bytes = kPlaneBytes;
	j.minterm = 0x00u; // la ruta de copia fija su propio BLTCON0; solo la simetría de grupos importa
	return j;
}

struct BlitStateBench {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021101u);
			return;
		}

		gfx::SceneResources res = gfx::planar(kWidth, kHeight, kPlanes);
		if (!gfx::compose(m_scene, backend.memory_manager(), res, gfx::ocs_a500,
				  gfx::display(res),
				  gfx::palette(eng::PaletteWords {kPalette.color, 16u}, 0u, 16u))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021102u);
			return;
		}

		m_bank_a = backend.memory_manager().chip().reserve<eng::TileBankTag>(kBankABytes, 16u);
		m_bank_b = backend.memory_manager().chip().reserve<eng::TileBankTag>(kBankBBytes, 16u);
		if (!m_bank_a.valid() || !m_bank_b.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021103u);
			return;
		}

		fill_tile(m_bank_a.view.data(), kTilePlaneBytesA, kTileWordsPerRow * 2u, kColorA);
		fill_tile(m_bank_b.view.data(), kTilePlaneBytesB, kTileRowBytesB, kColorB);

		if (!build_plan()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021104u);
			return;
		}
		// Primer dibujo; las siguientes ejecuciones del mismo plan (estático) miden el coste.
		if (!backend.execute_frame_plan(m_plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021105u);
			return;
		}
		m_scene.takeover(backend);
		m_ready = true;
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) {
			return;
		}
		// Re-ejecuta el mismo plan y publica la medida del último frame: agrupar cambia
		// `common_hits` (reprogramaciones evitadas), no `blitter_starts`.
		if (context.frame.frame_index >= 3u) {
			(void)backend.execute_frame_plan(m_plan);
			const u32 hits = backend.blitter_common_hits();
			const u32 starts = backend.blitter_starts();
			eng::debug::mark_ready(g_eng_run_status, (hits << 16u) | (starts & 0xffffu));
		}
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_plan() {
		m_plan.clear();
		u8* dst = m_scene.bitplanes().data();
		for (u16 cy = 0u; cy < kTilesY; ++cy) {
			for (u16 cx = 0u; cx < kTilesX; ++cx) {
				const bool bank_b =
					(K_BENCH_STATES >= 2) && (((cx + cy) & 1u) != 0u);
				const eng::graphics::BlitJob job =
					bank_b ? make_tile_job(m_bank_b.view.data(), 2u, kTilePlaneBytesB,
							       cx, cy, dst)
					       : make_tile_job(m_bank_a.view.data(), 0u,
							       kTilePlaneBytesA, cx, cy, dst);
				if (!m_plan.add_tile_block_copy(job)) {
					return false;
				}
			}
		}
#if K_BENCH_SORT
		// Legal: los tiles tienen destinos disjuntos, así que reordenar no cambia el
		// resultado y acerca los blits de mismo estado (rachas -> menos reprogramaciones).
		m_plan.sort_by_state();
#endif
		return m_plan.ok();
	}

	bool m_ready = false;
	gfx::Scene m_scene {};
	eng::graphics::FramePlan m_plan {};
	eng::Block<eng::TileBankTag> m_bank_a {};
	eng::Block<eng::TileBankTag> m_bank_b {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	BlitStateBench game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);

	return 0;
}
