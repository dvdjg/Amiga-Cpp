// ============================================================================
// Demo 053: multiplexado de sprites hardware + color multiplexing.
// ============================================================================
//
// Valida el camino de sprites de la nueva estructura del engine:
// `SpriteTemplate` (plantilla portable con segmentos y cambios de paleta) +
// `SpriteManager::emit_template_into` (reuso vertical "chasing the raster" y
// color multiplexing por franja).
//
// Qué muestra: SEIS objetos de 16 px de ancho y altura 24, con anchura creciente
// (pirámide), servidos por UN SOLO canal de sprite hardware (canal 0). Cada
// objeto se rearma en su línea (el Copper reapunta SPRxPT/POS/CTL) y cambia su
// color `COLOR17` en esa misma línea. Es la técnica de amiga-bootcamp: el juego
// emite intenciones; el scheduler/manager materializa el hardware.
//
// Fondo EHB estático (6 planos) con COLOR00 navy para que los sprites destaquen.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/sprite.hpp>
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

// Geometría del display EHB 320x256 (igual que StaticEhbScene).
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u; // 10240
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;                    // 61440

// Sprites: 6 instancias de 16x24 (1 word de ancho, DAT+DATB intercalados).
constexpr eng::u8  kInstances = 6;
constexpr eng::u8  kSpriteHeight = 24;
constexpr eng::u16 kWordsPerLine = 2;   // DAT + DATB
constexpr eng::u16 kInstanceWords = static_cast<eng::u16>(kSpriteHeight) * kWordsPerLine; // 48
constexpr eng::u16 kSpriteWords = static_cast<eng::u16>(kInstanceWords * kInstances);     // 288
constexpr eng::u16 kBaseY = 40;         // primera línea del primer segmento
constexpr eng::u16 kSpriteHpos = 152;   // centrado: (320-16)/2

// Fondo navy (COLOR00) + grises. COLOR17 (cuerpo del sprite) se sobreescribe por
// instancia con los `SpritePaletteSwitch`.
constexpr drivers::EhbPalette kBasePalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x555, 0x555, 0x555, 0x555,
	0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333,
}};

// Seis tonos saturados (RGB444): uno por instancia.
constexpr eng::u16 kHues[kInstances] = { 0xf00, 0x0f0, 0x00f, 0xff0, 0x0ff, 0xf0f };

struct SpriteMultiplexDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005301u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(1024, 16);
		m_sprite_block = backend.memory().chip.allocate(static_cast<eng::u32>(kSpriteWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005302u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		eng::u16* sprite_data = static_cast<eng::u16*>(m_sprite_block.data);

		build_sprite_sheet(sprite_data);
		build_template(sprite_data);

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005303u);
			return;
		}

		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_copper_words));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// Rebote horizontal suave (onda triangular): los seis sprites se desplazan en
		// bloque, demostrando el reposicionado por Copper sin tocar los bitplanes.
		const eng::u16 frame = context.frame.frame_index;
		const eng::u16 period = 64u;
		const eng::u16 phase = static_cast<eng::u16>(frame & (2u * period - 1u));
		const eng::u16 t = (phase < period) ? phase : static_cast<eng::u16>(2u * period - 1u - phase);
		m_hpos = static_cast<eng::u16>(40u + t * 3u);  // 40..229 px
		if (build_copper()) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Hoja de sprites: 6 barras centradas de anchura decreciente (16..6 px), cada
	/// una de 24 líneas. El cuerpo usa DAT=1 (COLOR17) y DATB=0.
	void build_sprite_sheet(eng::u16* data) {
		for (eng::u8 inst = 0; inst < kInstances; ++inst) {
			const eng::u8 w = static_cast<eng::u8>(16u - static_cast<eng::u8>(inst) * 2u);
			const eng::u8 margin = static_cast<eng::u8>(16u - w);
			const eng::u16 body = static_cast<eng::u16>(
				(0xFFFFu >> margin) << (margin / 2u)
			);
			for (eng::u8 line = 0; line < kSpriteHeight; ++line) {
				data[static_cast<eng::u16>(inst) * kInstanceWords + line * 2u + 0u] = body;  // DAT
				data[static_cast<eng::u16>(inst) * kInstanceWords + line * 2u + 1u] = 0x0000u; // DATB
			}
		}
	}

	/// Plantilla: 6 segmentos (uno por instancia, reuso vertical del canal) y 6
	/// cambios de paleta (COLOR17 = tono de cada instancia).
	void build_template(const eng::u16* sprite_data) {
		m_template.width_words = 1;
		m_template.bitmap = eng::Span<const eng::u16>(sprite_data, kSpriteWords);
		for (eng::u8 i = 0; i < kInstances; ++i) {
			m_template.segments[i] = {
				static_cast<eng::u16>(i * kInstanceWords),
				kSpriteHeight,
				static_cast<eng::u16>(i * kSpriteHeight),
			};
			const eng::u16 line = static_cast<eng::u16>(kBaseY + i * (kSpriteHeight + 1u));
			m_template.switches[i] = { line, &kHues[i], 17, 1 };
		}
		m_template.segment_count = kInstances;
		m_template.switch_count = kInstances;
	}

	bool build_copper() {
		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplanes, kPlaneBytes
		);
		// Reset del sprite 0 (VSTART/VSTOP=0 + puntero a datos válidos) mientras su
		// DMA todavía está limpio (`emit_planes_display` lo apagó): evita que, al
		// habilitar SPREN abajo, el sprite arme con los registros basura de AmigaDOS
		// y haga una lectura DMA de un puntero inválido.
		const eng::u32 sprite_addr = reinterpret_cast<eng::u32>(m_sprite_block.data);
		sched.move(0x120, static_cast<eng::u16>(sprite_addr >> 16));   // SPR0PTH
		sched.move(0x122, static_cast<eng::u16>(sprite_addr & 0xffff)); // SPR0PTL
		sched.move(0x142, 0x0000); // SPR0CTL (VSTOP=0)
		sched.move(0x140, 0x0000); // SPR0POS (VSTART=0)
		// `emit_planes_display` no activa el DMA de sprites: lo habilitamos aquí.
		sched.move(
			eng::copper::Register::DMACON,
			static_cast<eng::u16>(
				eng::copper::DmaSetClear | eng::copper::DmaMaster | eng::copper::DmaCopper |
				eng::copper::DmaBitplane | eng::copper::DmaSprite
			)
		);
		sched.emit_palette(kBasePalette.color);
		m_sprites.emit_template_into(sched, m_template, 0, kBaseY, m_hpos);
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
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::MemoryBlock m_sprite_block {};
	eng::graphics::SpriteTemplate<kInstances, kInstances> m_template {};
	eng::graphics::SpriteManager m_sprites {};
	eng::u16 m_hpos = kSpriteHpos;   // posición horizontal animada en update()
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	SpriteMultiplexDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
