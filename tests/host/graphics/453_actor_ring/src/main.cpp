// ============================================================================
// Test HOST-453: actores sobre el anillo corkscrew (`eng::field/actor_target`)
// ============================================================================
//
// Cubre el **puente actor ↔ playfield X-Limited** (el hueco que descubrió la demo 121):
//   - `make_actor_target` construye el `BobTarget` del anillo interleaved;
//   - `actor_ring_y`/`actor_fits_ring` fijan el contrato de la costura;
//   - emisión completa con el pack interleaved CON GUARDA
//     (`pack_interleaved_pair`, blit_job.hpp): los 16 subpíxeles exactos en las DOS
//     mitades del byte (la guarda a cero neutraliza el arrastre del barrel);
//   - **SaveUnder sobre el anillo**: fondo guardado/restaurado byte a byte al mover el
//     actor 16 veces (el gap que bloqueaba la migración de la 121);
//   - **Costura del anillo** (`bob_ring.hpp`, `ActorEmitContext::ring_height`): un rect
//     que cruza el final del bucle se reparte en dos trabajos (arriba + envuelto a la
//     línea 0) — dibujo, save-under y restore; y las variantes Or (hoja flat
//     interleaved y hoja planar apilada) para cubrir los tres convenios de avance.
//
//   bash tools/run-host-tests.sh tests/host/graphics/453_actor_ring

#include <cstdio>

#include <eng/field/actor_target.hpp>
#include <eng/graphics/bob_ring.hpp>
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
//  Anillos sintéticos
//  - ring32:  32x8 visibles, 3 planos, bpr 4 (prueba de dibujo y costura)
//  - ring64:  64x8 visibles, 3 planos, bpr 8 (subpíxeles 0..15 + SaveUnder)
// ---------------------------------------------------------------------------

constexpr u16 kPlanes = 3u;
constexpr u16 kLines = 16u;
constexpr u16 kOffset = 5u;

alignas(16) u8 g_ring32[kLines * kPlanes * 4u] {};
alignas(16) u8 g_ring64[kLines * kPlanes * 8u] {};

eng::playfield::PlayfieldHardwareView make_view(u8* ring, u16 bpr, u16 view_w) {
	eng::playfield::PlayfieldHardwareView v {};
	v.bitplanes = eng::Address<eng::MemoryKind::Chip>::from_storage(ring);
	v.bitmap_bytes_per_row = bpr;
	v.planes = u8(kPlanes);
	v.bitmap_height = kLines;
	v.display_height = kLines;
	v.display_offset = kOffset;
	v.viewport_w = view_w;
	v.viewport_h = 8u;
	v.plane_bytes = u32(kLines) * kPlanes * bpr;
	return v;
}

// Arte 16x8: plano 0 con dos píxeles por fila (columna r y 15-r) — cubre AMBAS mitades
// del byte para cualquier shift. Planos 1..2 transparentes (igual que un índice 1).
alignas(16) u8 g_art[16u * 8u] {};
alignas(16) u16 g_pack_host[8u * kPlanes * 4u] {}; // img+g+máscara+g por fila y plano
alignas(16) u16 g_pack_be[8u * kPlanes * 4u] {};   // copia BIG-ENDIAN (lectura del soft blitter)
alignas(16) u16 g_save[512] {};


[[nodiscard]] bool art_at(u16 r, u16 c) { return c == r || c == (15u - r); }

void cook_art() {
	for (u16 r = 0u; r < 8u; ++r) {
		for (u16 c = 0u; c < 16u; ++c) {
			g_art[u32(r) * 16u + c] = art_at(r, c) ? 1u : 0u;
		}
	}
	eng::graphics::pack_interleaved_pair<16u, 8u, u8(kPlanes)>(g_art, nullptr, g_pack_host);
	// El ejecutor software lee memoria BIG-ENDIAN (convención Amiga): byte swap al vuelo.
	const auto be = [](u16 v) { return u16((v >> 8u) | (v << 8u)); };
	for (u32 i = 0u; i < 8u * kPlanes * 4u; ++i) {
		g_pack_be[i] = be(g_pack_host[i]);
	}
}

// Frame 32x8 (ventana con guarda) de la animación del actor.
constexpr eng::scene::Frame kFrames[1] = {{0, 0, 32u, 8u}};
constexpr eng::scene::Animation kAnim {eng::Span<const eng::scene::Frame> {kFrames, 1u}, true};

template <class Tag = eng::BobTag, class T, eng::usize N>
eng::ChipView<Tag> chip_view(const T (&a)[N]) {
	return eng::ChipView<Tag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(a),
		static_cast<eng::usize>(N) * sizeof(T)};
}

