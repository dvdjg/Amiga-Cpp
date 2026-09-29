// ============================================================================
// Test HOST-364: banco con POOL PROPIO (free real) y presupuesto por banco.
// ============================================================================
//
// Modelo vigente (Fase 3/6 de ROADMAP_MEMORY_OWNERSHIP.md): los **bancos** (`MemBank`) son la
// puerta de reserva **persistente**; tienen **pool propio** con `free` real en cualquier orden.
// La antigua `configure_backing` (bancos enlazados al cursor de la arena) ya no se usa en el
// backend: la escena reserva del banco, no de la arena.
//
// Cubre: pool propio (free real entre reservas), `restore` tras liberar, `used_bytes`/`free_bytes`
// coherentes, y `res::Budget` leyendo del banco.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/364_allocator_sharing

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/res/budget.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

alignas(16) eng::u8 g_chip[64u * 1024u];
alignas(16) eng::u8 g_slow[16u * 1024u];

int main() {
	std::printf("== HOST-364 allocator sharing ==\n");

	eng::MemoryManager mm {};
	check(mm.configure(g_chip, sizeof(g_chip), g_slow, sizeof(g_slow), nullptr, 0u, 16u),
	      "configure ok");

	// --- Pool propio del banco: reservar graficos+sonido, liberar graficos (no LIFO) ---
	const auto gfx = mm.chip().reserve<eng::PlaneTag>(4096u, 16u);
	const auto snd = mm.chip().reserve<eng::AudioTag>(1024u, 16u);
	check(gfx.valid() && snd.valid(), "dos reservas en el banco Chip");
	check(static_cast<const void*>(gfx.view.data()) != static_cast<const void*>(snd.view.data()),
	      "bloques distintos");

	const eng::u32 used_before = mm.chip().used_bytes();
	mm.chip().release(gfx); // libera el PRIMERO con el segundo vivo: imposible en bump
	check(mm.chip().used_bytes() < used_before, "release devuelve memoria (free real)");
	check(mm.chip().free_bytes() > 0u, "hay hueco libre tras el release");

	// Re-reservar reutiliza el hueco (misma direccion).
	const auto gfx2 = mm.chip().reserve<eng::PlaneTag>(4096u, 16u);
	check(gfx2.valid() && gfx2.view.data() == gfx.view.data(), "reutiliza el hueco liberado");

	// --- `release` invalida el bloque (diagnostico): usar su direccion tras release trapa ---
	// (solo en builds con ENG_ASSERT; en host con ENG_DEBUG no se fuerza aqui para no abortar).

	// --- Telemetria por banco: capacity/used/free coherentes -------------------------
	check(mm.chip().capacity() == sizeof(g_chip), "capacity del banco Chip = buffer");
	check(mm.chip().used_bytes() + mm.chip().free_bytes() == mm.chip().capacity(),
	      "usado + libre = capacidad");
	check(mm.chip().kind() == eng::MemoryKind::Chip, "kind del banco = Chip");

	// --- `res::Budget` lee del BANCO -----------------------------------------------
	eng::res::Budget budget {mm};
	check(budget.valid(), "budget valido");
	check(budget.capacity_chip() == sizeof(g_chip), "capacity_chip = banco");
	check(budget.used_chip() == mm.chip().used_bytes(), "used_chip = banco");
	check(budget.can_fit_chip(1024u), "can_fit_chip dentro de capacidad");
	check(!budget.can_fit_chip(static_cast<eng::u32>(sizeof(g_chip) + 1u)),
	      "can_fit_chip rechaza si no cabe");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: banco con pool propio (free real) + Budget por banco validados.\n");
	return 0;
}
