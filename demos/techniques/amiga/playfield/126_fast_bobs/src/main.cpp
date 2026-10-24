// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/126_fast_bobs --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/126_fast_bobs --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/126_fast_bobs --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/126_fast_bobs --keep-running

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
#include <eng/debug/host_hud.hpp>
#include <eng/graphics/fast_bob_margins.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/setup/coherence.hpp>
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

// Hoja del BOB: contenido 16×16 + **márgenes F5** (`fast_bob_margins`): el asset declara el
// desplazamiento máximo por eje y el helper devuelve padding y tamaño final; el ancla del
// contenido queda en `(kBobPadX, kBobPadY)`. Pasos REALES de los actores: (3, 2) px/frame
// (bob0 `tri(f,192)`/`tri(f*2,128)`; bob1 `tri(f*3,128)`/`tri(f,96)`) → hoja 22×20.
constexpr u16 kBobVisible = 16u;
constexpr u16 kBobStepX = 3u;
constexpr u16 kBobStepY = 2u;
constexpr eng::graphics::FastBobMargins kBobMargins = eng::graphics::fast_bob_margins(
	kBobVisible, kBobVisible, kBobStepX, kBobStepX, kBobStepY, kBobStepY,
	/*double_buffer=*/false);
constexpr u16 kBobPaddedW = kBobMargins.padded_w; // 22
constexpr u16 kBobPaddedH = kBobMargins.padded_h; // 18
constexpr u8 kBobPadX = static_cast<u8>(kBobMargins.left); // 3
constexpr u8 kBobPadY = static_cast<u8>(kBobMargins.top);  // 1
constexpr u8 kBobPlanes = 3u;
// Fila de la hoja en BYTES: palabras de contenido + 1 de guarda (contrato del engine).
constexpr u32 kBobRow = (((static_cast<u32>(kBobPaddedW) + 15u) / 16u) + 1u) * 2u;
constexpr u32 kBobPlaneBytes = static_cast<u32>(kBobPaddedH) * kBobRow;
constexpr u32 kBobSheetBytes = kBobPlaneBytes * kBobPlanes;

