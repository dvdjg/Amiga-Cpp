// ============================================================================
// Test HOST-469: entrada del HUD (`eng/hud/input.hpp`, `HudInput` + `hud_hit_test`).
// ============================================================================
//
// Fase H2 del roadmap (`docs/guides/roadmap/ROADMAP_HUD.md`): hit-test por capa y por
// formula de rejilla (pitch NO multiplo de 16), click (down+up en el mismo objetivo),
// seleccion y foco independientes, navegacion por joystick/accion y consumo segun
// `capture_input` (HUD_SYSTEM.md §7).
//
//   bash tools/run-host-tests.sh tests/host/ui/469_hud_input

#include <cstdio>

#include <eng/hud/input.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

using eng::s16;
using eng::u16;

/// Evento de raton en coordenadas de region.
eng::ui::UiEvent mouse(eng::ui::UiEventKind kind, s16 x, s16 y) {
	eng::ui::UiEvent ev {};
	ev.kind = kind;
	ev.x = x;
	ev.y = y;
	return ev;
}

/// Evento de joystick con bitmask de `UiEvent::joy_buttons`.
eng::ui::UiEvent joy(u16 buttons) {
	eng::ui::UiEvent ev {};
	ev.kind = eng::ui::UiEventKind::JoyButton;
	ev.joy_buttons = buttons;
	return ev;
}

/// Evento de accion semantica.
eng::ui::UiEvent action(eng::ui::UiAction a) {
	eng::ui::UiEvent ev {};
	ev.kind = eng::ui::UiEventKind::Action;
	ev.action = a;
	return ev;
}

} // namespace

