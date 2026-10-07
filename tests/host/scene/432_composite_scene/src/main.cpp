// ============================================================================
// Test HOST-432: fachada de sprites compuestos (`eng::CompositeScene`) - F1 nivel A.
// ============================================================================
//
// Respalda `eng/api/objects.hpp`: el juego da de alta contenidos multi-parte y avanza
// ticks; el engine materializa cada parte (Sprite HW / BOB), emite los BOB al `FramePlan`
// y publica los Sprite HW con `present` — sin exponer intents, canales ni placements.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/432_composite_scene

#include <cstdio>

#include <eng/api/objects.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

using eng::u16;
using eng::u32;

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

alignas(16) eng::u8 g_mem[8u * 1024u];
alignas(16) u16 g_legs[2u * 16u] {};
alignas(16) u16 g_torso[32u] {};
alignas(16) u16 g_big[64u] {};
alignas(16) u16 g_mask[16u] {};
alignas(16) u16 g_bitmap[4u * 16u * 20u] {};
alignas(16) u16 g_cooked[256u] {};

eng::graphics::BobTarget make_target() {
	return eng::graphics::make_bob_target(
		eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(g_bitmap),
			sizeof(g_bitmap)},
		40u, 16u, 4u, eng::graphics::BobLayout::Planar);
}

} // namespace

