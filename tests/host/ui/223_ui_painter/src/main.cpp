// ============================================================================
// Test HOST-223: GUI G0 — UiPainter, UiTheme, medida/recorte de texto y eng::Box.
// ============================================================================
//
// Pinta sobre un `ContiguousPlayfield` enlazado a memoria host (sin Chip) y lee el color de
// cada pixel del mapeo planar para comprobar que el chrome cae en el lado correcto:
// `frame` (perimetro), `bevel_out`/`bevel_in` (shine/shadow), `panel`, `button_face`, glifos,
// `text_width` (UTF-8) y `draw_text_clipped` (sin partir glifos a medias).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/223_ui_painter

#include <cstdio>

#include <eng/core/types/box.hpp>
#include <eng/field/surface.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/text.hpp>
#include <eng/ui/theme.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u16 kW = 32u;
constexpr eng::u16 kH = 16u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kW / 8u) + 3u) & ~3u); // 4
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kH;      // 64
constexpr eng::u8 kPlanes = 4u;
const eng::ui::Rect kFull {0, 0, kW, kH};

// Tema de prueba: colores 0..7 en planos distintos (0=limpio, shine=1, shadow=2, fill=3,
// fill_active=4, text=5, edit_bg=6, focus=7).
constexpr eng::ui::UiTheme kT {
	.bg = 0u, .shine = 1u, .shadow = 2u, .fill = 3u, .fill_active = 4u,
	.text = 5u, .text_dim = 0u, .edit_bg = 6u, .focus_ring = 7u,
	.panel_frame = eng::ui::FrameStyle::Raised, .button_frame = eng::ui::FrameStyle::Raised,
};
constexpr eng::ui::UiTheme kFlatT {
	.shine = 1u, .shadow = 2u, .fill = 6u,
	.panel_frame = eng::ui::FrameStyle::Flat, .button_frame = eng::ui::FrameStyle::Flat,
};

const eng::u8* g_planes = nullptr;

// Color del pixel (x,y) segun el mapeo planar (MSB primero, palabra en `byte_for(x)`).
eng::u8 pixel_at(eng::s16 x, eng::s16 y) {
	eng::u8 c = 0u;
	for (eng::u8 p = 0u; p < kPlanes; ++p) {
		const eng::u16* w = reinterpret_cast<const eng::u16*>(
			g_planes + p * kPlaneStride + static_cast<eng::u32>(y) * kRow +
			static_cast<eng::u32>(x / 16) * 2u);
		const eng::u8 bit = static_cast<eng::u8>((*w >> (15u - (x & 15))) & 1u);
		c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
	}
	return c;
}

eng::u32 count_colored(eng::s16 x0, eng::s16 x1, eng::s16 y0, eng::s16 y1) {
	eng::u32 n = 0u;
	for (eng::s16 y = y0; y <= y1; ++y) {
		for (eng::s16 x = x0; x <= x1; ++x) {
			if (pixel_at(x, y) != 0u) {
				++n;
			}
		}
	}
	return n;
}

} // namespace

