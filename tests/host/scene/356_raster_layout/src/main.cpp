// ============================================================================
// Test HOST-356: layout de pantalla por bandas (eng::scene::RasterLayout).
// ============================================================================
//
// Compone un dual playfield 3+3 (6 planos, DBLPF) y una franja inferior de 0 planos (efectos
// copper chunky con su propia paleta), y comprueba que la materializacion emite el display
// principal y una `ModeSwitchZone` que apaga los planos y carga la paleta del tramo.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/356_raster_layout

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/mem_bank.hpp>
#include <eng/scene/display.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-356 raster layout ==\n");

	eng::u8 chip[512] {};
	eng::MemBank<eng::MemoryKind::Chip> bank {};
	bank.configure(chip, sizeof(chip), 16u);
	const auto planes = bank.reserve<eng::PlaneTag>(6u * 40u * 2u, 16u);

	eng::u16 copper[512] {};
	eng::MemoryBlock copper_block {copper, sizeof(copper), eng::MemoryKind::Chip};
	eng::copper::SchedulerT<false> sched {copper_block};

	const eng::u16 pf_pal[4] {0x000u, 0x111u, 0x222u, 0x333u};
	const eng::u16 chunky_pal[2] {0x123u, 0x456u};

	eng::scene::RasterLayout layout {};
	{
		eng::scene::Band b0 = eng::scene::band_of_planes(planes, 6u, 40u, 0u);
		b0.height = 208u;
		b0.dual_playfield = true;
		b0.palette = eng::PaletteWords {pf_pal, 4u};
		b0.palette_colors = 4u;
		layout.add(b0);
	}
	layout.add({.top = 208,
		    .height = 48,
		    .planes = 0u,
		    .color = false,
		    .palette = eng::PaletteWords {chunky_pal, 2u},
		    .palette_colors = 2u});

	check(layout.count() == 2u, "dos bandas");
	check(layout[0].bplcon0() == 0x6600u, "banda 0: 6 planos + COLOR + DBLPF");
	check(layout[0].bpl1mod == 200u, "banda 0: modulo interleaved");
	check(layout[1].bplcon0() == 0x0000u, "banda 1: 0 planos, sin COLOR");
	check(layout[1].bpl1mod == 0u, "banda 1: sin modulo");
	const auto t0 = layout[0].bob_target();
	check(t0.base == planes.view.data() && t0.planes == 3u && t0.row_bytes == 240u &&
		      t0.plane_bytes == 80u && t0.layout == eng::graphics::BobLayout::Planar,
	      "bob_target PF1 en DPF (3 planos a stride 2)");

	check(layout.materialize(sched), "materializa");
	sched.end();
	check(sched.ok(), "scheduler ok");

	const eng::u16* w = sched.data();
	const eng::u32 usage = sched.words_used();
	eng::u16 bplcon0[4] {};
	eng::u8 n_bplcon0 = 0u;
	eng::u16 last_color0 = 0xffffu;
	bool color0_seen = false;
	for (eng::u32 i = 0u; i + 1u < usage; ++i) {
		if (w[i] == 0x0100u && n_bplcon0 < 4u) { // BPLCON0 (MOVE register)
			bplcon0[n_bplcon0++] = w[i + 1u];
		}
		if (w[i] == 0x0180u) { // COLOR00
			last_color0 = w[i + 1u];
			color0_seen = true;
		}
	}
	check(n_bplcon0 == 2u, "dos BPLCON0 (display + mode switch)");
	check(bplcon0[0] == 0x6600u, "display: 6 planos DBLPF");
	check(bplcon0[1] == 0x0000u, "mode switch: planos apagados");
	check(color0_seen, "paleta del tramo emitida");
	check(last_color0 == chunky_pal[0], "COLOR00 final = paleta chunky");

	// Banda desde una superficie de scroll: refleja base + fine scroll sin campo propio.
	{
		eng::field::PlayfieldHardwareView view {};
		view.planes = 3u;
		view.bitmap_bytes_per_row = 48u;
		view.bitplanes = planes.mem_view().address(0);
		view.plane_bytes = 3u * 48u * 256u;
		view.bplcon1 = 0x0033u;
		view.bpl1mod = 0x0050u;
		view.bpl2mod = 0x0050u;
		eng::scene::RasterLayout l2 {};
		l2.add(eng::scene::band_from_view(view, 0u));
		check(l2[0].planes == 3u && l2[0].bytes_per_row == 48u && l2[0].bplcon1 == 0x0033u &&
			      l2[0].bpl1mod == 0x0050u,
		      "band_from_view refleja la superficie");
		eng::u16 cop[256] {};
		eng::MemoryBlock cb {cop, sizeof(cop), eng::MemoryKind::Chip};
		eng::copper::SchedulerT<false> s2 {cb};
		check(l2.materialize(s2), "band_from_view materializa");
		s2.end();
		bool bplcon1_ok = false;
		for (eng::u32 i = 0u; i + 1u < s2.words_used(); ++i) {
			if (cop[i] == 0x0102u) { // BPLCON1
				bplcon1_ok = (cop[i + 1u] == 0x0033u);
				break;
			}
		}
		check(bplcon1_ok, "BPLCON1 = fine scroll de la vista");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: layout por bandas (DPF + franja 0 planos) validado.\n");
	return 0;
}
