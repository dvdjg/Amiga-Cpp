// ============================================================================
// Test HOST-061: map_flat_scroll (mapeo cámara -> BPLxPT/BPLCON1/BPLMOD)
// ============================================================================
//
// Valida `eng::field::map_flat_scroll` (amiga_display_mapper.hpp): la traducción
// NEUTRAL de la cámara de un bitmap flat a los registros del display (fetch
// ancho DDF $30). Aísla el mapper de la superficie y del Copper (modelo objetivo
// §5). Fórmula verificada en hardware por la demo 120.

#include <cstdio>

#include <eng/field/amiga_display_mapper.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

} // namespace

int main() {
	using eng::field::map_flat_scroll;

	const eng::u16 row = 56; // 448 px / 8
	const eng::u8 planes = 4;
	const eng::u16 mod = static_cast<eng::u16>(row * planes - 42);

	// cam_x mínimo (1): fine=1 -> BPLCON1 = 15 duplicado; planeaddx = 0.
	const auto a = map_flat_scroll(1, 0, row, planes);
	check(a.planeaddx == 0u, "cam_x=1: planeaddx=0");
	check(a.bplcon1 == 0x00ffu, "cam_x=1: bplcon1=0xFF");
	check(a.bpl1mod == mod, "BPLMOD = row*planes - 42");
	check(a.planeaddy == 0u, "cam_y=0: planeaddy=0");

	// fine==0: una word antes (planeaddx=0 en cam_x=16, bplcon1=0).
	const auto b = map_flat_scroll(16, 0, row, planes);
	check(b.planeaddx == 0u && b.bplcon1 == 0u, "cam_x=16: word antes, fine 0");

	// cam_x=17: coarse=(16)/8=2 y fine=1.
	const auto c = map_flat_scroll(17, 0, row, planes);
	check(c.planeaddx == 2u && c.bplcon1 == 0x00ffu, "cam_x=17: coarse+fine");

	// cam_x=32: coarse=(31&~15)/8=2, fine=0.
	const auto d = map_flat_scroll(32, 0, row, planes);
	check(d.planeaddx == 2u && d.bplcon1 == 0u, "cam_x=32: coarse sin fine");

	// Propiedad continua en todo el rango representable.
	bool cont = true;
	for (eng::s32 cx = 1; cx <= 400; ++cx) {
		const auto m = map_flat_scroll(cx, 0, row, planes);
		cont = cont && (m.planeaddx * 8u == static_cast<eng::u32>((cx - 1) & ~15));
		cont = cont && (static_cast<eng::u16>(m.bplcon1 & 0x0fu) == static_cast<eng::u16>((16 - (cx & 15)) & 15));
		cont = cont && ((m.bplcon1 >> 4) == (m.bplcon1 & 0x0fu));
	}
	check(cont, "propiedad continua planeaddx/BPLCON1 en 1..400");

	// planeaddy = cam_y * planes * row_bytes (interleaved).
	const auto e = map_flat_scroll(1, 10, row, planes);
	check(e.planeaddy == static_cast<eng::u32>(10) * planes * row, "planeaddy = cam_y*planes*row");

	// Clamp por debajo del mínimo.
	const auto f = map_flat_scroll(0, 0, row, planes);
	check(f.planeaddx == 0u && f.bplcon1 == 0x00ffu, "cam_x<min se clampa a 1");

	// Utilizable en compile-time.
	static_assert(map_flat_scroll(1, 0, 56, 4).bplcon1 == 0x00ffu, "constexpr");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: map_flat_scroll (camara -> BPLxPT/BPLCON1/BPLMOD) validado.\n");
	return 0;
}
