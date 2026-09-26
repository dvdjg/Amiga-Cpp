// ============================================================================
// Demo 126 - fast_bobs
// ----------------------------------------------------------------------------
// Tutorial: como dibujar BOBs con la tecnica **Fast Bobs** (dual playfield) usando
// SOLO la fachada de escena del engine. No se nombra ni un registro del chipset, ni
// un `BlitJob`, ni un puntero crudo: eso lo resuelven las capas de abajo.
//
// Que ilustra (y con que tipo del engine):
//   1. Pantalla por **bandas** -> `eng::scene::RasterLayout`:
//        banda 0: dual playfield 3+3 (6 planos, `DBLPF`) en las lineas 0..207.
//        banda 1: franja de 0 planos en 208..255 (apaga el DMA de planos; sitio para
//                 efectos "copper chunky"). Se materializa como `ModeSwitchZone`.
//      El juego declara GEOMETRIA y el engine emite el display + las conmutaciones.
//   2. Un BOB es un asset (`eng::graphics::Sprite`) mas una **capa** que lo mueve:
//      `eng::scene::FastBobLayer`. La capa conoce la tecnica (copia con padding: el
//      propio padding limpia los restos del frame anterior) y degrada sola a
//      clear+cookie-cut cuando hace falta. El juego SOLO mueve actores (`BobActor`).
//   3. El dibujo por frame es un `eng::graphics::FramePlan`: la capa lo RELLENA
//      (`emit`) y el backend lo EJECUTA (`execute_frame_plan`). El juego nunca
//      programa el Blitter.
//
// La tecnica, con su justificacion y limites, esta en la ficha
// `docs/reference/amiga/techniques/dual-playfield-fastbobs.md` (dual playfield +
// BOB con padding; PF1 vacio de origen, el fondo real vive en PF2).
//
// Por que es un buen tutorial: si intentaras hacer lo mismo "a mano" tendrias que
// calcular modulos, punteros de plano, el layout interleaved del DPF y el padding;
// aqui todo eso es una consecuencia de declarar la banda y mover un actor.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/126_fast_bobs --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/126_fast_bobs --warp
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/scene/bobs.hpp>
#include <eng/scene/display.hpp>

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

namespace copper = eng::copper;

using eng::s16;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kBytesPerRow = kWidth / 8u;   // 40
constexpr u8 kPlanes = 6u;                  // DPF 3+3
constexpr u32 kBitmapBytes = static_cast<u32>(kBytesPerRow) * kPlanes * kHeight; // 61440
constexpr u16 kBandBottom = 208u;           // franja inferior de 0 planos

// Hoja del BOB: 32x32 (16x16 visible + 8 px de padding por lado), planar de 3 planos.
constexpr u16 kBobPadded = 32u;
constexpr u16 kBobVisible = 16u;
constexpr u8 kBobPad = 8u;
constexpr u8 kBobPlanes = 3u;
constexpr u32 kBobRow = ((kBobPadded / 16u) + 1u) * 2u;       // 6 (2 palabras + guarda)
constexpr u32 kBobPlaneBytes = kBobPadded * kBobRow;          // 192
constexpr u32 kBobSheetBytes = kBobPlaneBytes * kBobPlanes;   // 576

// Paleta DPF: COLOR00..07 = PF1, COLOR08..15 = PF2.
constexpr eng::Palette32 kPalette {{
	0x000, 0xf00, 0x0f0, 0xff0, 0x00f, 0x0ff, 0xf0f, 0xfff,
	0x000, 0x024, 0x042, 0x246, 0x061, 0x083, 0x0a4, 0x0c6,
}};

constexpr u16 kCopperWords = 2048u;

[[nodiscard]] constexpr s16 tri(u32 t, u32 period) noexcept {
	const u32 p = t % period;
	return static_cast<s16>(p < period / 2u ? p : period - 1u - p);
}

