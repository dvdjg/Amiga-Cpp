// Demo 210: BOBs opacos 5bpp con grupos atomicos y ventana de haz.
//
// Un unico framebuffer de 5 planos con fondo plano (indice 0). Cada BOB es una
// hoja 16x16 opaca: 12x12 de arte + 2 px de padding por lado. El padding pinta
// color 0, el mismo del fondo, asi que cada copia borra su posicion anterior
// sin save-under ni restore. Un BOB = un BlitJob = un grupo beam-aware.
//
// La hoja es interleaved [fila][plano] y se dibuja con un solo blit Opaque por
// objeto. El ejecutor solo lanza el grupo si el haz ya paso su caja o si el
// Blitter termina antes; si no, lo difiere entero.

#include <eng/api/api.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/palette32.hpp>
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
constexpr eng::u16 kSheet = 12u; // arte 8x8 + 2 px de padding
constexpr eng::u16 kArt = 8u;
constexpr eng::u16 kPad = 2u;
constexpr eng::u16 kFrames = 4u;
constexpr eng::u8 kVariants = 5u;
constexpr eng::u16 kRowWords = 2u; // 16 px + guarda
constexpr eng::u32 kFrameBytes = static_cast<eng::u32>(kSheet) * kRowWords * 2u; // 1 plano
constexpr eng::u32 kSheetBytes = kFrameBytes * kFrames;
constexpr eng::u32 kSheetTotal = kSheetBytes; // una hoja compartida por las 5 variantes
constexpr eng::u32 kSheetCanary = 16u;
constexpr eng::u16 kBobCount = 6u;
constexpr eng::u16 kMaxX = 288u; // caja de 2 palabras (32 px) dentro de 320
constexpr eng::u16 kMaxY = 244u;
constexpr eng::u32 kCopperWords = 1024u;
constexpr eng::u32 kChipBytes = 128u * 1024u;
constexpr eng::u32 kReadyDetail = 0x21000000u;

constexpr eng::Palette32 kPalette {{
	0x012u, 0x911u, 0x191u, 0x119u, 0x991u, 0x199u, 0x333u, 0x666u,
	0xaaau, 0x124u, 0x247u, 0x142u, 0x642u, 0x531u, 0xa64u, 0xfffu,
	0x146u, 0x37au, 0x6bfu, 0xbdfu,
	0x611u, 0xa32u, 0xe64u, 0xfb8u,
	0x141u, 0x372u, 0x6b4u, 0xae8u,
	0x527u, 0x94au, 0xd8du, 0xfdbu,
}};

enum class Fail : eng::u32 {
	Memory = 0x00021001u,
	Display = 0x00021002u,
	Art = 0x00021003u,
	Plan = 0x00021004u,
	Blitter = 0x00021005u,
	Canary = 0x00021006u,
};

struct Ball {
	eng::s32 xq = 0;
	eng::s32 yq = 0;
	eng::s16 vxq = 0;
	eng::s16 vyq = 0;
	eng::u8 variant = 0;
};

