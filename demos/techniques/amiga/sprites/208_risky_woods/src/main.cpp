// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --keep-running

// ============================================================================
// Demo 208 â€” fondo por sprites *Risky Woods*  Â·  ETAPA E4
// ============================================================================
//
// Plan (docs/debugging/investigaciones/risky-woods-208-sprite-scroll.md):
//   E0 OK (1 canal) Â· E1 OK (8 sueltos, 128 px) Â· E2 OK (repeticiÃ³n a 320 px) Â·
//   E3 OK (scroll 1 px/frame por punteros + rotaciÃ³n de columna).
//   E4 (esta) â€” **3 franjas** con las 3 variantes y scroll continuo:
//     A (8 sprites sueltos, patrÃ³n 128 px, 2,5 repeticiones)
//     B (6 sprites sueltos, patrÃ³n 96 px + 2 sprites con vaivÃ©n senoidal)
//     C (8 sprites emparejados/attached, patrÃ³n 64 px, 5 repeticiones, 15 colores)
//   Las 8 canales se **reutilizan verticalmente** entre franjas (no se solapan).
//
// **Rendimiento**: la copperlist se emite UNA vez; por frame solo se parchean los
// `SPRxPT` (scroll) y la X de los 2 objetos. CPU ~0.

#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/backend.hpp>

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

namespace {

// Display 320x256, 4 planos.
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u16 kBandLines = 80;
constexpr eng::u16 kColWidth = 16;
constexpr eng::u8  kShifts = 16;                        // sets pre-shifteados (1 px)
constexpr eng::u16 kDisplayX0 = 128;                    // borde izquierdo del display
constexpr eng::u16 kDisplayW = 320;
constexpr eng::u16 kStride = static_cast<eng::u16>(2u + kBandLines * 2u + 2u); // 164
// Margen del WAIT respecto a la 1.Âª columna del perÃ­odo (rÃ¡faga de â‰¤8 px, sin pisar antes).
constexpr eng::u16 kCuGap = 24;
constexpr eng::u16 kOffWords = 8u;
constexpr eng::u16 kOffWord = 0xfe00u; // VSTART=VSTOP=254 (nunca arma)

// Objetos (franja B): 2 sprites con vaivÃ©n.
constexpr eng::u8  kObjs = 2;
constexpr eng::u16 kObjH = 16;
constexpr eng::u16 kObjStride = static_cast<eng::u16>(2u + kObjH * 2u + 2u); // 36
constexpr eng::u16 kObjY = 152; // dentro de la franja B [128,208)

// Franjas: top, nÂº canales, patrÃ³n (px), attached.
struct BandSpec {
	eng::u16 top;
	eng::u8  channels;
	eng::u16 pattern;
	bool     attach;
};

// COLOR00 playfield. 3 colores coherentes para A/B (suelo/cresta/cielo) en cada par.
constexpr eng::Palette32 kPalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x000, 0x630, 0xff0, 0x24a, // 16-19
	0x000, 0x630, 0xff0, 0x24a, // 20-23
	0x000, 0x630, 0xff0, 0x24a, // 24-27
	0x000, 0x630, 0xff0, 0x24a, // 28-31
}};

// --- Figuras -----------------------------------------------------------------
// Valor 1..3 (3 colores) de la franja `style` (0=colina, 1=ola) en `px` (0..pattern-1).
[[nodiscard]] constexpr eng::u16 figure3(eng::u16 px, eng::u16 y, eng::u16 pattern, eng::u8 style) {
	const eng::u16 half = static_cast<eng::u16>(pattern / 2u);
	eng::u16 d = static_cast<eng::u16>(px < half ? (half - px) : (px - half));
	// Ola (style 1): perfil triangular invertido; colina (0): suave.
	eng::u16 wy;
	if (style == 1u) {
		wy = static_cast<eng::u16>(30u + (d * 30u) / half);
	} else {
		wy = static_cast<eng::u16>(40u - (20u * (half - d)) / half);
	}
	if (y + 1u < wy) { return 3u; }
	if (y > wy + 1u) { return 1u; }
	return 2u;
}

