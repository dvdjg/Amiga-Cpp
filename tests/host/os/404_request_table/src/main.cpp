// ============================================================================
// Test HOST-404: peticiones con generación (R6.2)
// ============================================================================
//
// Verifica `eng::os::RequestTable`: abrir, validar por generación, completar (rechazando duplicados
// y respuestas **tardías** de un slot reutilizado), cancelar y llenado. Puro, sin E/S.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/os/404_request_table

#include <cstdio>

#include <eng/os/request.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

void test_lifecycle() {
	eng::os::RequestTable<4u> t {};
	const eng::os::RequestId r = t.acquire('A');
	check(r.valid() && t.alive(r), "acquire → viva");
	check(t.complete(r), "complete la primera vez → true");
	check(!t.alive(r), "tras completar → no viva");
	check(!t.complete(r), "complete duplicado → false");
}

void test_late_response_rejected() {
	eng::os::RequestTable<4u> t {};
	const eng::os::RequestId old = t.acquire('A');
	check(t.complete(old), "completa la petición antigua");

	// El slot se reutiliza con la generación siguiente.
	const eng::os::RequestId fresh = t.acquire('A');
	check(fresh.slot == old.slot, "reutiliza el mismo slot libre");
	check(fresh.gen != old.gen, "generación distinta");
	check(t.alive(fresh), "la nueva petición está viva");

	// Llega la respuesta TARDÍA de la petición antigua: no debe cerrar el slot nuevo.
	check(!t.complete(old), "respuesta tardía (gen vieja) → false");
	check(t.alive(fresh), "la petición nueva sigue viva pese a la respuesta tardía");
}

void test_cancel() {
	eng::os::RequestTable<4u> t {};
	const eng::os::RequestId r = t.acquire('L');
	check(t.cancel(r), "cancel de una viva → true");
	check(!t.alive(r), "tras cancelar → no viva");
	check(!t.complete(r), "respuesta posterior al cancel → false");
	check(!t.cancel(r), "cancel duplicado → false");
}

void test_full() {
	eng::os::RequestTable<2u> t {};
	check(t.acquire().valid(), "slot 0");
	check(t.acquire().valid(), "slot 1");
	check(!t.acquire().valid(), "tabla llena → inválido");
	check(t.pending_count() == 2u, "2 pendientes");
}

} // namespace

int main() {
	test_lifecycle();
	test_late_response_rejected();
	test_cancel();
	test_full();
	if (failures == 0) {
		std::printf("OK: peticiones con generación (R6.2) validadas.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
