// Lanzar:
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/209_free_scroll --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/209_free_scroll --keep-running

// ============================================================================
// Demo 209 — cota del scroll de fondo NO repetitivo SOLO con Copper
// ============================================================================
//
// Un canal de sprite muestra en cada linea su MISMA DAT/DATB (16 px): reposicionar la POS repite
// ese contenido. Para columnas DISTINTAS hay que reescribir tambien el dato (DAT+DATB = 2 MOVE)
// ademas de la POS (1 MOVE) = 3 MOVE por columna. Con 8 canales y kCols columnas se ciclan los
// canales (ch = c % 8). El objetivo es MEDIR cuantas columnas distintas caben antes de saturar la
// linea (presupuesto de Copper: cada MOVE/WAIT = 8 px lo-res). Ver README.md.

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

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u16 kBandTop = 48;
constexpr eng::u16 kBandLines = 80;
constexpr eng::u8  kChannels = 8;
constexpr eng::u16 kColWidth = 16;
constexpr eng::u16 kDisplayX0 = 128;
constexpr eng::u16 kDisplayW = 320;
constexpr eng::u16 kCuGap = 56;            // head-start del WAIT
constexpr eng::u32 kCuBytes = 128u * 1024u;

// Columnas DISTINTAS a dibujar con el Copper (cota: 3 MOVE/columna). 20 = 320 px (deberia saturar);
// 18 = 288 px (cota esperada). Ajustar para medir.
constexpr eng::u16 kCols = 20;

// COLOR00 fondo navy; colores 1/2/3 de cada par de sprite distintos (rojo/verde/azul).
constexpr eng::Palette32 kPalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x000, 0x630, 0x950, 0xc60, 0xfc0, 0xff0, 0xcf0, 0x8f0, // 16-23 (arcoiris)
	0x0f0, 0x0f8, 0x0cf, 0x09f, 0x60f, 0x90f, 0xc0f, 0xf0f, // 24-31
}};

[[nodiscard]] constexpr eng::u16 sprite_pos(eng::u16 vstart, eng::u16 x) {
	return static_cast<eng::u16>(((vstart & 0xffu) << 8u) | ((x >> 1u) & 0xffu));
}
[[nodiscard]] constexpr eng::u16 sprite_ctl(eng::u16 vstart, eng::u16 vstop, eng::u16 x, bool attach) {
	return static_cast<eng::u16>(((vstop & 0xffu) << 8u) | (attach ? 0x0080u : 0u) |
				     (((vstart >> 8u) & 0x1u) << 2u) | (((vstop >> 8u) & 0x1u) << 1u) |
				     (x & 0x1u));
}

// Valor 0..15 (15 colores + transparente) del pixel de la columna `c`, linea `l`. Patron diagonal
// con desfase por columna -> cada columna DISTINTA (fondo no repetitivo); 16 colores via par attached.
[[nodiscard]] constexpr eng::u16 col_value(eng::u16 px, eng::u16 c, eng::u16 l) {
	return static_cast<eng::u16>((px + l + c * 7u) & 15u);
}

struct FreeScrollDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// 384K chip (bitplanes + copper 128K + sprites), 8K slow (Bogo), 8K fast.
		if (!backend.configure_memory({ 384u * 1024u, 8u * 1024u, 8u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020901u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(kCuBytes, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(400u * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020902u);
			return;
		}
		// Estructura "off" (VSTART=VSTOP) para el SPRxPT de todos los canales: el Copper escribe
		// su POS/DATA por linea; asi el DMA no los auto-arma con basura (columna fantasma, §2.5).
		eng::u16* off = m_sprite_block.view.as_words().data();
		for (eng::u16 i = 0; i < 400u; ++i) { off[i] = static_cast<eng::u16>(0xfe00u); }

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020906u);
			return;
		}
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u16>(kCols * 16u));
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext&) {}
	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		// Registros de display: BPLCON0/BPLCON1/DIWSTRT/DIWSTOP; 40 B/fila, 4 planos, 320x256.
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		const eng::uintptr spr_base = reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		for (eng::u8 c = 0; c < kChannels; ++c) {
			sched.move(static_cast<eng::u16>(0x120u + c * 4u), static_cast<eng::u16>(spr_base >> 16));
			sched.move(static_cast<eng::u16>(0x122u + c * 4u), static_cast<eng::u16>(spr_base & 0xffffu));
			sched.move(static_cast<eng::u16>(0x140u + c * 8u), 0xfe00u); // POS off
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), 0xfe00u); // CTL off
		}
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);

		const eng::u16 bottom = static_cast<eng::u16>(kBandTop + kBandLines);
		for (eng::u16 line = kBandTop; line < bottom; ++line) {
			// Carrera contra el haz: un solo WAIT al inicio (sin WAITs intermedios; presupuesto).
			const eng::s32 w0 = static_cast<eng::s32>(kDisplayX0) - static_cast<eng::s32>(kCuGap);
			if (w0 > 0) {
				sched.wait_position_safe(line, static_cast<eng::u8>((static_cast<eng::u16>(w0) >> 1u) & 0xfeu));
			}
			for (eng::u16 col = 0; col < kCols; ++col) {
				const eng::u8 p = static_cast<eng::u8>(col % 4u);       // par (0..3)
				const eng::u8 che = static_cast<eng::u8>(p * 2u);       // canal par (bits 0-1)
				const eng::u8 cho = static_cast<eng::u8>(p * 2u + 1u);  // canal impar (bits 2-3, ATTACH)
				const eng::u16 x = static_cast<eng::u16>(kDisplayX0 + col * kColWidth);
				eng::u16 de = 0u, dbe = 0u, do_ = 0u, dbo = 0u;
				for (eng::u16 px = 0; px < kColWidth; ++px) {
					const eng::u16 v = col_value(px, col, static_cast<eng::u16>(line - kBandTop));
					if ((v & 1u) != 0u) { de = static_cast<eng::u16>(de | (0x8000u >> px)); }
					if ((v & 2u) != 0u) { dbe = static_cast<eng::u16>(dbe | (0x8000u >> px)); }
					if ((v & 4u) != 0u) { do_ = static_cast<eng::u16>(do_ | (0x8000u >> px)); }
					if ((v & 8u) != 0u) { dbo = static_cast<eng::u16>(dbo | (0x8000u >> px)); }
				}
				// Par *attached* (15 colores): CTL+POS+DAT+DATB por canal (8 MOVE/columna).
				sched.move(static_cast<eng::u16>(0x142u + che * 8u), sprite_ctl(line, bottom, x, false));
				sched.move(static_cast<eng::u16>(0x140u + che * 8u), sprite_pos(line, x));
				sched.move(static_cast<eng::u16>(0x142u + cho * 8u), sprite_ctl(line, bottom, x, true));
				sched.move(static_cast<eng::u16>(0x140u + cho * 8u), sprite_pos(line, x));
				sched.move(static_cast<eng::u16>(0x146u + che * 8u), dbe);
				sched.move(static_cast<eng::u16>(0x144u + che * 8u), de);
				sched.move(static_cast<eng::u16>(0x146u + cho * 8u), dbo);
				sched.move(static_cast<eng::u16>(0x144u + cho * 8u), do_);
			}
		}
		// Reset tras la banda: canales desarmados (VSTART=VSTOP) -> evita el fantasma por debajo.
		for (eng::u8 c = 0; c < kChannels; ++c) {
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), 0xfe00u);
			sched.move(static_cast<eng::u16>(0x140u + c * 8u), 0xfe00u);
		}
		sched.wait_line(0xf8);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_copper_ok = false;
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
	FreeScrollDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
