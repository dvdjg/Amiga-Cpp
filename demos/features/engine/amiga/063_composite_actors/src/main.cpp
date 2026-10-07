// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/features/engine/amiga/063_composite_actors --debug   && bash ./tools/run/run-demo.sh demos/features/engine/amiga/063_composite_actors --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/features/engine/amiga/063_composite_actors --release && bash ./tools/run/run-demo.sh demos/features/engine/amiga/063_composite_actors --keep-running

// ============================================================================
// Demo 063: sprites compuestos (F1) - `eng::CompositeScene`.
// ============================================================================
//
// Tutorial: un **sprite** de juego puede estar formado por **varias partes**. Aquí cada
// personaje son tres partes con su propio `Visual` y sus offsets respecto al ancla (pies):
//
//   - **piernas**: BOB planar 16x8 con máscara y **2 frames** (animación de andar);
//   - **torso**:   BOB planar 16x8 con máscara;
//   - **cabeza**:  **Sprite HW** de 16x8 (DATA DAT/DATB en Chip), que el engine arma en el
//     canal y el Copper mueve con el personaje.
//
// El juego solo describe contenido y estado (`CompositeState`: posición, secuencia,
// espejo), avanza ticks y llama `emit`/`present`: **no ve** intents, canales, placements ni
// `SpriteManager`. La política por parte (`set_part_modes`) decide qué partes van a BOB y
// cuáles a Sprite HW; el engine degrada solo si algo no cabe.
//
// Qué muestra: dos personajes cruzando la pantalla en direcciones opuestas, con espejo
// horizontal al girar en los bordes y cambio de secuencia `idle`/`walk` cada 64 frames.
// El borrado del rastro es por caja (`scene::clear_box`) sobre un fondo liso bajo los
// personajes (requisito de `ClearRect`).
//
// API: `eng/api/api.hpp` (`CompositeScene`, `CompositeVisual`, `Box`) + el backend Amiga.

#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/scene/bobs.hpp> // `scene::clear_box` (borrado por caja)

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

// Display 320x256, 4 planos (16 colores de playfield), planos contiguos.
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;
constexpr eng::u16 kArmLine = 32u;

// Personaje: 16 px de ancho, 24 de alto; ancla en los pies (8, 24).
constexpr eng::u8  kHeroes = 2;
constexpr eng::u16 kHeroW = 16u;
constexpr eng::u16 kHeroH = 24u;
constexpr eng::s16 kFeetY = 176;
constexpr eng::u16 kWalkMinX = 20u;
constexpr eng::u16 kWalkMaxX = 300u;

// Arte planar de las partes BOB: filas de plano de 2 words (1 base + 1 guarda), 2 planos.
constexpr eng::u16 kBobRowWords = 2u;                  // 16 px -> base 1 + guarda
constexpr eng::u16 kPartRows = 8u;
constexpr eng::u16 kLegsFrameWords =
	static_cast<eng::u16>(kPartRows * 2u * kBobRowWords); // 32 words/frame
constexpr eng::u16 kLegsWords = static_cast<eng::u16>(kLegsFrameWords * 2u);
constexpr eng::u16 kTorsoWords = kLegsFrameWords;
constexpr eng::u16 kMaskWords = static_cast<eng::u16>(kPartRows * kBobRowWords); // 16
// Cabeza como Sprite HW: 8 líneas x (DAT+DATB) + terminador 0,0.
constexpr eng::u16 kHeadWords = static_cast<eng::u16>(8u * 2u + 2u);
constexpr eng::u16 kSpriteBlockWords = static_cast<eng::u16>(kHeadWords + 16u); // + resets

// Paleta: playfield (COLOR00 fondo, 01/02 partes), sprites (COLOR16-19 del par 0/1).
constexpr eng::Palette32 kPalette {{
	0x013, 0xf80, 0x58f, 0xfff, 0x222, 0x333, 0x444, 0x555,
	0x666, 0x777, 0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd,
	0x000, 0xf80, 0x58f, 0xfff, // COLOR16-19: par 0/1 (cabeza)
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
}};

