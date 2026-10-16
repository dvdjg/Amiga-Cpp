// ============================================================================
// Test HOST-449: texto por palabras (`Surface::draw_text_words`).
// ============================================================================
//
// Cubre la impresión de texto por **palabras de 16 px** (Font8, 8×8) de las franjas de
// HUD: dos glifos por palabra, ocho escrituras por par — patrón y fondo de golpe — a
// través de `Playfield::store_plane_word` (genérico en planos y color: cada plano recibe
// el patrón o fondo 0 según su bit del color). Se verifica contra un **oráculo
// independiente píxel a píxel** (bit a bit de `Font8::row`, bit 0 = columna izquierda):
// la línea completa coincide con el carácter esperado en UN plano (franja de depuración,
// color 1) y en CUATRO planos con color no contiguo (el patrón llega solo a los planos
// del color), además del relleno de la celda impar, el truncado por clip y las guardas.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/449_word_text

#include <eng/field/surface.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/memory/mem_bank.hpp>

#include <cstdio>
#include <vector>

using namespace eng;
using namespace eng::playfield;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// Playfield de prueba interleaved (mismo mapeo que `CanvasPlayfield`).
struct MockPlayfield : Playfield {
	std::vector<u8> mem;

	void init(u16 w, u16 h, u8 p) {
		m_width = w;
		m_height = h;
		m_planes = p;
		m_bytes_per_row = static_cast<u16>((w / 8u) & ~1u);
		m_total_bytes = static_cast<u32>(m_bytes_per_row) * m_planes * h;
		mem.assign(m_total_bytes, 0);
		eng::MemBank<eng::MemoryKind::Chip> bank {};
		bank.configure(mem.data(), static_cast<eng::u32>(mem.size()), 2u);
		m_frontbuffer = bank.reserve<eng::PlaneTag>(m_total_bytes, 2u).address();
		m_initialized = true;
	}
	u8 planes_count() const { return m_planes; }
	u32 planeline_for(s32 wy) const override { return static_cast<u32>(wy) * m_planes; }
	u32 byte_for(s32 wx) const override { return static_cast<u32>(wx) / 8u & ~1u; }
	bool in_bounds(s32 wx, s32 wy) const override {
		return wx >= 0 && wy >= 0 && static_cast<u32>(wx) < m_width &&
		       static_cast<u32>(wy) < m_height;
	}
	PlayfieldHardwareView hardware_view() const override {
		PlayfieldHardwareView v {};
		v.planes = m_planes;
		v.bitmap_bytes_per_row = m_bytes_per_row;
		return v;
	}
	bool add_world_bitmap(graphics::FramePlan&, Span<const u16>, s32, s32, u16, u16, u16, u32,
			      u8, u8, bool, RasterOp) override {
		return false;
	}
	bool add_world_bitmap_masked(graphics::FramePlan&, Span<const u16>, Span<const u16>, s32,
				     s32, u16, u16, u16, u32, u8, u8) override {
		return false;
	}

	// Píxel del plano `p` (MSB = izquierda, como el bitplane Amiga).
	bool plane_px(u8 p, s32 x, s32 y) const {
		const u32 base = (static_cast<u32>(y) * m_planes + p) * m_bytes_per_row;
		const u32 byte = byte_for(x);
		const u16 mask = static_cast<u16>(0x8000u >> (x & 15));
		const u16 v = *reinterpret_cast<const u16*>(mem.data() + base + byte);
		return (v & mask) != 0u;
	}
	// Palabra completa de la fila `y` del plano `p` en el byte (par) `byte`.
	u16 word(u8 p, u32 byte, s32 y) const {
		const u32 base = (static_cast<u32>(y) * m_planes + p) * m_bytes_per_row;
		return *reinterpret_cast<const u16*>(mem.data() + base + byte);
	}
};

// Oráculo independiente: ¿el píxel (x, y) de la línea impresa debe estar encendido en el
// plano `p` según `Font8` con el color `color`? (bit `col` de la fila = columna `col`
// contada desde la izquierda del glifo).
bool oracle_px(const char* text, s32 len, s32 x0, s32 y0, u8 color, u8 p, s32 x, s32 y) {
	if ((color & static_cast<u8>(1u << p)) == 0u) return false; // plano fuera del color
	if (x < x0 || y < y0 || y >= y0 + 8) return false;
	const s32 cx = (x - x0) / 8;
	const s32 col = (x - x0) & 7;
	if (cx >= len) return false;
	const u8 glyph_row = Font8::row(static_cast<u8>(text[cx]), static_cast<u8>(y - y0));
	return ((glyph_row >> col) & 1u) != 0u;
}

