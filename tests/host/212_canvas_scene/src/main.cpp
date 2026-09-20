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

/// Sink de relleno de prueba: solo registra cuantas veces lo llama el rasterizador.
struct FillRec {
	int calls = 0;
};
FillRec g_fill_rec {};
bool record_fill(void* ctx, eng::u8*, eng::u8, eng::u32, eng::u32, eng::u16, eng::u16, eng::u16,
		 const eng::s16*, const eng::s16*, eng::u8, eng::u8) {
	static_cast<FillRec*>(ctx)->calls++;
	return true;
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

	// Blit planar contiguo por **Blitter** (BlitterRaster): un `CopyRect` por plano.
	s3.set_raster(&field::kBlitterRaster);
	u16 src[64] {};
	u16 mask[16] {};
	graphics::FramePlan plan {};
	check(ksurf.blit(plan, Span<const u16> {src, 64}, 0, 0, 32, 4, 4, 16, 4),
	      "Surface::blit (Blitter) encola");
	check(plan.blit_job_count() == 4u, "blit contiguo = 1 job por plano");
	graphics::FramePlan plan2 {};
	check(ksurf.blit_masked(plan2, Span<const u16> {src, 64}, Span<const u16> {mask, 16},
				0, 8, 32, 4, 4, 16, 4),
	      "Surface::blit_masked (Blitter) encola");
	check(plan2.blit_job_count() == 4u, "blit enmascarado contiguo = 1 job por plano");

	// Blit por **CPU** (CpuRaster): copia los pixeles sin encolar jobs.
	s3.set_raster(&field::kCpuRaster);
	u16 src2[64];
	for (u16 i = 0; i < 64u; ++i) src2[i] = 0xffffu;
	graphics::FramePlan plan3 {};
	check(ksurf.blit(plan3, Span<const u16> {src2, 64}, 0, 16, 32, 4, 4, 16, 4),
	      "Surface::blit (CPU) copia");
	check(plan3.blit_job_count() == 0u, "blit CPU no encola jobs");
	check(color_at_contiguous(s3, 0, 16) == 0x0fu, "blit CPU escribe los pixeles");

	// Copia enmascarada CPU: solo se copian los pixeles con mascara a 1 (fila 0).
	u16 src3[64];
	for (u16 i = 0; i < 64u; ++i) src3[i] = 0xffffu;
	u16 mask3[8] = {0xffffu, 0xffffu, 0, 0, 0, 0, 0, 0};
	graphics::FramePlan plan4 {};
	check(ksurf.blit_masked(plan4, Span<const u16> {src3, 64}, Span<const u16> {mask3, 8},
				0, 32, 32, 4, 4, 16, 4),
	      "Surface::blit_masked (CPU) copia");
	check(plan4.blit_job_count() == 0u, "blit_masked CPU no encola jobs");
	check(color_at_contiguous(s3, 0, 32) == 0x0fu, "mascara 1 copia el pixel");
	check(color_at_contiguous(s3, 0, 33) == 0x00u, "mascara 0 deja el pixel");

	// --- RasterOp: operaciones logicas uniformes (seam CPU/Blitter) -----------
	Scene s4;
	check(graphics::scene::compose(
		      s4, mem, graphics::scene::planar(320, 256, 4), graphics::scene::ocs_a500,
		      graphics::scene::display(graphics::scene::kPal320x256,
					       graphics::scene::kBplcon0_4Planes)),
	      "escena para RasterOp compone");
	field::Surface r4 = s4.surface();
	r4.fill_rect(0, 0, 32, 8, 3u, field::RasterOp::Copy);
	check(color_at_contiguous(s4, 0, 0) == 3u, "RasterOp::Copy escribe el color");
	r4.fill_rect(0, 0, 32, 8, 3u, field::RasterOp::Xor);
	check(color_at_contiguous(s4, 0, 0) == 0u, "Xor dos veces = 0");
	r4.fill_rect(0, 0, 32, 8, 1u, field::RasterOp::Or);
	check(color_at_contiguous(s4, 0, 0) == 1u, "Or enciende el plano");
	r4.fill_rect(0, 0, 32, 8, 1u, field::RasterOp::And);
	check(color_at_contiguous(s4, 0, 0) == 1u, "And conserva el plano 1");
	r4.fill_rect(0, 0, 32, 8, 0u, field::RasterOp::Clear);
	check(color_at_contiguous(s4, 0, 0) == 0u, "Clear borra");

	// BlitterRaster: el relleno va por `fill_polygon` (sink/Blitter si lo hay; CPU si no).
	Scene s5;
	check(graphics::scene::compose(
		      s5, mem, graphics::scene::planar(320, 256, 4), graphics::scene::ocs_a500,
		      graphics::scene::display(graphics::scene::kPal320x256,
					       graphics::scene::kBplcon0_4Planes)),
	      "escena para BlitterRaster compone");
	s5.set_raster(&field::kBlitterRaster, field::RasterPolicy {field::AccelMode::Blitter, 0u, true});
	field::Surface r5 = s5.surface();
	check(r5.fill_rect(0, 0, 32, 8, 5u), "BlitterRaster::fill_rect encola/pinta");
	check(color_at_contiguous(s5, 8, 4) == 5u, "BlitterRaster pinta el color pedido");

	// --- AccelMode::Auto: umbral de area para ir al Blitter (sink) -----------
	Scene s6;
	check(graphics::scene::compose(
		      s6, mem, graphics::scene::planar(320, 256, 4), graphics::scene::ocs_a500,
		      graphics::scene::display(graphics::scene::kPal320x256,
					       graphics::scene::kBplcon0_4Planes)),
	      "escena para Auto compone");
	s6.set_polygon_fill_sink(field::PolygonFillSink {&g_fill_rec, record_fill});
	s6.set_raster(&field::kBlitterRaster, field::RasterPolicy {field::AccelMode::Auto, 64u, true});
	g_fill_rec.calls = 0;
	(void)s6.surface().fill_rect(0, 0, 16, 16, 5u); // 256 >= 64 -> sink
	check(g_fill_rec.calls == 1, "Auto: area grande usa el sink (Blitter)");
	g_fill_rec.calls = 0;
	(void)s6.surface().fill_rect(0, 0, 4, 4, 5u); // 16 < 64 -> CPU
	check(g_fill_rec.calls == 0, "Auto: area pequena usa CPU");
	check(color_at_contiguous(s6, 0, 0) == 5u, "Auto CPU pinta el pixel");
	s6.set_raster(&field::kBlitterRaster, field::RasterPolicy {field::AccelMode::Blitter, 0u, true});
	g_fill_rec.calls = 0;
	(void)s6.surface().fill_rect(0, 0, 4, 4, 5u);
	check(g_fill_rec.calls == 1, "Blitter: fuerza el sink aunque el area sea pequena");

	// Linea por Blitter: con plan y dentro del clip, una `Line` por plano del color.
	s6.set_raster(&field::kBlitterRaster, field::RasterPolicy {field::AccelMode::Blitter, 0u, true});
	graphics::FramePlan line_plan {};
	check(s6.surface().draw_line(0, 0, 31, 15, 5u, &line_plan), "BlitterRaster::draw_line encola");
	check(line_plan.blit_job_count() == 2u, "linea por Blitter = 1 job por plano (color 5 = planos 0,2)");
	graphics::FramePlan line_plan2 {};
	check(s6.surface().draw_line(-10, 8, 40, 8, 5u, &line_plan2), "linea parcial se recorta y encola");
	check(line_plan2.blit_job_count() == 2u, "linea parcial = 1 job por plano (recortada)");
	graphics::FramePlan eor_plan {};
	check(s6.surface().draw_line(0, 0, 31, 15, 5u, &eor_plan, field::RasterOp::Xor),
	      "draw_line Xor (EOR) encola");
	check(eor_plan.blit_job_count() == 2u &&
		      eor_plan.blit_job(0).kind == graphics::BlitJobKind::LineEor,
	      "linea EOR usa BlitJobKind::LineEor");

	// Blit con operacion logica (B=D, minterm por op): sombras/glow/mascaras.
	s3.set_raster(&field::kBlitterRaster);
	graphics::FramePlan or_plan {};
	check(ksurf.blit(or_plan, Span<const u16> {src, 64}, 0, 48, 32, 4, 4, 16, 4, 0u, false,
			 field::RasterOp::Or),
	      "blit con RasterOp::Or encola");
	check(or_plan.blit_job_count() == 4u &&
		      or_plan.blit_job(0).kind == graphics::BlitJobKind::LogicBlit,
	      "blit Or usa BlitJobKind::LogicBlit");

	// Azucar de alto nivel: sombra (And) y glow (Or).
	graphics::FramePlan shadow_plan {};
	check(ksurf.blit_shadow(shadow_plan, Span<const u16> {src, 64}, 0, 64, 32, 4, 4, 16, 4),
	      "blit_shadow encola");
	check(shadow_plan.blit_job_count() == 4u &&
		      shadow_plan.blit_job(0).kind == graphics::BlitJobKind::LogicBlit &&
		      shadow_plan.blit_job(0).minterm == 0xC0u,
	      "blit_shadow usa LogicBlit con minterm $C0 (A&B)");
	graphics::FramePlan glow_plan {};
	check(ksurf.blit_glow(glow_plan, Span<const u16> {src, 64}, 0, 64, 32, 4, 4, 16, 4),
	      "blit_glow encola");
	check(glow_plan.blit_job(0).minterm == 0xFCu, "blit_glow usa minterm $FC (A|B)");

	// Colision pixel-perfect por CPU (referencia del camino Blitter).
	{
		eng::u8 ma[64] = {};
		eng::u8 mb[64] = {};
		ma[0] = 0x80; // pixel (0,0) de la mascara A
		mb[0] = 0x80; // mismo pixel -> colision
		check(field::collide_cpu(eng::PlaneBytes {ma, 64}, eng::PlaneBytes {mb, 64},
					 16u, 2u, 1u, 0u, 0u, 1u, 1u),
		      "collide_cpu detecta el solape");
		mb[0] = 0x40; // pixel distinto -> sin colision
		check(!field::collide_cpu(eng::PlaneBytes {ma, 64}, eng::PlaneBytes {mb, 64},
					  16u, 2u, 1u, 0u, 0u, 1u, 1u),
		      "collide_cpu sin solape");
	}

	graphics::FramePlan line_plan3 {};
	(void)s6.surface().draw_line(-100, -100, -50, -50, 5u, &line_plan3);
	check(line_plan3.blit_job_count() == 0u, "linea fuera del clip no encola (CPU)");

	// Recorte de segmento directo (Cohen-Sutherland entero).
	{
		field::ClipRect cr {0, 0, 31, 31};
		eng::s32 a = -10, b = 8, c = 40, d = 8;
		check(field::clip_segment(cr, a, b, c, d) && a == 0 && c == 31,
		      "clip_segment recorta a [0,31]");
		eng::s32 e = -100, f = -100, g = -50, h = -50;
		check(!field::clip_segment(cr, e, f, g, h), "clip_segment rechaza el segmento fuera");
	}

	// --- bind_raw: tamano de plano con dimensiones no triviales (regresion mulu32x16) ---
	{
		alignas(16) static u8 buf[61440u + 16u];
		field::ContiguousPlayfield cp;
		// 320x256, 6 planos: row = ((320/8)+3)&~3 = 40; need = 40*256*6 = 61440.
		check(cp.bind_raw(buf, 61440u, 320u, 256u, 6u), "bind_raw 320x256x6 con tamano exacto");
		field::ContiguousPlayfield cp2;
		check(!cp2.bind_raw(buf, 61439u, 320u, 256u, 6u), "bind_raw 1 byte corto falla");
	}

	if (failures == 0) {
		std::printf("OK: scene::compose interleaved y contiguo (Surface + copperlist).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
