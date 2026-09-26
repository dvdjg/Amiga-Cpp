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
//   bash tools/run-host-tests.sh tests/host/scene/072_actor

#include <cstdio>

#include <eng/scene/actor.hpp>

#include <eng/graphics/sprite.hpp>
#include <eng/graphics/sprite_manager.hpp>

namespace {

using eng::graphics::Animation;
using eng::graphics::BlitJobKind;
using eng::graphics::Bob;
using eng::graphics::BobDraw;
using eng::graphics::BobErase;
using eng::graphics::BobLayout;
using eng::graphics::BobMaskPack;
using eng::graphics::BobTarget;
using eng::graphics::CopperIntent;
using eng::graphics::CopperIntentKind;
using eng::graphics::DirtyRect;
using eng::graphics::Frame;
using eng::graphics::FramePlan;
using eng::graphics::SpriteAllocator;
using eng::graphics::SpriteIntent;
using eng::graphics::SpriteIntentSet;
using eng::graphics::HwSpritePaletteSwitch;
using eng::graphics::HwSpritePlacement;
using eng::graphics::SpriteManager;
using eng::graphics::HwSpriteSegment;
using eng::graphics::SpriteSlot;
using eng::graphics::HwSpriteTemplate;
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
alignas(16) eng::u16 g_pixel_pool[256];
alignas(16) eng::u8 g_chip_plan[32 * 1024];
alignas(16) eng::u8 g_matrix_sheet[4096];

/// Valores de los MOVEs a `reg`, en orden de aparicion.
unsigned collect_moves(const eng::u16* words, eng::u16 count, eng::u16 reg, eng::u16* out,
		       unsigned max) {
	unsigned n = 0;
	for (eng::u16 i = 0; i + 1u < count && n < max; i += 2u) {
		const eng::u16 w0 = words[i];
		if (w0 == 0xffffu) break;
		if ((w0 & 1u) != 0u) continue; // WAIT
		if (w0 == reg) out[n++] = words[i + 1u];
	}
	return n;
}
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

/// Composiciones de prueba: `g_targets[0]` es el playfield por defecto y `g_targets[1]`
/// un segundo playfield, para comprobar la selección por `ActorDesc::surface`.
BobTarget g_targets[2] {};

void use_targets(ActorEmitContext& ctx) {
	g_targets[0] = make_target();
	g_targets[1] = make_target();
	g_targets[1].base = g_screen + kPlaneBytes; // "otro" playfield
	ctx.targets = g_targets;
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
	CHECK(store.get(a).valid(), "get del id valido");
	CHECK(store.get(a)->actual == Representation::Sprite, "16x8 cabe como sprite");

	const ActorId b = store.add(make_desc(), alloc);
	const ActorId c = store.add(make_desc(), alloc);
	const ActorId d = store.add(make_desc(), alloc);
	CHECK(b.valid() && c.valid() && d.valid(), "hasta capacidad");
	CHECK(store.full(), "store lleno");
	const ActorId extra = store.add(make_desc(), alloc);
	CHECK(!extra.valid(), "sin capacidad devuelve id invalido");

	CHECK(store.remove(b), "baja valida");
	CHECK(!store.get(b).valid(), "id dado de baja no resuelve");
	CHECK(store.count() == 3u, "cuenta tras baja");

	const ActorId e = store.add(make_desc(), alloc);
	CHECK(e.valid(), "alta recicla slot");
	CHECK(e.index == b.index, "recicla el mismo slot");
	CHECK(e.generation != b.generation, "generacion nueva");
	CHECK(!store.get(b).valid(), "id viejo sigue invalido");
	CHECK(store.get(e).valid(), "id nuevo valido");

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
	auto a = store.get(id);

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
	auto a = store.get(id);

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
	auto a = store.get(id);
	a->prev[0] = DirtyRect {10, 20, 26, 28}; // caja previa (16x8)

	FramePlan plan {};
	plan.clear();
	ActorEmitContext ctx {};
	use_targets(ctx);
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
	// El objeto previo esta en x=10 (shift 10): la caja cubre base + la palabra extra.
	CHECK(clear.words_per_row == 2u, "2 palabras por fila (base + shift)");
	CHECK(clear.height == 8u, "altura de la caja previa");
	CHECK(clear.destination_modulo_bytes == 36, "modulo destino clear");
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
	auto a = store.get(id);
	eng::scene::actor_tick(*a, 1u); // pasa al frame 1 (x = 16 en la hoja)

	FramePlan plan {};
	plan.clear();
	ActorEmitContext ctx {};
	use_targets(ctx);
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
	auto a = store.get(id);

	FramePlan plan {};
	plan.clear();
	ActorEmitContext ctx {};
	use_targets(ctx);
	ctx.clip = DirtyRect {0, 0, 90, 256}; // el objeto acaba en x = 104
	CHECK(eng::scene::actor_emit(plan, *a, ctx) == ActorEmitStatus::Clipped, "recorte parcial rechazado");
	CHECK(plan.blit_job_count() == 0u, "sin jobs si no cabe entero");

	// Job inválido (destino sin base): el plan lo rechaza y el actor devuelve Full.
	plan.clear();
	a->prev[0] = DirtyRect {10, 20, 26, 28};
	ctx.clip = DirtyRect {};
	ctx.buffer = 0;
	g_targets[0].base = nullptr;
	CHECK(eng::scene::actor_emit(plan, *a, ctx) == ActorEmitStatus::Full, "rechazo controlado si un job no vale");
	CHECK(plan.blit_job_count() == 0u, "sin jobs cuando el destino no vale");

	// Superficie declarada fuera de la composición: rechazo controlado.
	g_targets[0].base = g_screen;
	ActorDesc lejos = make_desc();
	lejos.surface = 5u;
	ActorStore<2> store2;
	store2.reset();
	RepresentationAllocator alloc2 {};
	alloc2.reset(RepresentationBudget {8u, 10000u, 0u});
	const ActorId id2 = store2.add(lejos, alloc2);
	auto a2 = store2.get(id2);
	FramePlan plan2 {};
	plan2.clear();
	CHECK(eng::scene::actor_emit(plan2, *a2, ctx) == ActorEmitStatus::Full, "surface fuera de rango");
	CHECK(plan2.blit_job_count() == 0u, "sin jobs si la surface no existe");
}

void test_surface_selection_and_sprite_intent() {
	// El mismo actor dibujado en la superficie 1 (segundo playfield de un DPF).
	ActorStore<2> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});
	ActorDesc d = make_desc();
	d.surface = 1u;
	d.z = 10u;
	d.sprite_priority = 2u;
	const ActorId id = store.add(d, alloc);
	auto a = store.get(id);

	FramePlan plan {};
	plan.clear();
	ActorEmitContext ctx {};
	use_targets(ctx);
	ctx.cam_x = 10;
	ctx.cam_y = 10;
	DirtyRect rect {};
	CHECK(eng::scene::actor_emit(plan, *a, ctx, &rect) == ActorEmitStatus::Ok, "emision en superficie 1");
	const auto& draw = plan.blit_job(0);
	CHECK(draw.destination.words ==
	      reinterpret_cast<const eng::u16*>(g_screen + kPlaneBytes + 33u * kRowBytes + 10u),
	      "el BOB va al segundo playfield");

	// Proyeccion al camino de sprite hardware: prioridad frente a playfields y ancho.
	const Frame f = eng::scene::actor_current_frame(*a);
	const SpriteIntent si = eng::scene::actor_to_sprite_intent(*a, f, rect, 3u);
	CHECK(si.channel == 3u, "canal propuesto");
	CHECK(si.top == 33u && si.bottom == 41u, "franja vertical del sprite");
	CHECK(si.hpos == 88u, "X del sprite");
	CHECK(si.width_words == 1u && !si.attach, "16 px sin attach");
	CHECK(si.priority == 2u, "prioridad del sprite frente a los playfields");
}