/// Construye el actor del test con el pack cocinado (bob width 32 = 16 + guarda).
[[nodiscard]] eng::scene::ActorId make_pack_actor(ActorStore<1>& store) {
	eng::scene::ActorDesc d {};
	d.visual.pixels = eng::Span<const u16> {g_pack_be, 8u * kPlanes * 4u};
	d.visual.w = 16u;
	d.visual.h = 8u;
	d.visual.bitplanes = u8(kPlanes);
	d.x = 0;
	d.y = 2;                          // visible; el anillo sale del offset
	d.offset.y = s16(kOffset);        // visible -> anillo
	d.transparency = TransparencyMode::Mask1Bit;
	d.background = BackgroundPolicy::SaveUnder;
	d.save[0] = chip_view(g_save);
	d.save_words_per_row = 4u;        // 2 palabras + guarda de shift (x&15)
	d.save_height = 8u;
	d.animation = &kAnim;
	const eng::scene::ActorId id = store.add(d);
	return id;
}

/// Deja el `bob` del actor como pack interleaved CON GUARDA (contrato del camino).
void set_pack_bob(eng::scene::Actor& a) {
	a.bob.sheet = chip_view(g_pack_be);
	a.bob.mask = {};
	a.bob.width = 32u; // ventana 32 = 16 de arte + 16 de guarda (la guarda viaja DENTRO del width)
	a.bob.height = 8u;
	a.bob.planes = u8(kPlanes);
	a.bob.layout = eng::graphics::BobLayout::Interleaved;
	a.bob.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
	a.bob.draw = eng::graphics::BobDraw::CookieCut;
	a.bob.frame_count = 1u;
	a.bob.frame_stride = 0u;
}

// ---------------------------------------------------------------------------
//  1. Geometría del puente
// ---------------------------------------------------------------------------

void test_bridge_geometry() {
	const auto view = make_view(g_ring32, 4u, 32u);
	const eng::graphics::BobTarget t = eng::playfield::make_actor_target(view);
	check(t.row_bytes == 4u, "target.row_bytes = bytes por fila y plano");
	check(t.height == kLines, "target.height = display_height");
	check(t.plane_count == u8(kPlanes), "target.plane_count = planos");
	check(t.layout == eng::graphics::BobLayout::Interleaved, "target interleaved");
	check(t.data() == g_ring32, "target.data() = base del anillo");

	check(eng::playfield::actor_ring_y(view, 0) == s16(kOffset), "ring_y(0) = offset");
	check(eng::playfield::actor_ring_y(view, 143) == 148, "ring_y(visible) = +offset");
	check(eng::playfield::actor_fits_ring(view, 7, 8), "8 filas en linea 7 caben");
	check(eng::playfield::actor_fits_ring(view, 8, 8), "limite exacto cabe");
	check(!eng::playfield::actor_fits_ring(view, 12, 8), "costura se rechaza");
	check(!eng::playfield::actor_fits_ring(view, -1, 8), "linea negativa se rechaza");
}

// ---------------------------------------------------------------------------
//  2. Los 16 subpíxeles sobre el anillo (pack con guarda) + SaveUnder round-trip
// ---------------------------------------------------------------------------

