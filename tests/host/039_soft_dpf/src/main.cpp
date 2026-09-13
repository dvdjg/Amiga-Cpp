// ============================================================================
// Test HOST-039: SoftDpfComposition (composición soft DPF extraída del playfield)
// ============================================================================
//
// Valida `eng::field::SoftDpfComposition` con punteros crudos: geometría, doble
// buffer, y la construcción del blit de copia del patrón (destino en el buffer
// trasero, origen con el shift del barrel shifter, módulos).

#include <cstdio>

#include <eng/field/soft_dpf.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
using eng::field::SoftDpfComposition;
using eng::field::bg_shift_for;
} // namespace

int main() {
	eng::u8 mr[1024] {}, mf[1024] {}, er[1024] {}, ef[1024] {};
	eng::u8 pattern[256] {};
	const eng::u16 row = 64u;

	SoftDpfComposition c {};
	c.configure({row, /*display_height=*/288u, /*planes=*/5u, /*parallax_plane=*/4u});
	c.bind_raw(mr, mf, er, ef);
	check(c.active() && c.double_buffered(), "activa y con doble buffer");
	check(c.display_base() == mr && c.write_base() == ef, "front=main, back=extra");

	// Blit: destino en el buffer trasero, fila del plano de parallax.
	const eng::u8* pat = pattern;
	auto job = c.make_copy_rect_job(pat, row, /*src_x=*/3u, /*src_y=*/1u,
	                                /*dest_row=*/2u, /*rows=*/4u,
	                                /*dest_byte_off=*/6u, /*words=*/10u);
	check(job.height == 4u && job.words_per_row == 10u && job.bitplane_count == 1u, "geometría del job");
	check(job.destination == reinterpret_cast<eng::u16*>(ef + (2u * 5u + 4u) * row + 6u),
	      "destino en el BACK, plano parallax");
	const auto bs = bg_shift_for(3u);
	check(job.source == reinterpret_cast<const eng::u16*>(pat + 1u * row + bs.word_bytes),
	      "origen con word del barrel shifter");
	check(job.source_shift == bs.shift, "shift del barrel shifter");
	check(job.source_modulo_bytes == static_cast<eng::s16>(row - 20u) &&
	      job.destination_modulo_bytes == static_cast<eng::s16>(row * 5u - 20u),
	      "módulos origen/destino");

	// flip: el blit pasa al bitmap principal.
	c.flip();
	auto job2 = c.make_copy_rect_job(pat, row, 3u, 1u, 2u, 4u, 6u, 10u);
	check(job2.destination == reinterpret_cast<eng::u16*>(mf + (2u * 5u + 4u) * row + 6u),
	      "tras flip: destino en el MAIN");

	// Fila completa (compatibilidad): display_height filas, dest_row 0.
	auto full = c.make_copy_job(pat, row, 3u, 0u);
	check(full.height == 288u && full.words_per_row == row / 2u, "make_copy_job (fila completa)");

	// Inactiva: parallax_plane fuera de planes -> sin doble buffer.
	SoftDpfComposition c2 {};
	c2.configure({row, 288u, /*planes=*/4u, /*parallax_plane=*/4u});
	c2.bind_raw(mr, mf, nullptr, nullptr);
	check(!c2.active() && !c2.double_buffered(), "inactiva: sin doble buffer");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: SoftDpfComposition (doble buffer + blit de copia) validado.\n");
	return 0;
}
