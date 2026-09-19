// Demo 125 - layers_dualpf (PORTE de demoscene-repo-orig/effects/layers/layers.c)
//
// Efecto: DUAL PLAYFIELD (3+3 = 6 planos OCS) con dos imagenes ya pre-renderizadas
// (`background` en PF1/planos 1-3-5 y `foreground` en PF2/planos 2-4-6), scroll
// omnidireccional INDEPENDIENTE de cada capa, y gradientes verticales aplicados por el
// Copper cada ~8 lineas (bandas de color) leyendo `bg-gradient`/`fg-gradient`. El
// wrapping vertical se hace por cambios de `BPL1MOD`/`BPL2MOD` sincronizados con el
// barrido, no redibujando.
//
// Fidelidad (regla del repo): mismos assets, mismos 320x256, mismo contrato base
// (`BPLCON0=0x6600`, `BPLCON2=0x0024`, `BPLCON3=0x0c00`, `DDFSTRT/STOP=0x30/0xd0`,
// `DIWSTRT/STOP=0x2c81/0x2cc1`), mismo scroll (`frameCount*12`), mismos modulos
// (`(384-(320+16))/8 = 6`) y doble buffer de copperlist con commit en VBlank.
//
// Diferencias con el original (idiomaticas del engine):
//   - El original usa `CopListT`/macros de libgfx y escribe registros directos; aqui se
//     usa `eng::copper::Scheduler` (MOVE/WAIT + `wait_line_safe`, port de `CopWaitSafe`).
//   - Los bitplanes importados se COPIAN a un bloque CHIP del engine antes de mostrarlos
//     (leccion de imports: un asset DMA fuera de CHIP se ve como basura; ver
//     docs/demos/effects/dx39-layers-original-analysis.md).
//   - Doble buffer de copperlist: se reconstruye la lista de BACK cada frame y se instala
//     (commit) una vez por frame; nunca se parchea la lista activa.
//
// Build/run:
//   bash ./tools/build/build-demo.sh demos/amiga/125_layers_dualpf --debug
//   bash ./tools/run/run-demo.sh demos/amiga/125_layers_dualpf --warp
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/retro/fixed_trig.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

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

// --- Assets del original (importados tal cual) -------------------------------
// Los datos usan tipos y section-macros de libgfx; se neutralizan aqui (como 116/117).
using u_short = eng::u16;
using u_char = eng::u8;
#define __data
#define __rodata
#define __data_chip
#define background_bpl_section
#define foreground_bpl_section
#define bg_gradient_pixels_section
#define fg_gradient_pixels_section
enum { PM_RGB12 = 9 };
enum { BM_STATIC = 0x40 };
struct BitmapT {
	eng::u16 width, height, depth, bytesPerRow, bplSize;
	eng::u8 flags;
	void* planes[8];
};
struct PixmapT {
	int type;
	eng::s16 width, height;
	void* pixels;
};
#include "data/background.c"
#include "data/foreground.c"
#include "data/bg-gradient.c"
#include "data/fg-gradient.c"

namespace {

namespace copper = eng::copper;

using eng::s16;
using eng::s32;
using eng::u8;
using eng::u16;
using eng::u32;

// Geometria del display y de los bitmaps (contrato del original).
constexpr u16 kWidth = 320;
constexpr u16 kHeight = 256;
constexpr u16 kBytesPerRow = kWidth / 8; // 40
constexpr u16 kBmpBytesPerRow = 48;	 // background/foreground_bytesPerRow
constexpr u32 kBmpPlaneBytes = 18432;	 // background_bplSize
constexpr u32 kBmpBytes = kBmpPlaneBytes * 3u;
constexpr s16 kBplMod = static_cast<s16>((384 - (kWidth + 16)) / 8); // 6

// Registros del display (contrato base del original: SetupDisplayWindow/Fetch/Mode).
constexpr u16 kDiwstrt = 0x2c81;
constexpr u16 kDiwstop = 0x2cc1;
constexpr u16 kDdfstrt = 0x0030;
constexpr u16 kDdfstop = 0x00d0;
constexpr u16 kBplcon0 = 0x6600; // 6 planos + DBLPF
constexpr u16 kBplcon2 = 0x0024;
constexpr u16 kBplcon3 = 0x0c00; // offset 0x106 (el enum del engine no lo nombra)
constexpr u16 kVstrt = 0x2c;	 // DIWSTRT alto: linea visible 0 -> vpos 0x2c+y

constexpr u16 kDmacon = 0x8380; // SET | MASTER | COPPER | BITPLANE

constexpr u8 kLists = 2;
constexpr u32 kCopperWords = 2048;
constexpr s16 kStep = 8; // banda de gradiente en lineas

/// `normfx` de libgfx: normaliza un producto fijo a entero (`(x) >> 12`).
[[nodiscard]] constexpr s16 norm12(s32 x) {
	return static_cast<s16>(x >> 12);
}
/// Modulo no negativo (como `mod16`).
[[nodiscard]] constexpr s16 mod16(s16 a, s16 b) {
	const s16 r = static_cast<s16>(a % b);
	return r < 0 ? static_cast<s16>(r + b) : r;
}

struct LayersDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({200u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012501u);
			return;
		}
		// Los bitplanes DMA DEBEN vivir en CHIP (leccion de imports).
		m_planes_block =
			backend.memory().chip.allocate_block<eng::PlaneTag>(kBmpBytes * 2u, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(
			kLists * kCopperWords, 16);
		if (!m_planes_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012502u);
			return;
		}
		copy_to_chip(reinterpret_cast<const u8*>(_background_bpl), 0u, kBmpBytes);
		copy_to_chip(reinterpret_cast<const u8*>(_foreground_bpl), kBmpBytes, kBmpBytes);