void test_subpixels_and_save_under() {
	cook_art();
	for (auto& b : g_ring64) b = 0u;
	for (auto& s : g_save) s = 0u;

	ActorStore<1> store;
	store.reset();
	const eng::scene::ActorId id = make_pack_actor(store);
	auto a = store.get(id);
	check(a.valid(), "actor creado");
	set_pack_bob(*a);

	eng::graphics::FramePlan plan {};
	const eng::graphics::BobTarget ring = eng::playfield::make_actor_target(make_view(g_ring64, 8u, 64u));
	ActorEmitContext ctx {};
	ctx.targets = eng::Span<const eng::graphics::BobTarget> {&ring, 1u};
	// El rect va en líneas del anillo: el clip cubre el bucle completo.
	ctx.clip = eng::scene::DirtyRect {0, 0, 64, s16(kLines)};
	ctx.cam_x = 0;
	ctx.cam_y = 0;
	ctx.buffer = 0;

	eng::test::SoftBitmap bm {g_ring64, 8u, kLines, u8(kPlanes), true, 0u};

	// Barrido de los 16 subpíxeles, moviendo el actor de x = 16 a x = 31: en cada paso
	// SaveUnder restaura el fondo del paso anterior ANTES de guardar y dibujar.
	for (u16 s = 0u; s < 16u; ++s) {
		const s16 x = s16(16u + s);
		a->desc.x = x;
		plan.clear();
		eng::scene::DirtyRect rect {};
		(void)eng::scene::actor_tick(*a, 1u); // animación de 1 frame: no-op
		(void)eng::scene::actor_emit(plan, *a, ctx, eng::Ref<eng::scene::DirtyRect> {&rect});
		check(rect.left == x && rect.top == s16(2 + kOffset), "rect en x visible y linea del anillo");
		const eng::test::SoftBlitterResult res = eng::test::execute_plan(plan, bm);
		check(!res.unsupported && !res.out_of_bounds && res.jobs_executed >= 1u,
		      "plan ejecutado sin incidencias");
		// Píxeles exactos del arte en AMBAS mitades del byte.
		for (u16 r = 0u; r < 8u; ++r) {
			for (u16 c = 0u; c < 16u; ++c) {
				const bool want = art_at(r, c);
				const bool got = eng::test::soft_pixel(bm, s16(x + s16(c)), s16(7 + r), 0u);
				if (want != got) {
					std::printf("[PX] sub=%u r=%u c=%u want=%d got=%d\n", unsigned(s),
						    unsigned(r), unsigned(c), int(want), int(got));
					check(false, "pixel exacto (ambas mitades del byte)");
					return;
				}
			}
		}
	}

	// Tras 16 movimientos con SaveUnder, TODO el anillo salvo la caja del último actor
	// debe estar a cero: cada restauración devolvió el fondo byte a byte.
	{
		const s16 last_x = s16(16u + 15u);
		u32 dirty = 0u;
		for (u16 line = 0u; line < kLines; ++line) {
			for (u16 b = 0u; b < 8u; ++b) {
				const u32 off = (u32(line) * kPlanes) * 8u + b;
				const bool in_box = (line >= u32(kOffset + 2u)) && (line < u32(kOffset + 2u) + 8u) &&
						    (b >= u32(last_x & ~15) / 8u) && (b < (u32(last_x & ~15) + 32u) / 8u);
				if (!in_box && g_ring64[off] != 0u) {
					++dirty;
				}
			}
		}
		check(dirty == 0u, "restauraciones byte a byte (anillo limpio fuera de la caja actual)");
	}

	// Y el actor actual sigue exacto tras el último paso (ya verificado en el bucle).
}

// ---------------------------------------------------------------------------
//  Costura del anillo: el actor CRUZA el final del bucle
// ---------------------------------------------------------------------------
// `ActorEmitContext::ring_height` activa el reparto (`graphics/bob_ring.hpp`): el dibujo
// sale como dos trabajos (filas del arte 0..3 en las líneas 12..15 y filas 4..7
// envueltas a las líneas 0..3), el save-under guarda con la MISMA partición (las filas
// del buffer espejan las de la caja) y el restore posterior deja el anillo byte a byte.
void test_ring_split_actor() {
	cook_art();
	for (auto& b : g_ring64) b = 0u;

	ActorStore<1> store;
	store.reset();
	const eng::scene::ActorId id = make_pack_actor(store);
	auto a = store.get(id);
	set_pack_bob(*a);

	const eng::graphics::BobTarget ring =
		eng::playfield::make_actor_target(make_view(g_ring64, 8u, 64u));
	ActorEmitContext ctx {};
	ctx.targets = eng::Span<const eng::graphics::BobTarget> {&ring, 1u};
	ctx.clip = eng::scene::DirtyRect {0, 0, 64, s16(kLines)};
	ctx.buffer = 0;
	ctx.ring_height = kLines; // reparto en la costura activo

	eng::test::SoftBitmap bm {g_ring64, 8u, kLines, u8(kPlanes), true, 0u};
	const s16 x = 16;
	const u16 ring_y = u16(kLines - 4u); // 12: el rect de 8 filas cruza (4+4)
	a->desc.offset.y = 0;
	a->desc.y = s16(ring_y);
	a->desc.x = x;
	eng::graphics::FramePlan plan {};
	eng::scene::DirtyRect rect {};
	check(eng::scene::actor_emit(plan, *a, ctx, eng::Ref<eng::scene::DirtyRect> {&rect}) ==
		      eng::scene::ActorEmitStatus::Ok, "actor en la costura: emite");
	(void)eng::test::execute_plan(plan, bm);
	// Arte exacto con la línea del anillo envuelta: fila r -> (12 + r) % 16.
	for (u16 r = 0u; r < 8u; ++r) {
		for (u16 c = 0u; c < 16u; ++c) {
			const bool want = art_at(r, c);
			const s16 line = s16((u32(ring_y) + u32(r)) % u32(kLines));
			const bool got = eng::test::soft_pixel(bm, s16(x + s16(c)), line, 0u);
			if (want != got) {
				std::printf("[PX-costura] r=%u c=%u want=%d got=%d\n", unsigned(r),
					    unsigned(c), int(want), int(got));
				check(false, "actor partido en la costura: píxeles exactos");
				return;
			}
		}
	}
	check(true, "actor partido en la costura: píxeles exactos");

	// Mover a una posición SIN cruce: el restore del rect partido (prev) debe dejar el
	// anillo limpio fuera de la caja nueva, y la caja nueva correcta.
	plan.clear();
	a->desc.y = s16(kOffset);
	check(eng::scene::actor_emit(plan, *a, ctx, eng::Ref<eng::scene::DirtyRect> {&rect}) ==
		      eng::scene::ActorEmitStatus::Ok, "actor movido tras la costura: emite");
	(void)eng::test::execute_plan(plan, bm);
	for (u16 r = 0u; r < 8u; ++r) {
		for (u16 c = 0u; c < 16u; ++c) {
			const bool want = art_at(r, c);
			const bool got =
				eng::test::soft_pixel(bm, s16(x + s16(c)), s16(kOffset + r), 0u);
			if (want != got) {
				check(false, "actor movido: caja nueva exacta");
				return;
			}
		}
	}
	check(true, "actor movido: caja nueva exacta");
	// La caja VIEJA (líneas 12..15 y 0..3, columnas del actor) quedó restaurada: solo la
	// caja nueva puede tener píxeles (el arte vive en el plano 0; el resto de los planos
	// y columnas no se tocan).
	{
		const u32 xb0 = u32(x) >> 3u;        // byte inicial de la ventana (2)
		const u32 xb1 = (u32(x) + 32u) >> 3u; // byte final exclusivo (6)
		u32 dirty = 0u;
		for (u16 line = 0u; line < kLines; ++line) {
			for (u16 b = 0u; b < 8u; ++b) {
				const u32 off = (u32(line) * kPlanes) * 8u + b;
				const bool in_new_box =
					(line >= kOffset && line < kOffset + 8u) && (b >= xb0 && b < xb1);
				if (!in_new_box && g_ring64[off] != 0u) {
					++dirty;
				}
			}
		}
		check(dirty == 0u,
		      "restore partido byte a byte (anillo limpio fuera de la caja nueva)");
	}
}

