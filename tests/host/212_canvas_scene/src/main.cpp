// Test host de `scene::compose` con layout *interleaved* (`scene::canvas`): expone una
// `Surface` de dibujo sobre un `CanvasPlayfield` (bitmap interleaved) + su copperlist.
// Cierra el hueco «efecto -> dibujo con Surface» sin depender de `CanvasScene`.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/212_canvas_scene

#include <cstdio>

#include <eng/graphics/scene/compose.hpp>

using namespace eng;
using eng::graphics::scene::Scene;
using eng::graphics::scene::SceneResources;

namespace {

alignas(16) u8 g_chip[512 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

/// Color (0..15) de un pixel del bitmap interleaved del lienzo.
u8 color_at(const field::CanvasPlayfield& pf, s32 x, s32 y) {
	const u8* base = pf.bitplanes().data();
	const u32 row = pf.bytes_per_row();
	const u32 planes = pf.planes();
	const u32 byte = static_cast<u32>(y) * planes * row +
			 (static_cast<u32>(x / 8) & ~1u);
	const u16 mask = static_cast<u16>(0x8000u >> (x & 15));
	u8 c = 0;
	for (u8 p = 0; p < planes; ++p) {
		const u16 v = *reinterpret_cast<const u16*>(base + byte + static_cast<u32>(p) * row);
		if ((v & mask) != 0u) c |= static_cast<u8>(1u << p);
	}
	return c;
}

/// Color (0..15) de un pixel del bitmap CONTIGUO de la escena (planos uno tras otro).
u8 color_at_contiguous(const Scene& sc, s32 x, s32 y) {
	const u32 row = static_cast<u32>(sc.width() / 8u) & ~1u;
	const u32 byte = static_cast<u32>(x / 8) & ~1u;
	const u16 mask = static_cast<u16>(0x8000u >> (x & 15));
	u8 c = 0;
	for (u8 p = 0; p < sc.planes(); ++p) {
		const u8* base = sc.plane(p).data();
		const u16 v = *reinterpret_cast<const u16*>(base + static_cast<u32>(y) * row + byte);
		if ((v & mask) != 0u) c |= static_cast<u8>(1u << p);
	}
	return c;
}

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

} // namespace

int main() {
	MemorySystem mem = make_memory();

	// --- Lienzo interleaved: layout `Interleaved` + `surface()` ----------------
	SceneResources creq = graphics::scene::planar(320, 256, 4);
	creq.layout = graphics::scene::SceneLayout::Interleaved;
	Scene sc;
	check(graphics::scene::compose(
		      sc, mem, creq, graphics::scene::ocs_a500,
		      graphics::scene::display(graphics::scene::kPal320x256,
					       graphics::scene::kBplcon0_4Planes)),
	      "scene::compose (interleaved) reserva bitmap + copperlist");
	check(sc.ok(), "la escena queda ok tras compose");
	check(sc.words() > 0u, "la copperlist tiene palabras");
	check(sc.playfield().bitplanes().data() != nullptr, "el playfield tiene bitplanes");

	field::Surface surf = sc.surface();
	// Cuadrado relleno con color 5.
	const s16 xs[4] = {40, 120, 120, 40};
	const s16 ys[4] = {40, 40, 120, 120};
	check(surf.fill_polygon(xs, ys, 4, 5), "Surface::fill_polygon sobre la escena");
	check(color_at(sc.playfield(), 80, 80) == 5u, "interior con el color pedido");
	check(color_at(sc.playfield(), 10, 10) == 0u, "fuera del cuadrado vacio");

	// --- Doble buffer contiguo: `buffers = 2` y `commit()` --------------------
	SceneResources res = graphics::scene::planar(320, 256, 4);
	res.buffers = 2;
	Scene s2;
	check(graphics::scene::compose(
		      s2, mem, res,
		      graphics::scene::ocs_a500,
		      graphics::scene::display(graphics::scene::kPal320x256,
					       graphics::scene::kBplcon0_4Planes)),
	      "escena con 2 buffers compone");
	check(s2.buffer_count() == 2u, "hay 2 buffers de display");
	check(s2.buffer(0).data() != s2.buffer(1).data(), "los dos buffers son distintos");
	const u8 before = s2.back_index();
	s2.commit();
	check(s2.back_index() != before, "commit avanza el buffer trasero");

	// --- Lienzo CONTIGUO: el mismo `surface()` sobre planos uno tras otro -----
	Scene s3;
	check(graphics::scene::compose(
		      s3, mem, graphics::scene::planar(320, 256, 4), graphics::scene::ocs_a500,
		      graphics::scene::display(graphics::scene::kPal320x256,
					       graphics::scene::kBplcon0_4Planes)),
	      "scene::compose (contiguo) compone");
	check(s3.ok(), "la escena contigua queda ok");
	field::Surface ksurf = s3.surface();
	check(ksurf.valid(), "surface() valido en layout contiguo");
	const s16 kxs[4] = {40, 120, 120, 40};
	const s16 kys[4] = {40, 40, 120, 120};
	check(ksurf.fill_polygon(kxs, kys, 4, 5), "Surface::fill_polygon contiguo");
	check(color_at_contiguous(s3, 80, 80) == 5u, "interior contiguo con el color pedido");
	check(color_at_contiguous(s3, 10, 10) == 0u, "fuera del cuadrado contiguo vacio");
	check(ksurf.draw_line(0, 5, 15, 5, 2), "draw_line contiguo");
	check(color_at_contiguous(s3, 8, 5) == 2u, "la linea contigua cae en el plano 1");

	// Blit planar contiguo: un `CopyRect` por plano en el `FramePlan`.
	u16 src[64] {};
	u16 mask[16] {};
	graphics::FramePlan plan {};
	check(ksurf.blit(plan, Span<const u16> {src, 64}, 0, 0, 32, 4, 4, 16, 4),
	      "Surface::blit contiguo encola");
	check(plan.blit_job_count() == 4u, "blit contiguo = 1 job por plano");
	graphics::FramePlan plan2 {};
	check(ksurf.blit_masked(plan2, Span<const u16> {src, 64}, Span<const u16> {mask, 16},
				0, 8, 32, 4, 4, 16, 4),
	      "Surface::blit_masked contiguo encola");
	check(plan2.blit_job_count() == 4u, "blit enmascarado contiguo = 1 job por plano");

	if (failures == 0) {
		std::printf("OK: scene::compose interleaved y contiguo (Surface + copperlist).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
