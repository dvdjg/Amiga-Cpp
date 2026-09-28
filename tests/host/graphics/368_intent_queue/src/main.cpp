// Test HOST-368: eng::graphics::IntentQueue — cola de intencion **no bloqueante** con
// **completacion**. Declarar una intencion solo ENCOLA (no ejecuta, no bloquea); `flush()` avanza
// sin esperar; `wait(ticket)` es el UNICO bloqueo; y al ejecutarse cada peticion se AVISA por la
// politica `Done` (en el engine real, un `Msg` IntentDone). Ver INTENT_PLANNER.md.
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/graphics/368_intent_queue

#include <eng/graphics/intent_queue.hpp>

#include <cstdio>

using eng::graphics::DrawIntent;
using eng::graphics::DrawKind;
using eng::graphics::DrawQueue;
using eng::graphics::Ticket;

namespace {
int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

/// Via de doble: siempre lista; cuenta ejecuciones y recuerda el ultimo tipo.
struct FakeExec {
	int ran = 0;
	DrawKind last = DrawKind::Rect;
	bool ready() { return true; }
	void run(const DrawIntent& it) {
		++ran;
		last = it.kind;
	}
};

/// Aviso de completacion: **handle** al estado compartido (como un posteador que apunta al puerto).
/// No es un valor con estado propio porque el `Done` se copia en la cola: debe compartir el destino.
struct FakeDone {
	int* n = nullptr;
	Ticket* last = nullptr;
	void operator()(Ticket t) {
		*last = t;
		++*n;
	}
};
} // namespace

int main() {
	std::printf("== HOST-368 intent_queue ==\n");

	FakeExec ex;
	int done_n = 0;
	Ticket done_last = 0;
	DrawQueue<4u, FakeExec, FakeDone> q;
	q.bind(ex);
	q.bind_done(FakeDone {&done_n, &done_last});

	// 1) Declarar NO ejecuta (no bloquea): solo encola y da un ticket.
	const Ticket t1 =
		q.enqueue(DrawIntent {DrawKind::Rect, 1, 2, 4u, 4u, 0, 0, 0u, 0u});
	const Ticket t2 = q.enqueue(DrawIntent {DrawKind::Line});
	check(!q.empty() && ex.ran == 0 && done_n == 0, "declarar no ejecuta (no bloquea)");
	check(t2 > t1, "los tickets son crecientes");

	// 2) flush avanza sin esperar: ejecuta en orden y avisa por cada peticion.
	q.flush();
	check(q.empty() && ex.ran == 2 && done_n == 2 && done_last == t2,
	      "flush ejecuta y avisa por ticket");

	// 3) wait(ticket) es el unico bloqueo y se cumple cuando su peticion se ejecuto.
	const Ticket t3 = q.enqueue(DrawIntent {DrawKind::Sprite, 0, 0, 0u, 0u, 0, 0, 0u, 3u});
	q.wait(t3);
	check(done_last == t3 && ex.ran == 3, "wait(t) espera a que t se ejecute");

	// 4) Receta precompilada (setup): el bucle la reproduce con trabajo minimo (encolar + flush).
	{
		eng::graphics::DrawRecipe<4u> recipe;
		recipe.add(DrawIntent {DrawKind::Rect, 0, 0, 8u, 8u, 0, 0, 1u, 0u});
		recipe.add(DrawIntent {DrawKind::Line, 0, 0, 0u, 0u, 9, 9, 2u, 0u});
		check(recipe.count() == 2u, "la receta guarda sus intenciones (setup)");

		FakeExec r2;
		int dn2 = 0;
		Ticket dl2 = 0;
		DrawQueue<4u, FakeExec, FakeDone> q2;
		q2.bind(r2);
		q2.bind_done(FakeDone {&dn2, &dl2});
		recipe.emit(q2);
		check(!q2.empty() && r2.ran == 0, "emit solo encola (no ejecuta)");
		q2.flush();
		check(r2.ran == 2 && dn2 == 2, "la receta se ejecuta en el frame");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: IntentQueue (no bloqueante + completacion) validado.\n");
	return 0;
}
