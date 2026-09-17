// ============================================================================
// Demo 087: rearmado HORIZONTAL de sprite (multiplexado por linea).
// ============================================================================
//
// Valida `Scheduler::emit_sprite_horizontal_rearm` / `graphics::SpriteHorizontalRearm`
// (multiplexado horizontal): UN MISMO canal de sprite reaparece VARIAS VECES en la
// MISMA linea, con imagen distinta, porque el Copper reescribe `SPRxPOS`/`SPRxCTL` y
// recarga `SPRxDATA`/`SPRxDATB` mientras el haz barre. Es la base de los fondos
// continuos tipo Risky Woods / Free Form
// (ver docs/reference/amiga/techniques/sprite-horizontal-multiplex.md).
//
// Que muestra: el canal 0 dibuja TRES tramos de 16 px en la misma linea (izquierda,
// centro, derecha), cada uno con una imagen distinta (bloque lleno, damero, rayas).
// Tres lineas delgadas a distintas Y para que se vea que el efecto se repite por
// scanline. La separacion horizontal entre usos del canal (>=24 px, regla de la
// carrera contra el haz) se cumple: 0x2a -> 0x6a -> 0xaa (~64/2 px de separacion).
//
// Fondo EHB estatico (6 planos) con COLOR00 navy para que los tramos destaquen.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/raster_intent.hpp>
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
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

/// Tres tramos del canal 0 en la MISMA línea, con `hpos` creciente (low-res px).
/// Separación ≥24 px (regla de la carrera Copper vs haz).
constexpr eng::u8  kTramos = 3;
constexpr eng::u16 kHpos[kTramos] = { 0x2au, 0x6au, 0xaau }; // 42, 106, 170 px
constexpr eng::u8  kLineas = 3;                              // 3 filas del efecto
constexpr eng::u16 kVStart = 60;                             // primera línea
constexpr eng::u16 kVStop = 61;                              // 1 línea de alto

/// Fondo navy + grises; COLOR17/18/19 se usan para los tramos (vía paleta del efecto).
constexpr drivers::EhbPalette kBasePalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0xf00, 0x0f0, 0x00f,          // COLOR16/17/18: rojo, verde, azul (cuerpo de los tramos)
	0x333,
	0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333,
}};

struct SpriteHRearmDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008701u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(2048, 16);
		m_sprite_block = backend.memory().chip.allocate_block<eng::SpriteTag>(64u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008702u);
			return;
		}
		build_sprite_seed(m_sprite_block.view.as_words());
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00008703u);
			return;
		}
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_copper_words));
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext&) {}
	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Imagen de un tramo de 16 px (1 línea): 3 patrones distintos.
	static eng::u16 patron(eng::u8 tramo) {
		switch (tramo) {
			case 0: return 0xffffu; // bloque lleno
			case 1: return 0xaaaau; // damero
			default: return 0xccccu;// rayas
		}
	}

	/// Estructura de sprite "semilla" del DMA del canal 0: POS/CTL + 1 línea
	/// (DAT+DATB) + par de fin. Sirve para que el canal tenga un `SPRxPT` válido y
	/// el DMA no lea basura; el efecto real lo hacen los rearms manuales, que
	/// reescriben POS/CTL/DATA (no el puntero).
	void build_sprite_seed(eng::Words<eng::SpriteTag> w) {
		// [0]=POS, [1]=CTL, [2]=DAT, [3]=DATB, [4]=0, [5]=0 (fin)
		w[0] = static_cast<eng::u16>((kVStart << 8) | ((kHpos[0] >> 1) & 0xffu));
		w[1] = static_cast<eng::u16>((static_cast<eng::u16>(kVStart + 1u) << 8) |
					      ((kHpos[0] & 1u) << 1));
		w[2] = 0xffffu;
		w[3] = 0x0000u;
		w[4] = 0x0000u;
		w[5] = 0x0000u;
	}

	bool build_copper() {
		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplane_block.view, kPlaneBytes
		);
		// Canal 0: puntero de DMA válido (semilla) + reset de POS/CTL, mientras el
		// DMA de sprites aún no está habilitado por esta lista.
		const eng::u32 seed = reinterpret_cast<eng::u32>(m_sprite_block.view.data());
		sched.move(0x120, static_cast<eng::u16>(seed >> 16));            // SPR0PTH
		sched.move(0x122, static_cast<eng::u16>(seed & 0xffffu));        // SPR0PTL
		sched.move(0x140, 0x0000); // SPR0POS
		sched.move(0x142, 0x0000); // SPR0CTL
		sched.move(
			eng::copper::Register::DMACON,
			static_cast<eng::u16>(
				eng::copper::DmaSetClear | eng::copper::DmaMaster | eng::copper::DmaCopper |
				eng::copper::DmaBitplane
				// Sin DmaSprite: el efecto usa el canal 0 en MODO MANUAL. Con SPREN
				// activo el DMA recarga POS/CTL/DATA de SPRxPT cada H-Blank y pisaría
				// el rearm (AHRM cap. 4: el modo manual no convive con el DMA del canal).
			)
		);
		sched.emit_palette(kBasePalette.color);

		// En cada línea del efecto, rearmar el canal 0 en los 3 hpos, con imagen
		// distinta y el mismo VSTART/VSTOP (la Y no cambia: es horizontal puro).
		eng::graphics::SpriteHorizontalRearm list[kTramos] {};
		for (eng::u8 l = 0; l < kLineas; ++l) {
			const eng::u16 v = static_cast<eng::u16>(kVStart + l * 24u);
			for (eng::u8 t = 0; t < kTramos; ++t) {
				list[t].channel = 0u;
				list[t].hpos = kHpos[t];
				list[t].vstart = v;
				list[t].vstop = static_cast<eng::u16>(v + 1u);
				list[t].data_high = patron(t);
				list[t].data_low = 0x0000u;
				list[t].attach = false;
			}
			sched.emit_sprite_horizontal_rearms(list, kTramos);
		}

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
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	SpriteHRearmDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
