// ============================================================================
// Test HOST-429: sprite compuesto (`eng/scene/composite_actor.hpp`) - F1.
// ============================================================================
//
// Respalda el contenido multi-parte (`graphics/composite_visual.hpp`): avance de secuencias
// por ticks (loop, `next`, `finished`), geometría de cada parte con espejo, hitboxes del
// frame (explícitas o `bounds` por defecto) y materialización a intents de Sprite HW (par
// *attached* derivado) y a BOBs en el `FramePlan`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/429_composite_actor

#include <cstdio>

#include <eng/scene/composite_actor.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

using eng::graphics::CompositeFrame;
using eng::graphics::CompositeHitBox;
using eng::graphics::CompositePart;
using eng::graphics::CompositeSequence;
using eng::graphics::CompositeVisual;
using eng::scene::CompositeState;

// Piernas: 16x8 a 2 planos, 2 frames (16 words por frame, planar).
alignas(16) eng::u16 g_legs[2u * 16u] {};
// Torso: 16x8 a 4 planos (par *attached* derivado al materializar), 1 frame.
alignas(16) eng::u16 g_torso[32u] {};
// Máscara planar 16x8 (cookie-cut de los BOBs).
alignas(16) eng::u16 g_mask[8u] {};
// Bitmap destino 4 planos para los BOB.
alignas(16) eng::u16 g_bitmap[4u * 8u * 20u] {};

constexpr eng::u8 kFrameLegs[2] = {1u, 0u}; // frame del compuesto 1: piernas frame 1, torso 0
constexpr eng::u8 kFrame0[2] = {0u, 0u};
constexpr eng::Box kHitBox {0, 0, 8u, 16u};
constexpr CompositeHitBox kHits[1] = {{kHitBox, 1u, true}};

constexpr CompositeFrame kIdleFrames[2] = {
	CompositeFrame {{kFrame0, 2u}, {kHits, 1u}, 2u, 10u},
	CompositeFrame {{kFrameLegs, 2u}, {}, 3u, 0u},
};
constexpr CompositeSequence kIdle {{kIdleFrames, 2u}, true, 0xffu};
constexpr CompositeFrame kWalkFrames[1] = {CompositeFrame {{kFrameLegs, 2u}, {}, 1u, 0u}};
constexpr CompositeSequence kWalk {{kWalkFrames, 1u}, false, 0u}; // al terminar -> idle
constexpr CompositeFrame kDieFrames[1] = {CompositeFrame {{kFrame0, 2u}, {}, 1u, 0u}};
constexpr CompositeSequence kDie {{kDieFrames, 1u}, false, 0xffu}; // se queda

constexpr CompositeSequence kSeqs[3] = {kIdle, kWalk, kDie};

eng::graphics::BobTarget make_target() {
	return eng::graphics::make_bob_target(
		eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(g_bitmap),
			sizeof(g_bitmap)},
		40u, 8u, 4u, eng::graphics::BobLayout::Planar);
}

} // namespace

