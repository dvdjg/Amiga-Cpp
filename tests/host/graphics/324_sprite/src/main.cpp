// ============================================================================
// Test HOST-324: sprite cocinado (eng::graphics::Sprite).
// ============================================================================
//
// Respalda `eng/graphics/sprite_asset.hpp`: el envoltorio de juego sobre `graphics::bob`.
// Comprueba que `Sprite` reenvía la geometría del `Bob` y que `draw`/`erase` producen los
// `BlitJob`s correctos en el `FramePlan` (planar cookie-cut, planar OR, interleaved OR y
// borrado por caja), con el desplazamiento fino y el rechazo de frame fuera de rango. La
// geometría de fondo (hoja, módulos, mintern) ya está cubierta por HOST-072; aquí se
// valida la capa `Sprite` que la expone.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/324_sprite

#include <cstdio>

#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/sprite_asset.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

eng::u16 g_sheet[256] {};
eng::u16 g_mask[256] {};
eng::u16 g_dest[256] {};

eng::graphics::Bob make_bob(eng::graphics::BobLayout layout, eng::graphics::BobDraw draw,
			    eng::u32 frame_stride) {
	eng::graphics::Bob b {};
	b.sheet = reinterpret_cast<const eng::u8*>(g_sheet);
	b.mask = reinterpret_cast<const eng::u8*>(g_mask);
	b.width = 32u;
	b.height = 16u;
	b.planes = 3u;
	b.frame_count = 2u;
	b.frame_stride = frame_stride;
	b.layout = layout;
	b.draw = draw;
	b.erase = eng::graphics::BobErase::ClearRect;
	return b;
}

const eng::graphics::BobTarget kTarget {
	reinterpret_cast<eng::u8*>(g_dest), 8u, 64u, 4u, eng::graphics::BobLayout::Planar};

} // namespace

int main() {
	std::printf("== HOST-324 sprite ==\n");

	// --- Geometría expuesta por `Sprite` ------------------------------------
	{
		const eng::graphics::Sprite s {make_bob(eng::graphics::BobLayout::Planar,
						       eng::graphics::BobDraw::CookieCut, 0u)};
		check(s.valid(), "sprite valido");
		check(s.width() == 32u && s.height() == 16u, "ancho/alto");
		check(s.planes() == 3u && s.frames() == 2u, "planos/frames");
		check(s.layout() == eng::graphics::BobLayout::Planar, "layout");
		check(s.draw_mode() == eng::graphics::BobDraw::CookieCut, "modo");

		eng::graphics::Sprite empty {};
		check(!empty.valid(), "sprite sin hoja no valido");
	}

	// --- Planar cookie-cut + borrado por caja -------------------------------
	{
		const eng::graphics::Sprite s {make_bob(eng::graphics::BobLayout::Planar,
						       eng::graphics::BobDraw::CookieCut, 0u)};
		eng::graphics::FramePlan plan;
		check(s.draw(plan, kTarget, 0u, 16, 0), "draw planar cookie-cut");
		check(plan.blit_job_count() == 1u, "1 job");
		const eng::graphics::BlitJob& j = plan.blit_job(0u);
		check(j.kind == eng::graphics::BlitJobKind::MaskedBobCookieCut, "job cookie-cut");
		check(j.words_per_row == 2u, "2 palabras (sin shift)");
		check(j.height == 16u && j.bitplane_count == 3u, "una fila por plano de bitplane");
		check(j.minterm == 0x00cau, "minterm $CA");

		eng::graphics::FramePlan plan2;
		check(s.erase(plan2, kTarget, 16, 0), "erase");
		check(plan2.blit_job_count() == 1u, "erase -> 1 job");
		check(plan2.blit_job(0u).minterm == 0x00u, "erase minterm $0");
	}

	// --- Planar OR con desplazamiento fino ----------------------------------
	{
		const eng::graphics::Sprite s {make_bob(eng::graphics::BobLayout::Planar,
						       eng::graphics::BobDraw::Or, 0u)};
		eng::graphics::FramePlan plan;
		check(s.draw(plan, kTarget, 0u, 20, 0), "draw planar OR");
		const eng::graphics::BlitJob& j = plan.blit_job(0u);
		check(j.kind == eng::graphics::BlitJobKind::OrBlob, "job OR");
		check(j.source_shift == 4u, "shift = x & 15");
		check(j.words_per_row == 3u, "3 palabras (2 + guarda del shift)");
		check(j.minterm == 0x00fcu, "minterm $FC");
	}

	// --- Interleaved OR (1 job, altura = alto x planos) ---------------------
	{
		const eng::graphics::Sprite s {make_bob(eng::graphics::BobLayout::Interleaved,
						       eng::graphics::BobDraw::Or, 0u)};
		const eng::graphics::BobTarget t {reinterpret_cast<eng::u8*>(g_dest), 8u, 0u, 3u,
						  eng::graphics::BobLayout::Interleaved};
		eng::graphics::FramePlan plan;
		check(s.draw(plan, t, 1u, 0, 0), "draw interleaved OR");
		const eng::graphics::BlitJob& j = plan.blit_job(0u);
		check(j.interleaved, "job interleaved");
		check(j.height == 48u && j.bitplane_count == 1u, "alto = 16 x 3, 1 plano-logico");
	}

	// --- Frame fuera de rango / cookie-cut interleaved no soportado --------
	{
		const eng::graphics::Sprite s {make_bob(eng::graphics::BobLayout::Planar,
						       eng::graphics::BobDraw::Or, 0u)};
		eng::graphics::FramePlan plan;
		check(!s.draw(plan, kTarget, 9u, 0, 0), "frame fuera de rango -> false");
		check(plan.blit_job_count() == 0u, "sin job");

		const eng::graphics::Sprite cc {make_bob(eng::graphics::BobLayout::Interleaved,
							eng::graphics::BobDraw::CookieCut, 0u)};
		const eng::graphics::BobTarget t {reinterpret_cast<eng::u8*>(g_dest), 8u, 0u, 3u,
						  eng::graphics::BobLayout::Interleaved};
		eng::graphics::FramePlan plan2;
		check(!cc.draw(plan2, t, 0u, 0, 0), "cookie-cut interleaved no soportado -> false");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Sprite (geometria, draw/erase, planar/interleaved) validado.\n");
	return 0;
}