int main() {
	eng::hud::Hud<> hud {};

	// Region valida (Band canonica): el modelo la exige para la composicion, no para el hit.
	eng::hud::HudRegionSpec spec {};
	spec.placement = eng::hud::HudPlacement::Band;
	spec.band = eng::hud::HudBand {208u, 48u, 2u, true};
	CHECK(hud.add_region(spec).valid());

	// --- Rejilla 3x5 con pitch 10x12 (NO multiplo de 16), 15 slots -------------------------
	const eng::hud::GridHandle grid = hud.add_grid(3u, 5u, 10u, 12u);
	CHECK(grid.valid());
	static const eng::hud::HudSlotView slots[15] = {};
	CHECK(hud.bind_grid(grid, eng::Span<const eng::hud::HudSlotView> {slots, 15u}));
	eng::hud::HudGridHit gh {};
	gh.area = eng::Box {10, 10, 50u, 36u}; // 5 columnas x 10 px, 3 filas x 12 px
	gh.cols = 5u;
	gh.rows = 3u;
	gh.pitch_x = 10u;
	gh.pitch_y = 12u;
	gh.first_id = 100u;
	gh.grid = grid;
	CHECK(hud.push_grid_hit(gh));

	// --- Dos rectangulos solapados: capa 1 (id 1) y capa 2 (id 2) --------------------------
	eng::hud::HudHitRect r1 {};
	r1.box = eng::Box {0, 0, 32u, 16u};
	r1.id = 1u;
	r1.layer = 1u;
	eng::hud::HudHitRect r2 {};
	r2.box = eng::Box {0, 0, 16u, 16u};
	r2.id = 2u;
	r2.layer = 2u;
	CHECK(hud.push_hit(r1));
	CHECK(hud.push_hit(r2));

	// 1) Prioridad por capa: (4,4) toca ambos; gana el de capa 2.
	{
		const eng::hud::HudHit h = eng::hud::hud_hit_test(hud, 4, 4);
		CHECK(h.hit && h.id == 2u && h.layer == 2u);
	}
	// 2) Empate de capa: el ULTIMO empujado gana (orden de pintado).
	{
		eng::hud::HudHitRect r3 {};
		r3.box = eng::Box {0, 0, 8u, 8u};
		r3.id = 3u;
		r3.layer = 2u;
		CHECK(hud.push_hit(r3));
		const eng::hud::HudHit h = eng::hud::hud_hit_test(hud, 4, 4);
		CHECK(h.hit && h.id == 3u);
	}
	// 3) Rejilla por formula (pitch no multiplo de 16): (45,40) -> col 3, fila 2 -> slot 113.
	{
		const eng::hud::HudHit h = eng::hud::hud_hit_test(hud, 45, 40);
		CHECK(h.hit && h.grid.valid() && h.slot == 13u && h.id == 113u);
	}
	// 4) Fuera de la rejilla: (70,20) no toca nada; (60,20) cae en el borde derecho
	//    (`contains` inclusivo) pero la columna 5 no existe -> sin hit.
	{
		const eng::hud::HudHit a = eng::hud::hud_hit_test(hud, 70, 20);
		const eng::hud::HudHit b = eng::hud::hud_hit_test(hud, 60, 20);
		CHECK(!a.hit && !b.hit);
	}

	// --- Click: down+up en el mismo objetivo -----------------------------------------------
	eng::hud::HudInput in {};
	in.set_focus_grid(grid);
	const eng::hud::HudInput::Policy open {}; // sin captura

	{
		const eng::hud::HudInput::Result d =
			in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseDown, 45, 40));
		CHECK(d.consumed && in.pressed());
		const eng::hud::HudInput::Result u =
			in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseUp, 45, 40));
		CHECK(u.consumed && u.clicked && u.selection_changed && u.slot == 13u && u.id == 113u);
		CHECK(hud.grid(grid).selection == 13u);
	}
	// 5) Down y up en objetivos distintos: consumo sin click.
	{
		(void)in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseDown, 45, 40));
		const eng::hud::HudInput::Result u =
			in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseUp, 55, 40));
		CHECK(u.consumed && !u.clicked);
		CHECK(hud.grid(grid).selection == 13u); // no cambio
	}
	// 6) Down fuera de todo: NO se consume (el juego ve el click) y no hay pressed.
	{
		const eng::hud::HudInput::Result d =
			in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseDown, 200, 200));
		CHECK(!d.consumed && !in.pressed());
		const eng::hud::HudInput::Result u =
			in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseUp, 200, 200));
		CHECK(!u.consumed && !u.clicked);
	}
	// 7) MouseMove: se consume sobre hit interactivo y pasa al juego sobre vacio.
	{
		const eng::hud::HudInput::Result over =
			in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseMove, 4, 4));
		const eng::hud::HudInput::Result empty =
			in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseMove, 200, 200));
		CHECK(over.consumed && !empty.consumed);
	}
	// 8) `capture_input`: la region modal consume TODO (aunque no haya hit). La politica se
	//    deriva del **contrato de region** (`HudRegionSpec::capture_input`).
	{
		eng::hud::HudRegionSpec modal_spec {};
		modal_spec.placement = eng::hud::HudPlacement::Screen;
		modal_spec.capture_input = true;
		const eng::hud::RegionHandle modal_region = hud.add_region(modal_spec);
		CHECK(modal_region.valid());
		const eng::hud::HudInput::Policy modal =
			eng::hud::HudInput::Policy::from_region(hud.region(modal_region));
		const eng::hud::HudInput::Result mv =
			in.handle(hud, modal, mouse(eng::ui::UiEventKind::MouseMove, 200, 200));
		const eng::hud::HudInput::Result dn =
			in.handle(hud, modal, mouse(eng::ui::UiEventKind::MouseDown, 200, 200));
		CHECK(mv.consumed && dn.consumed && !in.pressed());
	}
	// 9) PointerCancel con down pendiente: consume y limpia (el up posterior no clica).
	{
		(void)in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseDown, 45, 40));
		const eng::hud::HudInput::Result c =
			in.handle(hud, open, mouse(eng::ui::UiEventKind::PointerCancel, 45, 40));
		CHECK(c.consumed && !in.pressed());
		const eng::hud::HudInput::Result u =
			in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseUp, 45, 40));
		CHECK(!u.clicked);
	}

	// --- Foco por joystick y bordes de rejilla --------------------------------------------
	// Foco inicial 0; derecha -> 1; arriba en fila 0 -> sin cambio; abajo x2 -> 6, 11;
	// abajo otra vez en la ultima fila -> sin cambio.
	{
		const eng::hud::HudInput::Result r =
			in.handle(hud, open, joy(eng::hud::kHudJoyRight));
		CHECK(r.consumed && r.focus_changed);
		const eng::hud::HudInput::Result up =
			in.handle(hud, open, joy(eng::hud::kHudJoyUp));
		CHECK(up.consumed && !up.focus_changed); // borde superior
		(void)in.handle(hud, open, joy(eng::hud::kHudJoyDown));
		(void)in.handle(hud, open, joy(eng::hud::kHudJoyDown));
		CHECK(hud.grid(grid).focus == 11u);
		const eng::hud::HudInput::Result d3 =
			in.handle(hud, open, joy(eng::hud::kHudJoyDown));
		CHECK(d3.consumed && !d3.focus_changed); // borde inferior
	}
	// 10) Fire = click del foco (slot 11): selecciona sin raton.
	{
		const eng::hud::HudInput::Result f =
			in.handle(hud, open, joy(eng::hud::kHudJoyFire));
		CHECK(f.consumed && f.clicked && f.slot == 11u);
		CHECK(hud.grid(grid).selection == 11u);
	}
	// 11) Accion semantica: MoveLeft -> foco 10; Accept -> seleccion 10; Cancel tras down.
	{
		const eng::hud::HudInput::Result l =
			in.handle(hud, open, action(eng::ui::UiAction::MoveLeft));
		CHECK(l.consumed && l.focus_changed && hud.grid(grid).focus == 10u);
		const eng::hud::HudInput::Result a =
			in.handle(hud, open, action(eng::ui::UiAction::Accept));
		CHECK(a.consumed && a.clicked && hud.grid(grid).selection == 10u);
		(void)in.handle(hud, open, mouse(eng::ui::UiEventKind::MouseDown, 45, 40));
		const eng::hud::HudInput::Result c =
			in.handle(hud, open, action(eng::ui::UiAction::Cancel));
		CHECK(c.consumed && !in.pressed());
	}
	// 12) Teclas y Tick: el HUD no los consume (los gestiona el servicio/el juego).
	{
		eng::ui::UiEvent key {};
		key.kind = eng::ui::UiEventKind::KeyDown;
		const eng::hud::HudInput::Result k = in.handle(hud, open, key);
		eng::ui::UiEvent tick {};
		tick.kind = eng::ui::UiEventKind::Tick;
		const eng::hud::HudInput::Result t = in.handle(hud, open, tick);
		CHECK(!k.consumed && !t.consumed);
	}

	if (g_fail != 0) {
		std::printf("HOST-469 hud_input: %d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: hit-test por capa/formula, click, seleccion/foco, joystick/accion y "
		    "consumo por capture_input validados.\n");
	return 0;
}