// Compara TODOS los planos contra el oráculo en toda la pantalla.
int compare_all_planes(const MockPlayfield& pf, const char* text, s32 len, s32 x0, s32 y0,
		       s32 x1, s32 y1, u8 color) {
	int bad = 0;
	for (u8 p = 0u; p < pf.planes_count(); ++p) {
		for (s32 y = 0; y < 64; ++y) {
			for (s32 x = 0; x < 64; ++x) {
				bool expect = false;
				if (x < x1 && x >= x0 && y >= y0 && y < y1) {
					expect = oracle_px(text, len, x0, y0, color, p, x, y);
				}
				if (pf.plane_px(p, x, y) != expect) ++bad;
			}
		}
	}
	return bad;
}

} // namespace

int main() {
	// 1) Franja de depuración: UN plano, color 1 — idéntica al oráculo píxel a píxel.
	{
		MockPlayfield pf;
		pf.init(64, 64, 1);
		Surface surf {pf, SurfaceRect {0, 0, 64, 64}};
		check(surf.draw_text_words(16, 8, "AB", 1u), "1 plano color 1: devuelve true");
		const char text[] = "AB";
		check(compare_all_planes(pf, text, 2, 16, 8, 32, 16, 1u) == 0,
		      "'AB' coincide con el oráculo en toda la región");
	}

	// 2) Cuatro planos, color 5 (planos 0 y 2): el patrón llega SOLO a los planos del
	//    color; los demás quedan a fondo 0 (genérico en planos y color).
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		Surface surf {pf, SurfaceRect {0, 0, 64, 64}};
		check(surf.draw_text_words(16, 8, "ABC", 5u), "4 planos color 5: devuelve true");
		const char text[] = "ABC";
		check(compare_all_planes(pf, text, 3, 16, 8, 48, 16, 5u) == 0,
		      "4 planos: oráculo en TODOS los planos (0 y 2 con patrón; 1 y 3 a cero)");
	}

	// 3) Color 0: no selecciona ningún plano — todo a fondo 0 (pero geometría válida).
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		Surface surf {pf, SurfaceRect {0, 0, 64, 64}};
		check(surf.draw_text_words(16, 8, "AB", 0u), "color 0: devuelve true");
		const char text[] = "AB";
		check(compare_all_planes(pf, text, 2, 16, 8, 32, 16, 0u) == 0,
		      "color 0: ningún plano recibe patrón");
	}

	// 4) Cadena vacía: válida y sin pintar.
	{
		MockPlayfield pf;
		pf.init(64, 64, 1);
		Surface surf {pf, SurfaceRect {0, 0, 64, 64}};
		check(surf.draw_text_words(16, 8, "", 1u), "cadena vacía: devuelve true");
		const char text[] = "";
		check(compare_all_planes(pf, text, 0, 16, 8, 16, 16, 1u) == 0,
		      "cadena vacía no pinta nada");
	}

	// 5) Truncado por clip: con clip de 32 px de ancho solo cabe el primer par.
	{
		MockPlayfield pf;
		pf.init(64, 64, 1);
		Surface surf {pf, SurfaceRect {0, 0, 32, 64}};
		check(surf.draw_text_words(16, 8, "AAAAA", 1u), "clip 32: devuelve true");
		const char text[] = "AA";
		check(compare_all_planes(pf, text, 2, 16, 8, 32, 16, 1u) == 0,
		      "clip 32: solo el primer par");
		check(pf.word(0u, 4u, 8) == 0u, "clip 32: la palabra en x=32 queda intacta");
	}

	// 6) Rechazos de geometría: x sin alinear a 16, línea fuera del clip.
	{
		MockPlayfield pf;
		pf.init(64, 64, 1);
		Surface surf {pf, SurfaceRect {0, 0, 64, 64}};
		check(!surf.draw_text_words(24, 8, "A", 1u), "x sin alinear a 16: false");
		check(!surf.draw_text_words(-16, 8, "A", 1u), "x < clip.x: false");
		check(!surf.draw_text_words(16, 60, "A", 1u), "línea 60..67 fuera del clip: false");
		check(pf.word(0u, 2u, 8) == 0u, "rechazos: no se pinta nada");
	}

	// 7) Escritura directa de palabra por planos: valor crudo y selección por color.
	{
		MockPlayfield pf;
		pf.init(64, 64, 4);
		check(pf.store_plane_word(16, 3, 0xA55Au, 0xAu), "store_plane_word (color 0xA)");
		check(pf.word(1u, 2u, 3) == 0xA55Au, "plano 1 recibe el patrón");
		check(pf.word(3u, 2u, 3) == 0xA55Au, "plano 3 recibe el patrón");
		check(pf.word(0u, 2u, 3) == 0u, "plano 0 (fuera del color) a fondo 0");
		check(pf.word(2u, 2u, 3) == 0u, "plano 2 (fuera del color) a fondo 0");
		check(!pf.store_plane_word(8, 3, 0x1234u, 1u), "store_plane_word x=8: false");
		check(pf.word(1u, 0u, 3) == 0u, "x=8 no escribe");
	}

	if (g_fail != 0) {
		std::printf("HOST-449 FALLO: %d comprobaciones\n", g_fail);
		return 1;
	}
	std::printf("HOST-449 OK\n");
	return 0;
}
