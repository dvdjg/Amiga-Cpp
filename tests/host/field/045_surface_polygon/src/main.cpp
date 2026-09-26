// Test host de `eng::field::Surface::fill_polygon` (rasterizado CPU de polígono
// convexo por scanline). Valida geometría (interior/exterior), distintos
// triángulos y el recorte (clip) de la superficie, con un `Playfield` de prueba
// (layout interleaved idéntico a `CanvasPlayfield`).
#include <eng/field/surface.hpp>
#include <eng/memory/mem_bank.hpp>

#include <cstdio>
#include <vector>

using namespace eng;
using namespace eng::field;

// Playfield mínimo de prueba: buffer en RAM del host, layout interleaved.
struct MockPlayfield : Playfield {
	std::vector<u8> mem;

	void init(u16 w, u16 h, u8 p) {
		m_width = w;
		m_height = h;
		m_planes = p;
		m_bytes_per_row = static_cast<u16>((w / 8) & ~1u);
		m_total_bytes = static_cast<u32>(m_bytes_per_row) * m_planes * m_height;
		mem.assign(m_total_bytes, 0);
		eng::MemBank<eng::MemoryKind::Chip> bank {};
		bank.configure(mem.data(), static_cast<eng::u32>(mem.size()), 2u);
		m_frontbuffer = bank.reserve<eng::PlaneTag>(m_total_bytes, 2u).address();
		m_initialized = true;
	}
	u32 planeline_for(s32 wy) const override { return static_cast<u32>(wy) * m_planes; }
	u32 byte_for(s32 wx) const override { return (static_cast<u32>(wx) / 8u) & ~1u; }
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

	// Lee el plano 0 del píxel (colores se prueban con color=1).
	bool px(s32 x, s32 y) const {
		const u32 row = m_bytes_per_row;
		const u32 base = static_cast<u32>(planeline_for(y)) * row;
		const u32 byte = byte_for(x);
		const u16 mask = static_cast<u16>(0x8000u >> (x & 15));
		const u16 v = *reinterpret_cast<const u16*>(mem.data() + base + byte);
		return (v & mask) != 0u;
	}
};

static int failures = 0;
static void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

int main() {
	MockPlayfield pf;
	pf.init(64, 64, 4);
	Surface surf(pf, SurfaceRect {0, 0, 64, 64});

	// Triangulo rectangulo (10,10)-(50,10)-(10,50): relleno.
	{
		const s16 xs[3] = {10, 50, 10};
		const s16 ys[3] = {10, 10, 50};
		check(surf.fill_polygon(xs, ys, 3, 1), "fill_polygon devuelve true");
		check(pf.px(12, 12), "interior cerca de la esquina superior izq");
		check(pf.px(20, 20), "interior sobre la hipotenusa");
		check(!pf.px(45, 45), "exterior al otro lado de la hipotenusa");
		check(!pf.px(5, 5), "exterior superior izquierdo");
		check(!pf.px(55, 12), "exterior a la derecha");
	}

	// Cuadrado centrado: interior/exterior.
	{
		MockPlayfield pf2;
		pf2.init(64, 64, 4);
		Surface s2(pf2, SurfaceRect {0, 0, 64, 64});
		const s16 xs[4] = {20, 44, 44, 20};
		const s16 ys[4] = {20, 20, 44, 44};
		check(s2.fill_polygon(xs, ys, 4, 1), "cuadrado relleno");
		check(pf2.px(32, 32), "centro relleno");
		check(pf2.px(21, 21), "esquina interior rellena");
		check(!pf2.px(19, 32), "fuera a la izquierda");
		check(!pf2.px(45, 32), "fuera a la derecha");
		check(!pf2.px(32, 19), "fuera arriba");
		check(!pf2.px(32, 45), "fuera abajo");
	}

	// Clip: el poligono se recorta contra la sub-region.
	{
		MockPlayfield pf3;
		pf3.init(64, 64, 4);
		Surface s3(pf3, SurfaceRect {16, 16, 16, 16}); // clip 16..31
		const s16 xs[4] = {0, 63, 63, 0};
		const s16 ys[4] = {0, 0, 63, 63};
		check(s3.fill_polygon(xs, ys, 4, 1), "poligono grande con clip");
		check(pf3.px(20, 20), "dentro del clip relleno");
		check(!pf3.px(10, 10), "fuera del clip (arriba-izq) no pintado");
		check(!pf3.px(40, 40), "fuera del clip (abajo-der) no pintado");
	}

	if (failures == 0) {
		std::printf("OK: Surface::fill_polygon (poligono convexo por scanline) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
