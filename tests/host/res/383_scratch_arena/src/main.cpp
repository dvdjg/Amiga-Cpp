// ============================================================================
// Test HOST-383: ScratchArena — arena de scratch LIFO (mark/release) + reset_frame.
// ============================================================================
//
// Respalda `eng/memory/arena.hpp` (ScratchArena) y `MemorySystem::reset_frame`: la memoria
// **temporal** de una fase/frame se reserva con `allocate` y se libera entera por niveles con
// `mark()`/`release(mark)` (LIFO). Es la otra vida util frente al `BlockPool` persistente (free en
// cualquier orden, HOST-340). Ver `MEMORY_OWNERSHIP.md` §"Dos vidas utiles".
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/383_scratch_arena

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/memory/arena.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-383 scratch_arena ==\n");

	alignas(16) eng::u8 buf[1024] {};
	eng::ScratchArena sc {buf, sizeof(buf), eng::MemoryKind::Fast};

	check(sc.capacity() == 1024u && sc.remaining() == 1024u, "arena vacia");
	check(sc.kind() == eng::MemoryKind::Fast, "medio Fast");

	// Tramo 1: un nivel de scratch (p. ej. la fase de setup).
	const eng::ArenaMark outer = sc.mark();
	const auto a = sc.allocate_block<eng::PlaneTag>(256u, 16u);
	check(a.valid(), "reserva nivel externo");
	const eng::u32 after_outer = sc.used();
	check(after_outer == 256u, "cursor tras 256 alineado (base ya a 16)");

	// Tramo 2: nivel interno (p. ej. el frame), con mas reservas.
	const eng::ArenaMark inner = sc.mark();
	const auto b = sc.allocate_block<eng::PlaneTag>(128u, 16u);
	const auto c = sc.allocate_block<eng::MaskTag>(64u, 16u);
	check(b.valid() && c.valid(), "reservas del nivel interno");
	check(sc.used() == 256u + 128u + 64u, "cursor acumula los dos niveles");

	// Liberar SOLO el nivel interno -> vuelve al marcador `inner`.
	sc.release(inner);
	check(sc.used() == after_outer, "release(inner) libera el nivel interno (LIFO)");
	check(!a.view.empty(), "el nivel externo sigue vivo");

	// Reservar de nuevo reutiliza el mismo cursor (bump).
	const auto d = sc.allocate_block<eng::MaskTag>(64u, 16u);
	check(d.valid() && d.view.data() == b.view.data(), "reutiliza el hueco del nivel liberado");

	// Liberar el nivel externo -> arena entera libre.
	sc.release(outer);
	check(sc.used() == 0u && sc.remaining() == 1024u, "release(outer) deja la arena vacia");

	// `clear()` = release total (reset de scratch de frame).
	(void)sc.allocate_block<eng::PlaneTag>(300u, 16u);
	sc.clear();
	check(sc.used() == 0u, "clear() reinicia la scratch");

	// `mark`/`release` no puede "avanzar" el cursor hacia delante (no-op si el marcador es futuro).
	const eng::ArenaMark m0 = sc.mark();
	(void)sc.allocate_block<eng::PlaneTag>(16u, 16u);
	const eng::ArenaMark ahead = sc.mark();
	sc.release(m0);
	sc.release(ahead); // intento de liberar "hacia delante": no-op
	check(sc.used() == 0u, "release de un marcador futuro es no-op");

	// `MemorySystem::reset_frame()` limpia SOLO la scratch; los persistentes no se tocan.
	{
		eng::MemorySystem mem {};
		mem.chip.reset(buf, sizeof(buf), eng::MemoryKind::Chip);
		mem.frame.reset(buf, sizeof(buf), eng::MemoryKind::Chip);
		const auto persist = mem.chip.allocate_block<eng::PlaneTag>(256u, 16u);
		(void)mem.frame.allocate_block<eng::MaskTag>(128u, 16u);
		mem.reset_frame();
		check(mem.frame.used() == 0u, "reset_frame limpia la scratch");
		check(mem.chip.used() == 256u && !persist.view.empty(), "el persistente (chip) no se toca");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: ScratchArena (mark/release LIFO + reset_frame) validado.\n");
	return 0;
}
