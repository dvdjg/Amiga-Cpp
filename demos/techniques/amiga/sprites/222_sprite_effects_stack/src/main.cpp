// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/220_sprite_bend --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/220_sprite_bend --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/220_sprite_bend --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/220_sprite_bend --keep-running

// ============================================================================
// Demo 222: efectos de sprites HW apilados — bending + palette splitting.
// ============================================================================
//
// Prueba de que los efectos del sistema de sprites HW son **acumulables y aplicables a
// cualquier sprite**: las dos columnas de 16x64 de la demo 220 (canales 2 y 3) se doblan
// por línea (`drivers::emit_sprite_bend_into`) y, a mitad del sprite, el Copper **conmuta
// su paleta** (`COLOR20-23` del par 2/3): la mitad inferior de las columnas cambia de
// color sin tocar el arte. Los dos efectos se emiten **intercalados por línea** en la
// misma copperlist (el cambio de color va entre las filas 70 y 71, antes de la X de la
// columna), sobre los mismos canales y con el mismo contrato de DATA (filas + terminador).
//
// - Bending: `SPRxPOS/CTL` por línea con `VSTART = línea` (rearme del DMA,
//   `../WinUAE-DBG/custom.cpp:4078-4090`).
// - Palette split: `MOVE` a `COLOR20-23` a mitad del sprite (mecanismo real de color por
//   zona; `sprite-layer.md` §3-§4).
// - Anti-*Phantom Sprite*: terminador `0,0` tras la última fila (`winuae/sprite-dma.md`).

#include <eng/api/api.hpp>
#include <eng/os/os.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/sprite_bend.hpp>
#include <eng/graphics/sprite_manager.hpp>
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
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry {
	0xffffffffu, 0, 0, 0, 0, { 0, 0, 0 }
};
}

