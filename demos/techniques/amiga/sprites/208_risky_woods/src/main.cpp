// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --keep-running

// ============================================================================
// Demo 208 — fondo por sprites *Risky Woods*  ·  ETAPA E4
// ============================================================================
//
// Plan (docs/debugging/investigaciones/risky-woods-208-sprite-scroll.md):
//   E0 OK (1 canal) · E1 OK (8 sueltos, 128 px) · E2 OK (repetición a 320 px) ·
//   E3 OK (scroll 1 px/frame por punteros + rotación de columna).
//   E4 (esta) — **3 franjas** con las 3 variantes y scroll continuo:
//     A (8 sprites sueltos, patrón 128 px)   B (6 sueltos, 96 px + 2 objetos) ·
//     C (8 sprites emparejados/attached, 64 px, 15 colores).
//   Las 8 canales se **reutilizan verticalmente** entre franjas (no solapan).
//
// **Disciplina de coste (AGENTS 1.15)**: nada de `*`/`/`/`%` en bucles por píxel ni en el
// bucle de juego. Figuras precalculadas con `constexpr`, `mulsw` (16-bit) para los anillos,
// desplazamientos y una tabla de offsets; el scroll va por punteros pre-shifteados (CPU ~0).

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
constexpr eng::u8  kShifts = 16;                 // sets pre-shifteados (1 px cada uno)
constexpr eng::u16 kDisplayX0 = 128;             // borde izquierdo del display
constexpr eng::u16 kDisplayW = 320;
constexpr eng::u16 kStride = static_cast<eng::u16>(2u + kBandLines * 2u + 2u); // 164
constexpr eng::u16 kCuGap = 24;                  // margen del WAIT respecto a la 1.ª columna
constexpr eng::u16 kCuBytes = 48u * 1024u;
constexpr eng::u16 kOffWords = 8u;
constexpr eng::u16 kOffWord = 0xfe00u; // VSTART=VSTOP=254 (nunca arma)

constexpr eng::u8  kObjs = 2;
constexpr eng::u16 kObjH = 16;
constexpr eng::u16 kObjStride = static_cast<eng::u16>(2u + kObjH * 2u + 2u); // 36
constexpr eng::u16 kObjY = 152; // dentro de la franja B [128,208)

struct BandSpec {
	eng::u16 top;
	eng::u8  channels;
	eng::u16 pattern;
	bool     attach;
	eng::u8  channel_first; // primer canal de la corrida del fondo (prioridad: menor = delante)
};

// Franjas y tablas (namespace scope: arrays constexpr con constructor propio).
constexpr BandSpec kBands[3] = {
	{ 48u, 8u, 128u, false, 0u }, // A: 8 sueltos, 128 px
	{ 128u, 6u, 96u, false, 2u }, // B
	{ 208u, 8u, 64u, true, 0u },  // C: 4 pares attached, 64 px
};
constexpr eng::u8 kCols[3] { 8u, 6u, 4u }; // columnas (sueltos) o pares (attached)
// Offset (words) de la estructura `(banda, shift, columna)` **precalculado** (constexpr) para
// no multiplicar por `kStride` en el bucle de juego.
struct StructOffTable {
	eng::u16 v[3][kShifts][8] {};
	constexpr StructOffTable() {
		for (eng::u8 b = 0; b < 3u; ++b) {
			for (eng::u8 s = 0; s < kShifts; ++s) {
				for (eng::u8 c = 0; c < 8u; ++c) {
					v[b][s][c] = static_cast<eng::u16>(
						(s * kBands[b].channels + c) * kStride);
				}
			}
		}
	}
};
constexpr StructOffTable kStructOff {};

// COLOR00 playfield (navy). 3 colores coherentes para A/B en cada par.
constexpr eng::Palette32 kPalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x000, 0x630, 0xff0, 0x24a, // 16-19
	0x000, 0x630, 0xff0, 0x24a, // 20-23
	0x000, 0x630, 0xff0, 0x24a, // 24-27
	0x000, 0x630, 0xff0, 0x24a, // 28-31
}};

