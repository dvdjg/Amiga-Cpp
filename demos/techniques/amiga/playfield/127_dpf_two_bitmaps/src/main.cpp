// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/127_dpf_two_bitmaps --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/127_dpf_two_bitmaps
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/127_dpf_two_bitmaps --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/127_dpf_two_bitmaps

// ============================================================================
// Demo 127 - dpf_two_bitmaps
// ----------------------------------------------------------------------------
// Tutorial (§1.12 de AGENTS.md): **dual playfield de dos bitmaps** (PF1 + PF2) compuesto por
// bandas (`eng::scene::RasterLayout` + `band_from_dual_view`) con scroll **XLimited**: cada campo
// se desplaza en **X** (coarse por `planeaddx`), en direcciones opuestas (parallax). XLimited es
// "solo X, sin split" (`XYLIMITED_ALGORITMO_GENERICO.md` §0): el bitmap es un poco mas ancho y el
// fetch lineal hace el resto; NO hay split de Copper. El BOB (copia con padding) va en PF1.
//
// Que ilustra:
//   1. `PlayfieldHardwareView` = la **superficie** de cada campo. El "driver" de scroll solo mueve
//      `planeaddx`; la composicion reconstruye la banda por frame y publica la copperlist.
//   2. `Band::bob_target()` da el destino de BOBs de PF1 (con el offset de scroll, asi que el BOB
//      queda fijo en pantalla aunque su campo scrollee) y `eng::scene::FastBobLayer` lo pinta.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/127_dpf_two_bitmaps --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/127_dpf_two_bitmaps --warp
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
constexpr u16 kScreenBytesPerRow = kWidth / 8u; // 40 (lo que fetcha el display)
constexpr u8 kFieldPlanes = 3u;                 // cada campo: 3 planos (DPF 3+3)
// XLimited: el bitmap es mas ancho que la pantalla (margen de scroll X). El puntero se mueve por
// `planeaddx`; no hace falta split (el fetch lineal lee dentro de la fila).
constexpr u16 kPitch = 80u;                                   // bitmap_bytes_per_row (320 + 320 px)
constexpr u32 kFieldBytes = static_cast<u32>(kPitch) * kFieldPlanes * kHeight;
constexpr u16 kScrollBytes = kPitch - kScreenBytesPerRow;     // 40 B = 320 px de recorrido X
constexpr u16 kBandBottom = 208u;

constexpr u16 kBobPadded = 32u;
constexpr u16 kBobVisible = 16u;
constexpr u8 kBobPad = 8u;
constexpr u8 kBobPlanes = 3u;
constexpr u32 kBobWordsPerPlaneRow = ((kBobPadded / 16u) + 1u); // 3 (2 datos + guarda)
constexpr u32 kBobRowBytes = kBobWordsPerPlaneRow * 2u * kBobPlanes; // 18 (interleaved)
constexpr u32 kBobSheetBytes = kBobRowBytes * kBobPadded;            // 576

constexpr eng::Palette32 kPalette {{
	0x000, 0xf00, 0x0f0, 0xff0, 0x00f, 0x0ff, 0xf0f, 0xfff,
	0x000, 0x024, 0x042, 0x246, 0x061, 0x083, 0x0a4, 0x0c6,
}};

constexpr u16 kCopperWords = 2048u;
constexpr u8 kLists = 2u; // doble buffer de copperlist

[[nodiscard]] constexpr s16 tri(u32 t, u32 period) noexcept {
	const u32 p = t % period;
	return static_cast<s16>(p < period / 2u ? p : period - 1u - p);
}

