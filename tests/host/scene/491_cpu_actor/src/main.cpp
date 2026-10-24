// ============================================================================
// Test HOST-491: materialización CPU de actores (A500, copia 16-bit por palabras).
// ============================================================================
//
// Cubre:
//   - `ActorDesc::force_cpu` + `ActorStore::actor_emit` sin jobs de Blitter;
//   - X no alineada a 16 y palabra de guarda;
//   - borrado de la caja previa por CPU;
//   - `SpriteDirector` con `SpritePolicy::Cpu` (reporte `cpu`, sin canales HW);
//   - copia CPU enmascarada (cookie-cut) con máscara separable.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/491_cpu_actor

#include <cstdio>

#include <eng/graphics/cpu_blit.hpp>
#include <eng/scene/actor.hpp>
#include <eng/scene/sprite_director.hpp>

namespace {

using eng::graphics::BobTarget;
using eng::graphics::FramePlan;
using eng::graphics::Visual;
using eng::scene::ActorDesc;
using eng::scene::ActorEmitContext;
using eng::scene::ActorEmitStatus;
using eng::scene::ActorStore;
using eng::scene::BackgroundPolicy;
using eng::scene::SpriteDesc;
using eng::scene::SpritePolicy;
using eng::scene::TransparencyMode;

constexpr eng::u16 kRowBytes = 40u;
constexpr eng::u16 kHeight = 64u;
constexpr eng::u8 kPlanes = 2u;
constexpr eng::u32 kPlaneBytes = kRowBytes * kHeight;

alignas(16) eng::u8 g_screen[kPlaneBytes * kPlanes];
alignas(16) eng::u16 g_pixels[32];
alignas(16) eng::u16 g_mask[16];
alignas(16) eng::u16 g_compact[32];

int g_fail = 0;

void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("[FAIL] %s\n", msg);
		++g_fail;
	}
}

template <class Tag = eng::BobTag, class T, eng::usize N>
eng::ChipView<Tag> chip_view(const T (&a)[N]) {
	return eng::ChipView<Tag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(a),
		static_cast<eng::usize>(N) * sizeof(T)};
}

BobTarget make_target() {
	return eng::graphics::make_bob_target(chip_view<eng::PlaneTag>(g_screen), kRowBytes,
					      kHeight, kPlanes, eng::graphics::BobLayout::Planar);
}

void fill_art() {
	for (eng::u16 i = 0; i < 32u; ++i) {
		g_pixels[i] = 0u;
	}
	// Plano 0: 16 px opacos por fila, con la palabra de guarda a cero.
	for (eng::u16 row = 0; row < 8u; ++row) {
		g_pixels[row * 2u] = 0xffffu;
	}
}

Visual make_visual() {
	Visual v {};
	v.pixels = eng::Span<const eng::u16> {g_pixels, 32u};
	v.w = 16u;
	v.h = 8u;
	v.bitplanes = kPlanes;
	return v;
}

void clear_screen() {
	for (eng::usize i = 0; i < sizeof(g_screen); ++i) {
		g_screen[i] = 0u;
	}
}

void test_actor_emit_cpu() {
	clear_screen();
	FramePlan plan;
	ActorStore<2> store;
	ActorDesc d {};
	d.visual = make_visual();
	d.force_cpu = true;
	d.x = 10;
	d.y = 4;
	d.transparency = TransparencyMode::Opaque;
	d.background = BackgroundPolicy::None;
	const auto id = store.add(d);
	check(id.valid(), "alta CPU");

	BobTarget target = make_target();
	ActorEmitContext ctx {};
	ctx.targets = eng::Span<const BobTarget> {&target, 1u};
	eng::graphics::DirtyRect out {};
	const ActorEmitStatus st = eng::scene::actor_emit(plan, *store.get(id), ctx, out);
	check(st == ActorEmitStatus::Ok, "actor_emit CPU");
	check(plan.blit_budget().jobs == 0u, "CPU no encola BlitJobs");
	check(out.left == 10 && out.top == 4, "rect CPU");

	const eng::u16* plane0 = reinterpret_cast<const eng::u16*>(g_screen + 4u * kRowBytes);
	check(plane0[0] == 0x003fu, "ventana CPU word 0 (x=10)");
	check(plane0[1] == 0xffc0u, "ventana CPU word 1 (guarda)");
	const eng::u16* row3 = reinterpret_cast<const eng::u16*>(g_screen + 3u * kRowBytes);
	check(row3[0] == 0u && row3[1] == 0u, "fila anterior intacta");
	const eng::u16* plane1 = reinterpret_cast<const eng::u16*>(g_screen + kPlaneBytes + 4u * kRowBytes);
	check(plane1[0] == 0u && plane1[1] == 0u, "plano 1 vacío");

	// Mover y borrar la caja previa (ClearRect) por CPU.
	store.get(id)->desc.x = 11;
	store.get(id)->desc.background = BackgroundPolicy::ClearRect;
	check(eng::scene::actor_emit(plan, *store.get(id), ctx) == ActorEmitStatus::Ok,
	      "actor_emit CPU ClearRect");
	check(plane0[0] == 0x001fu && plane0[1] == 0xffe0u, "nueva ventana CPU x=11");
}