namespace {

// Display 320x256, 4 planos (patrón 214/219).
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Dos columnas de 16x64 en sendos canales del par 2/3 (COLOR20-23).
constexpr eng::u8  kChannels[2] = {2u, 3u};
constexpr eng::u16 kHeight = 64u;
constexpr eng::u16 kBlobWords = static_cast<eng::u16>(kHeight * 2u + 2u); // filas + terminador
constexpr eng::u16 kTotalBlobWords = static_cast<eng::u16>(2u * kBlobWords);
constexpr eng::u16 kBaseY = 40u;           // primera linea de las columnas
constexpr eng::u16 kAmp = 24u;             // amplitud del seno (px)
constexpr eng::u16 kX[2] = {152u, 210u};   // X base de cada columna (>= DIWSTRT 129)
constexpr eng::u16 kArmLine = 32u;         // armado temprano (patrón 214/428)

// Tabla de offsets: `eng::SineTable<Amp, 256, s16>` (Steps potencia de dos; máscara).
constexpr eng::SineTable<static_cast<eng::s32>(kAmp), 256u, eng::s16> kSine {};

constexpr eng::Palette32 kPalette {{
	0x012, 0x023, 0x034, 0x045, 0x256, 0x467, 0x578, 0x68a,
	0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0xfff,
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x3cf, 0x8ef, 0xfff, // COLOR20-23: par 2/3
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
}};

// Paleta magenta de la mitad inferior (COLOR20..23 del par 2/3; 20 = transparente).
constexpr eng::u16 kMagenta[4] = {0x000u, 0xf0fu, 0x90fu, 0xfffu};

struct BendDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022201u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(4096u, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			(static_cast<eng::u32>(kTotalBlobWords) + 32u) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022202u);
			return;
		}

		// DATA de las columnas: nucleo brillante + borde, dither vertical de fondo.
		eng::u16* const words = m_sprite_block.view.as_words().data();
		for (eng::u8 c = 0u; c < 2u; ++c) {
			eng::u16* const blob = words + c * kBlobWords;
			for (eng::u16 y = 0u; y < kHeight; ++y) {
				eng::u16 dat = 0u;
				eng::u16 datb = 0u;
				for (eng::u16 x = 0u; x < 16u; ++x) {
					const eng::u16 adx = static_cast<eng::u16>(x < 8u ? (7u - x) : (x - 8u));
					eng::u16 v = 0u;
					if (adx <= 1u) {
						v = 3u; // núcleo
					} else if (adx == 2u) {
						v = 2u;
					} else if ((y & 1u) == (x & 1u)) {
						v = 1u; // dither del borde
					}
					if ((v & 1u) != 0u) dat = static_cast<eng::u16>(dat | (0x8000u >> x));
					if ((v & 2u) != 0u) datb = static_cast<eng::u16>(datb | (0x8000u >> x));
				}
				blob[y * 2u + 0u] = dat;
				blob[y * 2u + 1u] = datb;
			}
			blob[kBlobWords - 2u] = 0u; // terminador (anti-Phantom Sprite)
			blob[kBlobWords - 1u] = 0u;
			m_data[c] = eng::ChipView<eng::SpriteTag> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(blob),
				kBlobWords * 2u};
		}
		// Tabla de offsets runtime (copia de la tabla constexpr; una vez, en init).
		for (eng::u16 i = 0u; i < 256u; ++i) {
			m_offsets[i] = kSine[static_cast<eng::u8>(i)];
		}

		build_background(m_bitplane_block.view.as_words().data());
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022203u);
			return;
		}
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 0x00022200u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		m_phase = static_cast<eng::u8>(m_phase + 2u); // la onda viaja ~2 px/frame

		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);

		// Armado temprano de los dos canales (PT a la DATA + POS/CTL; patrón validado de
		// 214/HOST-428); el bending sobreescribe POS/CTL en cada línea.
		sched.wait_line_safe(kArmLine);
		for (eng::u8 c = 0u; c < 2u; ++c) {
			eng::graphics::SpriteManager::arm_object(sched, kChannels[c], m_data[c], kX[c],
								 kBaseY, kHeight, false);
		}
		// Bending por línea **intercalado** + **conmutación de paleta** a mitad del sprite:
		// los dos efectos se apilan sobre los mismos canales. Primera mitad (filas 0..31)
		// con la paleta base; el cambio de `COLOR20-23` se emite al terminar la fila 30
		// (efectivo desde ~la fila 31, antes de la X de la columna); segunda mitad
		// (filas 32..63) con la paleta magenta y la fase continuada (+32).
		const eng::graphics::drivers::SpriteBend bends[2] = {
			{kChannels[0], m_data[0], kX[0], m_phase},
			{kChannels[1], m_data[1], kX[1], static_cast<eng::u8>(m_phase + 128u)},
		};
		constexpr eng::u16 kHalf = static_cast<eng::u16>(kHeight / 2u); // 32 filas por mitad
		eng::graphics::drivers::emit_sprite_bend_into(
			sched, eng::Span<const eng::graphics::drivers::SpriteBend> {bends, 2u}, kBaseY,
			kHalf, eng::Span<const eng::s16> {m_offsets, 256u}, 1u);
		// Conmutación de paleta del par 2/3 (COLOR20..23), sin WAIT propio: cae justo
		// después de las escrituras de la fila anterior (la siguiente llamada del driver
		// espera la línea 72, nunca hacia atrás).
		for (eng::usize k = 0u; k < 4u; ++k) {
			sched.move(static_cast<eng::copper::Register>(0x180u + (20u + k) * 2u),
				   kMagenta[k]);
		}
		const eng::graphics::drivers::SpriteBend bends2[2] = {
			{kChannels[0], m_data[0], kX[0], static_cast<eng::u8>(m_phase + kHalf)},
			{kChannels[1], m_data[1], kX[1],
			 static_cast<eng::u8>(m_phase + 128u + kHalf)},
		};
		eng::graphics::drivers::emit_sprite_bend_into(
			sched, eng::Span<const eng::graphics::drivers::SpriteBend> {bends2, 2u},
			static_cast<eng::u16>(kBaseY + kHalf), kHalf,
			eng::Span<const eng::s16> {m_offsets, 256u}, 1u);
		sched.wait_line(0xf8);
		sched.end();
		if (sched.ok()) {
			m_copper_ptr = sched.data();
			backend.install_copper_list(m_copper_ptr);
			auto& tel = g_eng_frame_telemetry;
			tel.frame = context.frame.frame_index;
			tel.copper_words = sched.words_used();
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		// Estado inicial: canales armados con el objeto completo; el primer update añade el
		// bending (sin él, las columnas se ven rectas un frame).
		sched.wait_line_safe(kArmLine);
		for (eng::u8 c = 0u; c < 2u; ++c) {
			eng::graphics::SpriteManager::arm_object(sched, kChannels[c], m_data[c], kX[c],
								 kBaseY, kHeight, false);
		}
		sched.wait_line(0xf8);
		sched.end();
		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	void build_background(eng::u16* words) {
		constexpr eng::u16 kWordsPerRow = static_cast<eng::u16>(kBytesPerRow / 2u);
		constexpr eng::u16 kPlaneWords = static_cast<eng::u16>(kPlaneBytes / 2u);
		eng::u32 rng = 0x2545f491u;
		for (eng::u16 y = 0u; y < 256u; ++y) {
			const eng::u16 band = static_cast<eng::u16>((y >> 5u) & 7u);
			for (eng::u16 wx = 0u; wx < kWordsPerRow; ++wx) {
				rng ^= rng << 13u;
				rng ^= rng >> 17u;
				rng ^= rng << 5u;
				const eng::u16 stars =
					((rng & 0x1fu) == 0u) ? static_cast<eng::u16>(1u << ((rng >> 8u) & 15u)) : 0u;
				for (eng::u8 p = 0u; p < kPlanes; ++p) {
					const eng::u16 fill = ((band >> p) & 1u) != 0u ? 0xffffu : 0x0000u;
					words[static_cast<eng::u32>(p) * kPlaneWords +
					      static_cast<eng::u32>(y) * kWordsPerRow + wx] =
						static_cast<eng::u16>(fill | stars);
				}
			}
		}
	}

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::ChipView<eng::SpriteTag> m_data[2] {};
	eng::s16 m_offsets[256] {};
	eng::u8 m_phase = 0u;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_sprite_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	BendDemo game {};
	eng::Engine engine { backend, game };
	(void)eng::os::init(engine, 0u);
	engine.run_frames(0xffff);

	return 0;
}