		compute_scroll(0u);
		if (!build_copper(0u) || !build_copper(1u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012503u);
			return;
		}
		backend.takeover_display(m_copper_ptrs[0]);
		eng::debug::mark_ready(g_eng_run_status, kHeight);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_planes_block.valid()) {
			return;
		}
		compute_scroll(context.frame.frame_index);
#if 1
		// EXPERIMENTO: no reconstruir la copperlist (usar la de init).
		backend.install_copper_list(m_copper_ptrs[0]);
#else
		if (!build_copper(m_active)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012504u);
			return;
		}
		backend.install_copper_list(m_copper_ptrs[m_active]);
		m_active = static_cast<u8>((m_active + 1u) % kLists);
#endif
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void copy_to_chip(const u8* src, u32 dst_off, u32 bytes) {
		u8* dst = m_planes_block.view.data() + dst_off;
		for (u32 i = 0; i < bytes; ++i) {
			dst[i] = src[i];
		}
	}

	/// `Render` del original: posiciones de scroll por seno/coseno (`frameCount*12`).
	void compute_scroll(u32 frame) {
		using eng::retro::cos;
		using eng::retro::sin;
		using eng::retro::turns;
		const u16 a = static_cast<u16>(frame * 12u);
		const s16 s = sin(turns(a)).v;
		const s16 c = cos(turns(a)).v;
		const s16 bg_h = static_cast<s16>(background_height / 2 - 1); // 191
		const s16 fg_h = static_cast<s16>(foreground_height / 2 - 1);
		const s16 bg_w = static_cast<s16>(background_width / 2); // 192
		const s16 fg_w = static_cast<s16>(foreground_width / 2);
		m_bg_y = static_cast<s16>(norm12(static_cast<s32>(s) * bg_h) + bg_h);
		m_fg_y = static_cast<s16>(norm12(static_cast<s32>(c) * fg_h) + fg_h);
		m_bg_x = static_cast<s16>(norm12(static_cast<s32>(c) * bg_w) + bg_w);
		m_fg_x = static_cast<s16>(norm12(static_cast<s32>(s) * fg_w) + fg_w);
	}

	/// `MakeCopperList` = `SetupLayers` + `SetupRaster`, pero con el `Scheduler` del engine.
	bool build_copper(u8 list) {
		const eng::Bytes<eng::CopperTag> slice =
			m_copper_block.view.subspan(static_cast<u32>(list) * kCopperWords, kCopperWords);
		copper::Scheduler sched {eng::Block<eng::CopperTag> {slice, m_copper_block.kind}};

		// --- display base ---
		sched.move(copper::Register::DMACON, kDmacon);
		sched.move(copper::Register::BPLCON0, kBplcon0);
		sched.move(copper::Register::BPLCON2, kBplcon2);
		sched.move(static_cast<u16>(0x106), kBplcon3);
		sched.move(copper::Register::DIWSTRT, kDiwstrt);
		sched.move(copper::Register::DIWSTOP, kDiwstop);
		sched.move(copper::Register::DDFSTRT, kDdfstrt);
		sched.move(copper::Register::DDFSTOP, kDdfstop);

		// --- SetupLayers ---
		const s16 fg_sh = static_cast<s16>(15 - (m_fg_x & 15));
		const s16 bg_sh = static_cast<s16>(15 - (m_bg_x & 15));
		const s32 bg_off = static_cast<s32>(m_bg_y) * kBmpBytesPerRow + (m_bg_x & -16) / 8;
		const s32 fg_off = static_cast<s32>(m_fg_y) * kBmpBytesPerRow + (m_fg_x & -16) / 8;
		for (u8 i = 0; i < 3u; ++i) {
			sched.move_bitplane_pointer(
				static_cast<u8>(i * 2u),
				m_planes_block.view.address(bg_off + static_cast<s32>(i) *
								 static_cast<s32>(kBmpPlaneBytes)));
		}
		for (u8 i = 0; i < 3u; ++i) {
			sched.move_bitplane_pointer(
				static_cast<u8>(i * 2u + 1u),
				m_planes_block.view.address(static_cast<s32>(kBmpBytes) + fg_off +
							    static_cast<s32>(i) *
								    static_cast<s32>(kBmpPlaneBytes)));
		}
		sched.move(copper::Register::BPL1MOD, static_cast<u16>(kBplMod));
		sched.move(copper::Register::BPL2MOD, static_cast<u16>(kBplMod));
		sched.move(copper::Register::BPLCON1,
			   static_cast<u16>((static_cast<u16>(fg_sh) << 4u) | static_cast<u16>(bg_sh)));

		// --- SetupRaster ---
		if (!emit_raster(sched)) {
			return false;
		}
		sched.end();
		m_copper_ptrs[list] = sched.data();
		m_last_words = sched.words_used();
		return sched.ok();
	}

	/// `SetupRaster` del original: paleta base + cambios de banda cada 8 lineas y wrap
	/// vertical por modulo, con WAIT seguro (vpos > 255).
	bool emit_raster(copper::Scheduler& sched) {
		const u16* bg_pal = bg_gradient_pixels;
		const u16* fg_pal = fg_gradient_pixels;
		s16 wrap_bg = -1;
		s16 wrap_fg = -1;

		{
			const s16 bg_pal_y = mod16(static_cast<s16>(m_bg_y / kStep), bg_gradient_height);
			const s16 fg_pal_y = mod16(static_cast<s16>(m_fg_y / kStep), fg_gradient_height);
			bg_pal += bg_pal_y * bg_gradient_width + 1;
			fg_pal += fg_pal_y * fg_gradient_width + 1;
		}

		sched.move(copper::Register::COLOR00, 0);
		for (u16 i = 0; i < 6u; ++i) {
			sched.move32(static_cast<u16>(0x180u + (i + 1u) * 2u), *bg_pal++);
		}
		for (u16 i = 0; i < 5u; ++i) {
			sched.move32(static_cast<u16>(0x180u + (i + 9u) * 2u), *fg_pal++);
		}

		if (m_bg_y + kHeight >= background_height - 1) {
			wrap_bg = static_cast<s16>(background_height - m_bg_y - 1);
		}
		if (m_fg_y + kHeight >= foreground_height - 1) {
			wrap_fg = static_cast<s16>(foreground_height - m_fg_y - 1);
		}

		const u16 bg_mod_wrap =
			static_cast<u16>(static_cast<s16>(-static_cast<s16>(kBmpPlaneBytes) + kBplMod +
							  static_cast<s16>(kBmpBytesPerRow)));
		const u16 fg_mod_wrap = bg_mod_wrap;

		s16 y_bg = m_bg_y;
		s16 y_fg = m_fg_y;
		for (s16 y = 0; y < static_cast<s16>(kHeight); ++y, ++y_bg, ++y_fg) {
			u8 f = 0;
			if (y == static_cast<s16>(wrap_bg - 1)) {
				f |= 1u;
			}
			if (y == static_cast<s16>(wrap_fg - 1)) {
				f |= 2u;
			}
			if (y == wrap_bg) {
				f |= 4u;
			}
			if (y == wrap_fg) {
				f |= 8u;
			}
			if ((y_bg & 7) == 0) {
				f |= 16u;
			}
			if ((y_fg & 7) == 0) {
				f |= 32u;
			}
			if (!f) {
				continue;
			}

			sched.wait_line_safe(static_cast<u16>(kVstrt + y));

			if (f & 1u) {
				sched.move(copper::Register::BPL1MOD, bg_mod_wrap);
			}
			if (f & 2u) {
				sched.move(copper::Register::BPL2MOD, fg_mod_wrap);
			}
			if (f & 4u) {
				sched.move(copper::Register::BPL1MOD, static_cast<u16>(kBplMod));
			}
			if (f & 8u) {
				sched.move(copper::Register::BPL2MOD, static_cast<u16>(kBplMod));
			}

			if (y == 0) {
				continue;
			}

			if (f & 16u) {
				if (y_bg >= bg_gradient_height * kStep) {
					bg_pal = bg_gradient_pixels;
				}
				bg_pal++;
				for (u16 i = 0; i < 6u; ++i) {
					sched.move32(static_cast<u16>(0x180u + (i + 1u) * 2u), *bg_pal++);
				}
			}
			if (f & 32u) {
				if (y_fg >= fg_gradient_height * kStep) {
					fg_pal = fg_gradient_pixels;
				}
				fg_pal++;
				for (u16 i = 0; i < 5u; ++i) {
					sched.move32(static_cast<u16>(0x180u + (i + 9u) * 2u), *fg_pal++);
				}
			}
		}
		return sched.ok();
	}

	s16 m_bg_x = 0;
	s16 m_bg_y = 0;
	s16 m_fg_x = 0;
	s16 m_fg_y = 0;
	u8 m_active = 0;
	u16 m_last_words = 0;
	eng::Block<eng::PlaneTag> m_planes_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	const u16* m_copper_ptrs[kLists] = {nullptr, nullptr};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	LayersDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffff);

	return 0;
}