struct DpfTwoBitmapsDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({128u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012701u);
			return;
		}
		m_pf1 = backend.memory().chip.allocate_block<eng::PlaneTag>(kFieldBytes, 16u);
		m_pf2 = backend.memory().chip.allocate_block<eng::PlaneTag>(kFieldBytes, 16u);
		m_sheet = backend.memory().chip.allocate_block<eng::BobTag>(kBobSheetBytes, 16u);
		m_copper = backend.memory().chip.allocate_block<eng::CopperTag>(kLists * kCopperWords, 16u);
		if (!m_pf1.valid() || !m_pf2.valid() || !m_sheet.valid() || !m_copper.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012702u);
			return;
		}
		build_field(m_pf1.view.data(), 0u);   // PF1 = vacio de origen (solo BOBs)
		build_field(m_pf2.view.data(), 0x55u); // PF2 = rayas
		build_sheet();

		// Superficies de los dos campos: la composicion las intercala en el DPF.
		configure_view(m_v1, m_pf1.view.data());
		configure_view(m_v2, m_pf2.view.data());

		// BOB con padding (copia = dibuja y limpia en un blit) en PF1.
		eng::graphics::Bob bob {};
		bob.sheet = m_sheet.view.data();
		bob.width = kBobPadded;
		bob.height = kBobPadded;
		bob.planes = kBobPlanes;
		bob.layout = eng::graphics::BobLayout::Interleaved;
		bob.draw = eng::graphics::BobDraw::Opaque;
		m_sprite = eng::graphics::Sprite {bob};
		m_bobs.set_sheet(m_sprite, kBobPad, kBobPad);
		m_bobs.set_slow_sheet(m_sprite);
		m_bobs.resize(2u);

		if (!build_display(0u) || !build_display(1u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012703u);
			return;
		}
		backend.takeover_display(m_copper_ptrs[0]);
		m_active = 1u;
		eng::debug::mark_ready(g_eng_run_status, kHeight);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_sprite.valid()) {
			return;
		}
		const u32 f = context.frame.frame_index;

		// "Driver XLimited": scroll X de 1 px por frame con **fine + coarse** (la forma canonica:
		// `planeaddx` coarse cada 16 px + `BPLCON1` fino), en sentidos opuestos (parallax). Como el
		// BOB se dibuja a 1 px/frame, el padding (8 px) limpia sin estela.
		const u32 px_range = static_cast<u32>(kScrollBytes) * 8u; // 320 px de recorrido
		const u32 x1 = static_cast<u32>(tri(f, 2u * px_range));   // 0..px_range-1
		const u32 x2 = px_range - 1u - x1;                        // sentido opuesto
		m_v1.planeaddx = (x1 / 16u) * 2u; // coarse en bytes
		m_v1.bplcon1 = static_cast<u16>(15u - (x1 & 15u));
		m_v2.planeaddx = (x2 / 16u) * 2u;
		m_v2.bplcon1 = static_cast<u16>(15u - (x2 & 15u));
		if (!build_display(m_active)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012704u);
			return;
		}
		backend.install_copper_list(m_copper_ptrs[m_active]);
		m_active = static_cast<u8>((m_active + 1u) % kLists);

		// BOB sobre PF1: `bob_target()` incluye el offset de scroll, asi que queda fijo en pantalla;
		// la capa decide y el backend ejecuta el plan.
		m_bobs[0] = {static_cast<s16>(16 + tri(f, 192u)), static_cast<s16>(32 + tri(f * 2u, 128u)),
			     0u, true};
		m_bobs[1] = {static_cast<s16>(176 + tri(f * 3u, 128u)),
			     static_cast<s16>(128 + tri(f, 96u)), 0u, true};
		m_plan.clear();
		(void)m_bobs.emit(m_plan, m_band.bob_target(), m_band.bob_fine_scroll());
		(void)backend.execute_frame_plan(m_plan);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static void configure_view(eng::field::PlayfieldHardwareView& v, u8* base) {
		v.planes = kFieldPlanes;
		v.bitmap_bytes_per_row = kPitch;
		v.bitmap_height = kHeight;
		v.viewport_w = kWidth;
		v.viewport_h = kHeight;
		v.display_height = kHeight;
		v.plane_bytes = kFieldBytes;
		v.real_base = eng::Address<eng::MemoryKind::Chip>::from_storage(base);
		// XLimited: módulo = pitch*planos − bytes que fetcha el display (así tras leer una fila de
		// un plano, el DMA salta a la fila siguiente del MISMO plano).
		v.bpl1mod = static_cast<u16>(static_cast<u32>(kPitch) * kFieldPlanes - kScreenBytesPerRow);
		v.bpl2mod = v.bpl1mod;
	}

	static void build_field(u8* data, u8 pattern) {
		for (u32 i = 0u; i < kFieldBytes; ++i) {
			data[i] = 0u;
		}
		if (pattern == 0u) {
			return;
		}
		for (u16 y = 0u; y < kHeight; ++y) {
			u8* row = data + static_cast<u32>(y) * (kPitch * kFieldPlanes);
			for (u16 b = 0u; b < kPitch; ++b) {
				row[b] = ((b + (y >> 2u)) & 1u) ? pattern : static_cast<u8>(pattern ^ 0xffu);
			}
		}
	}

	void build_sheet() {
		u8* sheet = m_sheet.view.data();
		for (u32 i = 0u; i < kBobSheetBytes; ++i) {
			sheet[i] = 0u;
		}
		const auto put = [&](u8 plane, u16 x, u16 y) {
			u8* p = sheet + static_cast<u32>(y) * kBobRowBytes +
				static_cast<u32>(plane) * (kBobWordsPerPlaneRow * 2u) + (x >> 3u);
			*p = static_cast<u8>(*p | (0x80u >> (x & 7u)));
		};
		for (u16 y = kBobPad; y < kBobPad + kBobVisible; ++y) {
			for (u16 x = kBobPad; x < kBobPad + kBobVisible; ++x) {
				put(0u, x, y);
			}
		}
		for (u16 y = static_cast<u16>(kBobPad + 4u); y < kBobPad + kBobVisible - 4u; ++y) {
			for (u16 x = kBobPad + 4u; x < kBobPad + kBobVisible - 4u; ++x) {
				put(1u, x, y);
			}
		}
	}

	/// Reconstruye la banda desde las superficies (el driver solo movio sus offsets X) y la
	/// materializa en la copperlist `list`.
	bool build_display(u8 list) {
		m_band = eng::scene::band_from_dual_view(m_v1, m_v2, 0u);
		m_band.palette = kPalette.words();
		m_band.palette_colors = 16u;

		eng::scene::RasterLayout layout {};
		layout.add(m_band);
		layout.add({.top = kBandBottom, .planes = 0u, .color = false});

		const eng::Bytes<eng::CopperTag> slice =
			m_copper.view.subspan(static_cast<u32>(list) * kCopperWords, kCopperWords);
		copper::SchedulerT<false> sched {eng::Block<eng::CopperTag> {slice, m_copper.kind}};
		if (!layout.materialize(sched)) {
			return false;
		}
		sched.end();
		m_copper_ptrs[list] = sched.data();
		return sched.ok();
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_pf1 {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_pf2 {};
	eng::Block<eng::BobTag> m_sheet {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::field::PlayfieldHardwareView m_v1 {};
	eng::field::PlayfieldHardwareView m_v2 {};
	eng::scene::Band m_band {};
	eng::graphics::Sprite m_sprite {};
	eng::scene::FastBobLayer m_bobs {};
	eng::graphics::FramePlan m_plan {};
	const u16* m_copper_ptrs[kLists] = {nullptr, nullptr};
	u8 m_active = 0u;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DpfTwoBitmapsDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffffu);

	return 0;
}
