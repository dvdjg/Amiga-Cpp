// ============================================================================
// Test HOST-485: SpriteDirector (fachada por intención) + reporte + BandPalette.
// ============================================================================
//
// Cubre:
//   - VisualStore/SceneAssets con handles generacionales (sin `const Visual*`).
//   - SpriteDirector sobre SpriteScene + CompositeScene: simple, compuesto, attached,
//     tira horizontal, política BOB forzada y overflow a BOB.
//   - gap vertical de 1 línea (el allocator no reutiliza un canal contiguo).
//   - BandPalette con `top + scroll_y` y emisión a `copper::Plan`.
//   - SpriteLoadReport (canales, scanline, BOB, Copper, paleta, emisores).
//   - evaluate_static en `constexpr`/`static_assert`.
//   - present combinado (placements + paletas + cadenas opcionales).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/485_sprite_director

#include <cstdio>

#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/scene/sprite_director.hpp>
#include <eng/scene/sprite_pass.hpp>

namespace {

using eng::scene::SpriteStaticDesc;

/// Capacidad del doble buffer de Copper de este test (suficiente para pocas bandas).
constexpr eng::u32 kTestCopperBytes = 4096u;

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

constexpr eng::scene::SpriteHint hint_auto() {
	eng::scene::SpriteHint h {};
	h.policy = eng::scene::SpritePolicy::Sprite;
	return h;
}

constexpr eng::scene::SpriteHint hint_strip(eng::u8 id, eng::u8 index) {
	eng::scene::SpriteHint h = hint_auto();
	h.reuse = eng::scene::SpriteReuse::Horizontal;
	h.strip_id = id;
	h.strip_index = index;
	h.strip_span = 2u;
	return h;
}

constexpr SpriteStaticDesc kStatic[] {
	{40u, 48u, 1u, 0u, 1u, eng::scene::SpritePolicy::Sprite,
	 eng::scene::SpriteReuse::Auto},
	{48u, 56u, 1u, 0u, 1u, eng::scene::SpritePolicy::Sprite,
	 eng::scene::SpriteReuse::Auto},
	{80u, 96u, 2u, 0u, 1u, eng::scene::SpritePolicy::Sprite,
	 eng::scene::SpriteReuse::Auto},
};
constexpr auto kStaticReport = eng::scene::evaluate_static(
	eng::Span<const SpriteStaticDesc> {kStatic, 3u});
static_assert(kStaticReport.hw == 4u);
static_assert(kStaticReport.bob == 0u);
static_assert(kStaticReport.scanline_peak == 2u);
static_assert(kStaticReport.ok);

alignas(16) eng::u8 g_chip[16 * 1024];
alignas(16) eng::u16 g_bitmap[320u * 256u];

eng::MemoryManager make_memory() {
	eng::MemoryManager mem;
	mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
	return mem;
}

eng::graphics::BobTarget make_target() {
	return eng::graphics::make_bob_target(
		eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(g_bitmap),
			sizeof(g_bitmap)},
		40u, 256u, 2u, eng::graphics::BobLayout::Planar);
}

eng::graphics::Visual make_visual(eng::u16 h, eng::u8 planes, eng::u16 words) {
	eng::graphics::Visual v {};
	v.pixels = eng::Span<const eng::u16> {g_bitmap, words};
	v.w = 16u;
	v.h = h;
	v.bitplanes = planes;
	v.frame_count = 1u;
	v.frame_stride = 0u;
	return v;
}

