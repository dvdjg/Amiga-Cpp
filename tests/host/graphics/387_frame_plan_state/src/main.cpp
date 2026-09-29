// ============================================================================
// Test HOST-387: FramePlan::sort_by_state — agrupacion de blits por estado (opt-in).
// ============================================================================
//
// Respalda `FramePlan::sort_by_state` (`eng/graphics/frame_plan.hpp`): reordena los `BlitJob` de
// forma ESTABLE para que los de mismo estado comun del Blitter (kind/minterm/shift/mods/layout)
// queden adyacentes (rachas -> el backend omite reprogramaciones). Verifica:
//   (1) es una PERMUTACION (mismos jobs, mismo multiconjunto);
//   (2) los del mismo estado quedan contiguos;
//   (3) el orden relativo dentro de un grupo se conserva (sort estable);
//   (4) con todos distinto estado, un solo pase no los mezcla de forma incorrecta.
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/graphics/387_frame_plan_state

#include <cstdio>

#include <eng/graphics/frame_plan.hpp>

using eng::graphics::BlitJob;
using eng::graphics::BlitJobKind;
using eng::graphics::FramePlan;

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

/// Buffer estatico para dar punteros validos (add_blit_job los exige).
alignas(16) eng::u16 g_buf[512] {};

/// Job minimo con un estado comun distinguible y un id (modulo) para trazar.
BlitJob make_job(BlitJobKind kind, eng::u8 shift, eng::u8 minterm, eng::s16 id) {
	BlitJob j {};
	j.kind = kind;
	j.source_shift = shift;
	j.minterm = minterm;
	j.words_per_row = 4u;
	j.height = 8u;
	j.bitplane_count = 1u;
	j.source_modulo_bytes = 0;
	j.destination_modulo_bytes = static_cast<eng::s16>(40 - 8 + id); // id hace el estado unico
	j.source = eng::graphics::BlitPtr::from_storage(g_buf);
	j.destination = eng::graphics::BlitPtr::from_storage(g_buf);
	j.interleaved = true; // permite strides 0 (la validacion de add_blit_job lo exige)
	if (kind == BlitJobKind::MaskedBobCookieCut) {
		j.mask = eng::graphics::BlitPtr::from_storage(g_buf);
	}
	return j;
}

bool same_state(const BlitJob& a, const BlitJob& b) {
	return a.kind == b.kind && a.minterm == b.minterm && a.source_shift == b.source_shift &&
	       a.destination_modulo_bytes == b.destination_modulo_bytes && a.interleaved == b.interleaved;
}

} // namespace

int main() {
	std::printf("== HOST-387 frame_plan state ==\n");

	// Construye un plan con estados intercalados: A B A C B A.
	FramePlan plan {};
	plan.clear();
	BlitJob a0 = make_job(BlitJobKind::OrBlob, 0u, 0xfcu, 0);
	BlitJob b0 = make_job(BlitJobKind::MaskedBobCookieCut, 4u, 0xcau, 1);
	BlitJob a1 = make_job(BlitJobKind::OrBlob, 0u, 0xfcu, 2);
	BlitJob c0 = make_job(BlitJobKind::ClearRect, 0u, 0x00u, 3);
	BlitJob b1 = make_job(BlitJobKind::MaskedBobCookieCut, 4u, 0xcau, 4);
	BlitJob a2 = make_job(BlitJobKind::OrBlob, 0u, 0xfcu, 5);
	check(plan.add_or_blob(a0) && plan.add_masked_bob(b0) && plan.add_or_blob(a1) &&
		      plan.add_clear_rect(c0) && plan.add_masked_bob(b1) && plan.add_or_blob(a2),
	      "se anaden los 6 jobs");
	const eng::u8 n = plan.blit_job_count();
	check(n == 6u, "6 jobs en el plan");

	// Suma de los `destination_modulo_bytes` (id) = permutacion invariante: 0+1+2+3+4+5 = 15.
	auto id_sum = [&] {
		int s = 0;
		for (eng::u8 i = 0; i < plan.blit_job_count(); ++i) {
			s += plan.blit_job(i).destination_modulo_bytes - (40 - 8);
		}
		return s;
	};
	const int sum_before = id_sum();
	check(sum_before == 15, "suma de ids = 15 antes");

	plan.sort_by_state();
	check(plan.blit_job_count() == n, "el conteo no cambia");
	check(id_sum() == sum_before, "es una PERMUTACION (mismos jobs)");

	// Los de mismo estado quedan contiguos.
	bool contiguous = true;
	for (eng::u8 i = 0; i + 1u < plan.blit_job_count(); ++i) {
		const BlitJob& cur = plan.blit_job(i);
		const BlitJob& nxt = plan.blit_job(i + 1u);
		if (same_state(cur, nxt)) {
			continue;
		}
		// Si cambia el estado en `i`, no puede reaparecer el de `cur` mas adelante.
		for (eng::u8 k = i + 2u; k < plan.blit_job_count(); ++k) {
			if (same_state(cur, plan.blit_job(k))) {
				contiguous = false;
			}
		}
	}
	check(contiguous, "los de mismo estado quedan adyacentes");

	// Orden estable: dentro del grupo A (OrBlob), ids 0,2,5 en ese orden.
	eng::s16 a_ids[3] {};
	eng::u8 na = 0u;
	for (eng::u8 i = 0; i < plan.blit_job_count(); ++i) {
		if (plan.blit_job(i).kind == BlitJobKind::OrBlob && na < 3u) {
			a_ids[na++] = static_cast<eng::s16>(plan.blit_job(i).destination_modulo_bytes - (40 - 8));
		}
	}
	check(na == 3u && a_ids[0] == 0 && a_ids[1] == 2 && a_ids[2] == 5,
	      "orden estable dentro del grupo (0,2,5)");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: FramePlan::sort_by_state (permutacion + rachas + estabilidad) validado.\n");
	return 0;
}