// --- Figuras SIN mul/div de runtime (las libcalls son ~50-150 ciclos) ---------
// Perfil de la colina (`wy = 40 +/- 20*(.)/h`) precalculado en COMPILACION (constexpr): en
// runtime solo se indexa la tabla. Franja A: colina (pico central). Franja B: valle.
struct HillProfiles {
	eng::u16 wy[3][65] {};
	constexpr HillProfiles() {
		const eng::u16 hs[3] { 64u, 48u, 32u };
		for (eng::u8 b = 0; b < 3u; ++b) {
			for (eng::u16 d = 0; d <= 64u; ++d) {
				const eng::u16 dd = (d > hs[b]) ? hs[b] : d;
				wy[b][d] = (b == 1u)
						   ? static_cast<eng::u16>(40u + (20u * static_cast<eng::u32>(dd)) / hs[b])
						   : static_cast<eng::u16>(
							     40u - (20u * static_cast<eng::u32>(hs[b] - dd)) / hs[b]);
			}
		}
	}
};
constexpr HillProfiles kHill {};
constexpr eng::u16 kHalf[3] { 64u, 48u, 32u };

/// Valor 1..3 (3 colores) de la figura de la franja `band` en `px` (0..pattern-1).
[[nodiscard]] constexpr eng::u16 figure3(eng::u16 px, eng::u16 y, eng::u8 band) {
	const eng::u16 half = kHalf[band];
	const eng::u16 d = static_cast<eng::u16>(px < half ? (half - px) : (px - half));
	const eng::u16 wy = kHill.wy[band][d];
	if (y + 1u < wy) { return 3u; }
	if (y > wy + 1u) { return 1u; }
	return 2u;
}

/// Valor 0..15 (15 colores, attached) de la franja C: anillos concentricos (mulsw 16-bit).
[[nodiscard]] eng::u16 figure15(eng::u16 px, eng::u16 y, eng::u16 pattern) {
	const eng::s16 cx = static_cast<eng::s16>(pattern / 2u);
	const eng::s16 dx = static_cast<eng::s16>(static_cast<eng::s16>(px) - cx);
	const eng::s16 dy = static_cast<eng::s16>(static_cast<eng::s16>(y) - 40);
	const eng::s16 r2 = static_cast<eng::s16>(mulsw(dx, dx) + mulsw(dy, dy));
	return static_cast<eng::u16>((static_cast<eng::u16>(r2) >> 7u) & 15u);
}

/// Envuelve `v` a [0,mod) con un lazo de resta (sin `%`; aqui `v < 2*mod`).
[[nodiscard]] constexpr eng::u16 wrap(eng::u16 v, eng::u16 mod) {
	while (v >= mod) {
		v = static_cast<eng::u16>(v - mod);
	}
	return v;
}

[[nodiscard]] constexpr eng::u16 sprite_pos(eng::u16 vstart, eng::u16 x) {
	return static_cast<eng::u16>(((vstart & 0xffu) << 8u) | ((x >> 1u) & 0xffu));
}
[[nodiscard]] constexpr eng::u16 sprite_ctl(eng::u16 vstart, eng::u16 vstop, eng::u16 x, bool attach) {
	return static_cast<eng::u16>(((vstop & 0xffu) << 8u) | (attach ? 0x0080u : 0u) |
				     (((vstart >> 8u) & 0x1u) << 2u) | (((vstop >> 8u) & 0x1u) << 1u) |
				     (x & 0x1u));
}

