// ============================================================================
// Test HOST-355: Fast BOBs (dual playfield, copia con padding) - politica de la capa.
// ============================================================================
//
// Respalda `eng/scene/bobs.hpp::FastBobLayer`. Comprueba la DECISION por actor y frame:
// camino rapido (copia opaca `$F0` con padding) frente a la degradacion (clear del rectangulo
// previo + cookie-cut `$CA`) cuando el movimiento supera el padding o hay solape.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/355_fast_bobs

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/scene/bobs.hpp>

using eng::graphics::BlitJobKind;

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

alignas(2) eng::u8 g_fast_sheet[16384] {};
alignas(2) eng::u8 g_cookie_sheet[16384] {};
alignas(2) eng::u8 g_screen[16384] {};

// Padded: 32x16 visible + 8 px de padding por lado -> 48x32 (copia opaca interleaved).
eng::graphics::Sprite make_fast_sheet() {
	eng::graphics::Bob b {};
	b.sheet = g_fast_sheet;
	b.width = 48u;
	b.height = 32u;
	b.planes = 4u;
	b.layout = eng::graphics::BobLayout::Interleaved;
	b.draw = eng::graphics::BobDraw::Opaque;
	return eng::graphics::Sprite {b};
}

// Cookie-cut interleaved "par" (mascara+imagen) 32x16, para la degradacion.
eng::graphics::Sprite make_cookie_sheet() {
	eng::graphics::Bob b {};
	b.sheet = g_cookie_sheet;
	b.width = 32u;
	b.height = 16u;
	b.planes = 4u;
	b.layout = eng::graphics::BobLayout::Interleaved;
	b.draw = eng::graphics::BobDraw::CookieCut;
	b.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
	return eng::graphics::Sprite {b};
}

eng::graphics::BobTarget target() {
	eng::graphics::BobTarget t {};
	t.base = g_screen;
	t.row_bytes = 40u;
	t.planes = 4u;
	t.layout = eng::graphics::BobLayout::Interleaved;
	return t;
}

eng::u16 count_kind(const eng::graphics::FramePlan& plan, BlitJobKind kind) {
	eng::u16 n = 0u;
	for (eng::u16 i = 0u; i < plan.blit_job_count(); ++i) {
		if (plan.blit_job(i).kind == kind) {
			++n;
		}
	}
	return n;
}

eng::scene::FastBobLayer make_layer() {
	eng::scene::FastBobLayer l {};
	l.set_sheet(make_fast_sheet(), 8u, 8u);
	l.set_slow_sheet(make_cookie_sheet());
	return l;
}

void test_fast_path() {
	eng::scene::FastBobLayer l = make_layer();
	l.resize(2u);
	l[0] = {64, 32, 0u, true};
	l[1] = {200, 32, 0u, true};

	eng::graphics::FramePlan plan {};
	plan.clear();
	const eng::u16 drawn = l.emit(plan, target());
	check(drawn == 2u, "dos actores pintados");
	check(count_kind(plan, BlitJobKind::OrBlob) == 2u, "dos copias rapidas (OrBlob)");
	check(count_kind(plan, BlitJobKind::ClearRect) == 0u, "sin clears");
	check(count_kind(plan, BlitJobKind::MaskedBobCookieCut) == 0u, "sin cookie-cut");
	check(plan.blit_job(0).minterm == 0x00f0u, "copia opaca $F0");
	// El blit cubre el padding: origen en (x - pad) = 56, alineado a word (shift 8).
	check(plan.blit_job(0).destination.words ==
		      reinterpret_cast<eng::u16*>(g_screen + 24u * (40u * 4u) +
						  ((56u & ~15u) / 8u)),
	      "destino = (x-pad)");
	check(plan.blit_job(0).source_shift == 8u, "shift = pad (sub-word)");
}

void test_degrade_by_move() {
	eng::scene::FastBobLayer l = make_layer();
	l.resize(2u);
	l[0] = {64, 32, 0u, true};
	l[1] = {200, 32, 0u, true};
	eng::graphics::FramePlan p0 {};
	p0.clear();
	(void)l.emit(p0, target()); // primer frame: rapido

	// Mueve el actor 0 mas que el padding (12 > 8): no puede limpiar con el padding.
	l[0].x = static_cast<eng::s16>(64 + 12);
	eng::graphics::FramePlan plan {};
	plan.clear();
	const eng::u16 drawn = l.emit(plan, target());
	check(drawn == 2u, "ambos pintados");
	check(count_kind(plan, BlitJobKind::ClearRect) == 1u, "clear del area previa");
	check(count_kind(plan, BlitJobKind::MaskedBobCookieCut) == 1u, "el movido degrada a cookie-cut");
	check(count_kind(plan, BlitJobKind::OrBlob) == 1u, "el quieto sigue rapido");
}

void test_degrade_by_overlap() {
	eng::scene::FastBobLayer l = make_layer();
	l.resize(2u);
	l[0] = {64, 32, 0u, true};
	l[1] = {80, 32, 0u, true}; // padded de 48 px -> se solapan
	eng::graphics::FramePlan plan {};
	plan.clear();
	const eng::u16 drawn = l.emit(plan, target());
	check(drawn == 2u, "ambos pintados");
	check(count_kind(plan, BlitJobKind::MaskedBobCookieCut) == 2u, "solape: los dos degradan");
	check(count_kind(plan, BlitJobKind::OrBlob) == 0u, "ninguno usa copia");
}

void test_invisible_skipped() {
	eng::scene::FastBobLayer l = make_layer();
	l.resize(2u);
	l[0] = {64, 32, 0u, true};
	l[1] = {200, 32, 0u, false};
	eng::graphics::FramePlan plan {};
	plan.clear();
	check(l.emit(plan, target()) == 1u, "actor oculto no se pinta");
	check(plan.blit_job_count() == 1u, "un solo job");
}

} // namespace

int main() {
	std::printf("== HOST-355 fast bobs ==\n");
	test_fast_path();
	test_degrade_by_move();
	test_degrade_by_overlap();
	test_invisible_skipped();
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: politica de Fast BOBs (copia con padding + degradacion) validada.\n");
	return 0;
}
