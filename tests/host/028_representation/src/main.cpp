// ============================================================================
// Test HOST-028: representación de actores (sprite/BOB/CPU/playfield)
// ============================================================================
//
// Valida `eng::scene::choose_representation` / `RepresentationAllocator`: la app
// describe el actor y el engine elige cómo materializarlo, consumiendo presupuesto y
// reasignando (p. ej. sprite agotado -> BOB). Lógica pura, determinista.

#include <cstdio>

#include <eng/scene/representation.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
using eng::scene::ActorTemplate;
using eng::scene::Representation;
using eng::scene::RepresentationBudget;
using eng::scene::RepresentationAllocator;
} // namespace

int main() {
	const ActorTemplate small { 16, 16, 4, Representation::Sprite, 0, false };
	const ActorTemplate wide  { 32, 16, 4, Representation::Sprite, 0, false };
	const ActorTemplate boss  { 96, 64, 4, Representation::Layer, 5, true };

	// Cabe en sprite y hay canal -> Sprite.
	check(eng::scene::choose_representation(small, { 8, 100, 1 }) == Representation::Sprite, "pequeño -> Sprite");
	// Demasiado ancho -> BOB.
	check(eng::scene::choose_representation(wide, { 8, 100, 1 }) == Representation::Bob, "ancho -> Bob");
	// Preferido Layer + scroll propio + slot -> Layer.
	check(eng::scene::choose_representation(boss, { 8, 100, 1 }) == Representation::Layer, "jefe -> Layer");
	// Sin canales de sprite -> BOB.
	check(eng::scene::choose_representation(small, { 0, 100, 0 }) == Representation::Bob, "sin canal -> Bob");
	// Sin canales ni Blitter -> CPU.
	check(eng::scene::choose_representation(small, { 0, 0, 0 }) == Representation::Cpu, "sin recursos -> Cpu");
	// Preferido Layer pero sin scroll -> no promociona (sprite).
	check(eng::scene::choose_representation({ 64, 64, 4, Representation::Layer, 0, false },
	                                        { 8, 100, 1 }) == Representation::Bob, "Layer sin scroll -> Bob");

	// El asignador consume y reasigna: 2 canales de sprite.
	RepresentationAllocator alloc {};
	alloc.reset({ 2, 50, 1 });
	check(alloc.allocate(small) == Representation::Sprite, "1er sprite");
	check(alloc.allocate(small) == Representation::Sprite, "2º sprite");
	check(alloc.allocate(small) == Representation::Bob, "agotados -> Bob");
	check(alloc.remaining().sprite_channels == 0, "canales a 0");
	check(alloc.remaining().bob_budget_words == 50 - 16, "bob budget consumido");
	check(alloc.allocate(boss) == Representation::Layer, "jefe -> Layer");
	check(alloc.remaining().layer_slots == 0, "slot de capa consumido");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: representación de actores (Sprite/Bob/Cpu/Layer) validada.\n");
	return 0;
}