struct FastBobsDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012601u);
			return;
		}
		m_planes = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitmapBytes, 16u);
		m_sheet_block = backend.memory().chip.allocate_block<eng::BobTag>(kBobSheetBytes, 16u);
		m_copper = backend.memory().chip.allocate_block<eng::CopperTag>(kCopperWords, 16u);
		if (!m_planes.valid() || !m_sheet_block.valid() || !m_copper.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012602u);
			return;
		}
		build_background();
		build_sheet();

		// --- El BOB como ASSET de juego (no como geometria de Blitter) ----------------
		// `graphics::Sprite` reune geometria + hoja + politica. `BobDraw::Opaque` es la
		// "copia" del Fast Bob: escribe el bitmap TAL CUAL (incluido su padding de color 0),
		// de modo que el padding borra por si solo lo que quede del frame anterior. No hay
		// mascara: la transparencia la da el propio padding sobre un PF1 vacio.
		eng::graphics::Bob bob {};
		bob.sheet = m_sheet_block.view.data();
		bob.width = kBobPadded;
		bob.height = kBobPadded;
		bob.planes = kBobPlanes;
		bob.layout = eng::graphics::BobLayout::Planar;
		bob.draw = eng::graphics::BobDraw::Opaque; // copia: dibuja y limpia con el padding
		m_sprite = eng::graphics::Sprite {bob};

		// --- La CAPA que conoce la tecnica --------------------------------------------
		// Le damos la hoja **con padding** (8 px por lado); la capa decide por frame si
		// copia (rapido) o degrada a clear + cookie-cut. El juego nunca ve un `BlitJob`.
		m_bobs.set_sheet(m_sprite, kBobPad, kBobPad);
		m_bobs.set_slow_sheet(m_sprite);
		m_bobs.resize(2u);

		if (!build_display()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012603u);
			return;
		}
		backend.takeover_display(m_display_words);
		eng::debug::mark_ready(g_eng_run_status, kHeight);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_sprite.valid()) {
			return;
		}
		const u32 f = context.frame.frame_index;
		// Dos BOB en regiones separadas (no se solapan): siempre por el camino rapido.
		m_bobs[0] = {static_cast<s16>(16 + tri(f, 192u)), static_cast<s16>(32 + tri(f * 2u, 128u)),
			     0u, true};
		m_bobs[1] = {static_cast<s16>(176 + tri(f * 3u, 128u)),
			     static_cast<s16>(128 + tri(f, 96u)), 0u, true};

		m_plan.clear();
		// La capa RELLENA el `FramePlan` (¿que blits?) y el backend lo EJECUTA. El juego no
		// describe minterns, modulos ni strides: solo dijo donde esta cada actor.
		(void)m_bobs.emit(m_plan, m_band.bob_target());
		(void)backend.execute_frame_plan(m_plan);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void build_background() {
		u8* data = m_planes.view.data();
		for (u32 i = 0u; i < kBitmapBytes; ++i) {
			data[i] = 0u;
		}
		// PF2 = planos impares; bit0 de PF2 (plano de hardware 1) lleva una trama a rayas, de
		// modo que los restos de un BOB mal limpiado serian evidentes.
		for (u16 y = 0u; y < kHeight; ++y) {
			u8* row = data + static_cast<u32>(y) * (kBytesPerRow * kPlanes) + kBytesPerRow;
			for (u16 b = 0u; b < kBytesPerRow; ++b) {
				row[b] = ((b + (y >> 2u)) & 1u) ? 0x55u : 0xaau;
			}
		}
	}

	void build_sheet() {
		u8* sheet = m_sheet_block.view.data();
		for (u32 i = 0u; i < kBobSheetBytes; ++i) {
			sheet[i] = 0u;
		}
		const auto put = [&](u8 plane, u16 x, u16 y) {
			u8* p = sheet + static_cast<u32>(plane) * kBobPlaneBytes +
				static_cast<u32>(y) * kBobRow + (x >> 3u);
			*p = static_cast<u8>(*p | (0x80u >> (x & 7u)));
		};
		// Cuadrado visible [pad, pad+visible) con padding 0 alrededor (plano 0 -> color 1).
		for (u16 y = kBobPad; y < kBobPad + kBobVisible; ++y) {
			for (u16 x = kBobPad; x < kBobPad + kBobVisible; ++x) {
				put(0u, x, y);
			}
		}
		// Nucleo interior en el plano 1 (-> color 3).
		for (u16 y = static_cast<u16>(kBobPad + 4u); y < kBobPad + kBobVisible - 4u; ++y) {
			for (u16 x = kBobPad + 4u; x < kBobPad + kBobVisible - 4u; ++x) {
				put(1u, x, y);
			}
		}
	}

	bool build_display() {
		// --- Composicion por BANDAS (`RasterLayout`) ----------------------------------
		// Declaramos GEOMETRIA, no registros: la banda 0 es un dual playfield 3+3 y la banda 1
		// una franja sin planos. `band_of_planes` ata el display y la base de BOBs a la MISMA
		// memoria (una sola verdad). El engine emite el display y la `ModeSwitchZone`.
		eng::scene::RasterLayout layout {};
		eng::scene::Band band = eng::scene::band_of_planes(m_planes, kPlanes, kBytesPerRow, 0u);
		band.dual_playfield = true;
		band.palette = kPalette.words();
		band.palette_colors = 16u;
		m_band = band;
		layout.add(band);
		// Franja inferior de 0 planos (efectos copper): apaga el DMA de planos.
		layout.add({.top = kBandBottom, .planes = 0u, .color = false});

		copper::SchedulerT<false> sched {
			eng::Block<eng::CopperTag> {m_copper.view, m_copper.kind}};
		if (!layout.materialize(sched)) {
			return false;
		}
		sched.end();
		m_display_words = sched.data();
		return sched.ok();
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_planes {};
	eng::Block<eng::BobTag> m_sheet_block {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::scene::Band m_band {};
	eng::graphics::Sprite m_sprite {};
	eng::scene::FastBobLayer m_bobs {};
	eng::graphics::FramePlan m_plan {};
	u16* m_display_words = nullptr;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	FastBobsDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffffu);

	return 0;
}