class BeamBobsDemo {
public:
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		backend.set_vblank_sleep(false);
		if (!backend.configure_memory({kChipBytes, 8u * 1024u, 0u})) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Memory);
			return;
		}
		m_planes = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitmapBytes + 2u * kCanary, 16u);
		m_sheets = backend.memory_manager().chip().reserve<eng::BobTag>(kSheetTotal + 2u * kSheetCanary, 16u);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(kCopperWords, 16u);
		if (!m_planes.valid() || !m_sheets.valid() || !m_copper.valid()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Memory);
			return;
		}
		set_canaries();
		m_target = eng::graphics::make_bob_target(
			m_planes.mem_view().subview(kCanary, kBitmapBytes), kRowBytes, kHeight, kPlanes,
			eng::graphics::BobLayout::Interleaved);
		clear_bitmap();
		if (!build_art()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Art);
			return;
		}
		init_balls(m_balls);
		if (!build_display(backend)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Display);
			return;
		}
		m_plan.set_target_bounds(m_target.bound());
		if (!draw_frame(backend, true, 0u)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
		backend.takeover_display(m_display);
		eng::debug::mark_ready(g_eng_run_status, kReadyDetail);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		step_balls();
		if (!draw_frame(backend, false, context.frame.frame_index)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
		eng::util::TextLine<96> stats;
		stats << "J" << m_plan.blit_job_count() << " B" << backend.blitter_starts()
		      << " D" << backend.last_frame_plan_execution().deferred_bobs;
		m_hud.draw(backend.debug(), "210 BEAM BOBS", eng::debug::Version {1u, 0u},
			   context.frame.frame_index, eng::debug::prof_clock(), stats.c_str());
	}

private:
	void set_canaries() {
		eng::u8* const p = m_planes.view.data();
		for (eng::u32 i = 0u; i < kCanary; ++i) {
			p[i] = 0xa5u;
			p[kCanary + kBitmapBytes + i] = 0x5au;
		}
		eng::u8* const s = m_sheets.view.data();
		for (eng::u32 i = 0u; i < kSheetCanary; ++i) {
			s[i] = 0xa5u;
			s[kSheetCanary + kSheetTotal + i] = 0x5au;
		}
	}

	[[nodiscard]] bool canaries_ok() const {
		const eng::u8* const p = m_planes.view.data();
		for (eng::u32 i = 0u; i < kCanary; ++i) {
			if (p[i] != 0xa5u || p[kCanary + kBitmapBytes + i] != 0x5au) return false;
		}
		const eng::u8* const s = m_sheets.view.data();
		for (eng::u32 i = 0u; i < kSheetCanary; ++i) {
			if (s[i] != 0xa5u || s[kSheetCanary + kSheetTotal + i] != 0x5au) return false;
		}
		return true;
	}

	void clear_bitmap() {
		eng::u8* const p = m_planes.view.data() + kCanary;
		for (eng::u32 i = 0u; i < kBitmapBytes; ++i) p[i] = 0u;
	}

	bool build_art() {
		eng::u16* const w = reinterpret_cast<eng::u16*>(m_sheets.view.data() + kSheetCanary);
		const eng::u32 frame_words = kFrameBytes / 2u;
		for (eng::u8 f = 0u; f < kFrames; ++f) {
			const eng::u32 fo = static_cast<eng::u32>(f) * frame_words;
			for (eng::u16 y = 0u; y < kSheet; ++y) {
				eng::u16 img = 0u;
				for (eng::u16 x = 0u; x < kArt; ++x) {
					const eng::s32 dx = static_cast<eng::s32>(x) - 5;
					const eng::s32 dy = static_cast<eng::s32>(y) - 5;
					const eng::s32 d2 = dx * dx + dy * dy;
					const eng::s32 hx = static_cast<eng::s32>(x) - (3 + (f & 1u) * 3u);
					const eng::s32 hy = static_cast<eng::s32>(y) - (3 + (f >> 1u) * 3u);
					if (d2 <= 9 && hx * hx + hy * hy > 1) {
						img = static_cast<eng::u16>(img | (0x8000u >> (x + kPad)));
					}
				}
				w[fo + static_cast<eng::u32>(y) * kRowWords] = img;
				w[fo + static_cast<eng::u32>(y) * kRowWords + 1u] = 0u;
			}
		}
		for (eng::u8 v = 0u; v < kVariants; ++v) {
			eng::graphics::Bob bob {};
			bob.sheet = m_sheets.mem_view_chip().subview(kSheetCanary, kSheetBytes);
			bob.width = kSheet;
			bob.height = kSheet;
			bob.planes = 1u;
			bob.frame_count = kFrames;
			bob.frame_stride = kFrameBytes;
			bob.layout = eng::graphics::BobLayout::Planar;
			bob.draw = eng::graphics::BobDraw::Opaque;
			m_bobs[v] = bob;
		}
		return true;
	}

	bool build_display(eng::amiga::AmigaBackend&) {
		eng::scene::Band band {};
		band.planes = kPlanes;
		band.height = kHeight;
		band.bytes_per_row = kRowBytes;
		band.planes_view = m_planes.mem_view().subview(kCanary, kBitmapBytes);
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

	bool draw_frame(eng::amiga::AmigaBackend& backend, bool setup, eng::u32 frame_index) {
		m_plan.clear();
		m_plan.set_target_bounds(m_target.bound());
		for (eng::u16 i = 0u; i < kBobCount; ++i) {
			const Ball& b = m_balls[i];
			const eng::s16 x = static_cast<eng::s16>(b.xq >> 8);
			const eng::s16 y = static_cast<eng::s16>(b.yq >> 8);
			const eng::u8 first = m_plan.blit_job_count();
			const eng::u8 frame = static_cast<eng::u8>(((frame_index / 6u) + i) & 3u);
			if (!eng::graphics::bob_draw_plane_opaque(m_plan, m_bobs[b.variant], frame, x, y, m_target, b.variant)) {
				return false;
			}
			(void)first; // sin grupo: camino legacy sin guarda de haz
		}
		if (setup) backend.set_bob_beam_wait(true);
		const bool ok = backend.execute_frame_plan(m_plan);
		if (setup) backend.set_bob_beam_wait(false);
		if (!ok) return false;
		if (!canaries_ok()) return false;
		return true;
	}

	void step_balls() {
		for (eng::u16 i = 0u; i < kBobCount; ++i) {
			Ball& b = m_balls[i];
			b.xq += b.vxq;
			b.yq += b.vyq;
			if (b.xq < 0) {
				b.xq = 0;
				b.vxq = static_cast<eng::s16>(-b.vxq);
			} else if (b.xq > static_cast<eng::s32>(kMaxX) << 8) {
				b.xq = static_cast<eng::s32>(kMaxX) << 8;
				b.vxq = static_cast<eng::s16>(-b.vxq);
			}
			if (b.yq < 0) {
				b.yq = 0;
				b.vyq = static_cast<eng::s16>(-b.vyq);
			} else if (b.yq > static_cast<eng::s32>(kMaxY) << 8) {
				b.yq = static_cast<eng::s32>(kMaxY) << 8;
				b.vyq = static_cast<eng::s16>(-b.vyq);
			}
		}
		// Evitar solapes: un BOB opaco no puede tapar a otro ni dejar restos de su
		// caja anterior. La caja es la hoja completa 16x16.
		for (eng::u8 pass = 0u; pass < 2u; ++pass) {
			for (eng::u16 i = 0u; i < kBobCount; ++i) {
				for (eng::u16 j = static_cast<eng::u16>(i + 1u); j < kBobCount; ++j) {
					Ball& a = m_balls[i];
					Ball& b = m_balls[j];
					const eng::s32 ax = a.xq >> 8;
					const eng::s32 ay = a.yq >> 8;
					const eng::s32 bx = b.xq >> 8;
					const eng::s32 by = b.yq >> 8;
					const eng::s32 ox = (ax + kSheet) - bx;
					const eng::s32 ox2 = (bx + kSheet) - ax;
					const eng::s32 oy = (ay + kSheet) - by;
					const eng::s32 oy2 = (by + kSheet) - ay;
					if (ox <= 0 || ox2 <= 0 || oy <= 0 || oy2 <= 0) continue;
					const eng::s32 mx = ox < ox2 ? ox : ox2;
					const eng::s32 my = oy < oy2 ? oy : oy2;
					if (mx < my) {
						const eng::s32 push = (mx + 1) >> 1;
						if (ax < bx) { a.xq -= push << 8; b.xq += push << 8; }
						else { a.xq += push << 8; b.xq -= push << 8; }
						const eng::s16 t = a.vxq;
						a.vxq = static_cast<eng::s16>(-b.vxq);
						b.vxq = static_cast<eng::s16>(-t);
					} else {
						const eng::s32 push = (my + 1) >> 1;
						if (ay < by) { a.yq -= push << 8; b.yq += push << 8; }
						else { a.yq += push << 8; b.yq -= push << 8; }
						const eng::s16 t = a.vyq;
						a.vyq = static_cast<eng::s16>(-b.vyq);
						b.vyq = static_cast<eng::s16>(-t);
					}
					clamp_ball(a);
					clamp_ball(b);
				}
			}
		}
	}

	static void clamp_ball(Ball& b) {
		if (b.xq < 0) b.xq = 0;
		if (b.xq > static_cast<eng::s32>(kMaxX) << 8) b.xq = static_cast<eng::s32>(kMaxX) << 8;
		if (b.yq < 0) b.yq = 0;
		if (b.yq > static_cast<eng::s32>(kMaxY) << 8) b.yq = static_cast<eng::s32>(kMaxY) << 8;
	}

	static void init_balls(Ball* balls) {
		for (eng::u16 i = 0u; i < kBobCount; ++i) {
			const eng::u16 col = static_cast<eng::u16>(i & 3u);
			const eng::u16 row = static_cast<eng::u16>(i >> 2u);
			balls[i].xq = static_cast<eng::s32>(24u + col * 72u) << 8;
			balls[i].yq = static_cast<eng::s32>(28u + row * 56u) << 8;
			balls[i].vxq = static_cast<eng::s16>((i & 1u) ? 256 : -192);
			balls[i].vyq = static_cast<eng::s16>((i & 2u) ? 192 : -256);
			balls[i].variant = static_cast<eng::u8>(i % kVariants);
		}
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_planes {};
	eng::Block<eng::BobTag> m_sheets {};
	eng::Block<eng::CopperTag> m_copper {};
	eng::graphics::BobTarget m_target {};
	eng::graphics::Bob m_bobs[kVariants] {};
	Ball m_balls[kBobCount] { {0, 0, 0, 0, 0} };
	eng::graphics::FramePlan m_plan {};
	eng::ChipView<eng::CopperTag> m_display {};
	eng::debug::HostHud m_hud {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	backend.set_vblank_sleep(false);
	BeamBobsDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffffu);
	return 0;
}