void test_run() {
	using Director = eng::scene::SpriteDirector<8u, 16u, 4u, 8u, 32u, 16u>;
	Director dir {};
	Director::Assets& assets = dir.assets();

	// Visual simple 16x8 (2 planos): 8 filas * 2 planos * 2 words = 32 words.
	eng::u16 simple_pixels[32] {};
	for (eng::u16& w : simple_pixels) w = 0xffffu;
	eng::graphics::Visual simple = make_visual(8u, 2u, 32u);
	simple.pixels = eng::Span<const eng::u16> {simple_pixels, 32u};

	// Visual attached 16x16 (4 planos): 16*4 = 64 words.
	eng::u16 attached_pixels[64] {};
	for (eng::u16& w : attached_pixels) w = 0xaaaa;
	eng::graphics::Visual attached = make_visual(16u, 4u, 64u);
	attached.pixels = eng::Span<const eng::u16> {attached_pixels, 64u};

	eng::graphics::Visual v2 = simple;
	eng::u16 simple2_pixels[32] {};
	for (eng::u16& w : simple2_pixels) w = 0x5555u;
	v2.pixels = eng::Span<const eng::u16> {simple2_pixels, 32u};

	const auto id_simple = dir.add_visual(simple);
	const auto id_v2 = dir.add_visual(v2);
	const auto id_attached = dir.add_visual(attached);
	check(id_simple.valid() && id_v2.valid() && id_attached.valid(), "VisualStore add");
	check(assets.visual_count() == 3u, "VisualStore count");

	// Handle generacional: borrar y reutilizar invalida el id anterior.
	const auto old = id_v2;
	check(assets.remove_visual(id_v2), "VisualStore remove");
	check(!assets.valid(old), "id viejo invalido");
	const auto id_v2b = dir.add_visual(v2);
	check(id_v2b.valid() && id_v2b != old, "nuevo id con generacion");

	eng::scene::SpriteDesc d {};
	d.content = eng::scene::SpriteVisualRef::simple(id_simple);
	d.hint = hint_auto();
	d.transparency = eng::scene::TransparencyMode::Opaque;
	d.x = 40;
	d.y = 40;
	d.hint.sprite_priority = 2u;
	const auto s1 = dir.add(d);
	check(s1.valid(), "add simple 1");

	eng::scene::SpriteDesc d2 = d;
	d2.content = eng::scene::SpriteVisualRef::simple(id_v2b);
	d2.x = 80;
	d2.y = 48; // contiguo con el anterior: no debe compartir canal
	const auto s2 = dir.add(d2);
	check(s2.valid(), "add simple 2 (contiguo)");

	eng::scene::SpriteDesc da = d;
	da.content = eng::scene::SpriteVisualRef::simple(id_attached);
	da.x = 160;
	da.y = 100;
	const auto sa = dir.add(da);
	check(sa.valid(), "add attached");

	// Tira de dos tramos 16 px en la misma franja.
	eng::scene::SpriteDesc ds0 = d;
	ds0.content = eng::scene::SpriteVisualRef::simple(id_simple);
	ds0.hint = hint_strip(7u, 0u);
	ds0.x = 20;
	ds0.y = 140;
	eng::scene::SpriteDesc ds1 = ds0;
	ds1.hint = hint_strip(7u, 1u);
	ds1.x = 36;
	const auto str0 = dir.add(ds0);
	const auto str1 = dir.add(ds1);
	check(str0.valid() && str1.valid(), "add strip");

	// Política BOB forzada.
	eng::scene::SpriteDesc db = d;
	db.content = eng::scene::SpriteVisualRef::simple(id_simple);
	db.hint.policy = eng::scene::SpritePolicy::Bob;
	db.x = 240;
	db.y = 200;
	const auto sb = dir.add(db);
	check(sb.valid(), "add BOB forzado");

	// Compuesto de una parte.
	eng::graphics::CompositePart part {};
	part.visual = simple;
	part.offset_x = 0;
	part.offset_y = 0;
	part.z = 0;
	const eng::graphics::CompositePart parts[1] {part};
	const eng::graphics::CompositeFrame cframe {};
	const eng::graphics::CompositeSequence sequence {
		eng::Span<const eng::graphics::CompositeFrame> {&cframe, 1u}, true, 0xffu};
	eng::graphics::CompositeVisual cv {};
	cv.parts = eng::Span<const eng::graphics::CompositePart> {parts, 1u};
	cv.sequences = eng::Span<const eng::graphics::CompositeSequence> {&sequence, 1u};
	cv.anchor_x = 0;
	cv.anchor_y = 0;
	const auto id_composite = dir.add_composite(cv);
	check(id_composite.valid(), "CompositeStore add");
	eng::scene::SpriteDesc dc {};
	dc.content = eng::scene::SpriteVisualRef::composite_visual(id_composite);
	dc.x = 200;
	dc.y = 30;
	dc.sequence = 0u;
	const auto sc = dir.add(dc);
	check(sc.valid(), "add compuesto");
	check(dir.composite_state(sc).valid(), "estado compuesto");

	// Pool Chip para el par attached.
	eng::MemoryManager mem = make_memory();
	dir.set_cooked_pool(mem.chip().reserve<eng::SpriteTag>(2048u, 16u).mem_view());

	// Paleta por banda con scroll: top 60 + scroll 10 = línea 70.
	eng::u16 pal_play[2] {0x0f00u, 0x00f0u};
	eng::u16 pal_spr[2] {0x0f0fu, 0x00ffu};
	eng::scene::BandPalette band {};
	band.top = 60;
	band.height = 16;
	band.playfield = eng::PaletteWords {pal_play, 2u};
	band.sprites = eng::PaletteWords {pal_spr, 2u};
	band.scroll_y = 10;
	dir.set_band_palettes(eng::Span<const eng::scene::BandPalette> {&band, 1u});

	const eng::graphics::BobTarget target = make_target();
	const eng::graphics::BobTarget targets[1] {target};
	eng::scene::ActorEmitContext ctx {};
	ctx.targets = eng::Span<const eng::graphics::BobTarget> {targets, 1u};
	ctx.display_top = eng::copper::kPalVisibleFirstLine;

	eng::copper::Plan copper {};
	check(copper.begin(mem, eng::copper::PlanConfig {
				    .copper_bytes = kTestCopperBytes,
				    .first_line = eng::copper::kPalVisibleFirstLine,
			    }),
	      "Plan begin");
	copper.begin_frame();

	eng::graphics::FramePlan frame {};
	frame.clear();
	dir.set_budget(eng::scene::SpriteBudget {});
	const eng::scene::SpriteLoadReport report = dir.emit(frame, ctx, copper);
	check(report.ok, "emit ok");
	check(report.hw >= 4u, "hay sprites HW (simple + attached + tira)");
	check(report.degraded >= 1u || report.bob >= 1u, "BOB forzado/depurado");
	check(report.attached_pairs == 1u, "un par attached");
	check(report.strips == 2u, "dos tramos de tira");
	check(report.bands == 1u && report.band_lines == 16u, "banda contabilizada");
	check(report.palette_events >= 1u, "evento de paleta de banda");
	check(report.blit_words != 0u, "coste BOB medido");
	check(report.channels_used != 0u, "canales usados");
	check(report.scanline_peak != 0u, "scanline contabilizada");
	check(dir.placements().size() == report.hw, "placements == hw");

	// El gap: los dos primeros simples (y=40 y y=48, alto 8) son contiguos.
	// Deben estar en canales distintos.
	{
		const auto ps = dir.placements();
		bool found_a = false, found_b = false;
		eng::u8 ca = 0u, cb = 0u;
		for (eng::usize i = 0u; i < ps.size(); ++i) {
			if (ps[i].vstart == 40u) {
				ca = ps[i].channel;
				found_a = true;
			}
			if (ps[i].vstart == 48u) {
				cb = ps[i].channel;
				found_b = true;
			}
		}
		check(found_a && found_b && ca != cb, "gap: contiguos no comparten canal");
	}

	{
		bool found = false;
		for (const auto& p : dir.placements()) {
			if (p.vstart == 40u) {
				found = true;
				check(p.priority == 2u, "BPLCON2: prioridad del actor");
			}
		}
		check(found, "BPLCON2: actor con prioridad publicado");
	}

	// La banda se emitió como PaletteLine relativa/absoluta en el Plan.
	check(copper.intent_count() >= 1u, "Plan recibe PaletteLine de banda");
	copper.materialize();
	(void)copper.end_frame();
	check(copper.words() != 0u, "banda materializada");
	check(!copper.overflow(), "sin overflow de Copper");

	// Present combinado.
	eng::MemoryManager sched_mem = make_memory();
	eng::copper::Scheduler sched {sched_mem.chip().reserve<eng::CopperTag>(kTestCopperBytes, 16u)};
	dir.present(sched);
	sched.end();
	check(sched.words_used() != 0u, "present emite copperlist");

	// Reporte estático (mismo contrato) y presupuesto que falla.
	constexpr eng::graphics::BlitBudgetLimits blit_limits {.max_words = 123u};
	constexpr eng::scene::SpriteBudget derived = eng::scene::SpriteBudget::from_blit_limits(blit_limits);
	static_assert(derived.blit_words == 123u);

	constexpr eng::scene::SpriteBudget too_few {.channels = 1u, .scanline = 1u};
	constexpr auto bad = eng::scene::evaluate_static(
		eng::Span<const SpriteStaticDesc> {kStatic, 3u}, too_few);
	static_assert(!bad.ok);
	static_assert(bad.has(eng::scene::SpriteWarning::Channels) ||
		      bad.has(eng::scene::SpriteWarning::Scanline));

	// Baja y reciclaje generacional del director.
	check(dir.remove(s2), "remove simple 2");
	check(!dir.valid(s2), "id de entidad viejo invalido");
	check(dir.count() == 6u, "conteo tras remove");
}