void test_cookie_cut() {
	clear_screen();
	for (eng::u16 i = 0; i < 16u; ++i) {
		g_mask[i] = 0u;
	}
	// Máscara: mitad superior opaca (bits 15..8), mitad inferior transparente.
	for (eng::u16 row = 0; row < 4u; ++row) {
		g_mask[row] = 0xff00u;
	}
	Visual v = make_visual();
	v.mask = eng::Span<const eng::u16> {g_mask, 16u};

	BobTarget target = make_target();
	const eng::graphics::Bob bob = eng::scene::bob_from_visual(v, eng::graphics::BobLayout::Planar,
								   TransparencyMode::Mask1Bit);
	const eng::graphics::CpuBlitResult r =
		eng::graphics::cpu_draw_bob(target, bob, 0u, 0, 0);
	check(r.ok, "cookie-cut CPU");
	const eng::u16* plane0 = reinterpret_cast<const eng::u16*>(g_screen);
	check(plane0[0] == 0xff00u, "máscara opaca fila 0");
	check(plane0[4u * (kRowBytes / 2u)] == 0u, "máscara transparente fila 4");
}

void test_compact_bob_sheet() {
	clear_screen();
	// Hoja BOB planar compacta 16x16: 2 planos contiguos de 16 palabras, sin guarda.
	for (eng::u16 i = 0u; i < 32u; ++i) {
		g_compact[i] = 0u;
	}
	for (eng::u16 row = 0u; row < 16u; ++row) {
		g_compact[row] = 0xffffu;
	}
	Visual v {};
	v.pixels = eng::Span<const eng::u16> {g_compact, 32u};
	v.w = 16u;
	v.h = 16u;
	v.bitplanes = 2u;

	ActorDesc d {};
	d.visual = v;
	d.force_cpu = true;
	d.x = 256;
	d.y = 8;
	d.sheet_row_bytes = 2u;
	d.transparency = TransparencyMode::Opaque;
	d.background = BackgroundPolicy::None;

	ActorStore<1> store;
	const auto id = store.add(d);
	check(id.valid(), "alta CPU compacta");
	BobTarget target = make_target();
	ActorEmitContext ctx {};
	ctx.targets = eng::Span<const BobTarget> {&target, 1u};
	FramePlan plan;
	check(eng::scene::actor_emit(plan, *store.get(id), ctx) == ActorEmitStatus::Ok,
	      "CPU con hoja compacta sin guarda");
	const eng::u16* row8 = reinterpret_cast<const eng::u16*>(g_screen + 8u * kRowBytes);
	check(row8[16] == 0xffffu, "píxel CPU compacto");
}

void test_director_cpu() {
	clear_screen();
	eng::scene::SpriteDirector<2u, 2u, 1u, 1u, 8u, 4u> director;
	const eng::scene::VisualId vid = director.add_visual(make_visual());
	check(vid.valid(), "visual CPU");

	SpriteDesc d {};
	d.content = eng::scene::SpriteVisualRef::simple(vid);
	d.hint.policy = SpritePolicy::Cpu;
	d.x = 10;
	d.y = 4;
	d.transparency = TransparencyMode::Opaque;
	d.background = BackgroundPolicy::None;
	const eng::scene::SpriteId sid = director.add(d);
	check(sid.valid(), "entidad CPU");

	FramePlan plan;
	BobTarget target = make_target();
	ActorEmitContext ctx {};
	ctx.targets = eng::Span<const BobTarget> {&target, 1u};
	const eng::scene::SpriteLoadReport rep = director.emit(plan, ctx);
	check(rep.ok, "director CPU ok");
	check(rep.cpu == 1u && rep.hw == 0u && rep.bob == 0u, "reporte CPU");
	check(rep.parts == 1u && rep.emitter_cpu == 1u, "partes/emisor CPU");
	check(plan.blit_budget().jobs == 0u, "director no encola Blitter");
	const eng::u16* plane0 = reinterpret_cast<const eng::u16*>(g_screen + 4u * kRowBytes);
	check(plane0[0] == 0x003fu, "director copia píxeles");

	eng::scene::SpriteDirector<1u, 1u, 1u, 1u, 1u, 1u> no_cpu;
	eng::scene::SpriteCapabilities caps {};
	caps.cpu = false;
	no_cpu.set_capabilities(caps);
	const auto no_cpu_vid = no_cpu.add_visual(make_visual());
	SpriteDesc nd = d;
	nd.content = eng::scene::SpriteVisualRef::simple(no_cpu_vid);
	check(!no_cpu.add(nd).valid(), "CPU rechazada si el backend no la declara");
}

} // namespace

int main() {
	fill_art();
	test_actor_emit_cpu();
	test_cookie_cut();
	test_compact_bob_sheet();
	test_director_cpu();
	if (g_fail == 0) {
		std::printf("OK: HOST-491 actor CPU validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones.\n", g_fail);
	return 1;
}

