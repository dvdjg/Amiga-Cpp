// ============================================================================
// Test HOST-453: actores sobre el anillo corkscrew (`eng::field/actor_target`)
// ============================================================================
//
// Cubre el **puente actor ↔ playfield X-Limited** (el hueco que descubrió la demo 121):
//   - `make_actor_target` construye el `BobTarget` del anillo interleaved (geometría
//     [línea][plano] con `display_offset`, como los blits de mundo);
//   - `actor_ring_y`/`actor_fits_ring` fijan el contrato de la costura (el rect debe
//     caber en el bucle sin cruzarla);
//   - un actor emitido con `scene::actor_emit` (BOB cookie-cut **pack interleaved**,
//     que es el formato que exige `bob_draw` en destinos interleaved) escribe EN LA
//     LÍNEA DEL ANILLO correcta y con la transparencia de la máscara, comprobado
//     píxel a píxel con el ejecutor software del plan.
//
//   bash tools/run-host-tests.sh tests/host/graphics/453_actor_ring

#include <cstdio>

#include <eng/field/actor_target.hpp>
#include <eng/scene/actor.hpp>

#include "../../../support/software_blitter.hpp"

using eng::s16;
using eng::u8;
using eng::u16;
using eng::u32;

using eng::scene::ActorEmitContext;
using eng::scene::ActorEmitStatus;
using eng::scene::ActorStore;
using eng::scene::BackgroundPolicy;
using eng::scene::RepresentationAllocator;
using eng::scene::RepresentationBudget;
using eng::scene::TransparencyMode;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// ---------------------------------------------------------------------------
//  Anillo sintético 32x8 visible sobre 16 líneas de bucle, 3 planos, bpr 4 B
// ---------------------------------------------------------------------------

constexpr u16 kPlanes = 3u;
constexpr u16 kBpr = 4u;
constexpr u16 kLines = 16u;      // display_height
constexpr u16 kOffset = 5u;      // display_offset: visible 0 -> anillo 5
constexpr u16 kViewW = 32u;
constexpr u16 kViewH = 8u;

alignas(16) u8 g_ring[kLines * kPlanes * kBpr] {};

eng::playfield::PlayfieldHardwareView make_view() {
	eng::playfield::PlayfieldHardwareView v {};
	v.bitplanes = eng::Address<eng::MemoryKind::Chip>::from_storage(g_ring);
	v.bitmap_bytes_per_row = kBpr;
	v.planes = u8(kPlanes);
	v.bitmap_height = kLines;
	v.display_height = kLines;
	v.display_offset = kOffset;
	v.viewport_w = kViewW;
	v.viewport_h = kViewH;
	v.plane_bytes = u32(sizeof(g_ring));
	return v;
}

// Actor de 16x8, 2 planos, con la diagonal como imagen (px r de la fila r) y máscara con
// la misma silueta. Hoja PACK INTERLEAVED: por fila y plano, [imagen w][máscara w].
alignas(16) u16 g_pairs[8u * 3u * 2u] {}; // 8 filas × 3 planos DESTINO × (1 img + 1 mask)

void cook_actor_pairs() {
	// El ejecutor software lee la memoria como palabras BIG-ENDIAN (convención Amiga):
	// los datos del test se cocinan con el byte swap para que la palabra \`bit\` se lea
	// exactamente así. (En el Amiga real la memoria ya está en BE.)
	//
	// Pack interleaved: por CADA fila y CADA plano del DESTINO (3), el par
	// [imagen w/16][máscara w/16]. El bob tiene 2 planos de arte: para el plano 2 del
	// destino la imagen va a 0 (transparente) y la máscara se repite.
	const auto be = [](u16 v) { return u16((v >> 8u) | (v << 8u)); };
	for (u16 r = 0u; r < 8u; ++r) {
		const u16 bit = u16(0x8000u >> r);
		u16* p = &g_pairs[u32(r) * 6u];
		p[0] = r < 4u ? be(bit) : 0u; // plano destino 0: diagonal (filas 0..3)
		p[1] = be(bit); // mascara
		p[2] = 0u;      // plano destino 1: transparente
		p[3] = be(bit); // mascara
		p[4] = 0u;      // plano destino 2: transparente
		p[5] = be(bit); // mascara
	}
}

