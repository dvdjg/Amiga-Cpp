// ============================================================================
// Test HOST-072: `scene::actor` — estado retenido, políticas y emisión
// ============================================================================
//
// Verifica la capa de actores de `OBJECT_SYSTEM.md` (§14.1 a §14.6):
//
//   1. Almacén con handles generacionales (reciclado, capacidad, ids obsoletos).
//   2. Políticas de transparencia y de fondo (traducción y resolución de `Auto`).
//   3. Anclaje y offset: rect efectivo, y velocidad de animación entera.
//   4. Emisión al `FramePlan`: borrado de la caja previa, save-under, dibujo del BOB
//      con el origen del frame dentro de la hoja, y rechazo controlado por presupuesto.
//   5. Necesidades de Copper ancladas: de relativas al actor a líneas absolutas.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/072_actor

#include <cstdio>

#include <eng/scene/actor.hpp>

namespace {

using eng::graphics::Animation;
using eng::graphics::BlitJobKind;
using eng::graphics::BobLayout;
using eng::graphics::BobTarget;
using eng::graphics::CopperIntent;
using eng::graphics::CopperIntentKind;
using eng::graphics::DirtyRect;
using eng::graphics::Frame;
using eng::graphics::FramePlan;
using eng::graphics::Visual;
using eng::graphics::VisualKind;
using eng::scene::Actor;
using eng::scene::ActorDesc;
using eng::scene::ActorEmitContext;
using eng::scene::ActorEmitStatus;
using eng::scene::ActorId;
using eng::scene::ActorStore;
using eng::scene::BackgroundPolicy;
using eng::scene::Representation;
using eng::scene::RepresentationAllocator;
using eng::scene::RepresentationBudget;
using eng::scene::TransparencyMode;

constexpr eng::u16 kRowBytes = 40;
constexpr eng::u32 kPlaneBytes = 10240;

alignas(16) eng::u8 g_screen[4 * kPlaneBytes];
alignas(16) eng::u16 g_pixels[32];
alignas(16) eng::u16 g_mask[16];
alignas(16) eng::u16 g_save[64];
alignas(16) eng::u16 g_copper_colors[2] {};

constexpr Frame kFrames[2] = {
	Frame {0u, 0u, 16u, 8u, 1u, 0u},
	Frame {16u, 0u, 16u, 8u, 1u, 0u},
};

const Animation kAnim {eng::Span<const Frame> {kFrames, 2u}, true};

Visual make_visual() {
	Visual v {};
	v.kind = VisualKind::Bob;
	v.pixels = eng::Span<const eng::u16> {g_pixels, 32u};
	v.mask = eng::Span<const eng::u16> {g_mask, 16u};
	v.w = 16u;
	v.h = 8u;
	v.bitplanes = 1u;
	return v;
}

ActorDesc make_desc() {
	ActorDesc d {};
	d.visual = make_visual();
	d.animation = &kAnim;
	d.x = 100;
	d.y = 50;
	d.anchor = {5, 5};
	d.offset = {3, -2};
	d.transparency = TransparencyMode::ColorKey0;
	d.background = BackgroundPolicy::ClearRect;
	// Hoja de 2 frames de 16 px por fila: 2 palabras + guarda = 6 bytes.
	d.sheet_row_bytes = 6u;
	return d;
}

BobTarget make_target() {
	BobTarget t {};
	t.base = g_screen;
	t.row_bytes = kRowBytes;
	t.plane_bytes = kPlaneBytes;
	t.planes = 4u;
	t.layout = BobLayout::Planar;
	return t;
}

int fails = 0;

#define CHECK(cond, label)                                                                        \
	do {                                                                                      \
		if (!(cond)) {                                                                    \
			std::printf("[FAIL] %s (linea %d)\n", label, __LINE__);                   \
			++fails;                                                                  \
		}                                                                                 \
	} while (0)

void test_store() {
	ActorStore<4> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});

	const ActorId a = store.add(make_desc(), alloc);
	CHECK(a.valid(), "alta valida");
	CHECK(store.count() == 1u, "cuenta tras alta");
	CHECK(store.get(a) != nullptr, "get del id valido");
	CHECK(store.get(a)->actual == Representation::Sprite, "16x8 cabe como sprite");

	const ActorId b = store.add(make_desc(), alloc);
	const ActorId c = store.add(make_desc(), alloc);
	const ActorId d = store.add(make_desc(), alloc);
	CHECK(b.valid() && c.valid() && d.valid(), "hasta capacidad");
	CHECK(store.full(), "store lleno");
	const ActorId extra = store.add(make_desc(), alloc);
	CHECK(!extra.valid(), "sin capacidad devuelve id invalido");

	CHECK(store.remove(b), "baja valida");
	CHECK(store.get(b) == nullptr, "id dado de baja no resuelve");
	CHECK(store.count() == 3u, "cuenta tras baja");

	const ActorId e = store.add(make_desc(), alloc);
	CHECK(e.valid(), "alta recicla slot");
	CHECK(e.index == b.index, "recicla el mismo slot");
	CHECK(e.generation != b.generation, "generacion nueva");
	CHECK(store.get(b) == nullptr, "id viejo sigue invalido");
	CHECK(store.get(e) != nullptr, "id nuevo valido");

	CHECK(!store.remove(ActorId {0xffffu, 0u}), "baja de id invalido");

	// Sin contenido no se da de alta.
	ActorDesc sin_contenido = make_desc();
	sin_contenido.visual.pixels = {};
	const ActorId bad = store.add(sin_contenido, alloc);
	CHECK(!bad.valid(), "sin contenido no se da de alta");
}