void test_emit_save_under() {
	ActorStore<2> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});
	ActorDesc d = make_desc();
	d.background = BackgroundPolicy::SaveUnder;
	d.save[0] = eng::Span<eng::u16> {g_save, 64u};
	d.save_words_per_row = 2u; // con desplazamiento fino el blit cubre 1 palabra extra
	d.save_height = 8u;
	const ActorId id = store.add(d, alloc);
	auto a = store.get(id);
	a->prev[0] = DirtyRect {10, 20, 26, 28};

	FramePlan plan {};
	plan.clear();
	plan.set_blit_budget_limits({0xffffffffu, 0xffffffffu, 0xffffu, 8u});
	ActorEmitContext ctx {};
	use_targets(ctx);
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
	auto a2 = store2.get(id2);
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
	auto a = store.get(id);

	CopperIntent out[4] {};
	const eng::u8 n = eng::scene::actor_emit_copper(*a, 33, 0x2cu, out);
	CHECK(n == 2u, "dos intenciones de copper");
	CHECK(out[0].top == 0x2cu + 33u && out[0].bottom == 0x2cu + 37u, "ancla relativa a absoluta (0..4)");
	CHECK(out[1].top == 0x2cu + 37u && out[1].bottom == 0x2cu + 41u, "ancla relativa a absoluta (4..8)");
	CHECK(eng::scene::actor_emit_copper(*a, 33, 0x2cu, {out, 1u}) == 1u, "respeta la capacidad");
}