// ---------------------------------------------------------------------------
//  Costura del anillo: variantes Or (hoja flat interleaved y planar apilada)
// ---------------------------------------------------------------------------
// Cubre los TRES convenios de avance de fuente del rango de filas (`draw_rows`):
//   - par (actor, arriba): avance = `planos · fila` por fila de arte;
//   - flat Or: igual que el par (filas físicas = planos del destino);
//   - planar Or: avance = `fila` (una fila de arte por fila de la hoja).
void test_ring_split_variants() {
	const auto be = [](u16 v) { return u16((v >> 8u) | (v << 8u)); };
	// Hoja flat interleaved (solo imagen): por fila de arte, una fila por plano del
	// destino, cada una de 1 palabra (arte 16 px) + 1 de guarda.
	alignas(16) u16 sheet_il[8u * kPlanes * 2u] {};
	// Hoja planar apilada: 1 bloque por plano, 8 filas por bloque (1 palabra + guarda).
	alignas(16) u16 sheet_pl[kPlanes * 8u * 2u] {};
	for (u16 r = 0u; r < 8u; ++r) {
		u16 img = 0u;
		for (u16 c = 0u; c < 16u; ++c) {
			if (art_at(r, c)) {
				img = u16(img | u16(0x8000u >> c));
			}
		}
		for (u16 p = 0u; p < kPlanes; ++p) {
			sheet_il[(u32(r) * kPlanes + p) * 2u] = (p == 0u) ? be(img) : 0u;
			sheet_il[(u32(r) * kPlanes + p) * 2u + 1u] = 0u; // guarda del barrel
			sheet_pl[(u32(p) * 8u + r) * 2u] = (p == 0u) ? be(img) : 0u;
			sheet_pl[(u32(p) * 8u + r) * 2u + 1u] = 0u; // guarda del barrel
		}
	}
	const u16 ring_y = u16(kLines - 4u);
	const s16 x = 16;

	// (a) Or flat interleaved sobre el anillo de 64 columnas (destino = g_ring64).
	{
		for (auto& b : g_ring64) b = 0u;
		eng::graphics::Bob b {};
		b.sheet = chip_view(sheet_il);
		b.width = 16u;
		b.height = 8u;
		b.planes = u8(kPlanes);
		b.layout = eng::graphics::BobLayout::Interleaved;
		b.draw = eng::graphics::BobDraw::Or;
		const eng::graphics::BobTarget ring =
			eng::playfield::make_actor_target(make_view(g_ring64, 8u, 64u));
		eng::graphics::FramePlan plan {};
		check(eng::graphics::bob_draw_ring(plan, b, 0u, x, s16(ring_y), kLines, ring),
		      "Or flat: emite partido");
		eng::test::SoftBitmap bm {g_ring64, 8u, kLines, u8(kPlanes), true, 0u};
		const eng::test::SoftBlitterResult res = eng::test::execute_plan(plan, bm);
		check(!res.unsupported && !res.out_of_bounds && res.jobs_executed == 2u,
		      "Or flat: dos trabajos (arriba + envuelto)");
		for (u16 r = 0u; r < 8u; ++r) {
			for (u16 c = 0u; c < 16u; ++c) {
				const bool want = art_at(r, c);
				const s16 line = s16((u32(ring_y) + u32(r)) % u32(kLines));
				const bool got = eng::test::soft_pixel(bm, s16(x + s16(c)), line, 0u);
				if (want != got) {
					check(false, "Or flat: píxeles exactos en la costura");
					return;
				}
			}
		}
		check(true, "Or flat: píxeles exactos en la costura");
	}

	// (b) Or planar apilado sobre un anillo planar de 16 líneas (1 plano, 8 B/fila).
	{
		alignas(16) u8 g_ring_pl[kLines * 8u] {};
		eng::graphics::Bob b {};
		b.sheet = chip_view(sheet_pl);
		b.width = 16u;
		b.height = 8u;
		b.planes = 1u;
		b.layout = eng::graphics::BobLayout::Planar;
		b.draw = eng::graphics::BobDraw::Or;
		const eng::graphics::BobTarget ring = eng::graphics::make_bob_target(
			chip_view<eng::PlaneTag>(g_ring_pl), 8u, kLines, 1u,
			eng::graphics::BobLayout::Planar);
		eng::graphics::FramePlan plan {};
		check(eng::graphics::bob_draw_ring(plan, b, 0u, x, s16(ring_y), kLines, ring),
		      "Or planar: emite partido");
		eng::test::SoftBitmap bm {g_ring_pl, 8u, kLines, 1u, false, 0u};
		const eng::test::SoftBlitterResult res = eng::test::execute_plan(plan, bm);
		check(!res.unsupported && !res.out_of_bounds && res.jobs_executed == 2u,
		      "Or planar: dos trabajos (arriba + envuelto)");
		for (u16 r = 0u; r < 8u; ++r) {
			for (u16 c = 0u; c < 16u; ++c) {
				const bool want = art_at(r, c);
				const s16 line = s16((u32(ring_y) + u32(r)) % u32(kLines));
				const bool got = eng::test::soft_pixel(bm, s16(x + s16(c)), line, 0u);
				if (want != got) {
					check(false, "Or planar: píxeles exactos en la costura");
					return;
				}
			}
		}
		check(true, "Or planar: píxeles exactos en la costura");
	}

	// Sin cruce, el wrapper mantiene la vía lineal: UN solo trabajo.
	{
		for (auto& b : g_ring64) b = 0u;
		eng::graphics::Bob b {};
		b.sheet = chip_view(sheet_il);
		b.width = 16u;
		b.height = 8u;
		b.planes = u8(kPlanes);
		b.layout = eng::graphics::BobLayout::Interleaved;
		b.draw = eng::graphics::BobDraw::Or;
		const eng::graphics::BobTarget ring =
			eng::playfield::make_actor_target(make_view(g_ring64, 8u, 64u));
		eng::graphics::FramePlan plan {};
		check(eng::graphics::bob_draw_ring(plan, b, 0u, x, s16(kOffset), kLines, ring),
		      "Or sin cruce: emite");
		eng::test::SoftBitmap bm {g_ring64, 8u, kLines, u8(kPlanes), true, 0u};
		const eng::test::SoftBlitterResult res = eng::test::execute_plan(plan, bm);
		check(!res.unsupported && !res.out_of_bounds && res.jobs_executed == 1u,
		      "sin cruce: UN trabajo (via lineal, coste cero)");
	}
}

} // namespace

int main() {
	test_bridge_geometry();
	test_subpixels_and_save_under();
	test_ring_split_actor();
	test_ring_split_variants();
	if (g_fail == 0) {
		std::printf("OK: actor sobre el anillo (puente, costura, 16 subpixeles exactos, "
			    "save-under y reparto en la costura).\n");
		return 0;
	}
	std::printf("[FAIL] %d comprobaciones\n", g_fail);
	return 1;
}