void test_stable_channels() {
	using Director = eng::scene::SpriteDirector<4u, 4u, 1u, 1u, 8u, 4u>;
	Director dir {};
	eng::u16 pixels[32] {};
	for (eng::u16& w : pixels) w = 0xffffu;
	eng::graphics::Visual visual = make_visual(8u, 2u, 32u);
	visual.pixels = eng::Span<const eng::u16> {pixels, 32u};
	const auto id_visual = dir.add_visual(visual);
	check(id_visual.valid(), "stable: visual");

	auto add_actor = [&](eng::s16 x, eng::s16 y) {
		eng::scene::SpriteDesc d {};
		d.content = eng::scene::SpriteVisualRef::simple(id_visual);
		d.x = x;
		d.y = y;
		d.transparency = eng::scene::TransparencyMode::Opaque;
		return dir.add(d);
	};
	const auto a = add_actor(40, 40);
	const auto b = add_actor(80, 44);
	check(a.valid() && b.valid(), "stable: actores");
	const eng::graphics::BobTarget target = make_target();
	const eng::graphics::BobTarget targets[1] {target};
	eng::scene::ActorEmitContext ctx {};
	ctx.targets = eng::Span<const eng::graphics::BobTarget> {targets, 1u};
	ctx.display_top = eng::copper::kPalVisibleFirstLine;
	eng::graphics::FramePlan plan {};
	plan.clear();
	check(dir.emit(plan, ctx).ok, "stable: emit 1");
	eng::u8 ch_a = 0xffu;
	for (const auto& p : dir.placements()) {
		if (p.vstart == 40u) ch_a = p.channel;
	}
	check(ch_a != 0xffu, "stable: A publicado");
	check(dir.set_position(a, 40, 48), "stable: mover A");
	plan.clear();
	check(dir.emit(plan, ctx).ok, "stable: emit 2");
	eng::u8 ch_a2 = 0xffu;
	for (const auto& p : dir.placements()) {
		if (p.vstart == 48u) ch_a2 = p.channel;
	}
	check(ch_a2 == ch_a, "stable: A conserva canal al reordenar Y");
}