void test_emit_order_by_surface_and_z() {
	// Cuatro actores con (superficie, z) distintos: el orden de emisión debe agrupar por
	// superficie y, dentro de cada una, ir de atrás hacia delante.
	ActorStore<8> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 60000u, 0u});

	const auto add = [&](eng::u8 surface, eng::u8 z, eng::s16 y) {
		ActorDesc d = make_desc();
		d.anchor = {0, 0};
		d.offset = {0, 0};
		d.x = 0;
		d.y = y;
		d.surface = surface;
		d.z = z;
		return store.add(d, alloc);
	};
	add(1u, 200u, 200);
	add(0u, 50u, 50);
	add(0u, 200u, 120);
	add(1u, 10u, 10);

	ActorId order[8] {};
	const eng::u16 n = eng::scene::plan_actor_order(store, order);
	CHECK(n == 4u, "cuatro actores ordenados");
	CHECK(store.at(order[0].index).desc.surface == 0u && store.at(order[0].index).desc.z == 50u,
	      "1o: superficie 0, z 50");
	CHECK(store.at(order[1].index).desc.surface == 0u && store.at(order[1].index).desc.z == 200u,
	      "2o: superficie 0, z 200");
	CHECK(store.at(order[2].index).desc.surface == 1u && store.at(order[2].index).desc.z == 10u,
	      "3o: superficie 1, z 10");
	CHECK(store.at(order[3].index).desc.surface == 1u && store.at(order[3].index).desc.z == 200u,
	      "4o: superficie 1, z 200");

	FramePlan plan {};
	plan.clear();
	ActorEmitContext ctx {};
	use_targets(ctx);
	const eng::u16 emitted = eng::scene::emit_actors_in_order(plan, store, ctx, order);
	CHECK(emitted == 4u, "se emiten los cuatro");
	CHECK(plan.blit_job_count() == 4u, "un job por actor");
	CHECK(plan.blit_job(0).destination.words ==
	      reinterpret_cast<const eng::u16*>(g_screen + 50u * kRowBytes), "job 0: superficie 0, z 50");
	CHECK(plan.blit_job(1).destination.words ==
	      reinterpret_cast<const eng::u16*>(g_screen + 120u * kRowBytes), "job 1: superficie 0, z 200");
	CHECK(plan.blit_job(2).destination.words ==
	      reinterpret_cast<const eng::u16*>(g_screen + kPlaneBytes + 10u * kRowBytes),
	      "job 2: superficie 1, z 10");
	CHECK(plan.blit_job(3).destination.words ==
	      reinterpret_cast<const eng::u16*>(g_screen + kPlaneBytes + 200u * kRowBytes),
	      "job 3: superficie 1, z 200");

	// El orden no cabe en el buffer del llamador: rechazo controlado.
	CHECK(eng::scene::plan_actor_order(store, {order, 2u}) == 0u, "orden rechazado si no cabe");
}

