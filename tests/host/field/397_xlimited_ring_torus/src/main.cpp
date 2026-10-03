// ============================================================================
// Test HOST-397: el TORO del anillo XYLimited (demo 205)
// ============================================================================
//
// Con la geometría real del corcóscru de la 205 —anillo `display_height = 320`,
// viewport 208, tile 16, 3 planos, fila 44 bytes— valida que `map_ring_scroll`
// traduce bien la cámara a los registros del display a lo largo de TODO el
// recorrido Y (que en un anillo de 20 tiles es `320 - 208 = 112` px).
//
// El punto que arregló el "mapa roto" de la 205: el número de tiles del anillo
// (320/16 = 20) debe **dividir** la altura del mapa (40), de modo que el anillo
// cierre sin costura al envolver. Con anillo 288 (18 tiles, no divide 40) la
// banda inferior leía filas equivocadas del toro.
//
// (El mapper en sí —planeaddx/BPLCON1/offset/split/negativos— lo cubre HOST-063;
// aquí se fija la INVARIANTE de geometría y el recorrido concreto de la 205.)

#include <cstdio>

#include <eng/field/amiga_display_mapper.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}

using eng::playfield::map_ring_scroll;
using eng::s32;

// Geometría canónica de la 205: anillo 320, viewport 208, tile 16, 3 planos, row 44.
auto at(s32 vy) { return map_ring_scroll<320u>(0, vy, 16, 16, 3, 44, 320, 208, true, false); }

constexpr eng::u16 kDisplayHeight = 320u;
constexpr eng::u16 kViewportH = 208u;
constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kMapHeight = 40u;

} // namespace

int main() {
	// El recorrido Y completo del toro: anillo menos ventana.
	static_assert(kDisplayHeight - kViewportH == 112u, "kYMax = display_height - viewport_h");

	// vy=0: offset (0+16)%320=16, sin split.
	{
		const auto a = at(0);
		check(a.display_offset == 16u, "vy=0 offset=16");
		check(a.split_line == 304u && !a.split_active, "vy=0 split inactivo (304>=208)");
	}
	// Frontera: split_line == viewport (208) -> aún inactivo.
	{
		const auto a = at(96);
		check(a.display_offset == 112u, "vy=96 offset=112");
		check(a.split_line == 208u && !a.split_active, "vy=96 frontera sin split");
	}
	// Extremo del recorrido (kYMax=112): offset 128, split ACTIVO (192 < 208).
	{
		const auto a = at(112);
		check(a.display_offset == 128u, "vy=112 offset=128");
		check(a.split_line == 192u && a.split_active, "vy=112 split activo");
	}

	// INVARIANTE que arregló el mapa roto: los tiles del anillo dividen el mapa.
	static_assert(kDisplayHeight % kTile == 0u, "anillo multiplo del tile");
	const eng::u16 ring_tiles = kDisplayHeight / kTile; // 20
	check(ring_tiles != 0u && (kMapHeight % ring_tiles) == 0u,
	      "20 tiles del anillo dividen la altura del mapa (40): cierra sin costura");

	// El recorrido Y completo selecciona un offset distinto cada px (sin frames repetidos).
	{
		bool distinct = true;
		for (s32 vy = 1; vy <= static_cast<s32>(kDisplayHeight - kViewportH); ++vy) {
			if (at(vy).display_offset == at(vy - 1).display_offset) distinct = false;
		}
		check(distinct, "offsets distintos en [0,112] (0 frames repetidos por Y)");
	}

	// El anillo cierra: un periodo (320) vuelve al mismo offset sin activar el split.
	{
		const auto a = at(0);
		const auto b = at(320);
		check(a.display_offset == b.display_offset && b.display_offset == 16u,
		      "wrap exacto del anillo (320 -> offset 16)");
	}

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: toro del anillo XYLimited 320/208 (recorrido Y 112 + split) validado.\n");
	return 0;
}
