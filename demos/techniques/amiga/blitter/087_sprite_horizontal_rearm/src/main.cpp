// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/087_sprite_horizontal_rearm --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/087_sprite_horizontal_rearm --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/087_sprite_horizontal_rearm --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/087_sprite_horizontal_rearm --keep-running

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

#include <eng/core/types/span.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/raster_intent.hpp>
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

// Geometría del display EHB 320x256 (igual que StaticEhbScene).
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

/// Cuatro tramos del canal 0 en la MISMA línea (patrón repetido, como Risky Woods).
/// `hpos` en low-res px, separación ≥24 px (regla de la carrera Copper vs haz).
constexpr eng::u8  kTramos = 4;
constexpr eng::u16 kHpos[kTramos] = { 0x30u, 0x50u, 0x70u, 0x90u }; // 48, 80, 112, 144 px
constexpr eng::u16 kHposLeft = 0x30u;  // reset al principio de cada línea
constexpr eng::u8  kLineas = 8;        // filas del efecto (banda)
constexpr eng::u16 kVStart = 60;       // primera línea del efecto
constexpr eng::u16 kSpriteHeight = 8;  // alto del sprite (líneas que cubre el efecto)

/// Fondo navy + grises; COLOR17/18/19 se usan para los tramos (vía paleta del efecto).
constexpr eng::Palette32 kBasePalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0xf00, 0x0f0, 0x00f,          // COLOR16/17/18: rojo, verde, azul (cuerpo de los tramos)
	0x333,
	0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333,
}};

struct SpriteHRearmDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
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

	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}
	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Estructura de sprite del DMA del canal 0: POS/CTL + `kSpriteHeight` líneas
	/// (DAT+DATB) + par de fin. El DMA la carga una vez y arma el sprite; el Copper
	/// **solo mueve `SPRxPOS`** por línea (patrón Risky Woods). El VSTOP-VSTART cubre
	/// la banda entera del efecto.
	void build_sprite_seed(eng::Words<eng::SpriteTag> w) {
		w[0] = static_cast<eng::u16>((kVStart << 8) | ((kHposLeft >> 1) & 0xffu)); // POS
		w[1] = static_cast<eng::u16>(
			((static_cast<eng::u16>(kVStart + kSpriteHeight) & 0xffu) << 8) |
			((kHposLeft & 1u) << 1));  // CTL: VSTOP = VSTART + alto
		eng::u16 at = 2u;
		for (eng::u8 line = 0; line < kSpriteHeight; ++line) {
			w[at++] = 0xffffu; // DAT (bloque lleno, 16 px)
			w[at++] = 0x0000u; // DATB
		}
		w[at++] = 0x0000u; // fin
		w[at++] = 0x0000u;
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplane_block.view, kPlaneBytes
		);
		// Reset del sprite 0 ANTES de habilitar SPREN (mismo orden que la 053):
		// puntero a datos válidos + POS/CTL a 0 mientras el DMA está limpio, para que
		// no arme con los registros basura de AmigaDOS al encender el canal.
		const eng::u32 seed = reinterpret_cast<eng::u32>(m_sprite_block.view.data());
		sched.move(0x120, static_cast<eng::u16>(seed >> 16));     // SPR0PTH
		sched.move(0x122, static_cast<eng::u16>(seed & 0xffffu)); // SPR0PTL
		sched.move(0x142, 0x0000); // SPR0CTL (VSTOP=0: desarmado)
		sched.move(0x140, 0x0000); // SPR0POS (VSTART=0)
		sched.move(
			eng::copper::Register::DMACON,
			static_cast<eng::u16>(
				eng::copper::DmaSetClear | eng::copper::DmaMaster | eng::copper::DmaCopper |
				eng::copper::DmaBitplane | eng::copper::DmaSprite
			)
		);
		sched.emit_palette(kBasePalette.color);

		// Por cada línea de la banda, REARMAR el canal 0 en cada tramo (POS+CTL+DATA):
		// a diferencia de solo-POS, escribir DATA arma el sprite tras el WAIT, que es lo
		// que la 053 demuestra que funciona con su `emit_config` (POS+CTL+PT).
		eng::graphics::SpriteHorizontalRearm list[kTramos] {};
		for (eng::u8 l = 0; l < kLineas; ++l) {
			const eng::u16 v = static_cast<eng::u16>(kVStart + l);
			for (eng::u8 t = 0; t < kTramos; ++t) {
				list[t].channel = 0u;
				list[t].hpos = kHpos[t];
				list[t].vstart = v;
				list[t].vstop = static_cast<eng::u16>(v + 1u);
				list[t].data_high = 0xffffu; // bloque lleno de 16 px
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

	eng::amiga::AmigaBackend backend {};
	SpriteHRearmDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
