// ============================================================================
// Test HOST-071: `graphics::bob` — sistema de BOBs (copia de bitmap)
// ============================================================================
//
// Valida que un BOB se materializa en los `BlitJob`s correctos para la matriz de
// parametros: profundidad (3..6), layout (planar/intercalado), algoritmo de dibujo
// (OR `$FC` / cookie-cut `$CA`) y de borrado (nada / caja). Referencia de las reglas:
// `AGENTS.md` («BOB != poligono»), `amiga-bootcamp/08_graphics/blitter_programming.md`
// (minterms, *Use Case 4*) y `demoscene-repo-orig/effects/bobs3d/bobs3d.c`.
//
// Lo que se comprueba es la GEOMETRIA del job (palabras por fila, altura, modulos,
// minterm, planos): el ejecutor de hardware no se puede correr en host, pero un job mal
// construido (p. ej. altura sin los planos, o modulo del tamanio procesado) se detecta
// aqui.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/071_bob

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace {

using eng::graphics::Bob;
using eng::graphics::BobDraw;
using eng::graphics::BobErase;
using eng::graphics::BobLayout;
using eng::graphics::BobTarget;
using eng::graphics::FramePlan;
using eng::u8;
using eng::s16;

// Bitmap de pantalla ficticio (no se ejecuta nada: solo se encolan jobs).
constexpr eng::u16 kScreenRowBytes = 40;
constexpr eng::u32 kScreenPlaneBytes = 10240;
alignas(16) eng::u8 g_screen[4 * kScreenPlaneBytes];
alignas(16) eng::u8 g_sheet[4096];

BobTarget target(BobLayout layout, u8 planes = 4) {
	BobTarget t {};
	t.base = g_screen;
	t.row_bytes = kScreenRowBytes;
	t.plane_bytes = kScreenPlaneBytes;
	t.planes = planes;
	t.layout = layout;
	return t;
}

Bob make_bob(BobLayout layout, BobDraw draw, u8 planes) {
	Bob b {};
	b.sheet = g_sheet;
	b.width = 48;
	b.height = 32;
	b.planes = planes;
	b.frame_count = 4;
	b.frame_stride = 1u << 14;
	b.layout = layout;
	b.draw = draw;
	b.erase = BobErase::None;
	return b;
}

int fail(const char* what, eng::s32 got, eng::s32 want) {
	std::printf("[FAIL] %s: got=%d want=%d\n", what, (int)got, (int)want);
	return 1;
}

} // namespace