int main() {
	std::printf("== HOST-429 composite_actor ==\n");

	// Contenido (setup del asset).
	CompositePart parts[2] {};
	parts[0].visual.pixels = eng::Span<const eng::u16> {g_legs, 32u};
	parts[0].visual.mask = eng::Span<const eng::u16> {g_mask, 8u};
	parts[0].visual.w = 16u;
	parts[0].visual.h = 8u;
	parts[0].visual.bitplanes = 2u;
	parts[0].visual.frame_count = 2u;
	parts[0].visual.frame_stride = 32u; // 16 words por frame
	parts[0].offset_x = 0;
	parts[0].offset_y = 8; // piernas bajo el ancla
	parts[0].z = 0;
	parts[1].visual.pixels = eng::Span<const eng::u16> {g_torso, 32u};
	parts[1].visual.mask = eng::Span<const eng::u16> {g_mask, 8u};
	parts[1].visual.w = 16u;
	parts[1].visual.h = 8u;
	parts[1].visual.bitplanes = 4u; // par *attached* derivado al materializar
	parts[1].offset_x = 0;
	parts[1].offset_y = 0;
	parts[1].z = 1;
	const CompositeVisual vis {
		eng::Span<const CompositePart> {parts, 2u},
		eng::Span<const CompositeSequence> {kSeqs, 3u},
		0, 0,
		eng::Box {2, 3, 10u, 12u},
	};

	CompositeState st {};
	st.x = 100;
	st.y = 50;

	// Geometria de las partes (ancla 0,0): piernas abajo (offset_y 8), torso arriba.
	eng::Box legs = eng::scene::composite_part_box(vis, st, 0u);
	check(legs.x == 100 && legs.y == 58 && legs.w == 16u && legs.h == 8u, "parte 0 sin espejo");
	eng::Box torso = eng::scene::composite_part_box(vis, st, 1u);
	check(torso.x == 100 && torso.y == 50 && torso.w == 16u && torso.h == 8u, "parte 1 sin espejo");

	// Espejo: el offset X se refleja respecto al ancla (0 - offset - w).
	st.facing_left = true;
	legs = eng::scene::composite_part_box(vis, st, 0u);
	check(legs.x == 84 && legs.y == 58, "parte 0 espejada");
	st.facing_left = false;

	// Hitboxes del frame 0 (x=0,w=8): normal y espejada.
	eng::Box hits[2] {};
	check(eng::scene::composite_hitboxes(vis, st, eng::Span<eng::Box> {hits, 2u}) == 1u,
	      "una hitbox");
	check(hits[0].x == 100 && hits[0].y == 50 && hits[0].w == 8u, "hitbox normal");
	st.facing_left = true;
	check(eng::scene::composite_hitboxes(vis, st, eng::Span<eng::Box> {hits, 2u}) == 1u,
	      "una hitbox espejada");
	check(hits[0].x == 92, "hitbox espejada: x = -x-w + pos");
	st.facing_left = false;

	// Secuencia por defecto: idle en loop (2 y 3 ticks). Las hitboxes del frame mandan.
	const CompositeFrame* fr = eng::scene::composite_current_frame(vis, st);
	check(fr != nullptr && fr->event == 10u, "frame 0 con evento");
	check(!eng::scene::composite_advance(vis, st, 1u), "1 tick: mismo frame");
	check(st.elapsed == 1u, "elapsed 1");
	check(eng::scene::composite_advance(vis, st, 1u), "2 ticks: cambia al frame 1");
	check(st.frame == 1u && st.elapsed == 0u, "frame 1");
	fr = eng::scene::composite_current_frame(vis, st);
	check(fr != nullptr && fr->hits.empty(), "frame 1 sin hits -> bounds por defecto");

	// `bounds` por defecto cuando el frame no trae hits.
	check(eng::scene::composite_hitboxes(vis, st, eng::Span<eng::Box> {hits, 2u}) == 1u,
	      "bounds por defecto");
	check(hits[0].x == 102 && hits[0].y == 53 && hits[0].w == 10u, "bounds transformado");

	// Vuelta por loop tras el frame 1 (3 ticks).
	check(!eng::scene::composite_advance(vis, st, 2u), "2 de 3 ticks: mismo frame");
	check(eng::scene::composite_advance(vis, st, 1u), "3 ticks: vuelve al frame 0");
	check(st.frame == 0u, "loop a frame 0");

	// Cambio de secuencia: walk (1 frame) salta a idle al terminar.
	eng::scene::composite_set_sequence(st, 1u);
	check(st.sequence == 1u && st.frame == 0u && !st.finished, "set_sequence walk");
	check(eng::scene::composite_advance(vis, st, 1u), "fin de walk: salta a idle");
	check(st.sequence == 0u && st.frame == 0u && !st.finished, "walk -> idle");

	// Secuencia sin loop ni next: queda `finished` en el ultimo frame.
	eng::scene::composite_set_sequence(st, 2u);
	(void)eng::scene::composite_advance(vis, st, 5u);
	check(st.finished && st.sequence == 2u && st.frame == 0u, "die queda finished");

	// Materializacion a intents: piernas a BOB, torso (4 planos, 16 px) a Sprite HW -> par.
	const eng::scene::Representation repr[2] = {eng::scene::Representation::Bob,
						   eng::scene::Representation::Sprite};
	check(eng::scene::composite_to_sprite_intents(vis, st, {}, {}) == 0u,
	      "sin planner: el defecto no emite intents de sprite");
	eng::graphics::SpriteIntent intents[4] {};
	const eng::u16 n_intents = eng::scene::composite_to_sprite_intents(
		vis, st, eng::Span<const eng::scene::Representation> {repr, 2u},
		eng::Span<eng::graphics::SpriteIntent> {intents, 4u});
	check(n_intents == 2u, "torso attached -> dos intents");
	check(!intents[0].attach && intents[1].attach, "ATTACH solo en el impar");
	check(intents[0].top == 50u && intents[1].top == 50u && intents[0].hpos == 100u &&
		      intents[0].width_words == 1u,
	      "geometria del par attached");

	// Materializacion a BOB: solo las piernas (cookie-cut por defecto).
	eng::graphics::FramePlan plan {};
	const eng::graphics::BobTarget target = make_target();
	const eng::u16 drawn = eng::scene::composite_emit_bobs(
		vis, st, eng::Span<const eng::scene::Representation> {repr, 2u}, plan, target);
	check(drawn == 1u && plan.blit_job_count() == 1u, "una parte BOB");
	const eng::graphics::BlitJob& job = plan.blit_job(0u);
	check(job.kind == eng::graphics::BlitJobKind::MaskedBobCookieCut && job.minterm == 0xcau,
	      "piernas cookie-cut");
	check(job.words_per_row == 2u && job.height == 8u && job.bitplane_count == 2u,
	      "geometria del job (shift 100&15 -> 2 words)");

	// DATA del frame vigente: el frame 1 de las piernas apunta tras el stride.
	const eng::graphics::CompositeFrame* cur = eng::scene::composite_current_frame(vis, st);
	(void)cur;
	// Coloca el estado en el frame 1 de idle para comprobar el offset.
	eng::scene::composite_set_sequence(st, 0u);
	(void)eng::scene::composite_advance(vis, st, 2u);
	check(st.frame == 1u, "estado en frame 1");
	const eng::ChipView<eng::SpriteTag> data0 = eng::scene::composite_part_data(vis, st, 0u);
	check(data0.address(0).cptr() == reinterpret_cast<const eng::u8*>(g_legs + 16u),
	      "DATA del frame 1 = base + stride");
	const eng::ChipView<eng::SpriteTag> data1 = eng::scene::composite_part_data(vis, st, 1u);
	check(data1.address(0).cptr() == reinterpret_cast<const eng::u8*>(g_torso),
	      "DATA del torso (sin stride)");

	// Defecto sin planner: todas las partes a BOB.
	eng::graphics::FramePlan plan2 {};
	const eng::u16 drawn_all = eng::scene::composite_emit_bobs(vis, st, {}, plan2, target);
	check(drawn_all == 2u && plan2.blit_job_count() == 2u, "sin planner: dos BOBs");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: sprite compuesto (secuencias, hitboxes, intents y BOBs) validado.\n");
	return 0;
}
