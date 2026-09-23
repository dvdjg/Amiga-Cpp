// Test host del seam de relleno de RECTANGULO por hardware
// (`eng::field::RectFillSink` + `Playfield::fill_rect_hw` + `BlitterRaster::fill_rect`).
//
// Valida que un `Playfield` con sink instalado DELEGA el relleno (con la geometria planar
// correcta: planos, strides y dimensiones), que sin sink cae al relleno CPU (`draw_span` por
// fila) y que `BlitterRaster` elige la ruta D-only cuando procede. La ruta real por Blitter
// (`AmigaBackend::blitter_fill_rect`) se valida en hardware con la demo 215; aqui se fija el
// CONTRATO del seam que esa ruta consume.
#include <eng/field/raster.hpp>
#include <eng/field/surface.hpp>

#include <cstdio>
#include <vector>

using namespace eng;
using namespace eng::field;

namespace {

struct Record {
	bool called = false;
	u8* base = nullptr;
	u8 planes = 0;
	u32 plane_stride = 0;
	u32 row_stride = 0;
	u16 row_bytes = 0;
	u16 bw = 0;
	u16 bh = 0;
	s32 x = 0;
	s32 y = 0;
	u16 w = 0;
	u16 h = 0;
	u8 color = 0;
};
Record g_rec;

bool fake_rect_fill(void* ctx, u8* base, u8 planes, u32 plane_stride, u32 row_stride, u16 row_bytes,
		    u16 bw, u16 bh, s32 x, s32 y, u16 w, u16 h, u8 color) {
	auto* rec = static_cast<Record*>(ctx);
	rec->called = true;
	rec->base = base;
	rec->planes = planes;
	rec->plane_stride = plane_stride;
	rec->row_stride = row_stride;
	rec->row_bytes = row_bytes;
	rec->bw = bw;
	rec->bh = bh;
	rec->x = x;
	rec->y = y;
	rec->w = w;
	rec->h = h;
	rec->color = color;
	return true;
}

struct MockPlayfield : Playfield {
	std::vector<u8> mem;
	bool contiguous = false;

	void init(u16 w, u16 h, u8 p) {
		m_width = w;
		m_height = h;
		m_planes = p;
		m_bytes_per_row = static_cast<u16>((w / 8) & ~1u);
		m_total_bytes = static_cast<u32>(m_bytes_per_row) * m_planes * m_height;
		mem.assign(m_total_bytes, 0);
		m_frontbuffer = mem.data();
		m_initialized = true;
	}
	u32 planeline_for(s32 wy) const override { return static_cast<u32>(wy) * m_planes; }
	u32 byte_for(s32 wx) const override { return (static_cast<u32>(wx) / 8u) & ~1u; }
	u32 plane_stride() const override {
		return contiguous ? static_cast<u32>(m_bytes_per_row) * m_height : m_bytes_per_row;
	}
	u32 row_stride() const override {
		return contiguous ? m_bytes_per_row : static_cast<u32>(m_planes) * m_bytes_per_row;
	}
	bool in_bounds(s32 wx, s32 wy) const override {
		return wx >= 0 && wy >= 0 && static_cast<u32>(wx) < m_width && static_cast<u32>(wy) < m_height;
	}
	PlayfieldHardwareView hardware_view() const override { return {}; }
	bool add_world_bitmap(graphics::FramePlan&, Span<const u16>, s32, s32, u16, u16, u16, u32, u8, u8, bool, RasterOp) override {
		return false;
	}
	bool add_world_bitmap_masked(graphics::FramePlan&, Span<const u16>, Span<const u16>, s32, s32, u16, u16, u16, u32, u8, u8) override {
		return false;
	}

	bool px(s32 x, s32 y) const {
		const u32 row = m_bytes_per_row;
		const u32 base = static_cast<u32>(planeline_for(y)) * row;
		const u32 byte = byte_for(x);
		const u16 mask = static_cast<u16>(0x8000u >> (x & 15));
		const u16 v = *reinterpret_cast<const u16*>(mem.data() + base + byte);
		return (v & mask) != 0u;
	}
};

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

} // namespace