void test_sprite_template_projection() {
	static eng::u16 tpl_bitmap[64] {};
	static const eng::u16 sw_colors[2] {0x0f0u, 0x00fu};
	HwSpriteTemplate<3, 2> tpl {};
	tpl.bitmap = eng::Span<const eng::u16> {tpl_bitmap, 64u};
	tpl.width_words = 2u;
	tpl.attach = true;
	tpl.add_segment(HwSpriteSegment {0u, 8u, 0u});
	tpl.add_segment(HwSpriteSegment {16u, 8u, 8u});
	tpl.add_switch(HwSpritePaletteSwitch {104u, sw_colors, 16u, 2u});

	SpriteIntent intents[4] {};
	CopperIntent copper[4] {};
	SpriteIntentSet set {};
	set.intents = intents;
	set.intent_capacity = 4u;
	set.copper = copper;
	set.copper_capacity = 4u;

	eng::graphics::sprite_template_to_intents(tpl, 3u, 100u, 40u, 2u, set);
	CHECK(set.intent_count == 2u, "una intencion por franja");
	CHECK(!set.overflow, "cabe en los buffers");
	CHECK(intents[0].top == 100u && intents[0].bottom == 108u, "franja 0 en 100..108");
	CHECK(intents[1].top == 109u && intents[1].bottom == 117u, "franja 1 tras el gap de 1 linea");
	CHECK(intents[0].channel == 3u && intents[0].hpos == 40u, "canal y X de la franja");
	CHECK(intents[0].width_words == 2u && intents[0].attach, "32 px con attach");
	CHECK(intents[0].priority == 2u, "prioridad frente a playfields");
	CHECK(set.copper_count == 2u, "rearme + cambio de paleta");
	CHECK(copper[0].kind == CopperIntentKind::SpriteRearm, "rearme de la 2a franja");
	CHECK(copper[0].top == 109u && copper[0].sprite_channel == 3u, "rearme en la linea 109");
	CHECK(copper[0].sprite_ptr == tpl_bitmap + 16u, "rearme apunta a la DATA de la franja");
	CHECK(copper[1].kind == CopperIntentKind::PaletteLine && copper[1].top == 104u, "paleta en la 104");
	CHECK(copper[1].first == 16u && copper[1].count == 2u, "COLOR16.. del par");

	// Buffers diminutos: se marca el desbordamiento y no se sale del array.
	SpriteIntent one_intent[1] {};
	CopperIntent one_copper[1] {};
	SpriteIntentSet tiny {};
	tiny.intents = one_intent;
	tiny.intent_capacity = 1u;
	tiny.copper = one_copper;
	tiny.copper_capacity = 1u;
	eng::graphics::sprite_template_to_intents(tpl, 0u, 0u, 0u, 0u, tiny);
	CHECK(tiny.overflow, "desbordamiento marcado");
	CHECK(tiny.intent_count == 1u, "solo cabe una franja");
}

