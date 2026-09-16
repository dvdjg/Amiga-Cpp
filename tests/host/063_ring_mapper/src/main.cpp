// ============================================================================
// Test HOST-063: map_ring_scroll (mapper del corkscrew/XYLimited)
// ============================================================================
//
// Valida `eng::field::map_ring_scroll` (amiga_display_mapper.hpp): la traducción
// NEUTRAL de la cámara del corkscrew a los registros del display (planeaddx,
// BPLCON1 con fetch ancho, offset vertical del anillo y split). Es la fórmula
// extraída de `XLimitedPlayfield::hardware_view()` (réplica de UpdateCopperlist,
// xlimited.c) y verificada por las demos 107/110/201/202.
//
// Geometría de la tabla: fetch I=16, tile 16, 4 planos, row=44, anillo 288,
// viewport 256 (valores canónicos del corkscrew 320x256).

#include <cstdio>

#include <eng/field/amiga_display_mapper.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

using eng::field::map_ring_scroll;
using eng::field::RingDisplayMapping;
using eng::s32;

RingDisplayMapping ring(s32 vx, s32 vy, bool scroll_y = true, bool linear = false, eng::u16 I = 16) {
	return map_ring_scroll<288u>(vx, vy, I, 16, 4, 44, 288, 256, scroll_y, linear);
}

} // namespace

int main() {
	// --- Horizontal (anillo 288; el offset vertical no afecta a planeaddx/BPLCON1) ---
	{
		const auto a = ring(0, 0);
		check(a.planeaddx == 0u, "vx=0 planeaddx=0");
		check(a.bplcon1 == 0x0000u, "vx=0 bplcon1=0");
	}
	{
		const auto a = ring(1, 0);
		check(a.planeaddx == 2u, "vx=1 planeaddx=2");
		check(a.bplcon1 == 0x00ffu, "vx=1 bplcon1=0xFF");
	}
	{
		const auto a = ring(15, 0);
		check(a.planeaddx == 2u && a.bplcon1 == 0x0011u, "vx=15 coarse+fine");
	}
	{
		const auto a = ring(16, 0);
		check(a.planeaddx == 2u && a.bplcon1 == 0x0000u, "vx=16 fine 0");
	}
	{
		const auto a = ring(32, 0);
		check(a.planeaddx == 4u && a.bplcon1 == 0x0000u, "vx=32 coarse 4");
	}
	// Fetch ancho I=32: el nibble bajo sigue en 0x11 y aparecen los bits 0x4400/0x8800.
	{
		const auto a = ring(1, 0, true, false, 32);
		check(a.planeaddx == 4u, "I=32 vx=1 planeaddx=4");
		check(a.bplcon1 == 0x44ffu, "I=32 vx=1 bplcon1=0x44FF (fine&16)");
	}

	// --- Vertical (anillo 288, tile 16) ---
	{
		const auto a = ring(0, 0);
		check(a.display_offset == 16u, "vy=0 offset=(0+16)%288=16");
		check(a.planeaddy == 16u * 4u * 44u, "vy=0 planeaddy");
		check(a.split_line == 272u && !a.split_active, "vy=0 split inactivo");
	}
	{
		const auto a = ring(0, 32);
		check(a.display_offset == 48u, "vy=32 offset=48");
		check(a.planeaddy == 48u * 4u * 44u, "vy=32 planeaddy");
		check(a.split_line == 240u && a.split_active, "vy=32 split activo");
	}
	{
		const auto a = ring(0, 16);
		check(a.display_offset == 32u && a.split_line == 256u && !a.split_active,
		      "vy=16 split_line=256 (frontera) no activo");
	}
	{
		const auto a = ring(0, 272);
		check(a.display_offset == 0u && a.split_line == 288u && !a.split_active,
		      "vy=272 offset=0 (wrap)");
	}
	// En modo lineal el split nunca está activo (lo resuelve el espejo).
	{
		const auto a = ring(0, 32, true, true);
		check(!a.split_active, "linear: split inactivo");
	}
	// Sin eje Y: sin offset ni split.
	{
		const auto a = ring(0, 32, false, false);
		check(a.display_offset == 0u && a.planeaddy == 0u && !a.split_active,
		      "scroll_y=false: sin offset/split");
	}
	// vy negativo se normaliza (robusto a negativos, como dmod2).
	{
		const auto a = ring(0, -16);
		check(a.display_offset == 0u, "vy=-16 -> offset 0");
	}

	// El camino runtime (DisplayHeight=0) coincide con el compile-time.
	{
		bool same = true;
		for (s32 vy = -300; vy <= 600; vy += 7) {
			const auto a = map_ring_scroll<288u>(3, vy, 16, 16, 4, 44, 288, 256, true, false);
			const auto b = map_ring_scroll<0u>(3, vy, 16, 16, 4, 44, 288, 256, true, false);
			same = same && a.planeaddx == b.planeaddx && a.bplcon1 == b.bplcon1
			            && a.planeaddy == b.planeaddy && a.display_offset == b.display_offset
			            && a.split_line == b.split_line && a.split_active == b.split_active;
		}
		check(same, "camino runtime (display_h) == compile-time (NTTP)");
	}

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: map_ring_scroll (corkscrew -> planeaddx/BPLCON1/offset/split) validado.\n");
	return 0;
}