struct RiskyWoodsDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 384u * 1024u, 8u * 1024u, 8u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020801u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(kCuBytes, 16);
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

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		const eng::u32 pos = context.frame.frame_index;
		const eng::u8 s = static_cast<eng::u8>(pos & (kShifts - 1u));
		// Rotacion de columna cada kShifts px (contador incremental, sin `%`).
		if (pos != 0u && (pos & (kShifts - 1u)) == 0u) {
			for (eng::u8 b = 0; b < 3u; ++b) {
				if (++m_k[b] >= kCols[b]) {
					m_k[b] = 0u;
				}
			}
		}
		const eng::uintptr base = reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		for (eng::u8 b = 0; b < 3u; ++b) {
			const BandSpec& bd = kBands[b];
			const eng::u8 cols = kCols[b];
			const eng::u8 k = m_k[b];
			for (eng::u8 c = 0; c < bd.channels; ++c) {
				const eng::u8 col = bd.attach
					? static_cast<eng::u8>((wrap(static_cast<eng::u16>((c >> 1u) + k), cols) << 1u) | (c & 1u))
					: static_cast<eng::u8>(wrap(static_cast<eng::u16>(c + k), cols));
				const eng::u16 word = static_cast<eng::u16>(m_band_off[b] + kStructOff.v[b][s][col]);
				const eng::uintptr addr = base + (static_cast<eng::uintptr>(word) << 1u);
				m_copper_words[m_pt_idx[b][c][0] + 1u] = static_cast<eng::u16>(addr >> 16u);
				m_copper_words[m_pt_idx[b][c][1] + 1u] = static_cast<eng::u16>(addr & 0xffffu);
			}
		}
		// Objetos: vaiven senoidal dentro de la franja B (tabla ya escalada, sin `*`).
		for (eng::u8 i = 0; i < kObjs; ++i) {
			const eng::u16 phase = static_cast<eng::u16>(pos & 63u);
			const eng::s16 swing = static_cast<eng::s16>(kSine[phase]);
			const eng::s16 x = static_cast<eng::s16>(kObjBaseX[i] + swing);
			const eng::u16 xx = static_cast<eng::u16>(
				x < static_cast<eng::s16>(kDisplayX0)
					? kDisplayX0
					: (x > static_cast<eng::s16>(kDisplayX0 + kDisplayW - kColWidth)
						   ? static_cast<eng::u16>(kDisplayX0 + kDisplayW - kColWidth)
						   : static_cast<eng::u16>(x)));
			m_obj_x[i] = xx;
			m_copper_words[m_obj_pos_idx[i] + 1u] =
				static_cast<eng::u16>((kObjY << 8u) | ((xx >> 1u) & 0xffu));
		}
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static constexpr eng::s16 kSine[64] = {
		0, 12, 24, 36, 48, 56, 68, 76, 84, 92, 100, 104, 108, 112, 116, 116,
		120, 116, 116, 112, 108, 104, 100, 92, 84, 76, 68, 56, 48, 36, 24, 12,
		0, -12, -24, -36, -48, -56, -68, -76, -84, -92, -100, -104, -108, -112, -116, -116,
		-120, -116, -116, -112, -108, -104, -100, -92, -84, -76, -68, -56, -48, -36, -24, -12,
	};
	static constexpr eng::u16 kRainbow[16] = {
		0x000, 0x630, 0x950, 0xc60, 0xfc0, 0xff0, 0xcf0, 0x8f0,
		0x0f0, 0x0f8, 0x0cf, 0x09f, 0x60f, 0x90f, 0xc0f, 0xf0f,
	};
	static constexpr eng::u16 kObjBaseX[kObjs] { 240u, 336u };

	static constexpr eng::u16 sprite_words() {
		return static_cast<eng::u16>(kShifts * (8u + 6u + 8u) * kStride + kObjs * kObjStride + kOffWords);
	}

	void build_band(eng::u16* data, eng::u8 b) {
		const BandSpec& bd = kBands[b];
		for (eng::u8 s = 0; s < kShifts; ++s) {
			for (eng::u8 c = 0; c < bd.channels; ++c) {
				eng::u16* st = data + m_band_off[b] + static_cast<eng::u16>(s * bd.channels + c) * kStride;
				if (bd.attach) {
					build_attached(st, static_cast<eng::u8>(c >> 1u),
						       static_cast<eng::u8>(c & 1u), bd, s);
				} else {
					build_column3(st, c, bd, s, b);
				}
			}
		}
	}

	void build_column3(eng::u16* s, eng::u8 c, const BandSpec& bd, eng::u8 shift, eng::u8 band) {
		const eng::u16 bottom = static_cast<eng::u16>(bd.top + kBandLines);
		const eng::u16 gx0 = static_cast<eng::u16>(kDisplayX0 + c * kColWidth);
		s[0] = sprite_pos(bd.top, gx0);
		s[1] = sprite_ctl(bd.top, bottom, gx0, false);
		for (eng::u16 l = 0; l < kBandLines; ++l) {
			eng::u16 dat = 0u, datb = 0u;
			for (eng::u16 px = 0; px < kColWidth; ++px) {
				const eng::u16 pxg = wrap(static_cast<eng::u16>(c * kColWidth + px + shift), bd.pattern);
				const eng::u16 v = figure3(pxg, l, band);
				if ((v & 1u) != 0u) { dat = static_cast<eng::u16>(dat | (0x8000u >> px)); }
				if ((v & 2u) != 0u) { datb = static_cast<eng::u16>(datb | (0x8000u >> px)); }
			}
			s[2u + l * 2u + 0u] = dat;
			s[2u + l * 2u + 1u] = datb;
		}
		s[2u + kBandLines * 2u + 0u] = sprite_pos(bottom, gx0);
		s[2u + kBandLines * 2u + 1u] = sprite_ctl(bottom, bottom, gx0, false);
	}

	void build_attached(eng::u16* s, eng::u8 pair, eng::u8 plane_lo, const BandSpec& bd, eng::u8 shift) {
		const eng::u16 bottom = static_cast<eng::u16>(bd.top + kBandLines);
		const eng::u16 gx0 = static_cast<eng::u16>(kDisplayX0 + pair * kColWidth);
		const bool attach = plane_lo != 0u;
		s[0] = sprite_pos(bd.top, gx0);
		s[1] = sprite_ctl(bd.top, bottom, gx0, attach);
		for (eng::u16 l = 0; l < kBandLines; ++l) {
			eng::u16 dat = 0u, datb = 0u;
			for (eng::u16 px = 0; px < kColWidth; ++px) {
				const eng::u16 pxg = wrap(static_cast<eng::u16>(pair * kColWidth + px + shift), bd.pattern);
				const eng::u16 v = static_cast<eng::u16>((figure15(pxg, l, bd.pattern) >> (plane_lo ? 2u : 0u)) & 3u);
				if ((v & 1u) != 0u) { dat = static_cast<eng::u16>(dat | (0x8000u >> px)); }
				if ((v & 2u) != 0u) { datb = static_cast<eng::u16>(datb | (0x8000u >> px)); }
			}
			s[2u + l * 2u + 0u] = dat;
			s[2u + l * 2u + 1u] = datb;
		}
		s[2u + kBandLines * 2u + 0u] = sprite_pos(bottom, gx0);
		s[2u + kBandLines * 2u + 1u] = sprite_ctl(bottom, bottom, gx0, attach);
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

		emit_band(sched, spr_base, 0);
		// Objetos (canales 0/1, PRIORIDAD MAXIMA: se dibujan DELANTE del fondo): se **arman
		// antes** de la franja B (su Y=152) para que el haz los dibuje; su X se parchea por frame.
		// El fondo de la franja B va en canales 2..7 (numero mayor = detras). La franja C los reutiliza despues.
		for (eng::u8 i = 0; i < kObjs; ++i) {
			const eng::u8 ch = i;
			const eng::uintptr addr = spr_base + static_cast<eng::uintptr>(m_obj_off + i * kObjStride) * 2u;
			sched.move(static_cast<eng::u16>(0x120u + ch * 4u), static_cast<eng::u16>(addr >> 16));
			sched.move(static_cast<eng::u16>(0x122u + ch * 4u), static_cast<eng::u16>(addr & 0xffffu));
			m_obj_pos_idx[i] = sched.move_at(static_cast<eng::u16>(0x140u + ch * 8u),
							 static_cast<eng::u16>((kObjY << 8u) | ((kObjBaseX[i] >> 1u) & 0xffu)));
			sched.move(static_cast<eng::u16>(0x142u + ch * 8u),
				   static_cast<eng::u16>((kObjY + kObjH) << 8u));
		}
		emit_band(sched, spr_base, 1);
		emit_band(sched, spr_base, 2);
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
		// Sincroniza la recarga de paleta (y la armadura) con el inicio EXACTO de la franja:
		// sin este WAIT el Copper (que va por delante del haz) ejecuta las recargas durante las
		// ultimas lineas de la franja anterior y tine sus sprites (puntos en la separacion) y la
		// primera columna de la franja siguiente.
		if (b != 0u) {
			sched.wait_line(bd.top);
		}
		if (bd.attach) {
			// Franja C (15 colores): recarga COLOR16-31 con la paleta arcoiris.
			for (eng::u8 i = 0; i < 16u; ++i) {
				sched.move(static_cast<eng::u16>(0x180u + (16u + i) * 2u), kRainbow[i]);
			}
		} else if (b == 1u) {
			// Franja B: el fondo va en canales 2..7 (par 1-3, COLOR20-31); los objetos en
			// canales 0/1 (par 0, COLOR16-19), libres aqui. Se recargan con colores propios
			// para que NO se camuflen con el fondo.
			sched.move(static_cast<eng::u16>(0x180u + 16u * 2u), 0x000u);
			sched.move(static_cast<eng::u16>(0x180u + 17u * 2u), 0xf00u); // objeto 0 (rojo)
			sched.move(static_cast<eng::u16>(0x180u + 18u * 2u), 0xfffu); // objeto 1 (blanco)
			sched.move(static_cast<eng::u16>(0x180u + 19u * 2u), 0x0f0u);
		}
		for (eng::u8 c = 0; c < bd.channels; ++c) {
			const eng::u8 ch = static_cast<eng::u8>(bd.channel_first + c);
			const eng::uintptr addr =
				spr_base + static_cast<eng::uintptr>(m_band_off[b] + c * kStride) * 2u;
			// En attached el par (even/odd) comparte X: el odd solo aporta los bits altos del
			// mismo pixel, NO es un sprite 16px a la derecha (por eso no se usa c sino c>>1).
			const eng::u8 colx = bd.attach ? static_cast<eng::u8>(c >> 1u) : c;
			const eng::u16 x = static_cast<eng::u16>(kDisplayX0 + colx * kColWidth);
			m_pt_idx[b][c][0] = sched.move_at(static_cast<eng::u16>(0x120u + ch * 4u),
							  static_cast<eng::u16>(addr >> 16u));
			m_pt_idx[b][c][1] = sched.move_at(static_cast<eng::u16>(0x122u + ch * 4u),
							  static_cast<eng::u16>(addr & 0xffffu));
			sched.move(static_cast<eng::u16>(0x140u + ch * 8u), sprite_pos(bd.top, x));
			sched.move(static_cast<eng::u16>(0x142u + ch * 8u), sprite_ctl(bd.top, bottom, x, bd.attach && (c & 1u)));
		}
		const eng::u16 period = bd.pattern;
		for (eng::u16 line = bd.top; line < bottom; ++line) {
			for (eng::u16 xstart = kDisplayX0; xstart < kDisplayX0 + kDisplayW;
			     xstart = static_cast<eng::u16>(xstart + period)) {
				const eng::s32 wpx = static_cast<eng::s32>(xstart) - static_cast<eng::s32>(kCuGap);
				if (wpx > 0) {
					sched.wait_position_safe(
						line, static_cast<eng::u8>((static_cast<eng::u16>(wpx) >> 1u) & 0xfeu));
				}
				for (eng::u8 c = 0; c < bd.channels; ++c) {
					// En attached, el canal IMPAR sigue al par (ATTACH): no se reposiciona.
					if (bd.attach && (c & 1u)) { continue; }
					const eng::u8 ch = static_cast<eng::u8>(bd.channel_first + c);
					const eng::u8 colx = bd.attach ? static_cast<eng::u8>(c >> 1u) : c;
					const eng::u16 x = static_cast<eng::u16>(xstart + colx * kColWidth);
					if (x >= kDisplayX0 + kDisplayW) { break; }
					sched.move(static_cast<eng::u16>(0x140u + ch * 8u),
						   static_cast<eng::u16>((line << 8u) | ((x >> 1u) & 0xffu)));
				}
			}
		}
	}

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u16* m_copper_words = nullptr;
	eng::u16 m_band_off[3] {};
	eng::u8  m_k[3] {};
	eng::u16 m_obj_off = 0;
	eng::u16 m_pt_idx[3][8][2] {};
	eng::u16 m_obj_pos_idx[kObjs] {};
	eng::u16 m_obj_x[kObjs] { 168u, 200u };
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