void test_sprite_allocation_and_bob_fallback() {
	ActorStore<12> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 60000u, 0u});

	// 10 actores con la MISMA franja (solape total) y `z` creciente con el slot; cada
	// uno con su propia hoja, para poder identificar por la fuente quién se emite.
	for (eng::u16 i = 0; i < 10u; ++i) {
		ActorDesc d = make_desc();
		d.anchor = {0, 0};
		d.offset = {0, 0};
		d.x = 0;
		d.y = 100;
		d.surface = 0u;
		d.z = static_cast<eng::u8>(10u * (i + 1u));
		d.visual.pixels = eng::Span<const eng::u16> {g_pixel_pool + i * 16u, 16u};
		CHECK(store.add(d, alloc).valid(), "alta de actor para el reparto");
	}

	// Orden de emisión deliberadamente por `z` DESCENDENTE: así el orden por `top` de las
	// intenciones (empate) es el inverso del orden por `z` y se distinguen.
	ActorId order[12] {};
	for (eng::u16 i = 0; i < 10u; ++i) {
		order[i] = store.id_at(static_cast<eng::u16>(9u - i));
	}
	const eng::u16 n = 10u;

	ActorEmitContext ctx {};
	use_targets(ctx);

	SpriteIntent intents[12] {};
	eng::u16 intent_actor[12] {};
	CHECK(eng::scene::build_sprite_intents(store, {order, n}, ctx, intents, intent_actor) == 10u,
	      "una intencion por actor");
	CHECK(intents[0].top == intents[9].top, "franja solapada: mismo top");

	SpriteSlot slots[12] {};
	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 10u}, slots);
	CHECK(in_hw == 8u, "ocho caben en hardware");
	CHECK(slots[8].as_bob && slots[9].as_bob, "los dos ultimos de la intencion degradan");
	CHECK(intent_actor[8] == 1u && intent_actor[9] == 0u, "degradan los slots 1 y 0");

	FramePlan plan {};
	plan.clear();
	const eng::u16 emitted = eng::scene::emit_bob_fallbacks(plan, store, intent_actor, slots, 10u, ctx);
	CHECK(emitted == 2u, "se emiten los dos degradados como BOB");
	CHECK(plan.blit_job_count() == 2u, "un job por degradado");
	// Slots 0 (z=10) y 1 (z=20): por `z` primero el 0, aunque en la intencion iba después.
	CHECK(plan.blit_job(0).source.words ==
	      reinterpret_cast<const eng::u16*>(g_pixel_pool + 0u), "job 0: menor z (slot 0)");
	CHECK(plan.blit_job(1).source.words ==
	      reinterpret_cast<const eng::u16*>(g_pixel_pool + 16u), "job 1: mayor z (slot 1)");

	// Capacidad insuficiente para las intenciones: rechazo controlado.
	CHECK(eng::scene::build_sprite_intents(store, {order, n}, ctx, {intents, 4u}, intent_actor) == 0u,
	      "intents rechazadas si no caben");
}

void test_compose_sprites() {
	ActorStore<12> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 60000u, 0u});

	static const CopperIntent need {
		CopperIntentKind::PaletteLine, 0u, 4u, 0u,
		eng::PaletteWords {g_copper_colors, 2u}, 1u, 2u, 0, {}, 0u, nullptr};

	// 10 actores con solape total y su propia hoja: 8 caben, 2 degradan.
	for (eng::u16 i = 0; i < 10u; ++i) {
		ActorDesc d = make_desc();
		d.anchor = {0, 0};
		d.offset = {0, 0};
		d.x = 0;
		d.y = 100;
		d.surface = 0u;
		d.z = static_cast<eng::u8>(10u * (i + 1u));
		d.sprite_priority = 2u;
		d.visual.pixels = eng::Span<const eng::u16> {g_pixel_pool + i * 16u, 16u};
		d.copper = eng::Span<const CopperIntent> {&need, 1u};
		CHECK(store.add(d, alloc).valid(), "alta para componer");
	}

	ActorEmitContext ctx {};
	use_targets(ctx);

	FramePlan plan {};
	plan.clear();

	ActorId order[12] {};
	SpriteIntent intents[12] {};
	eng::u16 intent_actor[12] {};
	SpriteSlot slots[12] {};
	HwSpritePlacement placements[12] {};
	CopperIntent copper[16] {};
	eng::scene::SpriteComposeScratch sc {};
	sc.order = order;
	sc.intents = intents;
	sc.intent_actor = intent_actor;
	sc.slots = slots;
	sc.placements = placements;
	sc.copper = copper;

	const eng::scene::SpriteComposeResult res =
		eng::scene::compose_sprites(plan, store, ctx, 0x2cu, sc);
	CHECK(res.ok, "composicion OK");
	CHECK(res.sprites == 8u, "ocho sprites en hardware");
	CHECK(res.degraded == 2u, "dos degradados");
	CHECK(res.bobs == 2u, "dos dibujados como BOB");
	CHECK(plan.blit_job_count() == 2u, "dos jobs de BOB");
	CHECK(res.copper == 10u, "una necesidad de copper por actor");
	CHECK(copper[0].top == 0x2cu + 100u, "copper anclado a la Y del actor");

	CHECK(placements[0].channel == 0u && placements[7].channel == 7u, "canales 0..7");
	CHECK(placements[0].vstart == 100u && placements[0].hpos == 0u, "franja del placement");
	CHECK(placements[0].height == 8u, "altura del placement");
	CHECK(placements[0].priority == 2u, "prioridad en el placement");
	CHECK(placements[0].data == g_pixel_pool + 0u, "data del primer actor");

	// El emisor de sprites acepta los placements (sin tocar hardware).
	SpriteManager mgr {};
	CHECK(mgr.apply(placements, 8u) == 8u, "los ocho placements se aplican");
	CHECK(mgr.any_enabled(), "gestor con sprites habilitados");
	// El DMA de sprites es un unico bit (SPREN); no hay bits por canal.
	CHECK(mgr.dma_bits() == 0x0020u, "DMACON: SPREN");
	CHECK(mgr.apply(nullptr, 0u) == 0u, "sin placements no aplica nada");
}

