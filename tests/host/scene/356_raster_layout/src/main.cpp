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
		view.real_base = planes.mem_view().address(0);
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

	// DPF de dos superficies (bg XYLimited + fg XLimited) + franja 0-bpp, por bandas.
	{
		eng::field::PlayfieldHardwareView pf1 {};
		pf1.planes = 3u;
		pf1.bitmap_bytes_per_row = 48u;
		pf1.real_base = planes.mem_view().address(0);
		pf1.plane_bytes = 3u * 48u * 256u;
		pf1.bpl1mod = 0x28u;
		pf1.bplcon1 = 0x03u;
		eng::field::PlayfieldHardwareView pf2 {};
		pf2.planes = 3u;
		pf2.bitmap_bytes_per_row = 48u;
		pf2.real_base = planes.mem_view().address(0) + 240; // segunda superficie
		pf2.plane_bytes = 3u * 48u * 256u;
		pf2.bpl1mod = 0x28u;
		pf2.bplcon1 = 0x05u;

		eng::scene::RasterLayout l3 {};
		l3.add(eng::scene::band_from_dual_view(pf1, pf2, 0u));
		l3.add({.top = 208u, .planes = 0u, .color = false});
		check(l3[0].planes == 6u && l3[0].dual_playfield, "banda DPF 3+3");
		check(l3[0].bplcon1 == 0x0053u, "BPLCON1 = nibble PF1 | PF2<<4");
		check(l3[0].bpl1mod == 0x28u, "BPL1MOD de PF1");

		eng::u16 cop[512] {};
		eng::MemoryBlock cb {cop, sizeof(cop), eng::MemoryKind::Chip};
		eng::copper::SchedulerT<false> s3 {cb};
		check(l3.materialize(s3), "materializa DPF por bandas");
		s3.end();
		bool bplcon0_ok = false;
		for (eng::u32 i = 0u; i + 1u < s3.words_used(); ++i) {
			if (cop[i] == 0x0100u) {
				bplcon0_ok = (cop[i + 1u] == 0x6600u); // 6 planos + COLOR + DBLPF
				break;
			}
		}
		check(bplcon0_ok, "BPLCON0 del DPF por bandas");
	}

	// El driver refresca la banda por frame (base/bplcon1) sin campo `scroll`.
	{
		eng::field::PlayfieldHardwareView view {};
		view.planes = 3u;
		view.bitmap_bytes_per_row = 48u;
		view.real_base = planes.mem_view().address(0);
		view.plane_bytes = 3u * 48u * 256u;
		eng::scene::Band band = eng::scene::band_from_view(view, 0u);
		view.planeaddx = 16u; // el driver avanza el scroll
		view.bplcon1 = 0x07u;
		band.update_from_view(view);
		check(band.bplcon1 == 0x07u, "update_from_view refresca bplcon1");
		eng::u16 cop[128] {};
		eng::MemoryBlock cb {cop, sizeof(cop), eng::MemoryKind::Chip};
		eng::copper::SchedulerT<false> s {cb};
		eng::scene::emit_band_pointers(s, band, 0);
		s.end();
		const eng::u32 want = static_cast<eng::u32>(planes.mem_view().address(0).value) + 16u;
		bool ptr_ok = false;
		for (eng::u32 i = 0u; i + 1u < s.words_used(); ++i) {
			if (cop[i] == 0x00e0u) { // BPL1PTH
				ptr_ok = (cop[i + 1u] == static_cast<eng::u16>(want >> 16));
				break;
			}
		}
		check(ptr_ok, "los punteros siguen al scroll");
	}

	// El split vertical (wrap del corkscrew) es una intención de banda: reemite los punteros.
	{
		eng::field::PlayfieldHardwareView view {};
		view.planes = 3u;
		view.bitmap_bytes_per_row = 48u;
		view.real_base = planes.mem_view().address(0);
		view.plane_bytes = 3u * 48u * 256u;
		eng::scene::Band band = eng::scene::band_from_view(view, 0u);
		band.split_active = true;
		band.split_line = 100u;
		band.split_base_off = 240u;
		eng::scene::RasterLayout l {};
		l.add(band);
		eng::u16 cop[256] {};
		eng::MemoryBlock cb {cop, sizeof(cop), eng::MemoryKind::Chip};
		eng::copper::SchedulerT<false> s {cb};
		check(l.materialize(s), "materializa con split");
		s.end();
		eng::u8 bpl1 = 0u;
		for (eng::u32 i = 0u; i + 1u < s.words_used(); ++i) {
			if (cop[i] == 0x00e0u) {
				++bpl1;
			}
		}
		check(bpl1 == 2u, "split: punteros reemitidos");
	}

	// Composición por bandas y driver comparten la MISMA emisión de punteros (fuente única).
	{
		eng::field::PlayfieldHardwareView view {};
		view.planes = 3u;
		view.bitmap_bytes_per_row = 48u;
		view.real_base = planes.mem_view().address(0);
		view.plane_bytes = 3u * 48u * 256u;
		view.planeaddx = 64u;
		eng::u16 a[128] {};
		eng::u16 b[128] {};
		eng::MemoryBlock ca {a, sizeof(a), eng::MemoryKind::Chip};
		eng::MemoryBlock cb {b, sizeof(b), eng::MemoryKind::Chip};
		eng::copper::SchedulerT<false> sa {ca};
		eng::copper::SchedulerT<false> sb {cb};
		const eng::scene::Band band = eng::scene::band_from_view(view, 0u);
		eng::scene::emit_band_pointers(sa, band, 0);
		eng::field::emit_view_pointers(sb, view);
		sa.end();
		sb.end();
		bool same = (sa.words_used() == sb.words_used());
		for (eng::u32 i = 0u; same && i < sa.words_used(); ++i) {
			same = (a[i] == b[i]);
		}
		check(same, "banda y driver: misma emision de punteros");
	}

	// Los BOB de una banda con scroll se compensan con el desplazamiento vertical.
	{
		eng::field::PlayfieldHardwareView view {};
		view.planes = 3u;
		view.bitmap_bytes_per_row = 48u;
		view.real_base = planes.mem_view().address(0);
		view.plane_bytes = 3u * 48u * 256u;
		view.planeaddy = 480u;
		const eng::scene::Band band = eng::scene::band_from_view(view, 0u);
		check(band.bob_target().base == planes.view.data() + 480u,
		      "bob_target compensa el scroll vertical");
	}

	// El split tambien funciona en un tramo (banda != 0).
	{
		eng::scene::RasterLayout l {};
		l.add({.top = 0u, .planes = 0u, .color = false});
		eng::field::PlayfieldHardwareView view {};
		view.planes = 3u;
		view.bitmap_bytes_per_row = 48u;
		view.real_base = planes.mem_view().address(0);
		view.plane_bytes = 3u * 48u * 256u;
		eng::scene::Band zb = eng::scene::band_from_view(view, 100u);
		zb.split_active = true;
		zb.split_line = 20u;
		zb.split_base_off = 0;
		l.add(zb);
		eng::u16 cop[512] {};
		eng::MemoryBlock cb {cop, sizeof(cop), eng::MemoryKind::Chip};
		eng::copper::SchedulerT<false> s {cb};
		check(l.materialize(s), "materializa con tramo split");
		s.end();
		eng::u8 bpl1 = 0u;
		for (eng::u32 i = 0u; i + 1u < s.words_used(); ++i) {
			if (cop[i] == 0x00e0u) {
				++bpl1;
			}
		}
		check(bpl1 == 2u, "split en tramo: punteros reemitidos");
	}

	// La cabecera es la MISMA pieza para el display y para el campo (fuente única).
	{
		eng::scene::DisplayDesc d {};
		d.bplcon0 = 0x5200u;
		d.bplcon1 = 0x0003u;
		d.bplcon2 = 0x0011u;
		d.planes = 3u;
		d.bytes_per_row = 48u;
		eng::u16 a[128] {};
		eng::u16 b[128] {};
		eng::MemoryBlock ca {a, sizeof(a), eng::MemoryKind::Chip};
		eng::MemoryBlock cb {b, sizeof(b), eng::MemoryKind::Chip};
		eng::copper::SchedulerT<false> sa {ca};
		eng::copper::SchedulerT<false> sb {cb};
		eng::scene::emit_display(sa, d, false); // solo cabecera
		eng::field::FieldHeaderConfig h {};
		h.dmacon = d.dmacon;
		h.bplcon0 = d.bplcon0;
		h.bplcon1 = d.bplcon1;
		h.bplcon2 = d.bplcon2;
		h.bpl1mod = static_cast<eng::u16>(d.bytes_per_row * (d.planes - 1u));
		h.bpl2mod = h.bpl1mod;
		h.diwstrt = d.diwstrt;
		h.diwstop = d.diwstop;
		h.ddfstrt = d.ddfstrt;
		h.ddfstop = d.ddfstop;
		eng::field::emit_field_display_header(sb, h);
		sa.end();
		sb.end();
		bool same = (sa.words_used() == sb.words_used());
		for (eng::u32 i = 0u; same && i < sa.words_used(); ++i) {
			same = (a[i] == b[i]);
		}
		check(same, "cabecera unica: display == campo");
	}

	// El fine scroll de PF1 se expone para compensar la posicion del BOB.
	{
		eng::scene::Band b {};
		b.bplcon1 = 0x0103u; // PF2 = 1, PF1 = 3
		check(b.bob_fine_scroll() == 3u, "bob_fine_scroll = nibble PF1");
		eng::scene::Band z {};
		check(z.bob_fine_scroll() == 0u, "sin fine scroll -> 0");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: layout por bandas (DPF + franja 0 planos) validado.\n");
	return 0;
}