void test_band_and_deadline_contracts() {
	eng::u16 pal[2] {0x0f00u, 0x00f0u};
	eng::scene::BandPalette bands[2] {};
	for (eng::scene::BandPalette& b : bands) {
		b.height = 16u;
		b.sprites = eng::PaletteWords {pal, 2u};
	}
	bands[0].top = 100;
	bands[1].top = 108;
	eng::graphics::SpritePaletteEvent out[4] {};
	const auto overlap = eng::scene::emit_band_palettes(
		eng::Span<const eng::scene::BandPalette> {bands, 2u}, {},
		eng::Span<eng::graphics::SpritePaletteEvent> {out, 4u}, 0u, 256u);
	check(overlap.overlaps == 1u && !overlap.ok, "banda solapada se reporta");

	eng::scene::BandPalette oor {};
	oor.top = 250;
	oor.height = 20u;
	oor.sprites = eng::PaletteWords {pal, 2u};
	const auto out_of_range = eng::scene::emit_band_palettes(
		eng::Span<const eng::scene::BandPalette> {&oor, 1u}, {},
		eng::Span<eng::graphics::SpritePaletteEvent> {out, 4u}, 0u, 256u);
	check(out_of_range.out_of_range == 1u && !out_of_range.ok, "banda fuera de rango se reporta");

	const eng::scene::BlitDeadline deadline {100u, 120u, 32u};
	check(eng::scene::blit_deadline_ok(deadline, 32u), "deadline: tope exacto");
	check(!eng::scene::blit_deadline_ok(deadline, 33u), "deadline: palabras de mas");
	check(!eng::scene::blit_deadline_ok(eng::scene::BlitDeadline {120u, 100u, 32u}, 1u),
	      "deadline: ventana invertida");
}

