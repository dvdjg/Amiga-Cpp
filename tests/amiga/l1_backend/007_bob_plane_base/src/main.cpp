// L1-007: sonda minima de base de planos para un BOB.
//
// Un solo BOB opaco de 16x16 5bpp, un unico blit, sin restore, sin grupos
// beam-aware y sin Copper dinamico. La copperlist, el BobTarget y el relleno
// del fondo usan la MISMA subvista del bloque Chip. Canarios antes y despues
// del bitmap y de la hoja; el detalle de READY indica que se comprobo.

#include <eng/api/api.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/scene/display.hpp>
#include <eng/setup/coherence.hpp>

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
constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u16 kRowBytes = 40u;
constexpr eng::u8 kPlanes = 5u;
constexpr eng::u32 kBitmapBytes = static_cast<eng::u32>(kRowBytes) * kPlanes * kHeight;
constexpr eng::u32 kCanary = 16u;
constexpr eng::u32 kSheetBytes = 16u * 5u * 4u; // 16 filas fisicas * [imagen+guarda]
constexpr eng::u32 kSheetCanary = 16u;
constexpr eng::u32 kChipBytes = 96u * 1024u;
constexpr eng::u32 kCopperWords = 1024u;

constexpr eng::Palette32 kPalette {{
	0x011u, 0xf00u, 0x0f0u, 0x00fu, 0xff0u, 0x0ffu, 0xf0fu, 0xfffu,
	0x111u, 0x222u, 0x333u, 0x444u, 0x555u, 0x666u, 0x777u, 0x888u,
	0x999u, 0xaaau, 0xbbbu, 0xcccu, 0xdddu, 0xeeeu, 0xfffu, 0x123u,
	0x234u, 0x345u, 0x456u, 0x567u, 0x678u, 0x789u, 0x89au, 0x9abu,
}};

enum class Fail : eng::u32 {
	Memory = 0x00071001u,
	Display = 0x00071002u,
	Plan = 0x00071003u,
};

struct Probe {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		backend.set_vblank_sleep(false);
		if (!backend.configure_memory({kChipBytes, 8u * 1024u, 0u})) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Memory);
			return;
		}
		m_planes = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitmapBytes + 2u * kCanary, 16u);
		m_sheet = backend.memory_manager().chip().reserve<eng::BobTag>(kSheetBytes + 2u * kSheetCanary, 16u);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(kCopperWords, 16u);
		if (!m_planes.valid() || !m_sheet.valid() || !m_copper.valid()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Memory);
			return;
		}
		set_canaries();
		const auto bitmap = m_planes.mem_view().subview(kCanary, kBitmapBytes);
		const auto sheet = m_sheet.mem_view_chip().subview(kSheetCanary, kSheetBytes);
		paint_background(bitmap);
		build_sheet(sheet);
		eng::graphics::Bob bob {};
		bob.sheet = sheet;
		bob.width = 16u;
		bob.height = 16u;
		bob.planes = kPlanes;
		bob.layout = eng::graphics::BobLayout::Interleaved;
		bob.draw = eng::graphics::BobDraw::Opaque;
		const auto target = eng::graphics::make_bob_target(
			bitmap, kRowBytes, kHeight, kPlanes, eng::graphics::BobLayout::Interleaved);
		eng::graphics::FramePlan plan {};
		plan.set_target_bounds(target.bound());
		const bool draw_ok = eng::graphics::bob_draw(plan, bob, 0u, 100, 100, target);
		const bool draw_bottom = draw_ok && eng::graphics::bob_draw(plan, bob, 0u, 100, 240, target);
		const bool exec_ok = draw_bottom && backend.execute_frame_plan(plan);
		const bool pixel_ok = check_pixel(bitmap, 100u, 100u) && check_pixel(bitmap, 100u, 240u);
		const bool canary_ok = check_canaries();
		if (!build_display(backend, bitmap)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Display);
			return;
		}
		backend.takeover_display(m_display);
		eng::u32 detail = 0u;
		if (exec_ok) detail |= 1u << 0u;
		if (pixel_ok) detail |= 1u << 1u;
		if (canary_ok) detail |= 1u << 2u;
		if (plan.ok()) detail |= 1u << 3u;
		if (draw_bottom && check_pixel(bitmap, 100u, 240u)) detail |= 1u << 4u;
		eng::debug::mark_ready(g_eng_run_status, detail);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}
	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
		m_hud.draw(backend.debug(), "L1-007 BOB BASE", eng::debug::Version {1u, 0u},
			   context.frame.frame_index, eng::debug::prof_clock());
	}

