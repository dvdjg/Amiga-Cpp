// ============================================================================
// Demo 206 — colisión de hardware de sprites (CLXCON / CLXDAT)
// ============================================================================
//
// Un sprite sólido (16x16) se coloca SOBRE un rectángulo pintado en el bitplane 0.
// Con `CLXCON` se habilita la colisión del par 0 (sprites 0/1) con los bitplanes;
// cada frame se lee `CLXDAT` (que se autolimpia) y, si hay choque, `COLOR00` cambia
// a rojo. Es la validación en hardware de `graphics/sprite_collision.hpp` y de
// `MinimalBackend::set_sprite_collision`/`read_sprite_collision`.
//
//   bash ./tools/build/build-demo.sh demos/amiga/206_sprite_collision --debug
//   bash ./tools/run/run-demo.sh demos/amiga/206_sprite_collision --warp
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/graphics/sprite_collision.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

constexpr eng::u16 kBytesPerRow = 40;                    // 320 px / 8
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u16 kSpriteHeight = 16;
constexpr eng::u16 kSpriteWords = static_cast<eng::u16>(kSpriteHeight) * 2u + 2u; // DAT/DATB + terminador
constexpr eng::u16 kSpriteX = 160;   // hpos (par)
constexpr eng::u16 kSpriteY = 100;   // vstart (raster absoluto)
constexpr eng::u16 kRectY0 = 50;     // rectángulo ALTO: cubre la posición del sprite
constexpr eng::u16 kRectY1 = 120;

// COLOR00 fondo, COLOR01 color del rectángulo, COLOR17 color 1 del sprite (verde).
constexpr eng::Palette32 kPalette {{
	0x013, 0xf00, 0x0f0, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x111, 0x0f0, 0x111, 0x111,   // COLOR16-19 (par 0/1: color 1 = verde)
	0x222, 0x222, 0x222, 0x222,
	0x333, 0x333, 0x333, 0x333,
	0x444, 0x444, 0x444, 0x444,
}};

struct CollisionDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020601u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(1024u, 16);
		m_sprite_block = backend.memory().chip.allocate_block<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020602u);
			return;
		}

		paint_bitplane_rect();
		build_sprite();
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020603u);
			return;
		}

		backend.takeover_display(m_copper_ptr);
		// Par 0 (sprites 0/1) + bitplane 1 habilitado con **match 1**: el rectángulo es
		// color 1 (BP1=1), así que el choque registra cuando el sprite lo solapa.
		backend.set_sprite_collision(eng::graphics::SpriteCollisionConfig {0x1u, 0x01u, 0x01u});
		eng::debug::mark_ready(g_eng_run_status, 0x00020600u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		if (m_copper_ok) {
			backend.install_copper_list(m_copper_ptr);
		}
		// CLXDAT se autolimpia al leer: una lectura = el resultado del frame.
		const eng::graphics::SpriteCollisionResult c = backend.read_sprite_collision();
		m_raw = c.raw;
		// Choque del par 0 (sprites 0/1) con bitplanes pares o impares.
		const bool hit = c.odd_bpl_vs_sprite(0u) || c.even_bpl_vs_sprite(0u);
		if (hit) {
			++m_hits;
		}
		backend.set_color(0, hit ? 0xf00u : 0x013u); // COLOR00 rojo si hay choque
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void paint_bitplane_rect() {
		// Plano 0 (separado): rellenar 0xFF las filas del rectángulo -> color 1.
		eng::u8* plane0 = m_bitplane_block.view.data();
		for (eng::u16 y = kRectY0; y <= kRectY1; ++y) {
			eng::u8* row = plane0 + static_cast<eng::u32>(y) * kBytesPerRow;
			for (eng::u16 b = 0; b < kBytesPerRow; ++b) {
				row[b] = 0xffu;
			}
		}
	}

	void build_sprite() {
		eng::Words<eng::SpriteTag> data = m_sprite_block.view.as_words();
		for (eng::u16 line = 0; line < kSpriteHeight; ++line) {
			data[line * 2u + 0u] = 0xFFFFu; // DAT (color 1)
			data[line * 2u + 1u] = 0x0000u; // DATB
		}
		const eng::u16 tail = static_cast<eng::u16>(kSpriteHeight) * 2u;
		data[tail + 0u] = 0u; // terminador del canal DMA
		data[tail + 1u] = 0u;

		const eng::u16* sp = reinterpret_cast<const eng::u16*>(m_sprite_block.view.data());
		eng::graphics::SpriteConfig cfg {};
		cfg.enabled = true;
		cfg.data = eng::Span<const eng::u16> {sp, kSpriteWords};
		cfg.width_words = 1u;
		cfg.height = static_cast<eng::u8>(kSpriteHeight);
		cfg.hpos = kSpriteX;
		cfg.vstart = kSpriteY;
		cfg.vstop = static_cast<eng::u16>(kSpriteY + kSpriteHeight - 1u);
		m_sprites.set(0u, cfg);
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.view, kPlaneBytes);
		sched.move(eng::copper::Register::BPLCON2, 0x0000u); // sprites delante del playfield
		const eng::uintptr sprite_base =
			reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		for (eng::u8 c = 0; c < 8u; ++c) {
			sched.move(static_cast<eng::u16>(0x120u + c * 4u),
				   static_cast<eng::u16>(sprite_base >> 16));
			sched.move(static_cast<eng::u16>(0x122u + c * 4u),
				   static_cast<eng::u16>(sprite_base & 0xffffu));
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), 0x0000u);
			sched.move(static_cast<eng::u16>(0x140u + c * 8u), 0x0000u);
		}
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		m_sprites.emit_into(sched);
		sched.wait_line(0xf8);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u16 m_raw = 0u;
	eng::u32 m_hits = 0u;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
	eng::graphics::SpriteManager m_sprites {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	CollisionDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
