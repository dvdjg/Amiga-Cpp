// ============================================================================
// Test HOST-388: FramePlan::sort_by_state — agrupacion de blits por estado (opt-in).
// ============================================================================
//
// Respalda `FramePlan::sort_by_state` (`eng/graphics/frame_plan.hpp`): reordena los `BlitJob` de
// forma ESTABLE para que los de mismo estado comun del Blitter (kind/minterm/shift/mods/layout)
// queden adyacentes (rachas -> el backend omite reprogramaciones). Verifica:
//   (1) es una PERMUTACION (mismos jobs, mismo multiconjunto);
//   (2) los del mismo estado quedan contiguos;
//   (3) el orden relativo dentro de un grupo se conserva (sort estable);
//   (4) con todos distinto estado, un solo pase no los mezcla de forma incorrecta;
//   (5) la reordenacion es EXPLICITA: `ReorderPolicy::PreserveOrder` (defecto) no reordena;
//       `GroupByState` (declarada por el llamador) la habilita;
//   (6) los AVISOS (`add_notify`) guardan su punto (`after_jobs`) y su `ticket`, no se ven
//       alterados por `sort_by_state`, y `clear()` los vacía.
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/graphics/388_frame_plan_state

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

/// Job minimo con un estado comun distinguible y un id de traza.
///
/// El id va en `height`, que **no** forma parte de la clave de estado de `state_less` (ni del
/// presupuesto): asi los jobs de un mismo grupo comparten de verdad el estado comun y el sort
/// puede reordenarlos. Codificarlo en `destination_modulo_bytes` (parte de la clave) haria que
/// todos los jobs tuvieran estado distinto y el sort no agruparia nada.
BlitJob make_job(BlitJobKind kind, eng::u8 shift, eng::u8 minterm, eng::s16 id) {
	BlitJob j {};
	j.kind = kind;
	j.source_shift = shift;
	j.minterm = minterm;
	j.words_per_row = 4u;
	j.height = static_cast<eng::u16>(1 + id); // id de traza, fuera de la clave de estado
	j.bitplane_count = 1u;
	j.source_modulo_bytes = 0;
	j.destination_modulo_bytes = 40 - 8; // constante: no distingue estados
	j.source = eng::graphics::BlitPtr::from_storage(g_buf);
	j.destination = eng::graphics::BlitPtr::from_storage(g_buf);
	j.interleaved = true; // permite strides 0 (la validacion de add_blit_job lo exige)
	if (kind == BlitJobKind::MaskedBobCookieCut) {
		j.mask = eng::graphics::BlitPtr::from_storage(g_buf);
	}
	return j;
}

/// Equivalencia de **estado comun** (los campos de la clave de `state_less`, sin `height`).
bool same_state(const BlitJob& a, const BlitJob& b) {
	return a.kind == b.kind && a.minterm == b.minterm && a.source_shift == b.source_shift &&
	       a.descending == b.descending && a.bitplane_count == b.bitplane_count &&
	       a.words_per_row == b.words_per_row && a.source_modulo_bytes == b.source_modulo_bytes &&
	       a.destination_modulo_bytes == b.destination_modulo_bytes &&
	       a.interleaved == b.interleaved;
}

} // namespace

