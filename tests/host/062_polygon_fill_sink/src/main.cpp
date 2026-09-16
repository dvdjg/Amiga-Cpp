// Test host del seam de relleno de polígonos por hardware
// (`eng::field::PolygonFillSink` + `Playfield::fill_polygon`).
//
// Valida que un `Playfield` con sink instalado DELEGA el relleno (con la
// geometría planar correcta: planos, strides y dimensiones) y no toca la CPU; y
// que sin sink cae al relleno CPU por scanline. La ruta real por Blitter
// (`MinimalBackend::blitter_fill_polygon_strided`) se valida en hardware con la
// demo 110; aquí se fija el CONTRATO del seam que esa ruta consume.
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
	u16 w = 0;
	u16 h = 0;
	u8 n = 0;
	u8 color = 0;
	s16 xs[8] {};
	s16 ys[8] {};
};
Record g_rec;

bool fake_fill(void* ctx, u8* base, u8 planes, u32 plane_stride, u32 row_stride, u16 row_bytes,
	       u16 bw, u16 bh, const s16* xs, const s16* ys, u8 n, u8 color) {
	auto* rec = static_cast<Record*>(ctx);
	rec->called = true;
	rec->base = base;
	rec->planes = planes;
	rec->plane_stride = plane_stride;
	rec->row_stride = row_stride;
	rec->row_bytes = row_bytes;
	rec->w = bw;
	rec->h = bh;
	rec->n = n;
	rec->color = color;
	for (u8 i = 0; i < n && i < 8u; ++i) {
		rec->xs[i] = xs[i];
		rec->ys[i] = ys[i];
	}
	return true;
}

// Playfield de prueba interleaved (idéntico a CanvasPlayfield). Puede simular un
// layout CONTIGUO por plano sobrescribiendo los strides.
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
	bool add_world_bitmap(graphics::FramePlan&, Span<const u16>, s32, s32, u16, u16, u16, u32, u8) override {
		return false;
	}
	bool add_world_bitmap_masked(graphics::FramePlan&, Span<const u16>, Span<const u16>, s32, s32, u16, u16, u16, u32, u8) override {
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
	// 1) Con sink: se delega y llega la geometría interleaved correcta.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4); // row_bytes=8, planes=4
		g_rec = Record {};
		pf.set_polygon_fill_sink(PolygonFillSink { &g_rec, &fake_fill });
		Surface surf(pf, SurfaceRect {0, 0, 64, 64});
		const s16 xs[3] = {10, 50, 10};
		const s16 ys[3] = {10, 10, 50};
		check(surf.fill_polygon(xs, ys, 3, 5), "delegado devuelve true");
		check(g_rec.called, "el sink fue llamado");
		check(g_rec.base == pf.mem.data(), "plane_base = inicio del bitmap");
		check(g_rec.planes == 4, "planes = 4");
		check(g_rec.plane_stride == 8, "plane_stride interleaved = row_bytes");
		check(g_rec.row_stride == 32, "row_stride interleaved = planes*row_bytes");
		check(g_rec.row_bytes == 8, "row_bytes = 8");
		check(g_rec.w == 64 && g_rec.h == 64, "bitmap_w/h del playfield");
		check(g_rec.n == 3 && g_rec.color == 5, "n y color pasan al sink");
		check(g_rec.xs[0] == 10 && g_rec.ys[2] == 50, "coordenadas intactas");
		check(!pf.px(20, 20), "el sink sustituye al relleno CPU (buffer no tocado)");
	}

	// 2) Con sink y clip: las coordenadas llegan recortadas.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		g_rec = Record {};
		pf.set_polygon_fill_sink(PolygonFillSink { &g_rec, &fake_fill });
		Surface surf(pf, SurfaceRect {0, 0, 32, 32});
		const s16 xs[4] = {0, 63, 63, 0};
		const s16 ys[4] = {0, 0, 63, 63};
		check(surf.fill_polygon(xs, ys, 4, 1), "delegado con clip true");
		bool in = true;
		for (u8 i = 0; i < g_rec.n; ++i) {
			if (g_rec.xs[i] < 0 || g_rec.xs[i] > 31 || g_rec.ys[i] < 0 || g_rec.ys[i] > 31) in = false;
		}
		check(g_rec.called && in, "el sink recibe el polígono recortado al clip");
	}

	// 3) Layout contiguo: el playfield reporta otros strides al sink.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		pf.contiguous = true; // plane_stride = row_bytes*height, row_stride = row_bytes
		g_rec = Record {};
		pf.set_polygon_fill_sink(PolygonFillSink { &g_rec, &fake_fill });
		Surface surf(pf, SurfaceRect {0, 0, 64, 64});
		const s16 xs[3] = {1, 30, 1};
		const s16 ys[3] = {1, 1, 30};
		surf.fill_polygon(xs, ys, 3, 2);
		check(g_rec.plane_stride == 8u * 64u, "plane_stride contiguo = row_bytes*height");
		check(g_rec.row_stride == 8, "row_stride contiguo = row_bytes");
	}

	// 4) Sin sink: relleno CPU por scanline (interior pintado, exterior no).
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		Surface surf(pf, SurfaceRect {0, 0, 64, 64});
		const s16 xs[3] = {10, 50, 10};
		const s16 ys[3] = {10, 10, 50};
		check(surf.fill_polygon(xs, ys, 3, 1), "CPU fill true");
		check(pf.px(12, 12), "interior CPU pintado");
		check(!pf.px(45, 45), "exterior CPU sin pintar");
	}

	// 5) Sink inválido (sin fn): cae al relleno CPU.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		pf.set_polygon_fill_sink(PolygonFillSink {}); // vacío
		Surface surf(pf, SurfaceRect {0, 0, 64, 64});
		const s16 xs[3] = {10, 50, 10};
		const s16 ys[3] = {10, 10, 50};
		surf.fill_polygon(xs, ys, 3, 1);
		check(pf.px(20, 20), "sink vacío -> CPU pinta el interior");
	}

	if (failures == 0) {
		std::printf("OK: PolygonFillSink (delegación del relleno + strides) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