// Frame único 16x8 de la animación del actor (`actor_current_frame` lo exige: sin
// `ActorDesc::animation` el rect queda vacío y la emisión es `Nothing`).
constexpr eng::scene::Frame kFrames[1] = {{0, 0, 16u, 8u}};
constexpr eng::scene::Animation kAnim {eng::Span<const eng::scene::Frame> {kFrames, 1u}, true};

template <class Tag = eng::BobTag, class T, eng::usize N>
eng::ChipView<Tag> chip_view(const T (&a)[N]) {
	return eng::ChipView<Tag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(a),
		static_cast<eng::usize>(N) * sizeof(T)};
}

// ---------------------------------------------------------------------------
//  1. Geometría del puente: target del anillo, ring_y y costura
// ---------------------------------------------------------------------------

void test_bridge_geometry() {
	const auto view = make_view();
	const eng::graphics::BobTarget t = eng::playfield::make_actor_target(view);
	check(t.row_bytes == kBpr, "target.row_bytes = bytes por fila y plano (corkscrew)");
	check(t.height == kLines, "target.height = display_height (bucle del anillo)");
	check(t.plane_count == u8(kPlanes), "target.plane_count = planos del campo");
	check(t.layout == eng::graphics::BobLayout::Interleaved, "target.layout = interleaved");
	check(t.data() == g_ring, "target.data() = base del anillo");

	check(eng::playfield::actor_ring_y(view, 0) == s16(kOffset), "ring_y(0) = display_offset");
	check(eng::playfield::actor_ring_y(view, 143) == 148, "ring_y(visible) = visible + offset");

	check(eng::playfield::actor_fits_ring(view, 7, 8), "rect de 8 filas en la linea 7 cabe");
	check(eng::playfield::actor_fits_ring(view, 8, 8), "justo en el limite (8+8=16) cabe");
	check(!eng::playfield::actor_fits_ring(view, 12, 8), "cruzar la costura (20>16) se rechaza");
	check(!eng::playfield::actor_fits_ring(view, -1, 8), "linea negativa se rechaza");
}

// ---------------------------------------------------------------------------
//  2. Emisión del actor sobre el anillo + ejecución software píxel a píxel
// ---------------------------------------------------------------------------

