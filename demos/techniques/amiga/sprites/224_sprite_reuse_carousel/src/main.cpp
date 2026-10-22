// ============================================================================
// 224 — carrusel de reuso vertical (S1.1): lista de Copper estática + parcheo.
// ============================================================================
//
// Por qué estático: recomponer y reemitir la copperlist cada frame cuesta cientos de
// miles de ciclos (medido: ~670k para 30 actores con `SpriteScene`). Un juego real deja la
// ESTRUCTURA en Chip una vez y por frame solo parchea las palabras de dato: aquí 5 bandas
// raster × 8 canales = 40 placements con POS/CTL patcheados (~160 words/frame), y el
// puntero PT solo cuando rota el arte (1 vez/s). El DMA de sprites (SPREN) lo ejecuta todo.
//
// Reuso: cada canal aparece una vez por banda (5 reusos verticales) y muestra un objeto
// distinto en cada banda; al rotar, un canal cambia de objeto. Bandas 0/1 son pares
// *attached* (7 colores + transparencia, canales 0/1 y 2/3); bandas 2..5 son sprites libres
// de 3 colores + transparencia (canales 4..7). Cada placement tiene su propia X animada.

#include <eng/api/api.hpp>
#include <eng/debug/hud.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/composition/limits.hpp>
#include <eng/graphics/sprite_attached.hpp>
#include <eng/graphics/sprite_chain.hpp>
#include <eng/graphics/sprite_limits.hpp>
#include <eng/platform/amiga/entry.hpp>