void test_policies() {
	CHECK(eng::scene::transparency_plan(TransparencyMode::Opaque).minterm == 0xf0u, "minterm opaco");
	CHECK(!eng::scene::transparency_plan(TransparencyMode::Opaque).needs_mask, "opaco sin mascara");
	CHECK(eng::scene::transparency_plan(TransparencyMode::ColorKey0).minterm == 0xcau, "minterm key");
	CHECK(eng::scene::transparency_plan(TransparencyMode::Mask1Bit).needs_mask, "cookie-cut con mascara");
	CHECK(eng::scene::transparency_plan(TransparencyMode::AdditiveOr).minterm == 0xfcu, "minterm OR");
	CHECK(!eng::scene::transparency_plan(TransparencyMode::AdditiveOr).needs_mask, "OR sin mascara");

	CHECK(eng::scene::bob_draw_for(TransparencyMode::Opaque) == eng::graphics::BobDraw::Opaque, "draw opaco");
	CHECK(eng::scene::bob_draw_for(TransparencyMode::AdditiveOr) == eng::graphics::BobDraw::Or, "draw OR");
	CHECK(eng::scene::bob_draw_for(TransparencyMode::Mask1Bit) == eng::graphics::BobDraw::CookieCut, "draw cookie-cut");

	CHECK(eng::scene::resolve_background(BackgroundPolicy::Auto, 16u, 8u) == BackgroundPolicy::SaveUnder,
	      "auto pequeno -> save-under");
	CHECK(eng::scene::resolve_background(BackgroundPolicy::Auto, 64u, 64u) == BackgroundPolicy::ClearRect,
	      "auto grande -> clear");
	CHECK(eng::scene::resolve_background(BackgroundPolicy::None, 16u, 8u) == BackgroundPolicy::None,
	      "politica explicita se respeta");
}

void test_geometry() {
	ActorStore<2> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});
	const ActorId id = store.add(make_desc(), alloc);
	const Actor* a = store.get(id);

	CHECK(a->bob.draw == eng::graphics::BobDraw::CookieCut, "bob draw segun transparencia");
	CHECK(a->bob.mask != nullptr, "bob con mascara");
	CHECK(a->bob.width == 16u && a->bob.height == 8u, "bob con tamano del visual");

	const Frame f = eng::scene::actor_current_frame(*a);
	CHECK(f.w == 16u && f.h == 8u, "frame sin animar = visual");
	CHECK(f.x == 0u && f.y == 0u, "frame inicial en (0,0)");

	const DirtyRect r = eng::scene::actor_screen_rect(*a, f, 10, 10);
	CHECK(r.left == 88 && r.top == 33, "rect con ancla y offset");
	CHECK(r.right == 104 && r.bottom == 41, "rect con tamano del frame");

	CHECK(eng::scene::ring_physical(300, 320u) == 300, "anillo sin envolver");
	CHECK(eng::scene::ring_physical(330, 320u) == 10, "anillo envuelve");
	CHECK(eng::scene::ring_physical(-5, 320u) == 315, "anillo con negativos");
}

void test_animation_speed() {
	ActorStore<2> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});
	ActorDesc d = make_desc();
	d.anim_rate_num = 1u;
	d.anim_rate_den = 2u; // media velocidad
	const ActorId id = store.add(d, alloc);
	Actor* a = store.get(id);

	CHECK(!eng::scene::actor_tick(*a, 1u), "sin llegar al medio tick no avanza");
	CHECK(a->anim.index == 0u, "sigue en el frame 0");
	CHECK(eng::scene::actor_tick(*a, 1u), "al segundo tick avanza");
	CHECK(a->anim.index == 1u, "frame 1 tras un tick de animacion");
	CHECK(eng::scene::actor_tick(*a, 2u), "otro tick completo");
	CHECK(a->anim.index == 0u, "loop vuelve al frame 0");
}