/// `refresh_positions` debe actualizar la DATA del frame vigente (no solo X/Y) y
/// soportar pares *attached* recocinando el par cuando cambia el frame.
void test_refresh_animation() {
	using Director = eng::scene::SpriteDirector<2u, 2u, 1u, 1u, 8u, 4u>;
	Director dir {};
	eng::MemoryManager mem = make_memory();
	const eng::graphics::BobTarget target = make_target();
	const eng::graphics::BobTarget targets[1] {target};
	eng::scene::ActorEmitContext ctx {};
	ctx.targets = eng::Span<const eng::graphics::BobTarget> {targets, 1u};
	ctx.display_top = eng::copper::kPalVisibleFirstLine;

	// --- Simple animado: 2 frames de 16x8 y 2 planos (16 words/frame). ---
	eng::u16 simple_pixels[32] {};
	for (eng::u16 i = 0u; i < 16u; ++i) simple_pixels[i] = 0xffffu;
	for (eng::u16 i = 16u; i < 32u; ++i) simple_pixels[i] = 0xaaaau;
	eng::graphics::Visual simple {};
	simple.pixels = eng::Span<const eng::u16> {simple_pixels, 32u};
	simple.w = 16u;
	simple.h = 8u;
	simple.bitplanes = 2u;
	simple.frame_count = 2u;
	simple.frame_stride = 32u; // 16 words por frame

	constexpr eng::graphics::Frame kSimpleFrames[2] {
		{0u, 0u, 16u, 8u, 1u, 0u},
		{0u, 0u, 16u, 8u, 1u, 0u},
	};
	const eng::graphics::Animation simple_anim {
		eng::Span<const eng::graphics::Frame> {kSimpleFrames, 2u}, true};

	const auto simple_id = dir.add_visual(simple);
	eng::scene::SpriteDesc ds {};
	ds.content = eng::scene::SpriteVisualRef::simple(simple_id);
	ds.hint.policy = eng::scene::SpritePolicy::Sprite;
	ds.x = 40;
	ds.y = 40;
	ds.animation = &simple_anim;
	const auto actor = dir.add(ds);
	check(actor.valid(), "refresh: alta simple animado");

	eng::graphics::FramePlan plan {};
	plan.clear();
	const auto first = dir.emit(plan, ctx);
	check(first.ok && first.hw == 1u, "refresh: emit simple");
	check(dir.placements().size() == 1u, "refresh: un placement simple");
	const auto* data0 = reinterpret_cast<const eng::u16*>(
		dir.placements()[0].data.address(0).cptr());
	check(data0 == simple_pixels, "refresh: DATA del frame 0");

	dir.tick(1u); // avanza el estado de animación (no devuelve valor)
	check(dir.set_position(actor, 52, 44), "refresh: mover simple");
	check(dir.refresh_positions(ctx), "refresh: simple animado soportado");
	check(dir.placements()[0].hpos == 52u + eng::graphics::kSpriteScreenXOrigin,
	      "refresh: X actualizada");
	check(dir.placements()[0].vstart == 44u, "refresh: Y actualizada");
	const auto* data1 = reinterpret_cast<const eng::u16*>(
		dir.placements()[0].data.address(0).cptr());
	check(data1 == simple_pixels + 16u, "refresh: DATA del frame 1");

	dir.tick(1u); // vuelve al frame 0 (2 frames en bucle)
	check(dir.refresh_positions(ctx), "refresh: segundo refresh");
	const auto* data0b = reinterpret_cast<const eng::u16*>(
		dir.placements()[0].data.address(0).cptr());
	check(data0b == simple_pixels, "refresh: DATA vuelve al frame 0");

	// --- Par attached animado: 2 frames de 16x8 y 4 planos (planar, 32 words/frame). ---
	eng::u16 pair_pixels[64] {};
	for (eng::u16 plane = 0u; plane < 4u; ++plane) {
		for (eng::u16 row = 0u; row < 8u; ++row) {
			pair_pixels[plane * 8u + row] = static_cast<eng::u16>(0x1111u * (plane + 1u));
			pair_pixels[32u + plane * 8u + row] =
				static_cast<eng::u16>(0x2222u * (plane + 1u));
		}
	}
	eng::graphics::Visual pair {};
	pair.pixels = eng::Span<const eng::u16> {pair_pixels, 64u};
	pair.w = 16u;
	pair.h = 8u;
	pair.bitplanes = 4u;
	pair.frame_count = 2u;
	pair.frame_stride = 64u; // 32 words por frame

	constexpr eng::graphics::Frame kPairFrames[2] {
		{0u, 0u, 16u, 8u, 1u, 0u},
		{0u, 0u, 16u, 8u, 1u, 0u},
	};
	const eng::graphics::Animation pair_anim {
		eng::Span<const eng::graphics::Frame> {kPairFrames, 2u}, true};

	Director pair_dir {};
	pair_dir.set_cooked_pool(mem.chip().reserve<eng::SpriteTag>(2048u, 16u).mem_view());
	const auto pair_id = pair_dir.add_visual(pair);
	eng::scene::SpriteDesc dp {};
	dp.content = eng::scene::SpriteVisualRef::simple(pair_id);
	dp.hint.policy = eng::scene::SpritePolicy::Sprite;
	dp.x = 80;
	dp.y = 60;
	dp.animation = &pair_anim;
	const auto pair_actor = pair_dir.add(dp);
	check(pair_actor.valid(), "refresh: alta par attached");

	eng::graphics::FramePlan pair_plan {};
	pair_plan.clear();
	const auto pair_first = pair_dir.emit(pair_plan, ctx);
	check(pair_first.ok && pair_first.attached_pairs == 1u, "refresh: emit par");
	check(pair_dir.placements().size() == 2u, "refresh: dos placements del par");
	const auto* even0 = reinterpret_cast<const eng::u16*>(
		pair_dir.placements()[0].data.address(0).cptr());
	const auto* odd0 = reinterpret_cast<const eng::u16*>(
		pair_dir.placements()[1].data.address(0).cptr());
	check(even0[0] == 0x1111u, "refresh: par frame 0 (canal par)");
	check(odd0[0] == 0x3333u, "refresh: par frame 0 (canal impar)");

	pair_dir.tick(1u); // avanza el par al frame 1
	check(pair_dir.set_position(pair_actor, 96, 68), "refresh: mover par");
	check(pair_dir.refresh_positions(ctx), "refresh: par attached soportado");
	check(pair_dir.placements().size() == 2u, "refresh: sigue habiendo dos placements");
	check(pair_dir.placements()[0].hpos == 96u + eng::graphics::kSpriteScreenXOrigin,
	      "refresh: par X");
	check(pair_dir.placements()[1].hpos == 96u + eng::graphics::kSpriteScreenXOrigin,
	      "refresh: par X (impar)");
	check(pair_dir.placements()[0].vstart == 68u, "refresh: par Y");
	check(pair_dir.placements()[1].vstart == 68u, "refresh: par Y (impar)");
	check(!pair_dir.placements()[0].attach && pair_dir.placements()[1].attach,
	      "refresh: ATTACH del canal impar");
	const auto* even1 = reinterpret_cast<const eng::u16*>(
		pair_dir.placements()[0].data.address(0).cptr());
	const auto* odd1 = reinterpret_cast<const eng::u16*>(
		pair_dir.placements()[1].data.address(0).cptr());
	check(even1[0] == 0x2222u, "refresh: par recocinado al frame 1 (par)");
	check(odd1[0] == 0x6666u, "refresh: par recocinado al frame 1 (impar)");
}

