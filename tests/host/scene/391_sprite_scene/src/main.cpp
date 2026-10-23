// ============================================================================
// Test HOST-391: fachada de sprites de nivel A (`eng::SpriteScene`) - F7.7.
// ============================================================================
//
// Respalda `eng/api/sprites.hpp`: el juego da de alta **actores** (`ActorDesc`) y llama `emit`, y
// el engine compone (HW sprites + `SpriteAllocator` + BOB fallback) sin que el juego declare los
// buffers de trabajo ni un `FramePlan`. Los actores que no caben en sprite hardware se **degradan**
// (`result.degraded`) o se dibujan como BOB.
//
// Cubre además la **integración F3 de cadenas DMA** por la fachada: `set_chains` +
// `prepare_chains` (Build) + `present` → los actores que comparten canal (mismo
// `preferred_channel`, franjas disjuntas) se sirven por cadena: un solo WAIT de armado y
// **cero rearmes** en las franjas siguientes (contraste con el camino de rearme).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/391_sprite_scene

#include <cstdio>

#include <eng/scene/sprite_scene.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

/// Hoja 16x16 a 2 planos en memoria host (escape documentado del test: en Amiga sería Chip).
/// Hoja 16x16 a 2 planos con guarda: 2·16·4 = 128 B (64 words) por frame.
eng::u16 g_sheet[32u * 2u] {};

alignas(16) eng::u8 g_chip[8 * 1024];

eng::MemoryManager make_memory() {
	eng::MemoryManager mem;
	mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
	return mem;
}

eng::copper::Scheduler make_scheduler(eng::MemoryManager& mem, eng::u32 words) {
	return eng::copper::Scheduler {mem.chip().reserve<eng::CopperTag>(words * 2u, 16u)};
}

/// Cuenta los WAIT de una línea en la copperlist.
unsigned count_wait_line(const eng::u16* w, eng::u16 count, eng::u16 line) {
	unsigned n = 0;
	for (eng::u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u && (w[i] >> 8u) == line) ++n;
	}
	return n;
}

} // namespace

