// ============================================================================
// Test HOST-354: fachada de escena (eng::scene) - display declarativo + capa de BOBs.
// ============================================================================
//
// Respalda `eng/scene/display.hpp` (emit_display/emit_fine_scroll/emit_gradient sobre un
// `copper::Scheduler`) y `eng/scene/bobs.hpp` (`BobLayer`/`Actor`). Comprueba que el display
// emite BPLxPT que apuntan a los planos (Chip) y que la capa de BOBs cuenta/emite actores.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/354_scene_facade

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/mem_bank.hpp>
#include <eng/scene/bobs.hpp>
#include <eng/scene/display.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-354 scene facade ==\n");

	// --- Display declarativo -----------------------------------------------
	eng::u8 chip[64] {};
	eng::MemBank<eng::MemoryKind::Chip> bank {};
	bank.configure(chip, sizeof(chip), 16u);
	const auto planes_blk = bank.reserve<eng::PlaneTag>(32u, 16u);

	eng::u16 copper[256] {};
	eng::MemoryBlock copper_block {copper, sizeof(copper), eng::MemoryKind::Chip};
	eng::copper::SchedulerT<false> sched {copper_block};

	eng::scene::Band band {};
	band.planes = 2u;
	band.bytes_per_row = 4u;
	band.planes_view = planes_blk.mem_view();
	band.bpl1mod = band.modulo();
	band.bpl2mod = band.modulo();
	eng::scene::emit_display(sched, band);
	const auto scroll = eng::scene::emit_fine_scroll(sched, 0u);
	eng::scene::emit_gradient(sched, 0x41u, 0x43u, 0x0111u);
	sched.end();
	check(sched.ok(), "scheduler ok");
	check(scroll.valid(), "fine-scroll: handle valido");

	// `move_bitplane_pointer` emite DOS MOVEs: [BPLxPTH][high][BPLxPTL][low].
	const eng::u16* w = sched.data();
	const eng::u32 usage = sched.words_used();
	const eng::u32 plane0 = static_cast<eng::u32>(planes_blk.mem_view().address(0).value);
	bool bpl1pt_found = false;
	for (eng::u32 i = 0u; i + 3u < usage; ++i) {
		if (w[i] == 0x00e0u) {
			bpl1pt_found = true;
			check(w[i + 1u] == static_cast<eng::u16>(plane0 >> 16), "BPL1PTH = high(plano 0)");
			check(w[i + 2u] == 0x00e2u, "BPL1PTL emitido");
			check(w[i + 3u] == static_cast<eng::u16>(plane0 & 0xffffu), "BPL1PTL = low(plano 0)");
			break;
		}
	}
	check(bpl1pt_found, "BPL1PTH emitido");

	// --- Capa de BOBs ------------------------------------------------------
	eng::scene::BobLayer bobs {};
	check(bobs.count() == 0u, "sin actores");
	bobs.resize(4u);
	check(bobs.count() == 4u, "resize");
	bobs[0] = {10, 20, 0u, true};
	bobs[1] = {11, 21, 1u, false};
	check(bobs[0].x == 10 && bobs[1].frame == 1u && !bobs[1].visible, "actores");
	bobs.resize(99u); // recorta al maximo
	check(bobs.count() == eng::scene::BobLayer::kMaxActors, "resize recorta");

	// `BobActor` enriquecido: orden por defecto y animacion ligada (avanzada por `tick`).
	check(bobs[0].z == 128u && !bobs[0].animated(), "BobActor: z=128 y sin animacion por defecto");
	static const eng::u8 kFrames[2] = {0u, 1u};
	static const eng::u8 kDurs[2] = {2u, 2u};
	bobs[0].anim.frames = eng::Span<const eng::u8> {kFrames, 2u};
	bobs[0].anim.durations = eng::Span<const eng::u8> {kDurs, 2u};
	check(bobs[0].animated(), "BobActor: animacion valida con frames");
	const eng::u8 first = bobs[0].anim.frame();
	bobs.tick();
	bobs.tick(); // 2 ticks de juego: pasa el primer frame
	check(bobs[0].anim.frame() != first, "BobLayer::tick avanza la animacion ligada");

	// --- Limpieza de zona (clear_box) --------------------------------------
	eng::graphics::FramePlan plan {};
	plan.clear();
	eng::graphics::BobTarget t = eng::graphics::make_bob_target(
		eng::ChipView<eng::PlaneTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(copper),
			sizeof(copper)},
		4u, 4u, 2u, eng::graphics::BobLayout::Interleaved);
	check(eng::scene::clear_box(plan, t, 0, 8, 32u, 4u), "clear_box encola");
	check(plan.blit_job_count() == 1u, "clear_box: un blit");
	check(plan.blit_job(0).kind == eng::graphics::BlitJobKind::ClearRect, "clear_box kind");
	check(plan.blit_job(0).height == 4u * 2u, "clear_box altura = filas x planos");
	check(plan.blit_job(0).words_per_row == 2u, "clear_box palabras/fila");

	// `emit` ordenado por `z`: sin hoja valida no dibuja ni falla (la hoja es del asset).
	plan.clear();
	check(bobs.emit(plan, t) == 0u, "BobLayer::emit sin hoja valida: 0 dibujados");

	// Capa **heterogenea**: varias hojas (kMaxSheets) y cada actor elige la suya por `sheet_index`.
	eng::graphics::Bob sb {};
	sb.sheet = eng::ChipView<eng::BobTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(chip), sizeof(chip)};
	sb.width = 16u;
	sb.height = 16u;
	sb.planes = 2u;
	sb.frame_count = 1u;
	sb.layout = eng::graphics::BobLayout::Planar;
	const eng::graphics::Sprite spr_a {sb, sizeof(chip), 0u};
	check(spr_a.valid(), "Sprite valido (sheet+geometria)");
	eng::scene::BobLayer het {};
	het.set_sheet(0u, spr_a);
	het.set_sheet(1u, spr_a);                                 // segunda hoja (heterogenea)
	het.set_sheet(eng::scene::BobLayer::kMaxSheets, spr_a);   // fuera de rango: no debe escribir
	het.resize(2u);
	het[0].sheet_index = 0u;
	het[1].sheet_index = 1u;
	check(het.count() == 2u && het.sheet(0u).valid() && het.sheet(1u).valid(),
	      "heterogenea: hojas 0 y 1 validas");
	plan.clear();
	check(het.emit(plan, t) == 2u, "heterogenea: actores por sheet_index -> 2 dibujados");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: fachada de escena (display + bobs) validada.\n");
	return 0;
}