namespace {

namespace copper = eng::copper;
namespace graphics = eng::graphics;

#ifndef K224_PLANES
#define K224_PLANES 3
#endif
/// **Modo sonda (DT-045)**: `-DK224_PROBE=1` reduce la demo a un canal, una banda y una
/// única escritura de POS/CTL por frame, con la misma copperlist estática. Sirve para
/// aislar si el parón de `measure-fps` con GDB adjunto depende de la carga de la escena.
#ifndef K224_PROBE
#define K224_PROBE 0
#endif
constexpr bool kProbe = (K224_PROBE != 0);
/// **A/B de DT-045**: `-DK224_NO_ROTATE=1` desactiva la rotación de arte (parcheo de PT)
/// manteniendo las 5 bandas y los 40 placements, para aislar el factor que congela la medida.
#ifndef K224_NO_ROTATE
#define K224_NO_ROTATE 0
#endif
constexpr bool kRotateEnabled = (K224_NO_ROTATE == 0);

constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u16 kBytesPerRow = kWidth / 8u;
constexpr eng::u8 kPlanes = static_cast<eng::u8>(K224_PLANES);
constexpr eng::u16 kArmLine = 32u;
/// Banda de la sonda de DT-045 (un solo canal, sin multiplexado).
constexpr eng::u16 kProbeLine = 100u;

/// Cinco bandas raster con 44 líneas de paso; el sprite oscila 0..27 px dentro de su banda.
constexpr eng::u16 kBandCount = 5u;
constexpr eng::u16 kBandLine[kBandCount] {56u, 100u, 144u, 188u, 232u};
/// Oscilación máxima: el sprite empieza en `banda+4` y `VSTOP` no puede alcanzar el `WAIT`
/// de la banda siguiente (paso 44, alto 16): 44 − 4 − 16 − 1 = 23 líneas.
constexpr eng::u16 kBandOsc = 23u;
constexpr eng::u16 kLaneCount = 6u;      ///< 2 lanes attached (pares) + 4 lanes libres
constexpr eng::u16 kChannels = 8u;
constexpr eng::u16 kPlacements = kBandCount * kChannels;
constexpr eng::u16 kReusePerChannel = kBandCount;
constexpr eng::u16 kRotateFrames = 50u; ///< 1 s a 50 Hz: el canal cambia de objeto
constexpr eng::u16 kSpriteH = 16u;
constexpr eng::u16 kAttachedKinds = 5u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * kHeight;
constexpr eng::u16 kFreeArtCount = 20u; ///< 4 lanes libres × 5 bandas
constexpr eng::u16 kFreeDmaWords = kSpriteH * 2u + 2u;
constexpr eng::u32 kFreeSheetBytes =
	static_cast<eng::u32>(kFreeArtCount) * kFreeDmaWords * sizeof(eng::u16);
constexpr eng::u32 kCookedBytes = static_cast<eng::u32>(kAttachedKinds) * 2u *
				  graphics::attached_pair_structure_words(kSpriteH) *
				  sizeof(eng::u16);
constexpr eng::debug::Version kVersion {1u, 0u};

/// Paleta: COLOR00..15 playfield; COLOR16 transparente del par; COLOR17..23 los siete tonos
/// attached. Los canales libres leen su grupo (COLOR25..27 y COLOR29..31) sobre el fondo.
constexpr eng::Palette32 kPalette {{
	0x012, 0x26b, 0x37c, 0x48d, 0x59e, 0x6af, 0x7bf, 0x8c0,
	0x9d1, 0xae2, 0xbf3, 0xcf4, 0xdf5, 0xef6, 0xff7, 0xfff,
	0x000, 0xf22, 0xf72, 0xfc2, 0xcf2, 0x2f6, 0x2df, 0x36f,
	0x000, 0x2ff, 0x72f, 0xf6f, 0x000, 0xff4, 0xf84, 0x8ff,
}};

/// Cinco siluetas attached (rombo, disco, flecha, cruz, escudo) de siete tonos + clave 0.
struct AttachedArt {
	eng::u16 plane[kAttachedKinds][4u][kSpriteH] {};
};

constexpr AttachedArt make_attached_art() {
	AttachedArt out {};
	for (eng::u16 kind = 0u; kind < kAttachedKinds; ++kind) {
		for (eng::u16 y = 0u; y < kSpriteH; ++y) {
			for (eng::u16 x = 0u; x < 16u; ++x) {
				const eng::s16 dx = static_cast<eng::s16>(x) - 7;
				const eng::s16 dy = static_cast<eng::s16>(y) - 7;
				const eng::s16 ax = static_cast<eng::s16>(dx < 0 ? -dx : dx);
				const eng::s16 ay = static_cast<eng::s16>(dy < 0 ? -dy : dy);
				bool inside = false;
				switch (kind) {
					case 0u: inside = static_cast<eng::s16>(ax + ay) <= 7; break;
					case 1u: inside = static_cast<eng::s16>(ax * ax + ay * ay) <= 49; break;
					case 2u: inside = ay <= 2 || (dy < 0 && ax <= static_cast<eng::s16>(7 + dy)); break;
					case 3u: inside = ax <= 2 || ay <= 2; break;
					default: inside = ax <= 6 && ay <= 6 && (ax + ay >= 4); break;
				}
				if (!inside) continue;
				const eng::u16 color = static_cast<eng::u16>(1u + ((x + y * 2u + kind) % 7u));
				const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
				for (eng::u8 p = 0u; p < 4u; ++p) {
					if ((color & (1u << p)) != 0u) {
						out.plane[kind][p][y] = static_cast<eng::u16>(out.plane[kind][p][y] | bit);
					}
				}
			}
		}
	}
	return out;
}

constexpr AttachedArt kAttachedArt = make_attached_art();

/// Triángulo de 8 bits precalculable: sin multiplicaciones ni divisiones por frame.
constexpr eng::u8 tri8(eng::u8 phase) {
	return (phase < 128u) ? phase : static_cast<eng::u8>(255u - phase);
}

/// Arte libre: cuatro siluetas distintas por lane, rotando por las cinco bandas.
constexpr eng::u8 kLaneFreeSpeed[kLaneCount] {1u, 2u, 1u, 2u, 1u, 1u};

/// Demo: estructura de cobre estática y parcheo mínimo por frame.
struct ReuseCarouselDemo {
	void init(auto& app) {
		auto& memory = app.device().memory_manager();
		m_free_sheet = memory.chip().template reserve<eng::SpriteTag>(kFreeSheetBytes, 16u);
		m_cooked = memory.chip().template reserve<eng::SpriteTag>(kCookedBytes, 16u);
		bool lists_ok = true;
		for (eng::u8 i = 0u; i < kAttachedKinds; ++i) {
			m_lists[i] = memory.chip().template reserve<eng::CopperTag>(2048u, 16u);
			lists_ok = lists_ok && m_lists[i].valid();
		}
		if (!m_free_sheet.valid() || !m_cooked.valid() || !lists_ok) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		if constexpr (kProbe) {
			init_probe(app, memory);
			return;
		}
		build_free_sheet();
		build_attached_pool();
		build_background(app);
		for (eng::u8 r = 0u; r < kAttachedKinds; ++r) {
			if (!build_list(app, m_lists[r], r)) {
				eng::amiga::mark_failed(eng::debug::BootFail::Game);
				return;
			}
		}
		app.device().takeover_copper(m_lists[0].mem_view());
		m_active = 0u;
		eng::debug::DebugPeripheral::counter_name(0u, reinterpret_cast<eng::u32>("fps10"));
		eng::debug::DebugPeripheral::counter_name(1u, reinterpret_cast<eng::u32>("placements"));
		eng::debug::DebugPeripheral::counter_name(2u, reinterpret_cast<eng::u32>("channels"));
		eng::debug::DebugPeripheral::counter_name(3u, reinterpret_cast<eng::u32>("reuse"));
		eng::debug::DebugPeripheral::counter_name(4u, reinterpret_cast<eng::u32>("patched"));
		ENG_PROF_INIT(3u);
		m_fps.reset(app.frame(), eng::debug::prof_clock());
	}