void test_actor_on_ring() {
	cook_actor_pairs();
	for (auto& b : g_ring) b = 0u;

	ActorStore<1> store;
	store.reset();
	RepresentationAllocator alloc {};
	alloc.reset(RepresentationBudget {8u, 10000u, 0u});

	eng::scene::ActorDesc d {};
	d.visual.pixels = eng::Span<const u16> {g_pairs, 48u};
	d.visual.w = 16u;
	d.visual.h = 8u;
	d.visual.bitplanes = 2u;
	d.x = 4;                        // píxeles VISIBLES
	d.y = 2;
	d.offset.y = s16(kOffset);      // visible -> anillo (contrato del puente)
	d.animation = &kAnim;
	d.transparency = TransparencyMode::Mask1Bit;
	d.background = BackgroundPolicy::None;
	const eng::scene::ActorId id = store.add(d, alloc);
	auto a = store.get(id);
	check(a.valid(), "actor creado");

	// Contrato del camino interleaved: el BOB del actor va con la hoja PACK interleaved
	// (imagen+máscara por fila y plano) — `bob_draw` rechaza el pack suelto en destinos
	// interleaved (dispatch documentado en `bob.hpp`).
	a->bob.sheet = chip_view(g_pairs);
	a->bob.mask = {};
	a->bob.width = 16u;
	a->bob.height = 8u;
	a->bob.planes = u8(2u);
	a->bob.layout = eng::graphics::BobLayout::Interleaved;
	a->bob.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
	a->bob.draw = eng::graphics::BobDraw::CookieCut;
	a->bob.frame_count = 1u;
	a->bob.frame_stride = 0u;

	eng::graphics::FramePlan plan {};
	plan.clear();
	const eng::graphics::BobTarget ring = eng::playfield::make_actor_target(make_view());
	ActorEmitContext ctx {};
	ctx.targets = eng::Span<const eng::graphics::BobTarget> {&ring, 1u};
	// El rect del actor va en LÍNEAS DEL ANILLO (offset incluido): el clip de emisión
	// debe cubrir el bucle del anillo, no solo la ventana visible.
	ctx.clip = eng::scene::DirtyRect {0, 0, s16(kViewW), s16(kLines)};
	ctx.cam_x = 0;
	ctx.cam_y = 0;
	(void)eng::scene::actor_tick(*a, 1u);

	eng::scene::DirtyRect rect {};
	const ActorEmitStatus st =
		eng::scene::actor_emit(plan, *a, ctx, eng::Ref<eng::scene::DirtyRect> {&rect});
	check(st == ActorEmitStatus::Ok, "emision OK sobre el anillo");
	check(rect.top == s16(2 + kOffset), "rect en la linea del anillo (visible+offset)");
	check(plan.blit_job_count() == 1u, "fondo None: solo el job de dibujo");

	const auto& draw = plan.blit_job(0);
	check(draw.kind == eng::graphics::BlitJobKind::MaskedBobCookieCut, "job cookie-cut");
	check(draw.words_per_row == 1u, "1 palabra por fila (16 px)");
	check(draw.height == 8u * 2u, "8 filas x 2 planos en un solo blit (interleaved)");
	// Destino = anillo en (ring_y=7, plano 0): byte (7*planes + 0)*bpr = 84.
	check(draw.destination.words() == reinterpret_cast<const u16*>(g_ring + 84u),
	      "destino en la linea del anillo (7*3+0)*4");
	check(draw.source.words() == g_pairs, "origen = primera mitad del pack (imagen)");
	check(draw.mask.words() == g_pairs + 1u, "mascara = segunda mitad del pack (+1 palabra)");

	// Ejecución software: píxeles exactos en el anillo (modelo interleaved = corkscrew).
	eng::test::SoftBitmap bm {g_ring, kBpr, kLines, u8(kPlanes), true, 0u};
	const eng::test::SoftBlitterResult res = eng::test::execute_plan(plan, bm);
	check(res.jobs_executed == 1u && !res.unsupported && !res.out_of_bounds,
	      "ejecucion software sin incidencias");
	// La diagonal del arte ocupa x = 4..7 (bits del BYTE ALTO de la palabra ya desplazada);
	// los píxeles que el barrel arrastraría del byte bajo dependen de la palabra siguiente
	// del pack (guarda) — ese borde lo cubren los tests del barrel (HOST-439/450).
	for (u16 r = 0u; r < 4u; ++r) {
		check(eng::test::soft_pixel(bm, s16(4 + r), s16(7 + r), 0u), "diagonal visible (plano 0)");
		check(!eng::test::soft_pixel(bm, s16(4 + r), s16(7 + r), 1u), "plano 1 transparente");
	}
	for (u16 r = 4u; r < 8u; ++r) {
		check(!eng::test::soft_pixel(bm, s16(4 + r), s16(7 + r), 0u), "filas sin arte intactas");
	}
	check(!eng::test::soft_pixel(bm, 3, 7, 0u), "pixel a la izquierda del actor intacto");
	check(!eng::test::soft_pixel(bm, 4, 6, 0u), "fila por encima del actor intacta");

	// La costura: el llamador debe rechazar antes de emitir (el emisor de actores aun no
	// parte el rect; el de mundo si).
	check(!eng::playfield::actor_fits_ring(make_view(), s16(kOffset + 12), 8u),
	      "rect que cruzaria la costura: la guarda del llamador lo rechaza");
}

} // namespace

int main() {
	test_bridge_geometry();
	test_actor_on_ring();
	if (g_fail == 0) {
		std::printf("OK: actor sobre el anillo corkscrew (puente, costura y pixeles).\n");
		return 0;
	}
	std::printf("[FAIL] %d comprobaciones\n", g_fail);
	return 1;
}