void test_emit_clear() {
	ActorStore<2> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});
	const ActorId id = store.add(make_desc(), alloc);
	Actor* a = store.get(id);
	a->prev[0] = DirtyRect {10, 20, 26, 28}; // caja previa (16x8)

	FramePlan plan {};
	plan.clear();
	ActorEmitContext ctx {};
	ctx.target = make_target();
	ctx.cam_x = 10;
	ctx.cam_y = 10;
	ctx.buffer = 0;

	DirtyRect rect {};
	const ActorEmitStatus st = eng::scene::actor_emit(plan, *a, ctx, &rect);
	CHECK(st == ActorEmitStatus::Ok, "emision OK");
	CHECK(rect.left == 88 && rect.top == 33, "rect emitido");
	CHECK(plan.blit_job_count() == 2u, "clear + draw");

	const auto& clear = plan.blit_job(0);
	CHECK(clear.kind == BlitJobKind::ClearRect, "job 0 es borrado");
	CHECK(clear.minterm == 0x00u, "borrado D=0");
	CHECK(clear.words_per_row == 1u, "1 palabra por fila");
	CHECK(clear.height == 8u, "altura de la caja previa");
	CHECK(clear.destination_modulo_bytes == 38, "modulo destino clear");
	CHECK(clear.bitplane_count == 1u, "planos del BOB");
	CHECK(clear.destination.words == reinterpret_cast<const eng::u16*>(g_screen + 20 * kRowBytes + 0),
	      "destino del borrado en la caja previa");

	const auto& draw = plan.blit_job(1);
	CHECK(draw.kind == BlitJobKind::MaskedBobCookieCut, "job 1 es cookie-cut");
	CHECK(draw.minterm == 0xcau, "minterm cookie-cut");
	CHECK(draw.source_shift == 8u, "shift sub-byte de la X");
	CHECK(draw.words_per_row == 2u, "palabra extra por el shift");
	CHECK(draw.source_modulo_bytes == 2, "modulo origen de la hoja de 6 B");
	CHECK(draw.destination.words == reinterpret_cast<const eng::u16*>(g_screen + 33 * kRowBytes + 10),
	      "destino del dibujo");
	CHECK(draw.source.words == reinterpret_cast<const eng::u16*>(g_pixels), "origen del frame 0");
	CHECK(a->prev[0].left == 88 && a->prev[0].top == 33, "prev actualizado por buffer");
	CHECK(plan.dirty_rect_count() >= 1u, "dirty rect registrado");
}

void test_emit_frame_offset() {
	ActorStore<2> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});
	const ActorId id = store.add(make_desc(), alloc);
	Actor* a = store.get(id);
	eng::scene::actor_tick(*a, 1u); // pasa al frame 1 (x = 16 en la hoja)

	FramePlan plan {};
	plan.clear();
	ActorEmitContext ctx {};
	ctx.target = make_target();
	ctx.buffer = 1;
	CHECK(eng::scene::actor_emit(plan, *a, ctx) == ActorEmitStatus::Ok, "emision frame 1");
	const auto& draw = plan.blit_job(0);
	// Frame en x = 16 -> 2 bytes dentro de la misma fila de la hoja.
	CHECK(draw.source.words == reinterpret_cast<const eng::u16*>(reinterpret_cast<const eng::u8*>(g_pixels) + 2u),
	      "origen desplazado al frame 1 de la hoja");
	CHECK(draw.source_modulo_bytes == 2, "modulo origen de la hoja de 2 frames");
}

void test_emit_clipped_and_full() {
	ActorStore<2> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});
	const ActorId id = store.add(make_desc(), alloc);
	Actor* a = store.get(id);

	FramePlan plan {};
	plan.clear();
	ActorEmitContext ctx {};
	ctx.target = make_target();
	ctx.clip = DirtyRect {0, 0, 90, 256}; // el objeto acaba en x = 104
	CHECK(eng::scene::actor_emit(plan, *a, ctx) == ActorEmitStatus::Clipped, "recorte parcial rechazado");
	CHECK(plan.blit_job_count() == 0u, "sin jobs si no cabe entero");

	// Job inválido (destino sin base): el plan lo rechaza y el actor devuelve Full.
	plan.clear();
	a->prev[0] = DirtyRect {10, 20, 26, 28};
	ctx.clip = DirtyRect {};
	ctx.buffer = 0;
	ctx.target.base = nullptr;
	CHECK(eng::scene::actor_emit(plan, *a, ctx) == ActorEmitStatus::Full, "rechazo controlado si un job no vale");
	CHECK(plan.blit_job_count() == 0u, "sin jobs cuando el destino no vale");
}