int main() {
	std::printf("== HOST-391 sprite_scene ==\n");

	eng::SpriteScene<4u> sp {};
	// 2 canales de sprite hardware, sin capas, con presupuesto de BOB: 2 actores caben como sprite,
	// el resto degrada a BOB.

	for (eng::usize i = 0u; i < 32u; ++i) {
		g_sheet[i] = 0xffffu;
	}
	eng::graphics::Visual v {};
	v.pixels = eng::Span<const eng::u16> {g_sheet, 64u};
	v.w = 16u;
	v.h = 16u;
	v.bitplanes = 2u;
	v.frame_count = 1u;
	v.frame_stride = 0u;

	eng::scene::ActorDesc d {};
	d.visual = v;
	d.x = 10;
	d.y = 20;
	d.transparency = eng::scene::TransparencyMode::Mask1Bit;
	d.layout = eng::graphics::BobLayout::Planar;

	check(sp.add(d).valid(), "add (1)");
	check(sp.count() == 1u, "count = 1");
	// Un actor sin contenido se rechaza (no crece el conteo).
	eng::scene::ActorDesc bad {};
	check(!sp.add(bad).valid(), "add sin visual -> invalido");
	check(sp.count() == 1u, "count sigue 1");

	// Destino 320x256 a 2 planos (40 B/fila x 256 x 2 = 20480 B): cubre los actores hasta
	// y=140 del caso de conflicto/ristra.
	eng::u16 bitmap[320u * 32u] {};
	eng::graphics::FramePlan plan {};
	const eng::graphics::BobTarget target = eng::graphics::make_bob_target(
		eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(bitmap), sizeof(bitmap)},
		40u, 256u, 2u, eng::graphics::BobLayout::Planar);
	const eng::graphics::BobTarget targets[1] = {target};
	eng::scene::ActorEmitContext ctx {};
	ctx.targets = eng::Span<const eng::graphics::BobTarget> {targets, 1u};
	ctx.display_top = 0x2cu;

	const auto r = sp.emit(plan, ctx);
	check(r.ok, "emit ok");
	check(r.hw + r.degraded == 1u, "1 actor materializado (sprite o degradado)");
	check(sp.placements().size() == r.hw, "placements == sprites");

	check(sp.remove(sp.store().id_at(0u)), "remove");
	check(sp.count() == 0u, "count = 0 tras remove");

	// --- F3: cadenas DMA por la fachada (emit -> prepare_chains -> present) ---------------
	// Tres actores 16x8 con `preferred_channel = 0` y franjas disjuntas se sirven por UN
	// canal (multiplexado vertical): el caso de la ristra que la cadena optimiza.
	eng::SpriteScene<4u> ch_scene {};
	eng::graphics::Visual v8 = v;
	v8.h = 8u;
	v8.pixels = eng::Span<const eng::u16> {g_sheet, 32u}; // 8 líneas × 2 planos con guarda
	eng::scene::ActorDesc cd = d;
	cd.visual = v8;
	cd.preferred_channel = 0u;
	const eng::u16 ys[3] = {100u, 140u, 180u};
	for (eng::u16 y : ys) {
		cd.y = static_cast<eng::s16>(y);
		check(ch_scene.add(cd).valid(), "add actor de ristra");
	}
	eng::graphics::FramePlan plan_ch {};
	const auto rch = ch_scene.emit(plan_ch, ctx);
	check(rch.ok && rch.hw == 3u, "3 sprites compuestos");

	// Sin cadenas: present emite un rearme por franja (WAIT en 140 y 180).
	eng::MemoryManager mem_a = make_memory();
	eng::copper::Scheduler sched_a = make_scheduler(mem_a, 256u);
	ch_scene.present(sched_a, 32u);
	sched_a.end();
	check(count_wait_line(sched_a.data(), sched_a.words_used(), 32u) == 1u, "sin cadena: 1 armado");
	check(count_wait_line(sched_a.data(), sched_a.words_used(), 140u) == 1u, "sin cadena: rearme 140");
	check(count_wait_line(sched_a.data(), sched_a.words_used(), 180u) == 1u, "sin cadena: rearme 180");

	// Con cadenas: prepare construye la del canal 0 y present no rearma ninguna franja.
	alignas(4) eng::u16 chain_storage[64] {};
	eng::graphics::SpriteDmaChain chains[8] {};
	chains[0].storage = eng::ChipView<eng::SpriteTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(chain_storage),
		sizeof(chain_storage)};
	ch_scene.set_chains(eng::Span<eng::graphics::SpriteDmaChain> {chains, 8u},
			    eng::graphics::SpriteChainBudget {1000u});
	const eng::graphics::SpriteChainPrep prep = ch_scene.prepare_chains();
	check(prep.built == 1u && prep.kept == 0u, "prepare: 1 canal construido");
	check(prep.emit.strategy[0] == eng::graphics::SpriteEmitStrategy::Chain, "plan: Chain en 0");
	check(ch_scene.chains()[0].block_count == 3u && ch_scene.chains()[0].used != 0u,
	      "cadena de 3 bloques");

	eng::MemoryManager mem_b = make_memory();
	eng::copper::Scheduler sched_b = make_scheduler(mem_b, 256u);
	ch_scene.present(sched_b, 32u);
	sched_b.end();
	check(count_wait_line(sched_b.data(), sched_b.words_used(), 32u) == 1u, "con cadena: 1 armado");
	check(count_wait_line(sched_b.data(), sched_b.words_used(), 140u) == 0u, "con cadena: sin rearme 140");
	check(count_wait_line(sched_b.data(), sched_b.words_used(), 180u) == 0u, "con cadena: sin rearme 180");

	// Orden Y incremental: mover un actor y re-emitir mantiene el orden por `top` de los
	// placements (la fachada conserva la secuencia de actores entre frames como memoria).
	ch_scene.store().at(ch_scene.store().id_at(1u).index).desc.y = 108;
	const auto rch2 = ch_scene.emit(plan_ch, ctx);
	check(rch2.ok && rch2.hw == 3u, "re-emit con movimiento ok");
	{
		const auto ps = ch_scene.placements();
		bool sorted = true;
		for (eng::usize i = 1u; i < ps.size(); ++i) {
			if (ps[i].vstart < ps[i - 1u].vstart) sorted = false;
		}
		check(sorted, "placements ordenados por vstart (orden Y incremental)");
	}

	// --- F7: arbitraje de paleta de sprites (`ActorDesc::sprite_palette`) ---------------
	eng::u16 pal_a[32] {};
	eng::u16 pal_b[32] {};
	eng::u16 pal_c[32] {};
	for (eng::u16 i = 0u; i < 32u; ++i) {
		pal_a[i] = static_cast<eng::u16>(0x100u + i);
		pal_b[i] = static_cast<eng::u16>(0x200u + i);
		pal_c[i] = static_cast<eng::u16>(0x300u + i);
	}
	eng::SpriteScene<8u> pal_scene {};
	eng::scene::ActorDesc pa = d;
	pa.preferred_channel = 0u;
	pa.sprite_palette = eng::PaletteWords {pal_a, 32u};
	pa.y = 60;
	eng::scene::ActorDesc pb = pa;
	pb.sprite_palette = eng::PaletteWords {pal_b, 32u};
	pb.y = 140;
	check(pal_scene.add(pa).valid() && pal_scene.add(pb).valid(), "add paletas A/B");
	eng::graphics::FramePlan plan_pal {};
	const auto rpal = pal_scene.emit(plan_pal, ctx);
	check(rpal.ok && rpal.hw == 2u && rpal.degraded == 0u, "2 sprites con paletas A/B");
	check(rpal.palette_events == 1u, "un evento de conmutacion de paleta");
	const auto pev = pal_scene.palette_events();
	// La `y` del actor es la línea raster del sprite: la conmutación va en su `top`.
	check(pev.size() == 1u && pev[0].line == 140u, "conmutacion en la linea del segundo (y=140)");
	check(pev.size() == 1u && pev[0].first == 16u && pev[0].colors.size() == 4u,
	      "registros del par 0");
	check(pev.size() == 1u && pev[0].colors.data() == pal_b + 16u && pev[0].colors.size() == 4u,
	      "colores de B indexados por registro");

	// Conflicto simultáneo: C (canal 1, misma franja que B, paleta distinta y menor `z`)
	// degrada a BOB; B solo conmuta (A queda en otra franja).
	/// Máscara planar 16x16 con guarda: 4 B/fila × 16 = 64 B (32 words).
	eng::u16 g_mask[32] {};
	eng::graphics::Visual vm = v;
	vm.mask = eng::Span<const eng::u16> {g_mask, 32u};
	eng::SpriteScene<8u> pal_scene2 {};
	eng::scene::ActorDesc pc = pa;
	pc.visual = vm;
	pc.sprite_palette = eng::PaletteWords {pal_c, 32u};
	pc.preferred_channel = 1u;
	pc.y = 140;
	pc.z = 10; // se emite antes (menor z) → pierde el conflicto
	eng::scene::ActorDesc pb2 = pa;
	pb2.sprite_palette = pal_b;
	pb2.y = 140;
	eng::scene::ActorDesc pa2 = pa;
	pa2.y = 60;
	check(pal_scene2.add(pc).valid() && pal_scene2.add(pb2).valid() &&
		      pal_scene2.add(pa2).valid(),
	      "add paletas A/B/C en conflicto");
	eng::graphics::FramePlan plan_pal2 {};
	const auto rpal2 = pal_scene2.emit(plan_pal2, ctx);
	check(rpal2.ok, "compose con conflicto ok");
	check(rpal2.degraded == 1u && rpal2.bob == 1u, "el de menor z degrada a BOB");
	check(rpal2.hw == 2u, "A y B siguen como sprites");
	check(rpal2.palette_events == 1u, "B conmuta (A en otra franja)");

	// --- Telemetría de reparto: 8 canales × 5 franjas = 40 placements ------------------
	// Cada canal sirve un objeto por banda (Y separadas por 40 px > alto+gap): el allocator
	// debe reutilizarlo cinco veces y la telemetría del frame lo refleja sin contenedores.
	eng::SpriteScene<40u> grid {};
	for (eng::u8 band = 0u; band < 5u; ++band) {
		for (eng::u8 ch = 0u; ch < 8u; ++ch) {
			eng::scene::ActorDesc g = d;
			g.y = static_cast<eng::s16>(40u + band * 40u);
			g.x = static_cast<eng::s16>(16u + ch * 36u);
			g.preferred_channel = ch;
			g.assign_rank = 1u;
			if (!grid.add(g).valid()) {
				check(false, "alta de la rejilla 8x5");
				break;
			}
		}
	}
	eng::graphics::FramePlan plan_grid {};
	const auto rgrid = grid.emit(plan_grid, ctx);
	check(rgrid.ok && rgrid.degraded == 0u, "rejilla 8x5 sin degradados");
	check(rgrid.hw == 40u, "40 placements (8 canales x 5 franjas)");
	check(rgrid.channels_used == 0xffu, "telemetria: los 8 canales ocupados");
	check(rgrid.max_reuse == 5u, "telemetria: 5 reusos verticales por canal");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: SpriteScene (alta + compose + degrade + cadenas F3 + paleta F7) validada.\n");
	return 0;
}
