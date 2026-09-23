// ============================================================================
// Test HOST-231: eng::Box y adaptadores de los rectangulos del engine.
// ============================================================================
//
// Valida el tipo unico de rectangulo (`eng::Box`, 16 bits) y las conversiones a/desde los
// tipos con semantica distinta que ya existian: `field::SurfaceRect` (s32 + w/h),
// `field::ClipRect` (bordes inclusivos x1/y1) y `graphics::DirtyRect` (bordes exclusivos).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/231_box

#include <cstdio>

#include <eng/core/box.hpp>
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

} // namespace

int main() {
	// --- Helpers de Box ---
	const eng::Box a {0, 0, 10u, 10u};
	check(!a.empty(), "Box no vacio");
	check(a.right() == 9 && a.bottom() == 9, "right/bottom inclusivos");
	check(a.contains(0, 0) && a.contains(9, 9) && !a.contains(10, 0), "contains inclusivo");

	const eng::Box in = a.inset(1);
	check(in.x == 1 && in.y == 1 && in.w == 8u && in.h == 8u, "inset");
	check(eng::Box {0, 0, 0u, 5u}.empty(), "w=0 vacio");

	check(eng::overlaps(a, eng::Box {9, 9, 5u, 5u}), "overlaps (1 px)");
	check(!eng::overlaps(a, eng::Box {10, 10, 5u, 5u}), "sin overlap");

	const eng::Box i = eng::intersect(a, eng::Box {5, 5, 10u, 10u});
	check(i.x == 5 && i.y == 5 && i.w == 5u && i.h == 5u, "intersect");
	check(eng::intersect(a, eng::Box {20, 20, 4u, 4u}).empty(), "intersect vacio");

	const eng::Box m = eng::merge(eng::Box {0, 0, 4u, 4u}, eng::Box {6, 6, 4u, 4u});
	check(m.x == 0 && m.y == 0 && m.w == 10u && m.h == 10u, "merge");

	const eng::Box t = eng::translate(a, 3, -2);
	check(t.x == 3 && t.y == -2, "translate");

	check(eng::Box::from_ltrb(10, 20, 39, 59).w == 30u, "from_ltrb (inclusivo)");

	// --- Adaptadores: round-trip Box <-> cada tipo ---
	const eng::Box b {10, 20, 30u, 40u};

	const eng::field::SurfaceRect sr = eng::field::surface_rect_of(b);
	check(sr.x == 10 && sr.y == 20 && sr.w == 30u && sr.h == 40u, "Box -> SurfaceRect");
	const eng::Box b_sr = eng::field::box_of(sr);
	check(b_sr.x == b.x && b_sr.y == b.y && b_sr.w == b.w && b_sr.h == b.h,
	      "SurfaceRect -> Box");

	const eng::field::ClipRect cr = eng::field::clip_rect_of(b);
	check(cr.x0 == 10 && cr.y0 == 20 && cr.x1 == 39 && cr.y1 == 59, "Box -> ClipRect");
	const eng::Box b_cr = eng::field::box_of(cr);
	check(b_cr.x == b.x && b_cr.y == b.y && b_cr.w == b.w && b_cr.h == b.h, "ClipRect -> Box");

	const eng::graphics::DirtyRect dr = eng::graphics::dirty_rect_of(b);
	check(dr.left == 10 && dr.top == 20 && dr.right == 40 && dr.bottom == 60,
	      "Box -> DirtyRect (bordes exclusivos)");
	const eng::Box b_dr = eng::graphics::box_of(dr);
	check(b_dr.x == b.x && b_dr.y == b.y && b_dr.w == b.w && b_dr.h == b.h, "DirtyRect -> Box");

	// DirtyRect invalido -> Box vacio.
	check(eng::graphics::box_of(eng::graphics::DirtyRect {10, 10, 10, 20}).empty(),
	      "DirtyRect invalido -> Box vacio");

	if (failures == 0) {
		std::printf("OK: Box y adaptadores (SurfaceRect/ClipRect/DirtyRect) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