	/// Avanza fases y parchea POS/CTL de los 40 placements; PT solo cuando rota el arte.
	void update(auto& app) {
		if constexpr (kProbe) {
			probe_update(app);
			return;
		}
		const eng::u16 f = static_cast<eng::u16>(app.frame());
		for (eng::u8 lane = 0u; lane < kLaneCount; ++lane) {
			m_x_phase[lane] = static_cast<eng::u8>(m_x_phase[lane] + kLaneFreeSpeed[lane]);
			m_y_phase[lane] = static_cast<eng::u8>(m_y_phase[lane] + 1u + (lane & 1u));
		}
		ENG_PROF_BEGIN(0u);
		const bool rotate = kRotateEnabled && (f != 0u) && ((f % kRotateFrames) == 0u);
		if (rotate) {
			m_rotation = static_cast<eng::u8>((m_rotation + 1u) % kAttachedKinds);
			// Las cinco rotaciones están preconstruidas: rotar es solo swap de COP1LC.
			if (app.device().commit_copper(m_lists[m_rotation].mem_view())) {
				m_active = m_rotation;
			}
		}
		eng::Span<eng::u16> words = m_lists[m_active].view.as_words().raw();
		eng::u16 patched = 0u;
		for (eng::u8 band = 0u; band < kBandCount; ++band) {
			for (eng::u8 lane = 0u; lane < kLaneCount; ++lane) {
				// Una X propia por banda y carril: ninguna columna queda fija.
				const eng::u8 ph = static_cast<eng::u8>(m_x_phase[lane] +
						static_cast<eng::u8>(band * 29u));
				const eng::u16 t = tri8(ph);
				// `tri8` recorre 0..127: ×2 cubre los 320 px (16..301) sin multiplicación.
				const eng::u16 x = static_cast<eng::u16>(16u + (t << 1u) + (t >> 2u));
				eng::u16 osc = static_cast<eng::u16>(tri8(static_cast<eng::u8>(
					m_y_phase[lane] + static_cast<eng::u8>(band * 17u))) >> 3u);
				if (osc > kBandOsc) osc = kBandOsc;
				const eng::u16 y = static_cast<eng::u16>(kBandLine[band] + 4u + osc);
				const eng::graphics::SpritePosCtl e_pos =
					graphics::encode_sprite_pos_ctl(
						static_cast<eng::u16>(eng::graphics::kSpriteScreenXOrigin + x),
						y, kSpriteH, false);
				const eng::graphics::SpritePosCtl e_ctl =
					graphics::encode_sprite_pos_ctl(
						static_cast<eng::u16>(eng::graphics::kSpriteScreenXOrigin + x),
						y, kSpriteH, lane < 2u);
				const eng::u16 ch = (lane < 2u) ? static_cast<eng::u16>(lane * 2u)
							       : static_cast<eng::u16>(lane + 2u);
				copper::patch_data(words, m_pos[band][ch], e_pos.pos);
				copper::patch_data(words, m_ctl[band][ch], e_ctl.ctl);
				patched = static_cast<eng::u16>(patched + 2u);
				if (lane < 2u) {
					copper::patch_data(words, m_pos[band][ch + 1u], e_pos.pos);
					copper::patch_data(words, m_ctl[band][ch + 1u],
							   static_cast<eng::u16>(e_ctl.ctl | 0x0080u));
					patched = static_cast<eng::u16>(patched + 2u);
				}
			}
		}
		ENG_PROF_END(0u);
		eng::debug::DebugPeripheral::counter_value(4u, patched);
		eng::u32 fps10 = 0u;
		if (m_fps.update(app.frame(), eng::debug::prof_clock(), fps10)) {
			eng::debug::DebugPeripheral::counter_value(0u, fps10);
		}
		eng::debug::DebugPeripheral::counter_value(1u, kPlacements);
		eng::debug::DebugPeripheral::counter_value(2u, 0xffu);
		eng::debug::DebugPeripheral::counter_value(3u, kReusePerChannel);
		const eng::u32 detail = static_cast<eng::u32>(kPlacements) |
			(static_cast<eng::u32>(kReusePerChannel) << 8u) | (0xffu << 16u);
		if (!m_ready) { m_ready = true; eng::amiga::mark_ready(detail); }
	}

