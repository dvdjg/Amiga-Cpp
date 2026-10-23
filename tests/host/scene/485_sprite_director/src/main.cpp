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

namespace {

using eng::scene::SpriteStaticDesc;

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
	ctx.display_top = 0x2cu;

	eng::copper::Plan copper {};
	check(copper.begin(mem, eng::copper::PlanConfig {4096u, 0x2cu}), "Plan begin");
	copper.begin_frame();

	eng::graphics::FramePlan frame {};
	frame.clear();
	dir.set_budget(eng::scene::SpriteBudget {8u, 8u, 60000u, 320u, 32u, 0u, 0u,
						 0xffffu, 0xffffu});
	const eng::scene::SpriteLoadReport report = dir.emit(frame, ctx, copper);
	check(report.ok, "emit ok");
	check(report.hw >= 4u, "hay sprites HW (simple + attached + tira)");
	check(report.degraded >= 1u || report.bob >= 1u, "BOB forzado/depurado");
	check(report.attached_pairs == 1u, "un par attached");
	check(report.strips == 2u, "dos tramos de tira");
	check(report.bands == 1u && report.band_lines == 16u, "banda contabilizada");
	check(report.palette_events >= 1u, "evento de paleta de banda");
	check(report.bob_words != 0u, "coste BOB medido");
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

	// La banda se emitió como PaletteLine relativa/absoluta en el Plan.
	check(copper.intent_count() >= 1u, "Plan recibe PaletteLine de banda");
	copper.materialize();
	(void)copper.end_frame();
	check(copper.words() != 0u, "banda materializada");
	check(!copper.overflow(), "sin overflow de Copper");

	// Present combinado.
	eng::MemoryManager sched_mem = make_memory();
	eng::copper::Scheduler sched {sched_mem.chip().reserve<eng::CopperTag>(4096u, 16u)};
	dir.present(sched, 32u);
	sched.end();
	check(sched.words_used() != 0u, "present emite copperlist");

	// Reporte estático (mismo contrato) y presupuesto que falla.
	constexpr eng::scene::SpriteBudget too_few {1u, 1u, 60000u, 320u, 32u, 0u, 0u};
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

} // namespace

int main() {
	std::printf("== HOST-485 sprite_director ==\n");
	test_run();
	test_band_and_deadline_contracts();
	if (g_fail == 0) {
		std::printf("OK: SpriteDirector, reporte, banda y preflight estatico validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
