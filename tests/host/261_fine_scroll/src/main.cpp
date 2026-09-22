// ============================================================================
// Test HOST-261: `eng::effects::FineScroll` — scroll horizontal fino (1 px/frame)
// ============================================================================
//
// Valida en host (sin Amiga) la logica pura del helper promovido desde la demo 210:
//   1) `attach` rechaza configuracion invalida y fija la columna inicial.
//   2) Geometria del buffer (`row_bytes`) y del fetch (`DDFSTRT = $30`).
//   3) Cadencia de **1 px/frame**: `step()` es `false` 15 veces y `true` en el cruce de
//      word (cada 16 pasos); `column()` avanza exactamente 1 por cruce.
//   4) Secuencia de `BPLCON1 = (16 - fine) & 15` y su wrap.
//   5) Geometria de los `BlitJob` de desplazamiento y de columna entrante.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/261_fine_scroll

#include <cstdio>

#include <eng/api/effects.hpp>
#include <eng/core/types.hpp>

namespace {

using eng::u16;
using eng::u32;

alignas(16) u16 g_plane[21u * 8u];
alignas(16) u16 g_col[8u];

unsigned g_fail = 0;
#define CHECK(cond, msg)                                                          \
	do {                                                                      \
		if (!(cond)) {                                                    \
			std::printf("FAIL: %s (linea %d)\n", msg, __LINE__);      \
			++g_fail;                                                 \
		}                                                                 \
	} while (0)

eng::effects::FineScroll make_scroll() {
	eng::effects::FineScroll s;
	CHECK(s.attach({.plane = g_plane, .rows = 8u, .visible_words = 20u}),
	      "attach valido");
	return s;
}

void test_attach_validation() {
	eng::effects::FineScroll s;
	CHECK(!s.attach({.plane = nullptr, .rows = 8u, .visible_words = 20u}),
	      "plane nulo -> false");
	CHECK(!s.attach({.plane = g_plane, .rows = 0u, .visible_words = 20u}),
	      "rows 0 -> false");
	CHECK(!s.attach({.plane = g_plane, .rows = 8u, .visible_words = 0u}),
	      "visible_words 0 -> false");
}

void test_geometry() {
	eng::effects::FineScroll s = make_scroll();
	CHECK(s.row_bytes() == 42u, "row_bytes = (20 + 1) * 2 = 42");
	CHECK(eng::effects::FineScroll::ddfstrt() == 0x0030u, "DDFSTRT = $30 (1 word extra)");
	CHECK(!eng::effects::FineScroll::ring_wrap(), "el helper hace shift, no ring");
	CHECK(s.column() == 20u, "columna inicial = visible_words = 20");
	CHECK(s.bplcon1() == 0u, "fine=0 -> BPLCON1=0");
}

/// **1 px/frame**: `step()` avanza el fine; el buffer solo cambia cada 16 px (cruce de word).
void test_step_cadence() {
	eng::effects::FineScroll s = make_scroll();
	unsigned wraps = 0;
	for (unsigned i = 1u; i <= 32u; ++i) {
		const bool wrapped = s.step();
		const bool expect = (i % 16u) == 0u;
		CHECK(wrapped == expect, "step() true solo al cruzar 16 px");
		if (wrapped) {
			++wraps;
		}
	}
	CHECK(wraps == 2u, "dos cruces en 32 pasos (1 px/frame)");
	CHECK(s.column() == 22u, "column() avanza 1 por cruce (20 -> 22)");
}

void test_bplcon1_sequence() {
	eng::effects::FineScroll s = make_scroll();
	for (unsigned fine = 1u; fine <= 15u; ++fine) {
		s.step();
		CHECK(s.bplcon1() == static_cast<u16>(16u - fine),
		      "BPLCON1 = (16 - fine) & 15");
	}
	s.step(); // fine vuelve a 0 tras el cruce
	CHECK(s.bplcon1() == 0u, "fine=16 -> wrap a 0");
}

void test_jobs() {
	eng::effects::FineScroll s = make_scroll();
	const eng::graphics::BlitJob sh = s.shift_job();
	CHECK(sh.kind == eng::graphics::BlitJobKind::CopyRect, "shift: CopyRect");
	CHECK(sh.source.words == g_plane + 1u && sh.destination.words == g_plane,
	      "shift: src = plane+1, dst = plane");
	CHECK(sh.words_per_row == 20u && sh.height == 8u, "shift: 20 x 8");
	CHECK(sh.source_modulo_bytes == 2 && sh.destination_modulo_bytes == 2,
	      "shift: modulos = 2 (contiguo)");
	CHECK(sh.bitplane_count == 1u, "shift: 1 plano");

	const eng::graphics::BlitJob col = s.column_job(g_col);
	CHECK(col.source.words == g_col && col.destination.words == g_plane + 20u,
	      "col: src = col, dst = plane + visible_words");
	CHECK(col.words_per_row == 1u && col.height == 8u, "col: 1 x 8");
	CHECK(col.source_modulo_bytes == 0, "col: modulo de A = 0");
	CHECK(col.destination_modulo_bytes == 40,
	      "col: modulo de D = row_bytes - 2 = 40");
	CHECK(col.bitplane_count == 1u, "col: 1 plano");
}

} // namespace

int main() {
	test_attach_validation();
	test_geometry();
	test_step_cadence();
	test_bplcon1_sequence();
	test_jobs();
	if (g_fail == 0u) {
		std::printf("OK: effects::FineScroll (scroll fino 1 px/frame).\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