void test_copper_priority_wiring() {
	static eng::u16 hi_cols[2] {0u, 0x0ccu};
	static eng::u16 lo_cols[2] {0u, 0x0aau};

	eng::MemorySystem mem {};
	mem.chip = eng::ChipArena {g_chip_plan, sizeof(g_chip_plan), eng::MemoryKind::Chip};
	eng::copper::Plan plan {};
	CHECK(plan.begin(mem, {4096u, 0x00u}), "plan.begin");

	ActorStore<4> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 60000u, 0u});
	CopperIntent hi {
		CopperIntentKind::PaletteLine, 0u, 0u, 0u,
		eng::PaletteWords {hi_cols, 2u}, 1u, 1u, 0, {}, 0u, nullptr};
	ActorDesc d = make_desc();
	d.copper = eng::Span<const CopperIntent> {&hi, 1u};
	d.surface = 0u;
	d.z = 200u;
	d.anchor = {0, 0};
	d.offset = {0, 0};
	d.y = 100;
	const ActorId id = store.add(d, alloc);
	auto a = store.get(id);
	CHECK(a.valid(), "actor con necesidad de copper");

	plan.begin_frame();
	CHECK(eng::scene::actor_add_copper(plan, *a, 100u, 0u) == 1u, "necesidad al Plan");
	// Un segundo intent a la MISMA linea y registro, con prioridad menor, anadido DESPUES:
	// el Plan debe emitirlo antes (la ultima escritura es la del actor, que tiene mas z).
	CopperIntent lo = hi;
	lo.colors = eng::PaletteWords {lo_cols, 2u};
	plan.add_prioritized(&lo, 1u, 0u, 1u);
	plan.materialize();
	CHECK(plan.end_frame(), "end_frame del plan de prioridades");
	const eng::u16 reg01 = static_cast<eng::u16>(eng::copper::Register::COLOR00) + 2u;
	eng::u16 vals[4] {0};
	const unsigned n = collect_moves(plan.active_words(), plan.words(), reg01, vals, 4);
	CHECK(n == 2u && vals[0] == 0x0aau && vals[1] == 0x0ccu,
	      "gana el actor (mayor z) aunque su intent se anadiese antes");

	// `compose_sprites` con un Plan: las necesidades van al Plan (con su prioridad) y no
	// al buffer del llamador.
	eng::copper::Plan cplan {};
	CHECK(cplan.begin(mem, {4096u, 0x00u}), "cplan.begin");
	ActorEmitContext ctx {};
	use_targets(ctx);
	ActorId order[4] {};
	SpriteIntent intents[4] {};
	eng::u16 intent_actor[4] {};
	SpriteSlot slots[4] {};
	HwSpritePlacement placements[4] {};
	eng::scene::SpriteComposeScratch sc {};
	sc.order = order;
	sc.intents = intents;
	sc.intent_actor = intent_actor;
	sc.slots = slots;
	sc.placements = placements;
	FramePlan frame {};
	frame.clear();
	const eng::scene::SpriteComposeResult res =
		eng::scene::compose_sprites(frame, store, ctx, 0x00u, sc, cplan);
	CHECK(res.copper == 1u, "una necesidad enrutada");
	CHECK(cplan.intent_count() == 1u, "el Plan la recibio con su prioridad");
}

