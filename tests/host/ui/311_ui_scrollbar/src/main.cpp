// ============================================================================
// Test HOST-311: GUI — barra de desplazamiento (ScrollBar).
// ============================================================================
//
// Valida `eng/ui/scroll.hpp`: geometria del pomo (longitud proporcional a la pagina, minimo),
// click -> valor, nudge acotado y teclado (flechas, Shift+pagina, Home/End) en las dos
// orientaciones; y un smoke test de dibujo (pomo con `fill_active` sobre la pista). Ver
// ROADMAP_GUI.md (widgets de scroll) y GUI_LIBRARY.md §11.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/311_ui_scrollbar

#include <cstdio>

#include <eng/core/types/box.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widgets.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u16 kSW = 64u;
constexpr eng::u16 kSH = 32u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kSW / 8u) + 3u) & ~3u); // 8
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kSH;     // 256
constexpr eng::u8 kPlanes = 4u;

eng::u8 pixel_at(const eng::u8* base, eng::s16 x, eng::s16 y) {
	eng::u8 c = 0u;
	for (eng::u8 p = 0u; p < kPlanes; ++p) {
		const eng::u16* w = reinterpret_cast<const eng::u16*>(
			base + p * kPlaneStride + static_cast<eng::u32>(y) * kRow +
			static_cast<eng::u32>(x / 16) * 2u);
		const eng::u8 bit = static_cast<eng::u8>((*w >> (15u - (x & 15))) & 1u);
		c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
	}
	return c;
}

eng::ui::UiEvent mouse(eng::ui::UiEventKind k, eng::s16 x, eng::s16 y) {
	eng::ui::UiEvent e {};
	e.kind = k;
	e.x = x;
	e.y = y;
	return e;
}

eng::ui::UiEvent key(eng::u16 k, bool shift) {
	eng::ui::UiEvent e {};
	e.kind = eng::ui::UiEventKind::KeyDown;
	e.key = k;
	e.shift = shift;
	return e;
}

} // namespace

int main() {
	// --- Geometria vertical ------------------------------------------------
	eng::s16 val = 0;
	eng::ui::ScrollBar sb;
	sb.bounds = eng::ui::Rect {0, 0, 8u, 32u};
	sb.value = &val;
	sb.min = 0;
	sb.max = 90;
	sb.page = 10;
	check(sb.span() == 90, "span = max - min");
	// Pomo: 10*32/100 = 3, subido al minimo 6.
	check(sb.thumb_len() == 6, "pomo con minimo aplicado");
	check(sb.thumb_pos() == 0, "pomo al inicio con value=min");
	val = 90;
	check(sb.thumb_pos() == (32 - 6), "pomo al final con value=max");
	val = 45;
	check(sb.thumb_pos() == static_cast<eng::s16>((32 - 6) * 45 / 90), "pomo proporcional");

	// --- Click -> valor ----------------------------------------------------
	val = 0;
	check(eng::ui::event_scroll_bar(sb, mouse(eng::ui::UiEventKind::MouseDown, 4, 16)),
	      "click consume");
	// rel = 16 - 0 - pomo/2 = 13; room = 26; 90*13/26 = 45.
	check(val == 45, "click fija el valor centrando el pomo");
	check(sb.has(eng::ui::WfPressed), "queda pulsada tras MouseDown");
	eng::ui::event_scroll_bar(sb, mouse(eng::ui::UiEventKind::MouseUp, 4, 16));
	check(!sb.has(eng::ui::WfPressed), "MouseUp libera");

	// --- Nudge acotado -----------------------------------------------------
	sb.set_flag(eng::ui::WfFocused);
	val = 5;
	eng::ui::event_scroll_bar(sb, key(eng::ui::kKeyUp, false));
	check(val == 4, "flecha arriba -1");
	eng::ui::event_scroll_bar(sb, key(eng::ui::kKeyDown, false));
	check(val == 5, "flecha abajo +1");
	eng::ui::event_scroll_bar(sb, key(eng::ui::kKeyDown, true));
	check(val == 15, "Shift+flecha = pagina (10)");
	eng::ui::event_scroll_bar(sb, key(eng::ui::kKeyEnd, false));
	check(val == 90, "End -> max");
	eng::ui::event_scroll_bar(sb, key(eng::ui::kKeyHome, false));
	check(val == 0, "Home -> min");
	// Acotado por abajo.
	eng::ui::event_scroll_bar(sb, key(eng::ui::kKeyUp, false));
	check(val == 0, "nudge no baja del minimo");

	// En vertical, Left/Right no aplican.
	check(!eng::ui::event_scroll_bar(sb, key(eng::ui::kKeyRight, false)),
	      "Left/Right no aplican en vertical");

	// --- Orientacion horizontal -------------------------------------------
	eng::s16 hval = 0;
	eng::ui::ScrollBar hb;
	hb.bounds = eng::ui::Rect {0, 0, 40u, 8u};
	hb.value = &hval;
	hb.min = 0;
	hb.max = 100;
	hb.page = 10;
	hb.vertical = false;
	hb.set_flag(eng::ui::WfFocused);
	check(hb.track_len() == 40, "pista horizontal = ancho");
	eng::ui::event_scroll_bar(hb, key(eng::ui::kKeyRight, false));
	check(hval == 1, "derecha +1 en horizontal");
	eng::ui::event_scroll_bar(hb, key(eng::ui::kKeyLeft, true));
	check(hval == 0, "izquierda pagina acotada");
	check(!eng::ui::event_scroll_bar(hb, key(eng::ui::kKeyDown, false)),
	      "Up/Down no aplican en horizontal");

	// --- Smoketest de dibujo ----------------------------------------------
	alignas(2) eng::u8 mem[kPlaneStride * kPlanes] {};
	eng::field::ContiguousPlayfield pf {};
	check(pf.bind_raw(mem, sizeof(mem), kSW, kSH, kPlanes), "bind_raw pantalla");
	eng::field::Surface screen {pf, eng::field::SurfaceRect {0, 0, kSW, kSH}};
	eng::ui::UiTheme th {};
	th.edit_bg = 0u;
	th.fill_active = 1u;
	th.shine = 2u;
	th.shadow = 3u;
	eng::ui::UiPainter painter {screen, {}, th};
	val = 45;
	eng::ui::draw_scroll_bar(sb, painter);
	// value=45 -> thumb_pos = 26*45/90 = 13: pomo en y in [13, 19); interior (sin bisel).
	check(pixel_at(mem, 4, 16) == 1u, "pomo pintado con fill_active");
	check(pixel_at(mem, 4, 5) == 0u, "pista pintada con edit_bg");

	if (failures == 0) {
		std::printf("OK: barra de desplazamiento validada.\n");
	} else {
		std::printf("FALLOS: %d\n", failures);
	}
	return failures == 0 ? 0 : 1;
}