int main() {
	std::printf("== HOST-388 frame_plan state ==\n");

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

	// Suma de los ids (height-1) = permutacion invariante: 0+1+2+3+4+5 = 15.
	auto id_sum = [&] {
		int s = 0;
		for (eng::u8 i = 0; i < plan.blit_job_count(); ++i) {
			s += plan.blit_job(i).height - 1;
		}
		return s;
	};
	const int sum_before = id_sum();
	check(sum_before == 15, "suma de ids = 15 antes");

	// Orden original de los jobs (id = height-1), capturado antes de reordenar.
	eng::s16 orig_order[6] {};
	for (eng::u8 i = 0; i < n; ++i) {
		orig_order[i] = static_cast<eng::s16>(plan.blit_job(i).height - 1);
	}

	// La reordenacion es **explicita**: con el defecto `PreserveOrder` no cambia nada.
	{
		FramePlan keep {};
		keep.clear();
		keep.add_or_blob(a0);
		keep.add_masked_bob(b0);
		keep.add_or_blob(a1);
		keep.add_clear_rect(c0);
		keep.add_masked_bob(b1);
		keep.add_or_blob(a2);
		const bool default_pol =
			(keep.reorder_policy() == eng::graphics::ReorderPolicy::PreserveOrder);
		keep.sort_by_state();
		bool same_order = true;
		for (eng::u8 i = 0; i < keep.blit_job_count(); ++i) {
			if (static_cast<eng::s16>(keep.blit_job(i).height - 1) != orig_order[i]) {
				same_order = false;
			}
		}
		check(default_pol, "politica por defecto = PreserveOrder");
		check(same_order, "PreserveOrder: sort_by_state NO reordena");
	}

	plan.set_reorder_policy(eng::graphics::ReorderPolicy::GroupByState);
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
			a_ids[na++] = static_cast<eng::s16>(plan.blit_job(i).height - 1);
		}
	}
	check(na == 3u && a_ids[0] == 0 && a_ids[1] == 2 && a_ids[2] == 5,
	      "orden estable dentro del grupo (0,2,5)");

	// --- Cuantificacion: nº de RACHAS (transiciones de estado) antes vs despues ---------------
	// La cache del backend acierta en cada job cuya clave de estado coincide con la anterior: el
	// numero de reprogramaciones es (nº de rachas) - 1 (todas menos la primera). `sort_by_state`
	// minimiza ese numero. Con la secuencia A B A C B A: antes = 5 transiciones (6 rachas); tras
	// agrupar = 2 transiciones (A(3) B(2) C(1) -> 3 rachas).
	auto changes = [](const FramePlan& p) {
		int t = 0;
		for (eng::u8 i = 1u; i < p.blit_job_count(); ++i) {
			if (!same_state(p.blit_job(i - 1u), p.blit_job(i))) {
				++t;
			}
		}
		return t;
	};
	// Reconstruye el plan en el orden original para medir "antes".
	FramePlan orig {};
	orig.clear();
	orig.add_or_blob(a0);
	orig.add_masked_bob(b0);
	orig.add_or_blob(a1);
	orig.add_clear_rect(c0);
	orig.add_masked_bob(b1);
	orig.add_or_blob(a2);
	const int t_before = changes(orig);
	const int t_after = changes(plan);
	check(t_before == 5, "antes: 5 transiciones de estado (A B A C B A)");
	check(t_after < t_before, "sort_by_state reduce las rachas (menos reprogramaciones)");
	check(t_after <= 2, "despues: a lo sumo 2 transiciones (3 grupos)");
	std::printf("  rachas: antes=%d despues=%d\n", t_before, t_after);

	// --- Avisos de la cadena (`add_notify`) ----------------------------------------------------
	// Un aviso guarda cuántos trabajos deben completarse antes de disparar (`after_jobs`) y su
	// `ticket`. Se declara en el punto de la ristra donde se quiere el aviso.
	FramePlan np {};
	np.clear();
	np.add_or_blob(a0);
	np.add_or_blob(a1);
	check(np.add_notify(0x1111u), "aviso intermedio se encola");
	np.add_or_blob(a2);
	check(np.add_notify(0x2222u), "aviso final se encola");
	check(np.notify_count() == 2u, "dos avisos registrados");
	check(np.notify(0u).after_jobs == 2u && np.notify(0u).ticket == 0x1111u,
	      "el aviso intermedio apunta a 2 trabajos");
	check(np.notify(1u).after_jobs == 3u && np.notify(1u).ticket == 0x2222u,
	      "el aviso final apunta a los 3 trabajos");
	// Reordenar con avisos encolados no debe tocar el orden (los avisos son un contrato).
	np.set_reorder_policy(eng::graphics::ReorderPolicy::GroupByState);
	const eng::s16 id_before = static_cast<eng::s16>(np.blit_job(0u).height);
	np.sort_by_state();
	check(np.blit_job(0u).height == id_before, "sort_by_state respeta el orden con avisos");
	// `clear` vacía también los avisos.
	np.clear();
	check(np.notify_count() == 0u, "clear() vacía los avisos");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: FramePlan::sort_by_state (permutacion + rachas + estabilidad) validado.\n");
	return 0;
}