int main() {
	alignas(2) eng::u8 planes[kPlaneStride * kPlanes] {};
	g_planes = planes;
	eng::field::ContiguousPlayfield pf {};
	check(pf.bind_raw(planes, sizeof(planes), kW, kH, kPlanes), "bind_raw del playfield");
	eng::field::Surface surf {pf, eng::field::SurfaceRect {0, 0, kW, kH}};
	eng::ui::UiPainter p {surf, nullptr, kT};
	const auto clear = [&]() { p.fill(kFull, 0u); };

	// --- clip / Box ---
	check(p.clip().w == kW && p.clip().h == kH, "clip() = tamano de la surface");
	check(!p.clip().empty(), "clip no vacio");

	// --- fill ---
	clear();
	p.fill(eng::ui::Rect {2, 2, 4u, 3u}, 3u);
	check(pixel_at(2, 2) == 3u && pixel_at(5, 4) == 3u, "fill dentro");
	check(pixel_at(1, 2) == 0u && pixel_at(6, 5) == 0u, "fill no se sale");

	// --- frame (perimetro) ---
	clear();
	p.frame(eng::ui::Rect {0, 0, 8u, 8u}, 5u);
	check(pixel_at(0, 0) == 5u && pixel_at(7, 7) == 5u, "frame esquinas");
	check(pixel_at(4, 0) == 5u && pixel_at(0, 4) == 5u && pixel_at(7, 4) == 5u &&
		      pixel_at(4, 7) == 5u,
	      "frame lados");
	check(pixel_at(4, 4) == 0u && pixel_at(1, 1) == 0u, "frame interior intacto");

	// --- bevel_out / bevel_in ---
	clear();
	p.bevel_out(eng::ui::Rect {0, 0, 10u, 10u});
	check(pixel_at(5, 0) == 1u && pixel_at(0, 5) == 1u, "bevel_out shine arriba/izquierda");
	check(pixel_at(5, 9) == 2u && pixel_at(9, 5) == 2u, "bevel_out shadow abajo/derecha");
	check(pixel_at(5, 5) == 0u, "bevel_out interior intacto");

	clear();
	p.bevel_in(eng::ui::Rect {0, 0, 10u, 10u});
	check(pixel_at(5, 0) == 2u && pixel_at(0, 5) == 2u, "bevel_in shadow arriba/izquierda");
	check(pixel_at(5, 9) == 1u && pixel_at(9, 5) == 1u, "bevel_in shine abajo/derecha");

	// --- panel (relleno + marco del tema) ---
	clear();
	p.panel(eng::ui::Rect {0, 0, 10u, 10u});
	check(pixel_at(5, 5) == 3u, "panel rellena con fill");
	check(pixel_at(5, 0) == 1u && pixel_at(5, 9) == 2u, "panel Raised = bevel_out");

	eng::ui::UiPainter pf_flat {surf, nullptr, kFlatT};
	clear();
	pf_flat.panel(eng::ui::Rect {0, 0, 10u, 10u});
	check(pixel_at(5, 5) == 6u && pixel_at(5, 0) == 6u && pixel_at(0, 0) == 6u,
	      "panel Flat rellena hasta el borde");

	// --- button_face ---
	clear();
	p.button_face(eng::ui::Rect {0, 0, 10u, 10u}, false);
	check(pixel_at(5, 5) == 3u && pixel_at(5, 0) == 1u, "button_face normal = fill + bevel_out");
	clear();
	p.button_face(eng::ui::Rect {0, 0, 10u, 10u}, true);
	check(pixel_at(5, 5) == 4u && pixel_at(5, 0) == 2u,
	      "button_face pulsado = fill_active + bevel_in");

	// --- glyph (1-bit, MSB primero) ---
	clear();
	const eng::u16 g[1] = {0x0001u}; // bit 0 -> columna w-1
	p.glyph(0, 0, g, 16u, 1u, 5u);
	check(pixel_at(15, 0) == 5u && count_colored(0, 14, 0, 0) == 0u, "glyph MSB primero");

	// --- text_width (avance por code point, no por byte) ---
	check(eng::ui::text_width("") == 0u, "text_width vacio");
	check(eng::ui::text_width("ABC") == 24u, "text_width 3 ASCII");
	check(eng::ui::text_width("A\xC3\xA9") == 16u, "text_width 2 code points (acento UTF-8)");

	// --- draw_text_clipped (no parte glifos a medias) ---
	clear();
	eng::ui::draw_text_clipped(p, eng::ui::Rect {0, 0, 17u, 8u}, 0, 0, "ABC", 5u);
	check(count_colored(0, 15, 0, 7) > 0u, "texto dibuja lo que cabe");
	check(count_colored(16, 23, 0, 7) == 0u, "texto no pinta el glifo que no cabe");

	clear();
	p.text(0, 0, "ABC", 5u); // sin recorte: el 3.er glifo (x=16..23) si se pinta
	check(count_colored(16, 23, 0, 7) > 0u, "texto sin recorte pinta el 3.er glifo");

	// --- presets ---
	check(eng::ui::kThemeWb13.panel_frame == eng::ui::FrameStyle::Raised,
	      "kThemeWb13 Raised");
	check(eng::ui::kThemeFlat.panel_frame == eng::ui::FrameStyle::Flat &&
		      eng::ui::kThemeFlat.button_frame == eng::ui::FrameStyle::Flat,
	      "kThemeFlat sin bisel");

	if (failures == 0) {
		std::printf("OK: GUI G0 (UiPainter/UiTheme/texto) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
