// ============================================================================
// Test HOST-041: Block<Tag> (reserva de arena tipada)
// ============================================================================
//
// Valida el bloque tipado de Fase 7: `LinearArena::allocate_block<Tag>()` y
// `MemoryBlock::block<Tag>()` devuelven un `Block<Tag>` (vista del dominio) sin
// casts, con `valid()` y operadores de acceso; y dominios distintos no se mezclan.

#include <cstdio>

#include <eng/memory/arena.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
struct PatternTag {};
struct AudioTag {};

template <class From, class To>
concept ConstructibleFrom = requires(From f) { To {f}; };
} // namespace

int main() {
	eng::u8 raw[64] {};
	eng::LinearArena arena {raw, sizeof(raw), eng::MemoryKind::Any};

	// Reserva tipada: vista del dominio, mismo tamaño, sin casts.
	auto blk = arena.allocate_block<PatternTag>(16u, 4u);
	check(blk.valid(), "allocate_block valido");
	check(blk.view.size() == 16u, "tamano del bloque");
	check((*blk)[0] == 0u, "operator* y operator[]");
	blk.view[3] = 0xABu;
	check(raw[arena.used() - 16u + 3u] == 0xABu, "escribe la memoria de la arena");
	check(arena.used() >= 16u, "la arena consume la reserva");

	// Otro bloque del mismo arena.
	auto blk2 = arena.allocate_block<AudioTag>(8u, 2u);
	check(blk2.valid() && blk2.view.size() == 8u, "segundo bloque");

	// Sin espacio -> bloque invalido (no basura).
	eng::LinearArena small {raw, 8u, eng::MemoryKind::Any};
	check(!small.allocate_block<PatternTag>(16u, 4u).valid(), "overflow -> invalido");

	// MemoryBlock::block<Tag>() sobre una reserva existente.
	eng::LinearArena a2 {raw, sizeof(raw), eng::MemoryKind::Any};
	eng::MemoryBlock mb = a2.allocate(12u, 2u);
	auto mb_blk = mb.block<PatternTag>();
	check(mb_blk.valid() && mb_blk.view.size() == 12u, "MemoryBlock::block");

	// Dominios distintos no se convierten.
	static_assert(!ConstructibleFrom<eng::Block<AudioTag>, eng::Block<PatternTag>>,
	              "Block de otro dominio no debe convertir");
	static_assert(ConstructibleFrom<eng::Block<PatternTag>, eng::Block<PatternTag>>,
	              "mismo dominio debe copiarse");
	// El bloque lleva la vista y el medio (MemoryKind); es una reserva, no una vista
	// de coste cero, pero el sobrecoste es minimo (1 enumerado + padding).
	static_assert(sizeof(eng::Block<PatternTag>) >= sizeof(eng::Bytes<PatternTag>),
	              "Block no debe ser mas pequeno que su vista");

	// El MemoryKind de la reserva viaja con el bloque.
	check(blk.kind == eng::MemoryKind::Any, "kind Any en arena Any");
	eng::u8 chip_raw[64] {};
	eng::LinearArena chip_arena {chip_raw, sizeof(chip_raw), eng::MemoryKind::Chip};
	check(chip_arena.allocate_block<PatternTag>(16u, 4u).kind == eng::MemoryKind::Chip,
	      "kind Chip en arena Chip");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: Block<Tag> (reserva de arena tipada) validado.\n");
	return 0;
}