int main() {
	// --- Dibujo OR intercalado: UN blit para todos los planos -------------------
	for (eng::u8 planes = 3; planes <= 6; ++planes) {
		FramePlan plan;
		const Bob b = make_bob(BobLayout::Interleaved, BobDraw::Or, planes);
		if (!bob_draw(plan, b, 1u, 100, 64, target(BobLayout::Interleaved, planes))) {
			std::printf("[FAIL] bob_draw OR interleaved planes=%u\n", (unsigned)planes);
			return 1;
		}
		if (plan.blit_job_count() != 1u) {
			return fail("jobs OR interleaved", plan.blit_job_count(), 1);
		}
		const auto& j = plan.blit_job(0);
		if (j.kind != eng::graphics::BlitJobKind::OrBlob) return fail("kind", (int)j.kind, (int)eng::graphics::BlitJobKind::OrBlob);
		if (j.minterm != 0x00fcu) return fail("minterm OR", j.minterm, 0xfcu);
		if (!j.interleaved) return fail("interleaved flag", 0, 1);
		if (j.bitplane_count != 1u) return fail("planos en interleaved", j.bitplane_count, 1);
		if (j.height != 32u * planes) return fail("altura = alto*planos", j.height, 32 * planes);
		// 100 px -> word 96, shift 4 -> 3+1 palabras; 3*2=6 B procesados por fila.
		if (j.words_per_row != 4u) return fail("palabras/fila con shift", j.words_per_row, 4);
		if (j.source_shift != 4u) return fail("shift", j.source_shift, 4);
		if (j.destination_modulo_bytes != static_cast<eng::s16>(kScreenRowBytes - 8u)) {
			return fail("modulo destino", j.destination_modulo_bytes, kScreenRowBytes - 8);
		}
		if (j.source_modulo_bytes != 0) return fail("modulo origen (hoja densa)", j.source_modulo_bytes, 0);
	}

	// --- Dibujo cookie-cut PLANAR: N planos ------------------------------------
	{
		FramePlan plan;
		Bob b = make_bob(BobLayout::Planar, BobDraw::CookieCut, 4u);
		b.mask = g_sheet; // cookie-cut necesita mascara
		if (!bob_draw(plan, b, 0u, 32, 10, target(BobLayout::Planar))) {
			std::printf("[FAIL] bob_draw cookie-cut planar\n");
			return 1;
		}
		const auto& j = plan.blit_job(0);
		if (j.minterm != 0x00cau) return fail("minterm cookie-cut", j.minterm, 0xcau);
		if (j.bitplane_count != 4u) return fail("planos en planar", j.bitplane_count, 4);
		if (j.height != 32u) return fail("altura planar", j.height, 32);
		if (j.source_plane_stride_bytes != 32u * ((48u / 16u + 1u) * 2u)) {
			return fail("stride de plano origen", (eng::s32)j.source_plane_stride_bytes, 32 * 8);
		}
		// x=32 -> shift 0: el blit consume 3 palabras y la fila de la hoja mide 4 -> 2 B.
		if (j.source_modulo_bytes != 2) return fail("modulo origen shift 0", j.source_modulo_bytes, 2);
		if (j.interleaved) return fail("flag interleaved en planar", 1, 0);
	}

	// --- Cookie-cut intercalado: NO soportado (documentado) --------------------
	{
		FramePlan plan;
		const Bob b = make_bob(BobLayout::Interleaved, BobDraw::CookieCut, 4u);
		if (bob_draw(plan, b, 0u, 0, 0, target(BobLayout::Interleaved))) {
			std::printf("[FAIL] cookie-cut intercalado deberia rechazarse\n");
			return 1;
		}
	}

	// --- Borrado por caja: 1 job (intercalado) / N (planar) ---------------------
	{
		FramePlan plan;
		Bob b = make_bob(BobLayout::Interleaved, BobDraw::Or, 4u);
		b.erase = BobErase::ClearRect;
		if (!bob_erase(plan, b, 100, 64, target(BobLayout::Interleaved))) {
			std::printf("[FAIL] bob_erase\n");
			return 1;
		}
		if (plan.blit_job_count() != 1u) return fail("jobs clear interleaved", plan.blit_job_count(), 1);
		const auto& j = plan.blit_job(0);
		if (j.minterm != 0x00u) return fail("minterm clear", j.minterm, 0x00);
		if (j.height != 32u * 4u) return fail("altura clear", j.height, 128);
		if (plan.blit_job_count() != 1u || j.words_per_row != 3u) return fail("palabras clear", j.words_per_row, 3);
	}
	{
		FramePlan plan;
		Bob b = make_bob(BobLayout::Planar, BobDraw::Or, 4u);
		b.erase = BobErase::ClearRect;
		if (!bob_erase(plan, b, 100, 64, target(BobLayout::Planar))) {
			std::printf("[FAIL] bob_erase planar\n");
			return 1;
		}
		if (plan.blit_job_count() != 1u || plan.blit_job(0).bitplane_count != 4u) {
			return fail("jobs clear planar", plan.blit_job_count(), 1);
		}
	}

	// --- Sin borrado (aditivo) y entradas invalidas ----------------------------
	{
		FramePlan plan;
		const Bob b = make_bob(BobLayout::Interleaved, BobDraw::Or, 4u); // erase = None
		if (!bob_erase(plan, b, 10, 10, target(BobLayout::Interleaved)) || plan.blit_job_count() != 0u) {
			return fail("erase None no encola", plan.blit_job_count(), 0);
		}
		Bob bad = b;
		bad.sheet = nullptr;
		if (bob_draw(plan, bad, 0u, 0, 0, target(BobLayout::Interleaved))) {
			std::printf("[FAIL] bob sin hoja deberia fallar\n");
			return 1;
		}
		Bob bad_frame = b;
		if (bob_draw(plan, bad_frame, 9u, 0, 0, target(BobLayout::Interleaved))) {
			std::printf("[FAIL] frame fuera de rango deberia fallar\n");
			return 1;
		}
	}

	std::printf("OK: bob (OR/cookie-cut, planar/intercalado, borrado por caja; 3..6 planos).\n");
	return 0;
}