/// Matriz de geometría del BOB a nivel de job: dibujo x layout x borrado y profundidad
/// 3..6, más los rechazos documentados. Fija el contrato de `bob_draw`/`bob_erase` (el
/// camino del actor se prueba en los demás casos).
void test_bob_job_matrix() {
	const auto mk = [](BobLayout layout, BobDraw draw, eng::u8 planes) {
		Bob b {};
		b.sheet = g_matrix_sheet;
		b.width = 48u;
		b.height = 32u;
		b.planes = planes;
		b.frame_count = 4u;
		b.frame_stride = 1u << 14;
		b.layout = layout;
		b.draw = draw;
		b.erase = BobErase::None;
		return b;
	};
	const auto tgt = [](BobLayout layout, eng::u8 planes = 4u) {
		BobTarget t {};
		t.base = g_screen;
		t.row_bytes = kRowBytes;
		t.plane_bytes = kPlaneBytes;
		t.planes = planes;
		t.layout = layout;
		return t;
	};

	// OR intercalado (un blit/objeto) en 3..6 planos.
	for (eng::u8 planes = 3u; planes <= 6u; ++planes) {
		FramePlan plan {};
		plan.clear();
		CHECK(bob_draw(plan, mk(BobLayout::Interleaved, BobDraw::Or, planes), 1u, 100, 64,
			       tgt(BobLayout::Interleaved, planes)), "OR intercalado dibuja");
		CHECK(plan.blit_job_count() == 1u, "OR intercalado: 1 blit");
		const auto& j = plan.blit_job(0);
		CHECK(j.kind == BlitJobKind::OrBlob, "kind OrBlob");
		CHECK(j.minterm == 0x00fcu, "minterm OR $FC");
		CHECK(j.interleaved, "flag interleaved");
		CHECK(j.bitplane_count == 1u, "una columna de canales");
		CHECK(j.height == 32u * planes, "altura = alto x planos");
		CHECK(j.words_per_row == 4u, "palabras/fila con shift 4");
		CHECK(j.source_shift == 4u, "shift 4");
		CHECK(j.destination_modulo_bytes == static_cast<eng::s16>(kRowBytes - 8u), "modulo destino");
		CHECK(j.source_modulo_bytes == 0, "modulo origen de hoja densa");
	}

	// Cookie-cut planar: N planos, stride de la hoja y palabra de guarda.
	{
		FramePlan plan {};
		plan.clear();
		Bob b = mk(BobLayout::Planar, BobDraw::CookieCut, 4u);
		b.mask = g_matrix_sheet;
		CHECK(bob_draw(plan, b, 0u, 32, 10, tgt(BobLayout::Planar)), "cookie-cut planar dibuja");
		const auto& j = plan.blit_job(0);
		CHECK(j.minterm == 0x00cau, "minterm cookie-cut $CA");
		CHECK(j.bitplane_count == 4u, "N planos en planar");
		CHECK(j.height == 32u, "altura = alto");
		CHECK(j.source_plane_stride_bytes == 32u * 8u, "stride de plano origen");
		CHECK(j.source_modulo_bytes == 2, "modulo origen con shift 0 (guarda)");
		CHECK(!j.interleaved, "planar sin flag interleaved");
	}

	// Cookie-cut interleaved "par" ([máscara][imagen] por fila de plano): 1 blit $CA.
	{
		FramePlan plan {};
		plan.clear();
		Bob b = mk(BobLayout::Interleaved, BobDraw::CookieCut, 4u);
		b.mask_pack = BobMaskPack::InterleavedPair;
		CHECK(bob_draw(plan, b, 0u, 3, 10, tgt(BobLayout::Interleaved)), "cookie-cut par dibuja");
		CHECK(plan.blit_job_count() == 1u, "cookie-cut par: 1 blit");
		const auto& j = plan.blit_job(0);
		CHECK(j.kind == BlitJobKind::MaskedBobCookieCut, "kind cookie-cut");
		CHECK(j.minterm == 0x00cau, "minterm $CA");
		CHECK(j.words_per_row == 3u, "3 palabras (48/16)");
		CHECK(j.height == 32u * 4u, "altura = alto x planos");
		CHECK(j.source_modulo_bytes == 6, "modulo origen = palabras*2");
		CHECK(j.destination_modulo_bytes == static_cast<eng::s16>(kRowBytes - 6u),
		      "modulo destino");
		CHECK(j.interleaved && j.bitplane_count == 1u, "intercalado de 1 columna");
		CHECK(j.mask.words == reinterpret_cast<const eng::u16*>(g_matrix_sheet),
		      "mascara = inicio de la hoja");
		CHECK(j.source.words == reinterpret_cast<const eng::u16*>(g_matrix_sheet) + 3u,
		      "imagen = mascara + palabras");
	}

	// Cookie-cut con destino intercalado: rechazado (documentado).
	{
		FramePlan plan {};
		plan.clear();
		CHECK(!bob_draw(plan, mk(BobLayout::Interleaved, BobDraw::CookieCut, 4u), 0u, 0, 0,
				tgt(BobLayout::Interleaved)), "cookie-cut intercalado se rechaza");
	}

	// Borrado por caja: 1 job intercalado / 1 job de N planos en planar.
	{
		FramePlan plan {};
		plan.clear();
		Bob b = mk(BobLayout::Interleaved, BobDraw::Or, 4u);
		b.erase = BobErase::ClearRect;
		CHECK(bob_erase(plan, b, 100, 64, tgt(BobLayout::Interleaved)), "borrado intercalado");
		CHECK(plan.blit_job_count() == 1u, "borrado intercalado: 1 blit");
		const auto& j = plan.blit_job(0);
		CHECK(j.minterm == 0x00u, "minterm clear $00");
		CHECK(j.height == 32u * 4u, "altura clear = alto x planos");
		CHECK(j.words_per_row == 4u, "palabras clear (base + shift)");
	}
	{
		FramePlan plan {};
		plan.clear();
		Bob b = mk(BobLayout::Planar, BobDraw::Or, 4u);
		b.erase = BobErase::ClearRect;
		CHECK(bob_erase(plan, b, 100, 64, tgt(BobLayout::Planar)), "borrado planar");
		CHECK(plan.blit_job_count() == 1u && plan.blit_job(0).bitplane_count == 4u,
		      "clear planar: 1 job de N planos");
	}

	// Sin borrado (aditivo) y entradas inválidas.
	{
		FramePlan plan {};
		plan.clear();
		const Bob b = mk(BobLayout::Interleaved, BobDraw::Or, 4u);
		CHECK(bob_erase(plan, b, 10, 10, tgt(BobLayout::Interleaved)) && plan.blit_job_count() == 0u,
		      "erase None no encola");
		Bob bad = b;
		bad.sheet = nullptr;
		CHECK(!bob_draw(plan, bad, 0u, 0, 0, tgt(BobLayout::Interleaved)), "sin hoja falla");
		CHECK(!bob_draw(plan, b, 9u, 0, 0, tgt(BobLayout::Interleaved)), "frame fuera de rango falla");
	}
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
	test_surface_selection_and_sprite_intent();
	test_emit_order_by_surface_and_z();
	test_sprite_template_projection();
	test_sprite_allocation_and_bob_fallback();
	test_compose_sprites();
	test_copper_priority_wiring();
	test_emit_save_under();
	test_copper_anchoring();
	test_bob_job_matrix();

	if (fails != 0) {
		std::printf("[FAIL] %d comprobaciones\n", fails);
		return 1;
	}
	std::printf("OK: actor (store generacional, politicas, geometria, emision y copper anclado).\n");
	return 0;
}