int main() {
	std::printf("== HOST-432 composite_scene ==\n");

	eng::MemoryManager mem;
	mem.configure(g_mem, sizeof(g_mem), nullptr, 0u, nullptr, 0u, 16u);
	eng::copper::Scheduler sched {mem.chip().reserve<eng::CopperTag>(2048u, 16u)};

	// Contenido: piernas BOB (2 planos con mascara), torso *attached* (4 planos) y una
	// parte grande que no cabe como Sprite HW.
	eng::graphics::CompositePart parts[3] {};
	parts[0].visual.pixels = eng::Span<const eng::u16> {g_legs, 32u};
	parts[0].visual.mask = eng::Span<const eng::u16> {g_mask, 8u};
	parts[0].visual.w = 16u;
	parts[0].visual.h = 8u;
	parts[0].visual.bitplanes = 2u;
	parts[0].visual.frame_count = 2u;
	parts[0].visual.frame_stride = 32u;
	parts[0].offset_y = 8;

	parts[1].visual.pixels = eng::Span<const eng::u16> {g_torso, 32u};
	parts[1].visual.mask = eng::Span<const eng::u16> {g_mask, 8u}; // para el BOB degradado
	parts[1].visual.w = 16u;
	parts[1].visual.h = 8u;
	parts[1].visual.bitplanes = 4u; // par *attached* derivado

	parts[2].visual.pixels = eng::Span<const eng::u16> {g_big, 64u};
	parts[2].visual.mask = eng::Span<const eng::u16> {g_mask, 8u};
	parts[2].visual.w = 32u;
	parts[2].visual.h = 16u;
	parts[2].visual.bitplanes = 2u;

	const eng::u8 frames0[3] = {0u, 0u, 0u};
	const eng::u8 frames1[3] = {1u, 0u, 0u};
	const eng::graphics::CompositeFrame comp_frames[2] = {
		{{frames0, 3u}, {}, 2u, 0u},
		{{frames1, 3u}, {}, 2u, 0u},
	};
	const eng::graphics::CompositeSequence seqs[1] = {
		{eng::Span<const eng::graphics::CompositeFrame> {comp_frames, 2u}, true, 0xffu}};
	const eng::graphics::CompositeVisual vis {
		eng::Span<const eng::graphics::CompositePart> {parts, 3u},
		eng::Span<const eng::graphics::CompositeSequence> {seqs, 1u},
		0, 0,
		eng::Box {0, 0, 16u, 16u},
	};

	eng::CompositeScene<2, 8> scene {};
	const eng::CompositeScene<2, 8>::PartMode modes[3] = {
		eng::CompositeScene<2, 8>::PartMode::Bob,
		eng::CompositeScene<2, 8>::PartMode::Sprite,
		eng::CompositeScene<2, 8>::PartMode::Bob,
	};
	scene.set_part_modes(eng::Span<const eng::CompositeScene<2, 8>::PartMode> {modes, 3u});
	scene.set_cooked_pool(eng::ChipView<eng::SpriteTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(g_cooked), sizeof(g_cooked)});

	const auto id = scene.add(vis, 100, 50);
	check(id != eng::CompositeScene<2, 8>::kNoId && scene.count() == 1u, "alta y conteo");
	const auto id2 = scene.add(vis, 0, 0);
	check(id2 != eng::CompositeScene<2, 8>::kNoId, "segunda alta");
	check(scene.add(vis, 0, 0) == eng::CompositeScene<2, 8>::kNoId, "escena llena -> kNoId");
	check(scene.remove(id2) && scene.count() == 1u, "remove reutiliza el hueco");

	// Estado por la fachada: mover/girar/secuencia + tick de animacion.
	auto* st = scene.state(id);
	check(st != nullptr && st->x == 100 && st->y == 50, "estado inicial");
	st->facing_left = true;
	const eng::Box legs_l = scene.part_box(id, 0u);
	check(legs_l.x == 84 && legs_l.y == 58, "caja de parte espejada");
	scene.set_sequence(id, 0u);
	(void)scene.tick(2u);
	check(scene.state(id)->frame == 1u, "tick avanza el frame");

	// Hitboxes por la fachada (bounds por defecto).
	eng::Box hits[2] {};
	check(scene.hitboxes(id, eng::Span<eng::Box> {hits, 2u}) == 1u, "hitbox del frame");

	// Emision: piernas y grande a BOB; torso (4 planos, 16 px) a Sprite HW (par).
	eng::graphics::FramePlan plan {};
	const eng::graphics::BobTarget target = make_target();
	const auto r = scene.emit(plan, target);
	check(r.ok, "emit ok");
	check(r.sprites == 2u && r.bobs == 2u && r.degraded == 0u,
	      "2 sprites (par) + 2 bobs");
	check(plan.blit_job_count() == 2u, "dos jobs de BOB");

	// present: arma los Sprite HW (POS/CTL/PT) en la copperlist sin exponer placements.
	scene.present(sched, 32u);
	sched.end();
	u16 pos_moves = 0u;
	u16 pt_moves = 0u;
	const u16* w = sched.data();
	for (u16 i = 0u; i + 1u < sched.words_used(); i += 2u) {
		if (w[i] == 0xffffu) {
			break;
		}
		if ((w[i] & 1u) != 0u) {
			continue; // WAIT
		}
		if (w[i] == 0x140u || w[i] == 0x148u) {
			++pos_moves;
		}
		if (w[i] == 0x120u || w[i] == 0x126u) {
			++pt_moves;
		}
	}
	check(pos_moves == 2u && pt_moves == 2u, "present arma los dos canales del par");

	// Sin pool Chip, el par no se puede cocinar: degrada a BOB y lo marca (rechazo controlado).
	scene.set_cooked_pool({});
	eng::graphics::FramePlan plan2 {};
	const auto r2 = scene.emit(plan2, target);
	check(!r2.ok && r2.degraded == 1u && r2.sprites == 0u && r2.bobs == 3u,
	      "sin pool: el par degrada a BOB");

	// Modo global Bob: todo por Blitter.
	eng::CompositeScene<2, 8> bobscene {};
	bobscene.set_part_mode(eng::CompositeScene<2, 8>::PartMode::Bob);
	(void)bobscene.add(vis, 10, 10);
	eng::graphics::FramePlan plan3 {};
	const auto r3 = bobscene.emit(plan3, target);
	check(r3.ok && r3.sprites == 0u && r3.bobs == 3u, "modo Bob: tres partes por Blitter");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: CompositeScene (alta, ticks, hitboxes, Sprite HW y BOB) validada.\n");
	return 0;
}
