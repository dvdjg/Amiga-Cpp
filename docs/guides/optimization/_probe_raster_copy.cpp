// Sonda de codegen de la **copia CPU contigua** (`eng::field::Playfield::copy_rect_cpu`,
// `engine/include/eng/field/playfield.hpp`), para el doc `OPTIMIZACION_GPP_68000.md` §12.
//
// Compilar con el toolchain Amiga y revisar el .s:
//   m68k-amiga-elf-g++ -mcpu=68000 -std=gnu++23 -O2 -Iengine/include -S \
//       docs/guides/optimization/_probe_raster_copy.cpp -o out/tmp/raster68000.s
//   m68k-amiga-elf-g++ -mcpu=68020 -std=gnu++23 -O2 -Iengine/include -S \
//       docs/guides/optimization/_probe_raster_copy.cpp -o out/tmp/raster68020.s
//
// Comprobar:
//   - 68020: el bucle interno usa `move.l` (copia de 32 bits; `RasterPolicy::cpu_fast`);
//   - 68000 y 68020: sin libcalls de multiplicación/división 32 bits
//     (`__mulsi3`/`__udivsi3`) en el bucle de copia.
//
// Resultado (2026-09): tras sustituir las multiplicaciones de 32 bits del bucle
// (`p*plane_stride`, `wy*row_stride`) por `mulu16` + avance de punteros, la sonda
// compila **sin libcalls** en ambos targets (`grep -Ec '__mulsi3|__udivsi3'` = 0) y
// usa `move.l` para la copia de 32 bits.

#include <eng/field/playfield.hpp>

namespace {

eng::field::ContiguousPlayfield g_pf {};
eng::u16 g_src[512] {};
eng::u8 g_planes[8192] {};

} // namespace

/// Copia contigua (la sonda solo necesita que el compilador materialice el bucle).
extern "C" void probe_copy_rect_cpu(eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h) {
	(void)g_pf.bind_raw(g_planes, sizeof(g_planes), 64u, 64u, 4u, 256u);
	(void)g_pf.copy_rect_cpu(eng::Span<const eng::u16> {g_src, 512}, x, y, w, h, 8u, 64u, 4u);
}

/// Copia enmascarada contigua (cookie-cut CPU).
extern "C" void probe_copy_masked_cpu(eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h) {
	(void)g_pf.copy_masked_cpu(eng::Span<const eng::u16> {g_src, 512},
				   eng::Span<const eng::u16> {g_src, 128}, x, y, w, h, 8u, 64u, 4u);
}