/// La `SpriteDirectorPass` es orquestación, no una capa: entrega los placements y el
/// plan de BOB al host y usa el camino rápido de refresh cuando solo cambian posiciones.
void test_sprite_pass_forwarding() {
	struct Host {
		eng::scene::ActorEmitContext ctx {};
		eng::Span<const eng::graphics::HwSpritePlacement> placements {};
		eng::graphics::FramePlan* plan = nullptr;
		[[nodiscard]] eng::scene::ActorEmitContext sprite_context() const { return ctx; }
		void set_sprite_placements(
			eng::Span<const eng::graphics::HwSpritePlacement> p) { placements = p; }
		void attach_sprite_plan(eng::graphics::FramePlan& p) { plan = &p; }
	};

	using Director = eng::scene::SpriteDirector<2u, 2u, 1u, 1u, 8u, 4u>;
	Director dir {};
	Host host {};
	const eng::graphics::BobTarget target = make_target();
	host.ctx.targets = eng::Span<const eng::graphics::BobTarget> {&target, 1u};
	host.ctx.display_top = eng::copper::kPalVisibleFirstLine;

	eng::u16 pixels[32] {};
	for (eng::u16& w : pixels) w = 0xffffu;
	eng::graphics::Visual visual = make_visual(8u, 2u, 32u);
	visual.pixels = eng::Span<const eng::u16> {pixels, 32u};
	const auto visual_id = dir.add_visual(visual);
	eng::scene::SpriteDesc desc {};
	desc.content = eng::scene::SpriteVisualRef::simple(visual_id);
	desc.hint.policy = eng::scene::SpritePolicy::Sprite;
	desc.transparency = eng::scene::TransparencyMode::Opaque;
	desc.x = 40;
	desc.y = 40;
	const auto actor = dir.add(desc);
	check(actor.valid(), "pass: alta del actor");

	eng::MemoryManager mem = make_memory();
	int backend = 0; // la pasada no usa el backend: el host materializa
	eng::scene::SpriteDirectorPass<Director, Host, int> pass {dir, host};
	check(pass.begin(mem, backend), "pass: begin");
	pass.frame(backend);
	check(host.placements.size() == 1u, "pass: placements reenviados");
	check(host.plan != nullptr && host.plan->blit_job_count() == 0u,
	      "pass: plan de BOB ligado (vacío sin degradados)");
	check(pass.emit_count() == 1u && pass.refresh_count() == 0u, "pass: primer frame emite");
	check(pass.last_report().hw == 1u, "pass: reporte del director");

	check(dir.set_position(actor, 60, 40), "pass: mover actor");
	pass.frame(backend);
	check(pass.emit_count() == 1u && pass.refresh_count() == 1u, "pass: segundo frame refresca");
	check(host.placements.size() == 1u &&
		      host.placements[0].hpos == 60u + eng::graphics::kSpriteScreenXOrigin,
	      "pass: placement actualizado por refresh");

	pass.request_emit();
	pass.frame(backend);
	check(pass.emit_count() == 2u, "pass: request_emit fuerza emisión");

	eng::scene::SpriteDesc late = desc;
	late.x = 100;
	late.y = 80;
	const auto late_actor = dir.add(late);
	check(late_actor.valid(), "pass: alta dinámica");
	pass.frame(backend);
	check(pass.emit_count() == 3u, "pass: alta dinámica fuerza emisión");
	check(host.placements.size() == 2u, "pass: alta dinámica publica placements");
	check(dir.remove(late_actor), "pass: baja dinámica");
	pass.frame(backend);
	check(pass.emit_count() == 4u && host.placements.size() == 1u,
	      "pass: baja dinámica publica el conjunto nuevo");
}

