// ============================================================================
// Demo 054: SpriteAllocator — asigna canales de sprite con overflow → BOB.
// ============================================================================
//
// Demuestra el paso 4 de ENGINE_DESIGN.md §5: el `SpriteAllocator` reparte
// `SpriteIntent` entre los 8 canales hardware y decide el overflow (más de 8
// sprites en la misma franja vertical → BOB). El juego describe QUÉ quiere; el
// allocator decide CÓMO (canal) y el `SpriteManager` lo materializa en registros.
//
// Qué muestra: 9 sprites de 16×16 en una fila horizontal (y=100). Los 8 primeros
// caben en los canales 0..7 (parejas de color: rojo, verde, azul, amarillo); el
// noveno no cabe y se marca `as_bob` (no se dibuja). El número de BOBs se publica
// en `g_eng_run_status.detail`.
//
// Fondo EHB estático (6 planos) con COLOR00 navy.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/sprite_allocator.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

namespace drivers = eng::graphics::drivers;

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// 9 sprites de 16x16 en fila (x = 16, 48, ..., 272); solo caben 8 en hardware.
constexpr eng::u8  kSprites = 9;
constexpr eng::u8  kSpriteHeight = 16;
constexpr eng::u16 kWordsPerLine = 2;   // DAT + DATB
constexpr eng::u16 kInstanceWords = static_cast<eng::u16>(kSpriteHeight) * kWordsPerLine; // 32
constexpr eng::u16 kSpriteWords = static_cast<eng::u16>(kInstanceWords * kSprites);        // 288
constexpr eng::u16 kY = 100;
constexpr eng::u16 kHpos0 = 16;
constexpr eng::u16 kHposStep = 32;

// Fondo navy + parejas de color de sprite: COLOR17=rojo, COLOR21=verde,
// COLOR25=azul, COLOR29=amarillo (cada par de canales comparte su gama).
constexpr drivers::EhbPalette kBasePalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x111, 0xf00, 0x111, 0x111,   // COLOR16-19
	0x222, 0x0f0, 0x222, 0x222,   // COLOR20-23
	0x333, 0x00f, 0x333, 0x333,   // COLOR24-27
	0x444, 0xff0, 0x444, 0x444,   // COLOR28-31
}};

struct SpriteAllocatorDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005401u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(1024, 16);
		m_sprite_block = backend.memory().chip.allocate(static_cast<eng::u32>(kSpriteWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005402u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		eng::u16* sprite_data = static_cast<eng::u16*>(m_sprite_block.data);

		build_sprite_sheet(sprite_data);
		assign_channels(sprite_data);

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005403u);
			return;
		}

		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(
			g_eng_run_status,
			static_cast<eng::u32>(m_copper_words) | (static_cast<eng::u32>(m_bob_count) << 16u)
		);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		if (m_copper_ok) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void build_sprite_sheet(eng::u16* data) {
		for (eng::u8 inst = 0; inst < kSprites; ++inst) {
			for (eng::u8 line = 0; line < kSpriteHeight; ++line) {
				data[static_cast<eng::u16>(inst) * kInstanceWords + line * 2u + 0u] = 0xFFFFu; // DAT
				data[static_cast<eng::u16>(inst) * kInstanceWords + line * 2u + 1u] = 0x0000u; // DATB
			}
		}
	}

	/// Construye los `SpriteIntent`, los reparte con el `SpriteAllocator` y
	/// configura el `SpriteManager` para los que caben (el resto queda como BOB).
	void assign_channels(const eng::u16* sprite_data) {
		eng::graphics::SpriteIntent intents[kSprites] {};
		for (eng::u8 i = 0; i < kSprites; ++i) {
			intents[i].top = kY;
			intents[i].bottom = static_cast<eng::u16>(kY + kSpriteHeight - 1u);
			intents[i].hpos = static_cast<eng::u16>(kHpos0 + static_cast<eng::u16>(i) * kHposStep);
			intents[i].width_words = 1;
			intents[i].visual_index = i;
		}

		eng::graphics::SpriteSlot slots[kSprites] {};
		const eng::u8 in_hw = m_allocator.assign(intents, kSprites, slots);
		m_bob_count = static_cast<eng::u8>(kSprites - in_hw);

		for (eng::u8 i = 0; i < kSprites; ++i) {
			if (slots[i].as_bob) {
				continue;
			}
			eng::graphics::SpriteConfig cfg {};
			cfg.enabled = true;
			cfg.data = eng::Span<const eng::u16>(
				sprite_data + static_cast<eng::u16>(i) * kInstanceWords, kInstanceWords
			);
			cfg.width_words = 1;
			cfg.height = kSpriteHeight;
			cfg.hpos = intents[i].hpos;
			cfg.vstart = intents[i].top;
			cfg.vstop = intents[i].bottom;
			m_sprites.set(slots[i].channel, cfg);
		}
	}

	bool build_copper() {
		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplanes, kPlaneBytes
		);
		// Reset de los 8 sprites (puntero válido + VSTART/VSTOP=0) ANTES de habilitar
		// SPREN: así el DMA no arma con los registros basura que dejó AmigaDOS.
		const eng::uintptr sprite_base = reinterpret_cast<eng::uintptr>(m_sprite_block.data);
		for (eng::u8 c = 0; c < 8u; ++c) {
			sched.move(static_cast<eng::u16>(0x120u + c * 4u), static_cast<eng::u16>(sprite_base >> 16));   // SPRxPTH
			sched.move(static_cast<eng::u16>(0x122u + c * 4u), static_cast<eng::u16>(sprite_base & 0xffffu)); // SPRxPTL
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), 0x0000); // SPRxCTL (VSTOP=0)
			sched.move(static_cast<eng::u16>(0x140u + c * 8u), 0x0000); // SPRxPOS (VSTART=0)
		}
		sched.move(
			eng::copper::Register::DMACON,
			static_cast<eng::u16>(
				eng::copper::DmaSetClear | eng::copper::DmaMaster | eng::copper::DmaCopper |
				eng::copper::DmaBitplane | eng::copper::DmaSprite
			)
		);
		sched.emit_palette(kBasePalette.color);
		// Los sprites se programan DESPUÉS de habilitar SPREN.
		m_sprites.emit_into(sched);
		sched.wait_line(0xf8);
		sched.move(eng::copper::Register::COLOR00, 0x0000);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	eng::u8  m_bob_count = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::MemoryBlock m_sprite_block {};
	eng::graphics::SpriteAllocator m_allocator {};
	eng::graphics::SpriteManager m_sprites {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	SpriteAllocatorDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