void test_emit_save_under() {
	ActorStore<2> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});
	ActorDesc d = make_desc();
	d.background = BackgroundPolicy::SaveUnder;
	d.save[0] = eng::Span<eng::u16> {g_save, 64u};
	d.save_words_per_row = 1u;
	d.save_height = 8u;
	const ActorId id = store.add(d, alloc);
	Actor* a = store.get(id);
	a->prev[0] = DirtyRect {10, 20, 26, 28};

	FramePlan plan {};
	plan.clear();
	plan.set_blit_budget_limits({0xffffffffu, 0xffffffffu, 0xffffu, 8u});
	ActorEmitContext ctx {};
	ctx.target = make_target();
	ctx.cam_x = 10;
	ctx.cam_y = 10;
	ctx.buffer = 0;
	const ActorEmitStatus st = eng::scene::actor_emit(plan, *a, ctx);
	CHECK(st == ActorEmitStatus::Ok, "emision save-under OK");
	CHECK(plan.blit_job_count() == 3u, "restore + save + draw");

	CHECK(plan.blit_job(0).kind == BlitJobKind::RestoreRect, "job 0 restaura");
	CHECK(plan.blit_job(0).source.words == reinterpret_cast<const eng::u16*>(g_save), "restaura desde el buffer");
	CHECK(plan.blit_job(0).destination.words ==
	      reinterpret_cast<const eng::u16*>(g_screen + 20 * kRowBytes + 0), "restaura en la caja previa");
	CHECK(plan.blit_job(1).kind == BlitJobKind::CopyRect, "job 1 guarda el fondo");
	CHECK(plan.blit_job(1).destination.words == reinterpret_cast<const eng::u16*>(g_save), "guarda en el buffer");
	CHECK(plan.blit_job(2).kind == BlitJobKind::MaskedBobCookieCut, "job 2 dibuja");

	// Sin buffer de guardado: rechazo controlado.
	ActorDesc sin_buffer = d;
	sin_buffer.save[0] = {};
	ActorStore<2> store2;
	store2.reset();
	const ActorId id2 = store2.add(sin_buffer, alloc);
	Actor* a2 = store2.get(id2);
	FramePlan plan2 {};
	plan2.clear();
	CHECK(eng::scene::actor_emit(plan2, *a2, ctx) == ActorEmitStatus::Full, "sin buffer -> rechazo");
}

void test_copper_anchoring() {
	const CopperIntent needs[2] = {
		CopperIntent {CopperIntentKind::PaletteLine, 0u, 4u, 0u,
			      eng::PaletteWords {g_copper_colors, 2u}, 1u, 2u, 0, {}, 0u, nullptr},
		CopperIntent {CopperIntentKind::PaletteLine, 4u, 8u, 0u,
			      eng::PaletteWords {g_copper_colors, 2u}, 1u, 2u, 0, {}, 0u, nullptr},
	};

	ActorStore<2> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});
	ActorDesc d = make_desc();
	d.copper = eng::Span<const CopperIntent> {needs, 2u};
	const ActorId id = store.add(d, alloc);
	const Actor* a = store.get(id);

	CopperIntent out[4] {};
	const eng::u8 n = eng::scene::actor_emit_copper(*a, 33, 0x2cu, out, 4u);
	CHECK(n == 2u, "dos intenciones de copper");
	CHECK(out[0].top == 0x2cu + 33u && out[0].bottom == 0x2cu + 37u, "ancla relativa a absoluta (0..4)");
	CHECK(out[1].top == 0x2cu + 37u && out[1].bottom == 0x2cu + 41u, "ancla relativa a absoluta (4..8)");
	CHECK(eng::scene::actor_emit_copper(*a, 33, 0x2cu, out, 1u) == 1u, "respeta la capacidad");
}

} // namespace

int main() {
	test_store();
	test_policies();
	test_geometry();
	test_animation_speed();
	test_emit_clear();
	test_emit_frame_offset();
	test_emit_clipped_and_full();
	test_emit_save_under();
	test_copper_anchoring();

	if (fails != 0) {
		std::printf("[FAIL] %d comprobaciones\n", fails);
		return 1;
	}
	std::printf("OK: actor (store generacional, politicas, geometria, emision y copper anclado).\n");
	return 0;
}