/// El culling debe comparar el rect de pantalla, no el HSTART del sprite. Un actor a
/// x=224 es visible en 320 px; el `hpos` interno (224+0x81) no debe culled.
void test_cull_hstart() {
	using Director = eng::scene::SpriteDirector<2u, 2u, 1u, 1u, 8u, 4u>;
	Director dir {};
	eng::u16 pixels[32] {};
	for (eng::u16& w : pixels) w = 0xffffu;
	eng::graphics::Visual visual = make_visual(8u, 2u, 32u);
	visual.pixels = eng::Span<const eng::u16> {pixels, 32u};
	const auto vid = dir.add_visual(visual);
	eng::scene::SpriteDesc desc {};
	desc.content = eng::scene::SpriteVisualRef::simple(vid);
	desc.hint.policy = eng::scene::SpritePolicy::Sprite;
	desc.transparency = eng::scene::TransparencyMode::Opaque;
	desc.y = 40;
	desc.x = 224;
	const auto visible = dir.add(desc);
	desc.x = 400;
	const auto culled = dir.add(desc);
	check(visible.valid() && culled.valid(), "cull: alta de actores");

	const eng::graphics::BobTarget target = make_target();
	const eng::graphics::BobTarget targets[1] {target};
	eng::scene::ActorEmitContext ctx {};
	ctx.targets = eng::Span<const eng::graphics::BobTarget> {targets, 1u};
	ctx.display_top = eng::copper::kPalVisibleFirstLine;
	ctx.clip = eng::graphics::DirtyRect {0, 0, 320, 208};
	eng::graphics::FramePlan plan {};
	plan.clear();
	const auto report = dir.emit(plan, ctx);
	check(report.hw == 1u, "cull: actor a x=224 visible, x=400 fuera");
}

} // namespace

int main() {
	std::printf("== HOST-485 sprite_director ==\n");
	test_run();
	test_refresh_animation();
	test_sprite_pass_forwarding();
	test_cull_hstart();
	test_stable_channels();
	test_band_and_deadline_contracts();
	if (g_fail == 0) {
		std::printf("OK: SpriteDirector, reporte, banda y preflight estatico validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