// Hoja del camino LENTO (degradación de la capa): mismo gráfico **sin padding** con máscara
// (`CookieCut`) — el layer dibuja el lento en `(x, y)` (ancla = contenido) y limpia la caja
// pintada previa con `clear_box`. Misma geometría de fila (guarda por fila).
constexpr u32 kSlowRow = (((static_cast<u32>(kBobVisible) + 15u) / 16u) + 1u) * 2u;
constexpr u32 kSlowPlaneBytes = static_cast<u32>(kBobVisible) * kSlowRow;
constexpr u32 kSlowSheetBytes = kSlowPlaneBytes * kBobPlanes;
constexpr u32 kSlowMaskBytes = kSlowPlaneBytes;
constexpr u32 kSlowTotalBytes = kSlowSheetBytes + kSlowMaskBytes;

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
		// **Bajo un SO vivo**: la espera de VBlank debe ser ACTIVA. Con `Wait()` (defecto), el
		// planificador de exec reescribe INTENA en cada cambio de tarea y **borra el VERTB**
		// que arma el engine → la tarea queda dormida para siempre (enfermedad del 212;
		// detectada aquí como «DT-011»: la hoja de márgenes mínimos solo cambiaba el timing y
		// exponía la carrera). Ver `212-vblank-irq-apagada-por-el-so.md`.
		backend.set_vblank_sleep(false);
		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012601u);
			return;
		}
		m_planes = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitmapBytes, 16u);
		m_sheet_block = backend.memory_manager().chip().reserve<eng::BobTag>(
			kBobSheetBytes + kSlowTotalBytes, 16u);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(kCopperWords, 16u);
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
		bob.sheet = m_sheet_block.mem_view_chip();
		bob.width = kBobPaddedW;
		bob.height = kBobPaddedH;
		bob.planes = kBobPlanes;
		bob.layout = eng::graphics::BobLayout::Planar;
		bob.draw = eng::graphics::BobDraw::Opaque; // copia: dibuja y limpia con el padding
		m_sprite = eng::graphics::Sprite {bob};

		// --- La CAPA que conoce la tecnica --------------------------------------------
		// Le damos la hoja **con padding** (márgenes F5); la capa decide por frame si copia
		// (rápido) o degrada a clear + cookie-cut. El juego nunca ve un `BlitJob`.
		m_bobs.set_sheet(m_sprite, kBobPadX, kBobPadY);
		// **Hoja del camino lento** (cookie-cut sin padding + su máscara): la degradación
		// dibuja el contenido anclado en `(x, y)` y limpia la caja pintada previa.
		eng::graphics::Bob slow_bob {};
		slow_bob.sheet = eng::ChipView<eng::BobTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(
				reinterpret_cast<const eng::u8*>(m_sheet_block.view.data()) +
				kBobSheetBytes),
			kSlowSheetBytes};
		slow_bob.width = kBobVisible;
		slow_bob.height = kBobVisible;
		slow_bob.planes = kBobPlanes;
		slow_bob.layout = eng::graphics::BobLayout::Planar;
		slow_bob.draw = eng::graphics::BobDraw::CookieCut;
		slow_bob.mask_pack = eng::graphics::BobMaskPack::SeparatePlane;
		slow_bob.mask = eng::ChipView<eng::BobTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(
				reinterpret_cast<const eng::u8*>(m_sheet_block.view.data()) +
				kBobSheetBytes + kSlowSheetBytes),
			kSlowMaskBytes};
		m_slow = eng::graphics::Sprite {slow_bob};
		m_bobs.set_slow_sheet(m_slow);
		m_bobs.resize(2u);

		if (!build_display()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012603u);
			return;
		}
		// **Coherencia de setup** (capa 2, `COHERENCIA_DE_SETUP.md`): el formato de cada hoja
		// debe casar con el del target de la banda — un desajuste se lee mal **en silencio**
		// (`bob_draw` deriva la lectura de la hoja del layout del destino).
		{
			const auto fast_ok = eng::setup::check_bob_plan(bob, m_band.bob_target());
			const auto slow_ok = eng::setup::check_bob_plan(slow_bob, m_band.bob_target());
			if (!fast_ok.ok() || !slow_ok.ok()) {
				eng::debug::mark_failed(g_eng_run_status,
							0x0007c200u |
								(fast_ok.ok() ? slow_ok.code
									      : fast_ok.code));
				return;
			}
		}
		backend.takeover_display(m_display_view);
		eng::debug::mark_ready(g_eng_run_status, kHeight);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_sprite.valid()) {
			return;
		}
		const u32 f = context.frame.frame_index;
		// Dos BOB en regiones separadas (no se solapan): camino rápido, salvo en la
		// **ventana de disparo** [112,128) de cada 128 frames, donde bob0 acelera a 5 px/frame
		// (pad = 3) y la capa **degrada a cookie-cut + clear** — es el camino lento real de la
		// técnica, que quedó cubierto al arreglar DT-012 (el walk de máscara se salía de su
		// plano con hoja planar + destino interleaved; HOST-439 T4b lo fija).
		const u32 win = f % 128u;
		const s16 boost =
			(win >= 112u) ? static_cast<s16>((win - 112u) * 2) : static_cast<s16>(0);
		m_bobs[0] = {static_cast<s16>(16 + tri(f, 192u) + boost),
			     static_cast<s16>(32 + tri(f * 2u, 128u)), 0u, true};
		m_bobs[1] = {static_cast<s16>(176 + tri(f * 3u, 128u)),
			     static_cast<s16>(128 + tri(f, 96u)), 0u, true};

		m_plan.clear();
		// La capa RELLENA el `FramePlan` (¿que blits?) y el backend lo EJECUTA. El juego no
		// describe minterns, modulos ni strides: solo dijo donde esta cada actor.
		(void)m_bobs.emit(m_plan, m_band.bob_target());
		(void)backend.execute_frame_plan(m_plan);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
		m_host_hud.draw(backend.debug(), "126 FAST BOBS", eng::debug::Version {1u, 0u},
				context.frame.frame_index, eng::debug::prof_clock());
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
		for (u16 y = kBobPadY; y < kBobPadY + kBobVisible; ++y) {
			for (u16 x = kBobPadX; x < kBobPadX + kBobVisible; ++x) {
				put(0u, x, y);
			}
		}
		// Nucleo interior en el plano 1 (-> color 3).
		for (u16 y = static_cast<u16>(kBobPadY + 4u); y < static_cast<u16>(kBobPadY + kBobVisible - 4u); ++y) {
			for (u16 x = static_cast<u16>(kBobPadX + 4u); x < static_cast<u16>(kBobPadX + kBobVisible - 4u); ++x) {
				put(1u, x, y);
			}
		}
		// **Camino lento**: contenido 16×16 SIN padding (ancla = contenido) + su máscara
		// (silueta del cuadrado) para el cookie-cut.
		u8* const slow = sheet + kBobSheetBytes;
		for (u32 i = 0u; i < kSlowTotalBytes; ++i) {
			slow[i] = 0u;
		}
		const auto puts = [&](u8 plane, u16 x, u16 y) {
			u8* p = slow + static_cast<u32>(plane) * kSlowPlaneBytes +
				static_cast<u32>(y) * kSlowRow + (x >> 3u);
			*p = static_cast<u8>(*p | (0x80u >> (x & 7u)));
		};
		for (u16 y = 0u; y < kBobVisible; ++y) {
			for (u16 x = 0u; x < kBobVisible; ++x) {
				puts(0u, x, y);
			}
		}
		for (u16 y = 4u; y < static_cast<u16>(kBobVisible - 4u); ++y) {
			for (u16 x = 4u; x < static_cast<u16>(kBobVisible - 4u); ++x) {
				puts(1u, x, y);
			}
		}
		u8* const maskw = slow + kSlowSheetBytes;
		for (u16 y = 0u; y < kBobVisible; ++y) {
			for (u16 x = 0u; x < kBobVisible; ++x) {
				maskw[static_cast<u32>(y) * kSlowRow + (x >> 3u)] |=
					static_cast<u8>(0x80u >> (x & 7u));
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
		m_display_view = sched.chip_view();
		// Validación de la lista declarativa (`RasterLayout`) antes de publicarla: END,
		// registros escribibles (OCS) y orden de WAITs (capa 2 de COHERENCIA_DE_SETUP.md).
		return sched.ok() && eng::setup::check_copper_list(sched).ok();
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_planes {};
	eng::Block<eng::BobTag> m_sheet_block {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::scene::Band m_band {};
	eng::graphics::Sprite m_sprite {};
	eng::graphics::Sprite m_slow {};
	eng::scene::FastBobLayer m_bobs {};
	eng::debug::HostHud m_host_hud {};
	eng::graphics::FramePlan m_plan {};
	eng::ChipView<eng::CopperTag> m_display_view {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	backend.set_vblank_sleep(false);
	FastBobsDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffffu);

	return 0;
}