// Valor 0..15 (15 colores, attached) de la franja C: anillos concÃ©ntricos (64 px de patrÃ³n).
[[nodiscard]] constexpr eng::u16 figure15(eng::u16 px, eng::u16 y, eng::u16 pattern) {
	const eng::s32 cx = static_cast<eng::s32>(pattern / 2u);
	const eng::s32 cy = 40;
	const eng::s32 dx = static_cast<eng::s32>(px) - cx;
	const eng::s32 dy = static_cast<eng::s32>(y) - cy;
	const eng::s32 r2 = dx * dx + dy * dy;
	return static_cast<eng::u16>((r2 / 96) & 15);
}

struct RiskyWoodsDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 256u * 1024u, 8u * 1024u, 8u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020801u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(48u * 1024u, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(sprite_words()) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020802u);
			return;
		}

		eng::u16* data = m_sprite_block.view.as_words().data();
		eng::u16 off = 0;
		for (eng::u8 b = 0; b < 3u; ++b) {
			m_band_off[b] = off;
			build_band(data, b);
			off = static_cast<eng::u16>(off + kShifts * kBands[b].channels * kStride);
		}
		m_obj_off = off;
		build_objects(data + off);
		off = static_cast<eng::u16>(off + kObjs * kObjStride);
		for (eng::u16 i = 0; i < kOffWords; ++i) {
			data[off + i] = kOffWord;
		}

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020806u);
			return;
		}
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 3u * 256u + kBands[0].pattern);
	}

	// Scroll continuo + objetos. Se parchean los SPRxPT de cada franja y la X de los objetos.
	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		const eng::u32 pos = context.frame.frame_index;
		const eng::u8 s = static_cast<eng::u8>(pos % kShifts);
		const eng::uintptr base = reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		for (eng::u8 b = 0; b < 3u; ++b) {
			const BandSpec& bd = kBands[b];
			// Columnas = canales (sueltos) o pares (attached).
			const eng::u8 cols = bd.attach ? static_cast<eng::u8>(bd.channels / 2u) : bd.channels;
			const eng::u8 k = static_cast<eng::u8>((pos / kShifts) % cols); // rotaciÃ³n de columna
			for (eng::u8 c = 0; c < bd.channels; ++c) {
				eng::u8 col;
				if (bd.attach) {
					col = static_cast<eng::u8>((((c / 2u) + k) % cols) * 2u + (c & 1u));
				} else {
					col = static_cast<eng::u8>((c + k) % cols);
				}
				const eng::u16 word = static_cast<eng::u16>(m_band_off[b] + (s * bd.channels + col) * kStride);
				const eng::uintptr addr = base + static_cast<eng::uintptr>(word) * 2u;
				m_copper_words[m_pt_idx[b][c][0] + 1u] = static_cast<eng::u16>(addr >> 16u);
				m_copper_words[m_pt_idx[b][c][1] + 1u] = static_cast<eng::u16>(addr & 0xffffu);
			}
		}
		// Objetos: vaivÃ©n senoidal (tabla) dentro de la franja B.
		for (eng::u8 i = 0; i < kObjs; ++i) {
			const eng::u16 phase = static_cast<eng::u16>((pos * (i == 0u ? 1u : 1u) + i * 32u) & 63u);
			const eng::s16 swing = static_cast<eng::s16>(kSine[phase] * 4); // Â±~120 px
			const eng::s16 x = static_cast<eng::s16>(kObjBaseX[i] + swing);
			const eng::u16 xx = static_cast<eng::u16>(x < 0 ? 0 : (x > static_cast<eng::s16>(kDisplayW - kColWidth) ? (kDisplayW - kColWidth) : x));
			m_obj_x[i] = xx;
			m_copper_words[m_obj_pos_idx[i] + 1u] =
				static_cast<eng::u16>((kObjY << 8u) | ((xx >> 1u) & 0xffu));
		}
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static constexpr BandSpec kBands[3] = {
		{ 48u, 8u, 128u, false }, // A: 8 sueltos, 128 px
		{ 128u, 6u, 96u, false }, // B: 6 sueltos, 96 px (+2 objetos)
		{ 208u, 8u, 64u, true },  // C: 4 pares attached, 64 px
	};
	// Paleta de 16 colores para la franja C (attached, 15 colores).
	static constexpr eng::u16 kRainbow[16] = {
		0x000, 0x630, 0x950, 0xc60, 0xfc0, 0xff0, 0xcf0, 0x8f0,
		0x0f0, 0x0f8, 0x0cf, 0x09f, 0x60f, 0x90f, 0xc0f, 0xf0f,
	};
	static constexpr eng::s16 kSine[64] = {
		0, 3, 6, 9, 12, 14, 17, 19, 21, 23, 25, 26, 27, 28, 29, 29,
		30, 29, 29, 28, 27, 26, 25, 23, 21, 19, 17, 14, 12, 9, 6, 3,
		0, -3, -6, -9, -12, -14, -17, -19, -21, -23, -25, -26, -27, -28, -29, -29,
		-30, -29, -29, -28, -27, -26, -25, -23, -21, -19, -17, -14, -12, -9, -6, -3,
	};

	static constexpr eng::u16 sprite_words() {
		return static_cast<eng::u16>(kShifts * (8u + 6u + 8u) * kStride + kObjs * kObjStride + kOffWords);
	}

	/// Construye los kShifts sets pre-shifteados de la franja `b`.
	void build_band(eng::u16* data, eng::u8 b) {
		const BandSpec& bd = kBands[b];
		for (eng::u8 s = 0; s < kShifts; ++s) {
			for (eng::u8 c = 0; c < bd.channels; ++c) {
				eng::u16* st = data + m_band_off[b] + static_cast<eng::u16>(s * bd.channels + c) * kStride;
				if (bd.attach) {
					build_attached(st, static_cast<eng::u8>(c >> 1u), static_cast<eng::u8>(c & 1u), bd, s);
				} else {
					build_column3(st, c, bd, s, static_cast<eng::u8>(b == 1u ? 1u : 0u));
				}
			}
		}
	}

	void build_column3(eng::u16* s, eng::u8 c, const BandSpec& bd, eng::u8 shift, eng::u8 style) {
		const eng::u16 gx0 = static_cast<eng::u16>(kDisplayX0 + c * kColWidth);
		s[0] = static_cast<eng::u16>((bd.top << 8u) | (gx0 >> 1u));
		s[1] = static_cast<eng::u16>((bd.top + kBandLines) << 8u);
		for (eng::u16 l = 0; l < kBandLines; ++l) {
			eng::u16 dat = 0u, datb = 0u;
			for (eng::u16 px = 0; px < kColWidth; ++px) {
				const eng::u16 pxg = static_cast<eng::u16>((c * kColWidth + px + shift) % bd.pattern);
				const eng::u16 v = figure3(pxg, l, bd.pattern, style);
				if ((v & 1u) != 0u) { dat = static_cast<eng::u16>(dat | (0x8000u >> px)); }
				if ((v & 2u) != 0u) { datb = static_cast<eng::u16>(datb | (0x8000u >> px)); }
			}
			s[2u + l * 2u + 0u] = dat;
			s[2u + l * 2u + 1u] = datb;
		}
		const eng::u16 h = static_cast<eng::u16>(gx0 >> 1u);
		s[2u + kBandLines * 2u + 0u] = static_cast<eng::u16>(((bd.top + kBandLines) << 8u) | h);
		s[2u + kBandLines * 2u + 1u] = static_cast<eng::u16>((bd.top + kBandLines) << 8u);
	}

	/// Par attached: `plane_lo`=0 â†’ canales pares (bits 0-1); 1 â†’ impares (bits 2-3, ATTACH).
	void build_attached(eng::u16* s, eng::u8 pair, eng::u8 plane_lo, const BandSpec& bd, eng::u8 shift) {
		const eng::u16 gx0 = static_cast<eng::u16>(kDisplayX0 + pair * kColWidth);
		s[0] = static_cast<eng::u16>((bd.top << 8u) | (gx0 >> 1u));
		s[1] = static_cast<eng::u16>(((bd.top + kBandLines) << 8u) | (plane_lo ? 0x0080u : 0u)); // ATTACH
		for (eng::u16 l = 0; l < kBandLines; ++l) {
			eng::u16 dat = 0u, datb = 0u;
			for (eng::u16 px = 0; px < kColWidth; ++px) {
				const eng::u16 pxg = static_cast<eng::u16>((pair * kColWidth + px + shift) % bd.pattern);
				const eng::u16 v = static_cast<eng::u16>((figure15(pxg, l, bd.pattern) >> (plane_lo ? 2u : 0u)) & 3u);
				if ((v & 1u) != 0u) { dat = static_cast<eng::u16>(dat | (0x8000u >> px)); }
				if ((v & 2u) != 0u) { datb = static_cast<eng::u16>(datb | (0x8000u >> px)); }
			}
			s[2u + l * 2u + 0u] = dat;
			s[2u + l * 2u + 1u] = datb;
		}
		const eng::u16 h = static_cast<eng::u16>(gx0 >> 1u);
		s[2u + kBandLines * 2u + 0u] = static_cast<eng::u16>(((bd.top + kBandLines) << 8u) | h);
		s[2u + kBandLines * 2u + 1u] = static_cast<eng::u16>(((bd.top + kBandLines) << 8u) | (plane_lo ? 0x0080u : 0u));
	}

	void build_objects(eng::u16* data) {
		for (eng::u8 i = 0; i < kObjs; ++i) {
			eng::u16* s = data + static_cast<eng::u16>(i) * kObjStride;
			s[0] = static_cast<eng::u16>((kObjY << 8u) | ((kObjBaseX[i] >> 1u) & 0xffu));
			s[1] = static_cast<eng::u16>((kObjY + kObjH) << 8u);
			for (eng::u16 l = 0; l < kObjH; ++l) {
				const eng::u16 dy = static_cast<eng::u16>(l < 8u ? l : (15u - l));
				const eng::u16 half = static_cast<eng::u16>((dy + 1u) * 2u);
				eng::u16 dat = 0u;
				for (eng::u16 x = 0u; x < 16u; ++x) {
					const eng::u16 dx = static_cast<eng::u16>(x < 8u ? (8u - x) : (x - 8u));
					if (dx < half) { dat = static_cast<eng::u16>(dat | (0x8000u >> x)); }
				}
				s[2u + l * 2u + 0u] = (i == 1u) ? 0u : dat;
				s[2u + l * 2u + 1u] = (i == 1u) ? dat : 0u;
			}
			s[2u + kObjH * 2u + 0u] = 0u;
			s[2u + kObjH * 2u + 1u] = 0u;
		}
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		const eng::uintptr spr_base = reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		const eng::uintptr off_ptr = spr_base + static_cast<eng::uintptr>(sprite_words() - kOffWords) * 2u;
		for (eng::u8 c = 0; c < 8u; ++c) {
			sched.move(static_cast<eng::u16>(0x120u + c * 4u), static_cast<eng::u16>(off_ptr >> 16));
			sched.move(static_cast<eng::u16>(0x122u + c * 4u), static_cast<eng::u16>(off_ptr & 0xffffu));
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), kOffWord);
			sched.move(static_cast<eng::u16>(0x140u + c * 8u), kOffWord);
		}
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);

		for (eng::u8 b = 0; b < 3u; ++b) {
			emit_band(sched, spr_base, b);
		}
		// Objetos (canales 6/7).
		for (eng::u8 i = 0; i < kObjs; ++i) {
			const eng::u8 ch = static_cast<eng::u8>(6u + i);
			const eng::uintptr addr = spr_base + static_cast<eng::uintptr>(m_obj_off + i * kObjStride) * 2u;
			sched.move(static_cast<eng::u16>(0x120u + ch * 4u), static_cast<eng::u16>(addr >> 16));
			sched.move(static_cast<eng::u16>(0x122u + ch * 4u), static_cast<eng::u16>(addr & 0xffffu));
			m_obj_pos_idx[i] = sched.move_at(static_cast<eng::u16>(0x140u + ch * 8u),
							 static_cast<eng::u16>((kObjY << 8u) | ((kObjBaseX[i] >> 1u) & 0xffu)));
			sched.move(static_cast<eng::u16>(0x142u + ch * 8u),
				   static_cast<eng::u16>((kObjY + kObjH) << 8u));
		}
		sched.wait_line(0xf8);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		m_copper_words = sched.data();
		return m_copper_ok;
	}

	void emit_band(eng::copper::SchedulerT<false>& sched, eng::uintptr spr_base, eng::u8 b) {
		const BandSpec& bd = kBands[b];
		const eng::u16 bottom = static_cast<eng::u16>(bd.top + kBandLines);
		// La franja C usa 15 colores: recarga COLOR16-31 al empezar (las A/B usan las 3
		// colores de la paleta base).
		if (bd.attach) {
			for (eng::u8 i = 0; i < 16u; ++i) {
				sched.move(static_cast<eng::u16>(0x180u + (16u + i) * 2u), kRainbow[i]);
			}
		}
		// Arma los canales de la franja (PT a su set 0) + registra Ã­ndices para el scroll.
		for (eng::u8 c = 0; c < bd.channels; ++c) {
			const eng::uintptr addr =
				spr_base + static_cast<eng::uintptr>(m_band_off[b] + c * kStride) * 2u;
			const eng::u16 x = static_cast<eng::u16>(kDisplayX0 + c * kColWidth);
			m_pt_idx[b][c][0] = sched.move_at(static_cast<eng::u16>(0x120u + c * 4u),
							  static_cast<eng::u16>(addr >> 16u));
			m_pt_idx[b][c][1] = sched.move_at(static_cast<eng::u16>(0x122u + c * 4u),
							  static_cast<eng::u16>(addr & 0xffffu));
			const eng::u16 ctl = static_cast<eng::u16>((bottom << 8u) | ((bd.attach && (c & 1u)) ? 0x0080u : 0u));
			sched.move(static_cast<eng::u16>(0x140u + c * 8u),
				   static_cast<eng::u16>((bd.top << 8u) | ((x >> 1u) & 0xffu)));
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), ctl);
		}
		// RepeticiÃ³n a lo ancho: por lÃ­nea, por perÃ­odo, WAIT + rÃ¡faga (ciclando canales).
		const eng::u16 period = bd.pattern;
		for (eng::u16 line = bd.top; line < bottom; ++line) {
			for (eng::u16 xstart = kDisplayX0; xstart < kDisplayX0 + kDisplayW; xstart = static_cast<eng::u16>(xstart + period)) {
				const eng::s32 wpx = static_cast<eng::s32>(xstart) - static_cast<eng::s32>(kCuGap);
				if (wpx > 0) {
					sched.wait_position_safe(line, static_cast<eng::u8>((static_cast<eng::u16>(wpx) >> 1u) & 0xfeu));
				}
				for (eng::u8 c = 0; c < bd.channels; ++c) {
					// En attached, el par p (canales 2p y 2p+1) comparte X (16 px por par).
					const eng::u8 colx = bd.attach ? static_cast<eng::u8>(c / 2u) : c;
					const eng::u16 x = static_cast<eng::u16>(xstart + colx * kColWidth);
					if (x >= kDisplayX0 + kDisplayW) { break; }
					sched.move(static_cast<eng::u16>(0x140u + c * 8u),
						   static_cast<eng::u16>((line << 8u) | ((x >> 1u) & 0xffu)));
				}
			}
		}
	}

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u16* m_copper_words = nullptr;
	eng::u16 m_band_off[3] {};
	eng::u16 m_obj_off = 0;
	eng::u16 m_pt_idx[3][8][2] {};
	eng::u16 m_obj_pos_idx[kObjs] {};
	eng::u16 m_obj_x[kObjs] { 168u, 200u };
	static constexpr eng::u16 kObjBaseX[kObjs] { 184u, 200u };
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	RiskyWoodsDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}