	void render(auto&) {}

private:
	/// Sonda mínima de DT-045: un sprite, una banda, solo POS/CTL por frame.
	void init_probe(auto& app, auto& memory) {
		m_probe = memory.chip().template reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kFreeDmaWords) * 2u, 16u);
		if (!m_probe.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		// Rombo 16x16 de 2 planos (3 colores) con terminador, suficiente para la sonda.
		eng::u16* const w = m_probe.view.as_words().data();
		for (eng::u16 y = 0u; y < kSpriteH; ++y) {
			eng::u16 dat = 0u;
			eng::u16 datb = 0u;
			for (eng::u16 x = 0u; x < 16u; ++x) {
				const eng::s16 dx = static_cast<eng::s16>(x) - 7;
				const eng::s16 dy = static_cast<eng::s16>(y) - 7;
				const eng::s16 ax = static_cast<eng::s16>(dx < 0 ? -dx : dx);
				const eng::s16 ay = static_cast<eng::s16>(dy < 0 ? -dy : dy);
				if (static_cast<eng::s16>(ax + ay) > 7) continue;
				const eng::u8 color = static_cast<eng::u8>(1u + ((x + y) % 3u));
				const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
				if ((color & 1u) != 0u) dat = static_cast<eng::u16>(dat | bit);
				if ((color & 2u) != 0u) datb = static_cast<eng::u16>(datb | bit);
			}
			w[y * 2u] = dat;
			w[y * 2u + 1u] = datb;
		}
		w[kFreeDmaWords - 2u] = 0u;
		w[kFreeDmaWords - 1u] = 0u;
		build_background(app);
		copper::SchedulerT<false> sched {m_lists[0]};
		auto& scene = app.device().scene();
		sched.emit_planes_display(0x2c81u, 0x2cc1u, 0x0038u, 0x00d0u, kBytesPerRow,
			graphics::composition::bplcon0_for(
				kPlanes == 6u ? graphics::composition::SceneMode::Ehb
					      : graphics::composition::SceneMode::Standard,
				kPlanes),
			kPlanes, scene.chip_planes(), scene.plane_bytes());
		sched.move(copper::Register::DMACON,
			   static_cast<eng::u16>(copper::DmaSetClear | copper::DmaMaster |
						 copper::DmaCopper | copper::DmaBitplane |
						 copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		sched.wait_line(kProbeLine);
		constexpr eng::u8 kProbeChannel = 4u;
		m_probe_pos = sched.move_at(copper::sprite_pos_register(kProbeChannel), 0u);
		m_probe_ctl = sched.move_at(copper::sprite_ctl_register(kProbeChannel), 0u);
		const auto addr = m_probe.mem_view().address(0);
		(void)sched.move_at(copper::sprite_pointer_high_register(kProbeChannel),
				    static_cast<eng::u16>(addr.value >> 16));
		(void)sched.move_at(copper::sprite_pointer_low_register(kProbeChannel),
				    static_cast<eng::u16>(addr.value & 0xffffu));
		sched.wait_line(0xf8u);
		sched.end();
		if (!sched.ok()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		app.device().takeover_copper(m_lists[0].mem_view());
		eng::debug::DebugPeripheral::counter_name(0u, reinterpret_cast<eng::u32>("fps10"));
		eng::debug::DebugPeripheral::counter_name(4u, reinterpret_cast<eng::u32>("patched"));
		ENG_PROF_INIT(3u);
		m_fps.reset(app.frame(), eng::debug::prof_clock());
	}

	/// Un único par POS/CTL por frame, con barrido de X por todo el ancho.
	void probe_update(auto& app) {
		m_x_phase[0] = static_cast<eng::u8>(m_x_phase[0] + 3u);
		const eng::u16 t = tri8(m_x_phase[0]);
		const eng::u16 x = static_cast<eng::u16>(16u + (t << 1u) + (t >> 2u));
		const eng::graphics::SpritePosCtl e = graphics::encode_sprite_pos_ctl(
			static_cast<eng::u16>(eng::graphics::kSpriteScreenXOrigin + x),
			kProbeLine + 4u, kSpriteH, false);
		eng::Span<eng::u16> words = m_lists[0].view.as_words().raw();
		copper::patch_data(words, m_probe_pos, e.pos);
		copper::patch_data(words, m_probe_ctl, e.ctl);
		eng::debug::DebugPeripheral::counter_value(4u, 2u);
		eng::u32 fps10 = 0u;
		if (m_fps.update(app.frame(), eng::debug::prof_clock(), fps10)) {
			eng::debug::DebugPeripheral::counter_value(0u, fps10);
		}
		if (!m_ready) {
			m_ready = true;
			eng::amiga::mark_ready(1u | (1u << 8u) | (static_cast<eng::u32>(1u << 4u) << 16u));
		}
	}

	/// Hoja Chip para los 20 sprites libres, cada uno con su terminador 0,0.
	void build_free_sheet() {
		eng::u16* const words = m_free_sheet.view.as_words().data();
		for (eng::u16 art = 0u; art < kFreeArtCount; ++art) {
			eng::u16* const w = words + static_cast<eng::u32>(art) * kFreeDmaWords;
			for (eng::u16 y = 0u; y < kSpriteH; ++y) {
				eng::u16 dat = 0u;
				eng::u16 datb = 0u;
				for (eng::u16 x = 0u; x < 16u; ++x) {
					const eng::s16 dx = static_cast<eng::s16>(x) - 7;
					const eng::s16 dy = static_cast<eng::s16>(y) - 7;
					const eng::s16 ax = static_cast<eng::s16>(dx < 0 ? -dx : dx);
					const eng::s16 ay = static_cast<eng::s16>(dy < 0 ? -dy : dy);
					bool ink = false;
					switch (art & 3u) {
						case 0u: ink = ax + ay <= 7; break;
						case 1u: ink = ax <= 1 || ay <= 1; break;
						case 2u: ink = dy <= 0 && ax <= static_cast<eng::s16>(7 + dy); break;
						default: ink = ax <= 6 && ay <= 6 && (ax + ay >= 5); break;
					}
					if (!ink) continue;
					const eng::u8 color = static_cast<eng::u8>(1u + ((x + y + art) % 3u));
					const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
					if ((color & 1u) != 0u) dat = static_cast<eng::u16>(dat | bit);
					if ((color & 2u) != 0u) datb = static_cast<eng::u16>(datb | bit);
				}
				w[y * 2u] = dat;
				w[y * 2u + 1u] = datb;
			}
			w[kFreeDmaWords - 2u] = 0u;
			w[kFreeDmaWords - 1u] = 0u;
		}
	}

	/// Cocina UNA vez los cinco pares attached (cabecera OFF) en Chip: el PT es estático.
	void build_attached_pool() {
		constexpr eng::usize kStructBytes =
			static_cast<eng::usize>(graphics::attached_pair_structure_words(kSpriteH)) * 2u;
		for (eng::u16 kind = 0u; kind < kAttachedKinds; ++kind) {
			const eng::ChipView<eng::SpriteTag> even =
				m_cooked.mem_view().subview(static_cast<eng::u32>(kind) * 2u * kStructBytes,
							    kStructBytes);
			const eng::ChipView<eng::SpriteTag> odd =
				m_cooked.mem_view().subview(static_cast<eng::u32>(kind) * 2u * kStructBytes +
							    kStructBytes,
							    kStructBytes);
			(void)graphics::cook_attached_pair(
				eng::Span<const eng::u16> {&kAttachedArt.plane[kind][0][0],
							   static_cast<eng::usize>(4u * kSpriteH)},
				kSpriteH, graphics::kSpriteOffPos, graphics::kSpriteOffCtl, even, odd);
		}
	}

	/// Construye la lista completa (display, paleta, DMACON y 5 bandas × 8 canales) con los
	/// punteros PT del arte `rotation` ya cocidos: la lista es autocontenida y no se parchea PT
	/// sobre la que el Copper está ejecutando (eso corrompía el walk del DMA, DT-045).
	bool build_list(auto& app, eng::Block<eng::CopperTag, eng::MemoryKind::Chip>& block,
			eng::u8 rotation) {
		copper::SchedulerT<false> sched {block};
		auto& scene = app.device().scene();
		sched.emit_planes_display(0x2c81u, 0x2cc1u, 0x0038u, 0x00d0u, kBytesPerRow,
			graphics::composition::bplcon0_for(
				kPlanes == 6u ? graphics::composition::SceneMode::Ehb
					      : graphics::composition::SceneMode::Standard,
				kPlanes),
			kPlanes, scene.chip_planes(), scene.plane_bytes());
		sched.move(copper::Register::DMACON,
			   static_cast<eng::u16>(copper::DmaSetClear | copper::DmaMaster |
						 copper::DmaCopper | copper::DmaBitplane |
						 copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		for (eng::u8 band = 0u; band < kBandCount; ++band) {
			sched.wait_line(kBandLine[band]);
			for (eng::u8 ch = 0u; ch < kChannels; ++ch) {
				m_pos[band][ch] = sched.move_at(copper::sprite_pos_register(ch), 0u);
				m_ctl[band][ch] = sched.move_at(copper::sprite_ctl_register(ch), 0u);
				const auto addr = data_address<eng::Address<eng::MemoryKind::Chip>>(
					band, ch, rotation);
				(void)sched.move_at(copper::sprite_pointer_high_register(ch),
						    static_cast<eng::u16>(addr.value >> 16));
				(void)sched.move_at(copper::sprite_pointer_low_register(ch),
						    static_cast<eng::u16>(addr.value & 0xffffu));
			}
		}
		sched.wait_line(0xf8u);
		sched.end();
		return sched.ok();
	}

	/// Dirección de DATA de un placement: par attached (par/impar) o sprite libre del sheet.
	template <class Addr>
	Addr data_address(eng::u8 band, eng::u8 ch, eng::u8 rotation) const {
		if (ch < 4u) {
			const eng::u8 lane = static_cast<eng::u8>(ch >> 1u);
			const eng::u8 kind = static_cast<eng::u8>((band + rotation + lane * 2u) % kAttachedKinds);
			constexpr eng::usize kStructBytes =
				static_cast<eng::usize>(graphics::attached_pair_structure_words(kSpriteH)) * 2u;
			const eng::u8* base = reinterpret_cast<const eng::u8*>(
				m_cooked.mem_view().address(0).cptr());
			// `+4` salta la cabecera POS/CTL: el PT apunta a la DATA (DAT/DATB...).
			return Addr::from_storage(base + static_cast<eng::u32>(kind) * 2u * kStructBytes +
						  (ch & 1u) * kStructBytes + 4u);
		}
		const eng::u8 lane = static_cast<eng::u8>(ch - 4u);
		const eng::u8 art = static_cast<eng::u8>(lane * 5u + (band + rotation + lane) % 5u);
		const eng::u8* base = reinterpret_cast<const eng::u8*>(
			m_free_sheet.mem_view().address(0).cptr());
		return Addr::from_storage(base + static_cast<eng::u32>(art) * kFreeDmaWords * 2u);
	}

	/// Fondo indexado en bandas + HUD de una línea (pintado una vez, sin coste por frame).
	/// Usa solo COLOR00..07/15 para no pisar los registros de color de los sprites
	/// (COLOR16..31) cuando el playfield tiene 5 o 6 planos.
	void build_background(auto& app) {
		auto screen = app.screen();
		const eng::u8 mask = (kPlanes >= 4u) ? 15u : 7u;
		for (eng::u16 y = 0u; y < kHeight; y = static_cast<eng::u16>(y + 16u)) {
			for (eng::u16 x = 0u; x < kWidth; x = static_cast<eng::u16>(x + 32u)) {
				const eng::u8 color = static_cast<eng::u8>(
					1u + ((y / 16u * 3u + x / 32u * 5u) % mask));
				(void)screen.fill(eng::Box {static_cast<eng::s16>(x), static_cast<eng::s16>(y),
							    32u, 16u}, color);
			}
		}
		(void)eng::debug::draw_hud(screen, "224 SPRITE REUSE", kVersion, 0u, 1u);
		app.present();
	}

	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_free_sheet {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_cooked {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_probe {};
	copper::MoveSlot m_probe_pos {};
	copper::MoveSlot m_probe_ctl {};
	eng::Block<eng::CopperTag, eng::MemoryKind::Chip> m_lists[kAttachedKinds] {};
	copper::MoveSlot m_pos[kBandCount][kChannels] {};
	copper::MoveSlot m_ctl[kBandCount][kChannels] {};

	eng::u8 m_x_phase[kLaneCount] {0u, 37u, 79u, 121u, 163u, 205u};
	eng::u8 m_y_phase[kLaneCount] {0u, 29u, 61u, 97u, 139u, 181u};
	eng::u8 m_rotation = 0u;
	eng::u8 m_active = 0u;
	bool m_took_over = false;
	bool m_ready = false;
	eng::debug::HudFpsWindow m_fps {};
};

ReuseCarouselDemo g_game {};

} // namespace

int main() {
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = kPlanes;
	display.buffers = 1u;
	display.palette = kPalette;
	return eng::amiga::run_demo(g_game, display, eng::amiga::VblankWait::Active);
}