/// Máscara de bits de un rango de píxeles [x0, x1] (bit 15 = píxel 0, izquierda).
[[nodiscard]] constexpr eng::u16 bits(eng::u16 x0, eng::u16 x1) {
	eng::u16 m = 0u;
	for (eng::u16 x = x0; x <= x1; ++x) {
		m = static_cast<eng::u16>(m | (0x8000u >> x));
	}
	return m;
}

struct CompositeActorsDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006301u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_bob_block = backend.memory_manager().chip().reserve<eng::BobTag>(
			static_cast<eng::u32>(kLegsWords + kTorsoWords + 2u * kMaskWords) * 2u, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteBlockWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_bob_block.valid() || !m_sprite_block.valid() ||
		    !m_copper.begin(backend.memory_manager(), 2048u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006302u);
			return;
		}
		build_art();
		build_background();
		if (!add_heroes()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006303u);
			return;
		}
		m_target = eng::graphics::make_bob_target(m_bitplane_block.mem_view_chip(),
							  kBytesPerRow, 256u, kPlanes,
							  eng::graphics::BobLayout::Planar);
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006304u);
			return;
		}
		m_copper.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, (static_cast<eng::u32>(kHeroes) << 8u) | 2u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		const eng::u16 f = context.frame.frame_index;
		eng::debug::mark_frame(g_eng_run_status, f);
		m_frame_plan.clear();

		// 1) Borra el rastro de la posición previa de cada personaje (piernas + torso).
		for (eng::u8 i = 0u; i < kHeroes; ++i) {
			(void)eng::scene::clear_box(
				m_frame_plan, m_target,
				static_cast<eng::s16>(m_x[i] - 8u),
				static_cast<eng::s16>(m_y[i] - 16u), kHeroW, 16u);
		}

		// 2) Movimiento en X con giro en los bordes (1 px/frame, sin multiplicaciones).
		for (eng::u8 i = 0u; i < kHeroes; ++i) {
			if (m_dir[i] < 0) {
				m_x[i] = static_cast<eng::u16>(m_x[i] - 1u);
				if (m_x[i] <= kWalkMinX) {
					m_dir[i] = 1;
				}
			} else {
				m_x[i] = static_cast<eng::u16>(m_x[i] + 1u);
				if (m_x[i] >= kWalkMaxX) {
					m_dir[i] = -1;
				}
			}
		}

		// 3) Avance de las animaciones + sincronización del estado del actor.
		m_scene.tick(1u);
		for (eng::u8 i = 0u; i < kHeroes; ++i) {
			auto* st = m_scene.state(m_ids[i]);
			if (st == nullptr) {
				continue;
			}
			st->x = static_cast<eng::s16>(m_x[i]);
			st->y = kFeetY;
			st->facing_left = (m_dir[i] < 0);
			// Secuencia dirigida por juego: camina 64 frames, descansa 64 (fases opuestas).
			const bool walk = (((f + i * 64u) >> 6u) & 1u) == 0u;
			if (walk != m_walking[i]) {
				m_walking[i] = walk;
				m_scene.set_sequence(m_ids[i], walk ? kSeqWalk : kSeqIdle, true);
			}
		}

		// 4) Materializa el frame: BOBs al plan, Sprite HW (cabeza) preparado para `present`.
		const auto r = m_scene.emit(m_frame_plan, m_target);
		(void)r; // diagnóstico disponible (sprites/bobs/degraded) para telemetría del juego

		// 5) Ejecuta los blits del frame (Blitter).
		(void)backend.execute_frame_plan(m_frame_plan);

		// 6) Reconstruye la copperlist (el Sprite HW se mueve cada frame) y la publica.
		if (build_copper()) {
			m_copper.install(backend);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Arte de las partes: plano 0 = valor 1 (piernas), plano 1 = valor 2 (torso); la
	/// cabeza usa DAT/DATB (valores 1..3 sobre COLOR17-19). Las máscaras siguen la forma.
	void build_art() {
		eng::Words<eng::BobTag> bobs = m_bob_block.view.as_words();
		eng::u16* legs = bobs.data();
		eng::u16* torso = bobs.data() + kLegsWords;
		eng::u16* masks = bobs.data() + kLegsWords + kTorsoWords;
		eng::u16* mask_legs = masks;
		eng::u16* mask_torso = masks + kMaskWords;
		for (eng::u16 y = 0u; y < kPartRows; ++y) {
			const eng::u16 legs_bits = (y < 6u) ? bits(3u, 6u) | bits(9u, 12u)
							    : bits(2u, 6u) | bits(9u, 13u);
			const eng::u16 legs_bits_walk = bits(1u, 5u) | bits(10u, 14u);
			const eng::u16 torso_bits = bits(2u, 13u);
			write_planar_frame(legs, 0u, y, legs_bits, 1u);       // piernas: COLOR01
			write_planar_frame(legs, 1u, y, legs_bits_walk, 1u);  // andar: patrón abierto
			write_planar_frame(torso, 0u, y, torso_bits, 2u);     // torso: COLOR02
			mask_legs[y * kBobRowWords + 0u] = legs_bits;
			mask_legs[y * kBobRowWords + 1u] = 0u;
			mask_torso[y * kBobRowWords + 0u] = torso_bits;
			mask_torso[y * kBobRowWords + 1u] = 0u;
		}

		// Cabeza: DAT/DATB en Chip. Caja de 8x8 centrada, borde claro e interior naranja;
		// dos "ojos" azules (valor 2).
		eng::Words<eng::SpriteTag> spr = m_sprite_block.view.as_words();
		for (eng::u16 y = 0u; y < 8u; ++y) {
			eng::u16 dat = 0u;
			eng::u16 datb = 0u;
			for (eng::u16 x = 4u; x <= 11u; ++x) {
				const bool border = (y == 0u || y == 7u || x == 4u || x == 11u);
				const bool eye = (y == 3u || y == 4u) && (x == 6u || x == 9u);
				const eng::u16 v = border ? 3u : (eye ? 2u : 1u);
				if ((v & 1u) != 0u) {
					dat = static_cast<eng::u16>(dat | (0x8000u >> x));
				}
				if ((v & 2u) != 0u) {
					datb = static_cast<eng::u16>(datb | (0x8000u >> x));
				}
			}
			spr[y * 2u + 0u] = dat;
			spr[y * 2u + 1u] = datb;
		}
		spr[kHeadWords - 2u] = 0u; // terminador del canal de DMA
		spr[kHeadWords - 1u] = 0u;
	}

	/// Escribe la fila `y` del frame `frame` de una parte planar de 2 planos: `shape_bits`
	/// con el valor de color `v` (1 -> COLOR01, 2 -> COLOR02).
	static void write_planar_frame(eng::u16* part, eng::u16 frame, eng::u16 y,
				       eng::u16 shape_bits, eng::u16 v) {
		eng::u16* base = part + static_cast<eng::u16>(frame) * kLegsFrameWords;
		base[y * kBobRowWords + 0u] = ((v & 1u) != 0u) ? shape_bits : 0u;
		base[y * kBobRowWords + 1u] = 0u; // guarda del barrel shifter
		base[(kPartRows + y) * kBobRowWords + 0u] = ((v & 2u) != 0u) ? shape_bits : 0u;
		base[(kPartRows + y) * kBobRowWords + 1u] = 0u;
	}

	/// Fondo: COLOR00 liso bajo los personajes y un cielo estático (estrellas + luna) por
	/// encima, fuera de las cajas de borrado.
	void build_background() {
		eng::Words<eng::PlaneTag> words = m_bitplane_block.view.as_words();
		constexpr eng::u16 kWordsPerRow = static_cast<eng::u16>(kBytesPerRow / 2u);
		constexpr eng::u16 kPlaneWords = static_cast<eng::u16>(kPlaneBytes / 2u);
		auto set_pixel = [&](eng::u8 plane, eng::u16 x, eng::u16 y) {
			words[static_cast<eng::u32>(plane) * kPlaneWords +
			      static_cast<eng::u32>(y) * kWordsPerRow + (x >> 4u)] =
				static_cast<eng::u16>(
					words[static_cast<eng::u32>(plane) * kPlaneWords +
					      static_cast<eng::u32>(y) * kWordsPerRow + (x >> 4u)] |
					(0x8000u >> (x & 15u)));
		};
		eng::u32 rng = 0x12345678u;
		for (eng::u16 i = 0u; i < 200u; ++i) {
			rng = rng * 1103515245u + 12345u;
			const eng::u16 x = static_cast<eng::u16>((rng >> 16u) & 319u);
			rng = rng * 1103515245u + 12345u;
			const eng::u16 y = static_cast<eng::u16>((rng >> 16u) % 104u);
			set_pixel(0u, x, y); // estrella: COLOR01
		}
		// Luna (COLOR02) en el cielo.
		for (eng::s16 dy = -12; dy <= 12; ++dy) {
			for (eng::s16 dx = -12; dx <= 12; ++dx) {
				if (dx * dx + dy * dy <= 144) {
					set_pixel(1u, static_cast<eng::u16>(250 + dx),
						  static_cast<eng::u16>(44 + dy));
				}
			}
		}
	}

	/// Alinea los `Visual` de las partes con los bloques Chip y describe el contenido.
	bool add_heroes() {
		eng::Words<eng::BobTag> bobs = m_bob_block.view.as_words();
		eng::WordView<eng::SpriteTag> spr =
			m_sprite_block.view.as_words().as_const();
		m_parts[0].visual.pixels = bobs.subspan(0u, kLegsWords).raw();
		m_parts[0].visual.mask = bobs.subspan(
			static_cast<eng::u16>(kLegsWords + kTorsoWords), kMaskWords).raw();
		m_parts[0].visual.w = kHeroW;
		m_parts[0].visual.h = kPartRows;
		m_parts[0].visual.bitplanes = 2u;
		m_parts[0].visual.frame_count = 2u;
		m_parts[0].visual.frame_stride = static_cast<eng::u32>(kLegsFrameWords) * 2u;
		m_parts[0].offset_y = 16;

		m_parts[1].visual.pixels = bobs.subspan(kLegsWords, kTorsoWords).raw();
		m_parts[1].visual.mask = bobs.subspan(
			static_cast<eng::u16>(kLegsWords + kTorsoWords + kMaskWords), kMaskWords).raw();
		m_parts[1].visual.w = kHeroW;
		m_parts[1].visual.h = kPartRows;
		m_parts[1].visual.bitplanes = 2u;
		m_parts[1].offset_y = 8;

		m_parts[2].visual.pixels = spr.first(kHeadWords).raw();
		m_parts[2].visual.w = kHeroW;
		m_parts[2].visual.h = 8u;
		m_parts[2].visual.bitplanes = 2u;
		m_parts[2].offset_y = 0;

		m_frames[0] = eng::graphics::CompositeFrame {
			eng::Span<const eng::u8> {m_part_frames_idle, 3u}, {}, 1u, 0u};
		m_frames[1] = eng::graphics::CompositeFrame {
			eng::Span<const eng::u8> {m_part_frames_walk_a, 3u}, {}, 4u, 0u};
		m_frames[2] = eng::graphics::CompositeFrame {
			eng::Span<const eng::u8> {m_part_frames_walk_b, 3u}, {}, 4u, 0u};
		m_seqs[kSeqIdle] = eng::graphics::CompositeSequence {
			eng::Span<const eng::graphics::CompositeFrame> {&m_frames[0], 1u}, true, 0xffu};
		m_seqs[kSeqWalk] = eng::graphics::CompositeSequence {
			eng::Span<const eng::graphics::CompositeFrame> {&m_frames[1], 2u}, true, 0xffu};

		m_vis.parts = eng::Span<const eng::graphics::CompositePart> {m_parts, 3u};
		m_vis.sequences = eng::Span<const eng::graphics::CompositeSequence> {m_seqs, 2u};
		m_vis.anchor_x = 8;
		m_vis.anchor_y = 24;
		m_vis.bounds = eng::Box {0, 0, kHeroW, kHeroH};

		m_scene.clear();
		// Política por parte: piernas y torso en formato BOB -> Blitter; cabeza en formato
		// de Sprite HW -> canal DMA (el engine la arma y el Copper la mueve).
		m_scene.set_part_modes(eng::Span<const eng::CompositeScene<kHeroes>::PartMode> {
			m_modes, 3u});
		m_x[0] = 260u;
		m_x[1] = 60u;
		m_y[0] = kFeetY;
		m_y[1] = kFeetY;
		m_dir[0] = -1;
		m_dir[1] = 1;
		for (eng::u8 i = 0u; i < kHeroes; ++i) {
			m_ids[i] = m_scene.add(m_vis, static_cast<eng::s16>(m_x[i]), kFeetY,
					       kSeqIdle, m_dir[i] < 0);
			if (m_ids[i] == eng::CompositeScene<kHeroes>::kNoId) {
				return false;
			}
		}
		return true;
	}

	/// Copperlist por frame: display, reset de los 8 canales de sprite, `SPREN`, paleta y
	/// armado del Sprite HW del sprite compuesto (`CompositeScene::present`).
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper.inactive_block() };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		// Reset de los 8 canales (PT válido + VSTART/VSTOP=0) antes de habilitar SPREN.
		const eng::uintptr sprite_base = reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		for (eng::u8 c = 0u; c < 8u; ++c) {
			sched.move(static_cast<eng::copper::Register>(0x120u + c * 4u),
				   static_cast<eng::u16>(sprite_base >> 16));
			sched.move(static_cast<eng::copper::Register>(0x122u + c * 4u),
				   static_cast<eng::u16>(sprite_base & 0xffffu));
			sched.move(static_cast<eng::copper::Register>(0x142u + c * 8u), 0u);
			sched.move(static_cast<eng::copper::Register>(0x140u + c * 8u), 0u);
		}
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		m_scene.present(sched, kArmLine); // sin placements a la vista: una llamada
		sched.wait_line(0xf8);
		sched.end();
		if (!sched.ok()) {
			return false;
		}
		m_copper.flip();
		return true;
	}

	static constexpr eng::u8 kSeqIdle = 0u;
	static constexpr eng::u8 kSeqWalk = 1u;

	eng::u8 m_part_frames_idle[3] {0u, 0u, 0u};
	eng::u8 m_part_frames_walk_a[3] {0u, 0u, 0u};
	eng::u8 m_part_frames_walk_b[3] {1u, 0u, 0u};
	eng::graphics::CompositePart m_parts[3] {};
	eng::graphics::CompositeFrame m_frames[3] {};
	eng::graphics::CompositeSequence m_seqs[2] {};
	eng::graphics::CompositeVisual m_vis {};
	eng::CompositeScene<kHeroes>::PartMode m_modes[3] {
		eng::CompositeScene<kHeroes>::PartMode::Bob,
		eng::CompositeScene<kHeroes>::PartMode::Bob,
		eng::CompositeScene<kHeroes>::PartMode::Sprite,
	};
	eng::CompositeScene<kHeroes, 8u> m_scene {};
	eng::graphics::FramePlan m_frame_plan {};
	eng::graphics::BobTarget m_target {};
	eng::copper::DoubleBuffer m_copper {};
	eng::u16 m_x[kHeroes] {};
	eng::u16 m_y[kHeroes] {};
	eng::s16 m_dir[kHeroes] {};
	bool m_walking[kHeroes] {false, false};
	eng::CompositeScene<kHeroes>::Id m_ids[kHeroes] {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::BobTag> m_bob_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	CompositeActorsDemo game {};
	eng::Engine engine { backend, game };
	// Latido del mini-SO por la IRQ de VBlank; el bucle duerme en `Wait()` sin trabajo.
	(void)eng::os::init(engine, 0u);
	engine.run_frames(0xffff);

	return 0;
}