private:
	void set_canaries() {
		eng::u8* const p = m_planes.view.data();
		for (eng::u32 i = 0u; i < kCanary; ++i) {
			p[i] = 0xa5u;
			p[kCanary + kBitmapBytes + i] = 0x5au;
		}
		eng::u8* const s = m_sheet.view.data();
		for (eng::u32 i = 0u; i < kSheetCanary; ++i) {
			s[i] = 0xa5u;
			s[kSheetCanary + kSheetBytes + i] = 0x5au;
		}
	}

	[[nodiscard]] bool check_canaries() const {
		const eng::u8* const p = m_planes.view.data();
		for (eng::u32 i = 0u; i < kCanary; ++i) {
			if (p[i] != 0xa5u || p[kCanary + kBitmapBytes + i] != 0x5au) return false;
		}
		const eng::u8* const s = m_sheet.view.data();
		for (eng::u32 i = 0u; i < kSheetCanary; ++i) {
			if (s[i] != 0xa5u || s[kSheetCanary + kSheetBytes + i] != 0x5au) return false;
		}
		return true;
	}

	static void paint_background(eng::ChipView<eng::PlaneTag> bitmap) {
		eng::u8* const p = const_cast<eng::u8*>(bitmap.data());
		for (eng::u32 i = 0u; i < kBitmapBytes; ++i) p[i] = 0u;
		for (eng::u16 y = 0u; y < kHeight; ++y) {
			eng::u8* row = p + static_cast<eng::u32>(y) * kRowBytes * kPlanes;
			for (eng::u16 x = 0u; x < kRowBytes; ++x) row[x] = 0xffu; // plano 0 = color 1
		}
	}

	static void build_sheet(eng::ChipView<eng::BobTag> sheet) {
		eng::u16* const w = const_cast<eng::u16*>(reinterpret_cast<const eng::u16*>(sheet.data()));
		for (eng::u16 y = 0u; y < 16u; ++y) {
			for (eng::u8 p = 0u; p < kPlanes; ++p) {
				const eng::u32 at = (static_cast<eng::u32>(y) * kPlanes + p) * 2u;
				w[at] = 0xffffu;
				w[at + 1u] = 0u;
			}
		}
	}

	[[nodiscard]] static bool check_pixel(eng::ChipView<eng::PlaneTag> bitmap, eng::u16 x,
					      eng::u16 y) {
		const eng::u8* const p = const_cast<eng::u8*>(bitmap.data());
		const eng::u8 bit = static_cast<eng::u8>(0x80u >> (x & 7u));
		for (eng::u8 plane = 0u; plane < kPlanes; ++plane) {
			const eng::u8 v = p[static_cast<eng::u32>(y) * kRowBytes * kPlanes +
					   static_cast<eng::u32>(plane) * kRowBytes + (x >> 3u)];
			if ((v & bit) == 0u) return false;
		}
		return true;
	}

	bool build_display(eng::amiga::AmigaBackend&, eng::ChipView<eng::PlaneTag> bitmap) {
		eng::scene::Band band {};
		band.planes = kPlanes;
		band.height = kHeight;
		band.bytes_per_row = kRowBytes;
		band.planes_view = bitmap;
		band.bob_base = m_planes.view;
		band.bpl1mod = band.modulo();
		band.bpl2mod = band.modulo();
		band.palette = kPalette.words();
		band.palette_colors = 32u;
		eng::scene::RasterLayout layout {};
		layout.add(band);
		eng::copper::SchedulerT<false> sched {
			eng::Block<eng::CopperTag> {m_copper.view, m_copper.kind}};
		if (!layout.materialize(sched)) return false;
		sched.end();
		m_display = sched.chip_view();
		return sched.ok() && eng::setup::check_copper_list(sched).ok();
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_planes {};
	eng::Block<eng::BobTag> m_sheet {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::ChipView<eng::CopperTag> m_display {};
	eng::debug::HostHud m_hud {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	backend.set_vblank_sleep(false);
	Probe probe {};
	eng::Engine engine {backend, probe};
	engine.run_frames(1000u);
	return 0;
}