int main() {
	// 1) Con sink: `fill_rect_hw` delega con la geometria interleaved correcta.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4); // row_bytes=8, planes=4
		g_rec = Record {};
		pf.set_rect_fill_sink(RectFillSink { &g_rec, &fake_rect_fill });
		check(pf.fill_rect_hw(5, 7, 20, 9, 5), "delegado devuelve true");
		check(g_rec.called, "el sink fue llamado");
		check(g_rec.base == pf.mem.data(), "plane_base = inicio del bitmap");
		check(g_rec.planes == 4, "planes = 4");
		check(g_rec.plane_stride == 8, "plane_stride interleaved = row_bytes");
		check(g_rec.row_stride == 32, "row_stride interleaved = planes*row_bytes");
		check(g_rec.row_bytes == 8, "row_bytes = 8");
		check(g_rec.bw == 64 && g_rec.bh == 64, "bitmap_w/h del playfield");
		check(g_rec.x == 5 && g_rec.y == 7 && g_rec.w == 20 && g_rec.h == 9 && g_rec.color == 5,
		      "rect y color pasan intactos");
		check(!pf.px(10, 10), "el sink sustituye al relleno CPU (buffer no tocado)");
	}

	// 2) Sin sink: relleno CPU por fila (interior pintado, exterior no).
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		check(pf.fill_rect_hw(10, 10, 16, 12, 1), "CPU fill true");
		check(pf.px(12, 12) && pf.px(24, 20), "interior CPU pintado");
		check(!pf.px(9, 10) && !pf.px(26, 21), "exterior CPU sin pintar");
	}

	// 3) BlitterRaster elige la ruta D-only (sink) cuando hay area suficiente.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		g_rec = Record {};
		pf.set_rect_fill_sink(RectFillSink { &g_rec, &fake_rect_fill });
		pf.set_raster_policy(RasterPolicy {AccelMode::Auto, 64u, true});
		check(kBlitterRaster.fill_rect(pf, 2, 3, 20, 20, 3, RasterOp::Copy), "blit fill true");
		check(g_rec.called, "BlitterRaster usa el sink de rect (area 400 >= 64)");
	}
	{
		// Area por debajo del umbral -> CPU (no toca el sink).
		MockPlayfield pf;
		pf.init(64, 64, 4);
		g_rec = Record {};
		pf.set_rect_fill_sink(RectFillSink { &g_rec, &fake_rect_fill });
		pf.set_raster_policy(RasterPolicy {AccelMode::Auto, 1000u, true});
		(void)kBlitterRaster.fill_rect(pf, 2, 3, 4, 4, 3, RasterOp::Copy);
		check(!g_rec.called && pf.px(3, 4), "area < umbral -> CPU (sink no llamado)");
	}
	{
		// Modo Cpu -> siempre CPU aunque haya sink.
		MockPlayfield pf;
		pf.init(64, 64, 4);
		g_rec = Record {};
		pf.set_rect_fill_sink(RectFillSink { &g_rec, &fake_rect_fill });
		pf.set_raster_policy(RasterPolicy {AccelMode::Cpu, 0u, true});
		(void)kBlitterRaster.fill_rect(pf, 0, 0, 32, 32, 1, RasterOp::Copy);
		check(!g_rec.called && pf.px(16, 16), "modo Cpu -> CPU (sink no llamado)");
	}

	// 4) Layout contiguo: el playfield reporta otros strides al sink.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		pf.contiguous = true;
		g_rec = Record {};
		pf.set_rect_fill_sink(RectFillSink { &g_rec, &fake_rect_fill });
		(void)pf.fill_rect_hw(1, 1, 10, 10, 2);
		check(g_rec.plane_stride == 8u * 64u, "plane_stride contiguo = row_bytes*height");
		check(g_rec.row_stride == 8, "row_stride contiguo = row_bytes");
	}

	if (failures == 0) {
		std::printf("OK: RectFillSink (delegacion del rect + strides + eleccion del raster) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
