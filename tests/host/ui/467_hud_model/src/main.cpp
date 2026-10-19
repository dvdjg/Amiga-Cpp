// ============================================================================
// Test HOST-467: modelo del HUD (`eng/hud/model.hpp`, `region.hpp`, `theme.hpp`).
// ============================================================================
//
// Fase H0 del roadmap (`docs/guides/roadmap/ROADMAP_HUD.md`): alta de componentes
// hasta capacidad y error al desbordar, `set()` marca *dirty* SOLO al cambiar el
// valor visible, contador base 10/16 con ceros a la izquierda y negativos,
// transiciones de `DragSession`, selección/foco de rejilla y `HudRegionSpec`
// inválida rechazada. Cabeceras puras (sin hardware): se instancian con dos
// configuraciones de capacidad distintas.
//
//   bash tools/run-host-tests.sh tests/host/ui/467_hud_model

#include <cstdio>

#include <eng/hud/hud.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

/// Configuración pequeña: fuerza el desbordamiento de capacidad en pocos altas.
using HudSmall = eng::hud::Hud<2u, 4u, 8u, 8u, 4u>;

/// Configuración por defecto: el camino normal del modelo.
using HudDefault = eng::hud::Hud<>;

/// Compara un texto del modelo (sin terminador) con una cadena C.
bool text_is(eng::Span<const char> s, const char* expected) {
	eng::usize i = 0u;
	while (i < s.size() && expected[i] != '\0' && s[i] == expected[i]) {
		++i;
	}
	return i == s.size() && expected[i] == '\0';
}

/// Alta hasta la capacidad y rechazo tipado al desbordar (regiones, widgets, slots, ops, hits).
void test_capacity() {
	HudSmall hud;
	eng::hud::HudRegionSpec band;
	band.placement = eng::hud::HudPlacement::Band;
	band.band.top = 208u;
	band.band.height = 48u;
	CHECK(hud.add_region(band).valid());
	CHECK(hud.add_region(band).valid());
	CHECK(!hud.add_region(band).valid()); // MaxRegions = 2
	CHECK(hud.region_count() == 2u);

	const auto meter = hud.add_meter();
	const auto counter = hud.add_counter();
	const auto grid = hud.add_grid(2u, 2u);
	const auto label = hud.add_label(eng::Span<const char> {"hp", 2u});
	CHECK(meter.valid() && counter.valid() && grid.valid() && label.valid());
	CHECK(!hud.add_button(eng::Span<const char> {"ok", 2u}).valid()); // MaxWidgets = 4

	HudDefault big;
	CHECK(!big.add_counter(1u).valid());       // base < 2
	CHECK(!big.add_counter(37u).valid());      // base > 36
	CHECK(!big.add_counter(10u, 33u).valid()); // digits > kHudMaxDigits
	CHECK(!big.add_grid(0u, 2u).valid());      // filas = 0

	eng::hud::HudSlotView slots[9] {};
	CHECK(hud.bind_grid(grid, eng::Span<const eng::hud::HudSlotView> {slots, 8u}));
	CHECK(!hud.bind_grid(grid, eng::Span<const eng::hud::HudSlotView> {slots, 9u})); // MaxSlots = 8

	eng::hud::HudDrawOp op {};
	for (eng::u8 i = 0u; i < 8u; ++i) {
		CHECK(hud.push_op(op));
	}
	CHECK(!hud.push_op(op)); // MaxDrawOps = 8
	hud.clear_ops();
	CHECK(hud.ops().empty());

	eng::hud::HudHitRect hit {};
	for (eng::u8 i = 0u; i < 4u; ++i) {
		CHECK(hud.push_hit(hit));
	}
	CHECK(!hud.push_hit(hit)); // MaxHitRects = 4
	CHECK(hud.push_grid_hit(eng::hud::HudGridHit {}));
	hud.clear_hits();
	hud.clear_grid_hits();
	CHECK(hud.hits().empty() && hud.grid_hits().empty());
}

/// `set()` marca *dirty* solo cuando cambia el valor visible; `clear_dirty` no altera valores.
void test_dirty() {
	HudDefault hud;
	const auto meter = hud.add_meter();
	const eng::u32 r0 = hud.dirty_revision();
	CHECK(!hud.set_meter(meter, 0)); // mismo valor inicial
	CHECK(hud.dirty_revision() == r0);
	CHECK(hud.set_meter(meter, 7)); // cambio visible
	CHECK(hud.dirty_revision() == r0 + 1u);
	CHECK(hud.any_dirty());
	hud.clear_dirty();
	CHECK(!hud.any_dirty());
	CHECK(!hud.set_meter(meter, 7)); // mismo valor tras repintar
	CHECK(hud.dirty_revision() == r0 + 1u);
	CHECK(hud.set_meter(meter, 7, 12)); // cambia el máximo
	CHECK(hud.dirty_revision() == r0 + 2u);

	const auto counter = hud.add_counter(10u, 4u, false);
	CHECK(!hud.set_counter(counter, 0)); // mismo valor inicial
	CHECK(hud.set_counter(counter, 42));
	CHECK(hud.dirty_revision() == r0 + 3u);
	CHECK(!hud.set_counter(counter, 42));
	CHECK(hud.dirty_revision() == r0 + 3u);

	// **Skins (H6)**: el estilo del medidor/contador marca sucio solo al cambiar; el
	// valor y el texto formateado no cambian con el estilo.
	CHECK(hud.set_meter_style(meter, eng::hud::MeterStyle::Segmented));
	CHECK(hud.dirty_revision() == r0 + 4u);
	CHECK(hud.meter(meter).style == eng::hud::MeterStyle::Segmented);
	CHECK(!hud.set_meter_style(meter, eng::hud::MeterStyle::Segmented)); // sin cambio
	CHECK(hud.dirty_revision() == r0 + 4u);
	CHECK(hud.set_counter_style(counter, eng::hud::CounterStyle::Bar, 100u));
	CHECK(hud.dirty_revision() == r0 + 5u);
	CHECK(hud.counter(counter).style == eng::hud::CounterStyle::Bar);
	CHECK(hud.counter(counter).present_max == 100u);
	CHECK(text_is(hud.counter_text(counter), "42")); // el formato no cambia con el estilo
	CHECK(!hud.set_counter_style(counter, eng::hud::CounterStyle::Bar, 100u)); // sin cambio
	CHECK(hud.set_counter_style(counter, eng::hud::CounterStyle::Bar, 200u)); // cambia la escala
	CHECK(hud.dirty_revision() == r0 + 6u);
}

/// Contadores: base 10, relleno de ceros, base 16 en mayúsculas, negativos y base 2.
void test_counter() {
	HudDefault hud;
	const auto dec = hud.add_counter(10u, 1u, false);
	CHECK(hud.set_counter(dec, 1));
	CHECK(text_is(hud.counter_text(dec), "1"));
	(void)hud.set_counter(dec, 0);
	CHECK(text_is(hud.counter_text(dec), "0"));
	(void)hud.set_counter(dec, 12345);
	CHECK(text_is(hud.counter_text(dec), "12345"));

	const auto padded = hud.add_counter(10u, 6u, true);
	(void)hud.set_counter(padded, 42);
	CHECK(text_is(hud.counter_text(padded), "000042"));

	const auto hex = hud.add_counter(16u, 1u, false);
	(void)hud.set_counter(hex, 0xbeef);
	CHECK(text_is(hud.counter_text(hex), "BEEF"));

	const auto neg = hud.add_counter(10u, 1u, false);
	(void)hud.set_counter(neg, -42);
	CHECK(text_is(hud.counter_text(neg), "-42"));
	(void)hud.set_counter(neg, static_cast<eng::s32>(-2147483647 - 1));
	CHECK(text_is(hud.counter_text(neg), "-2147483648"));

	const auto bin = hud.add_counter(2u, 1u, false);
	(void)hud.set_counter(bin, 5);
	CHECK(text_is(hud.counter_text(bin), "101"));
	(void)hud.set_counter(bin, -1);
	CHECK(text_is(hud.counter_text(bin), "-1"));
}

/// Rejilla: selección dentro de rango y foco por filas/columnas con recorte en los bordes.
void test_grid() {
	HudDefault hud;
	const auto grid = hud.add_grid(2u, 2u);
	eng::hud::HudSlotView slots[4] {};
	CHECK(hud.bind_grid(grid, eng::Span<const eng::hud::HudSlotView> {slots, 4u}));
	CHECK(!hud.grid_select(grid, 9u)); // fuera de rango
	CHECK(hud.grid_select(grid, 2u));
	CHECK(hud.grid(grid).selection == 2u);
	CHECK(!hud.grid_select(grid, 2u)); // misma selección
	CHECK(hud.grid_move_focus(grid, 1, 0));
	CHECK(hud.grid(grid).focus == 1u);
	CHECK(hud.grid_move_focus(grid, 0, 1));
	CHECK(hud.grid(grid).focus == 3u);
	CHECK(!hud.grid_move_focus(grid, 1, 0)); // borde derecho
	CHECK(!hud.grid_move_focus(grid, 0, 1)); // borde inferior
	CHECK(hud.grid_move_focus(grid, -1, -1));
	CHECK(hud.grid(grid).focus == 0u);
}

/// Máquina de arrastre: umbral, captura, *click*, *drop*, cancelación y reset.
void test_drag() {
	HudDefault hud;
	const auto grid = hud.add_grid(2u, 2u);
	CHECK(hud.drag().phase == eng::hud::DragPhase::Idle);
	CHECK(!hud.drag_move(1, 1, eng::hud::kHudDragStartPx)); // sin sesión
	CHECK(hud.drag_begin(grid, 0u, 10u, 2u, 0, 0));
	CHECK(hud.drag().phase == eng::hud::DragPhase::Pressed);
	CHECK(!hud.drag_begin(grid, 1u, 11u, 1u, 0, 0)); // ya hay sesión
	CHECK(hud.drag_move(1, 1, eng::hud::kHudDragStartPx)); // bajo el umbral: sigue pulsado
	CHECK(hud.drag().phase == eng::hud::DragPhase::Pressed);
	CHECK(hud.drag_move(5, 0, eng::hud::kHudDragStartPx)); // supera el umbral
	CHECK(hud.drag().phase == eng::hud::DragPhase::Dragging);
	// Destino + politica `accepts(item_id, qty)` (H3): el juego la consulta por movimiento.
	CHECK(hud.set_drag_target(grid, 2u, true));
	CHECK(hud.drag().target_slot == 2u && hud.drag().accepts);
	CHECK(!hud.set_drag_target(grid, 2u, true)); // sin cambio
	CHECK(hud.set_drag_target(grid, 3u, false)); // destino rechazado por la politica
	CHECK(!hud.drag().accepts && hud.drag().target_slot == 3u);
	CHECK(!hud.set_drag_target(grid, 3u, false)); // sin cambio
	CHECK(!hud.drag_release()); // en arrastre no es click
	CHECK(hud.drag_drop());
	CHECK(hud.drag().phase == eng::hud::DragPhase::Dropped);
	CHECK(!hud.set_drag_target(grid, 2u, true)); // sin sesion activa
	CHECK(!hud.drag_drop()); // terminal
	CHECK(hud.drag_reset());
	CHECK(hud.drag().phase == eng::hud::DragPhase::Idle);
	CHECK(hud.drag().target_slot == eng::hud::kHudNoSlot); // reset limpia el destino
	CHECK(hud.drag_begin(grid, 0u, 10u, 1u, 0, 0));
	CHECK(hud.drag_cancel());
	CHECK(hud.drag().phase == eng::hud::DragPhase::Cancelled);
	CHECK(hud.drag_reset());
	CHECK(hud.drag_begin(grid, 0u, 10u, 1u, 0, 0));
	CHECK(hud.drag_release()); // click puro
	CHECK(hud.drag().phase == eng::hud::DragPhase::Idle);
}

/// Regiones: banda válida, corte fuera del display, planos imposibles y restauración de overlay.
void test_region() {
	eng::hud::HudRegionSpec band;
	band.placement = eng::hud::HudPlacement::Band;
	band.band.top = 208u;
	band.band.height = 48u;
	band.band.planes = 2u;
	band.band.own_palette = true;
	CHECK(eng::hud::band_valid(band.band));
	CHECK(eng::hud::region_valid(band));

	eng::hud::HudRegionSpec bad = band;
	bad.band.height = 0u;
	CHECK(!eng::hud::region_valid(bad));
	bad = band;
	bad.band.top = 240u;
	bad.band.height = 32u; // 272 > 256 lineas de display
	CHECK(!eng::hud::region_valid(bad));
	bad = band;
	bad.band.planes = 7u; // > kHudMaxPlanes
	CHECK(!eng::hud::region_valid(bad));

	eng::hud::HudRegionSpec overlay;
	overlay.placement = eng::hud::HudPlacement::Overlay;
	CHECK(eng::hud::region_valid(overlay)); // GameRedraws por defecto
	overlay.restore = eng::hud::HudOverlayRestore::Backing;
	CHECK(!eng::hud::region_valid(overlay)); // Backing sin bytes declarados
	overlay.budget.max_backing_bytes = 4096u;
	CHECK(eng::hud::region_valid(overlay));
	overlay.band.height = 32u; // un overlay no lleva banda
	CHECK(!eng::hud::region_valid(overlay));

	eng::hud::HudRegionSpec screen;
	screen.placement = eng::hud::HudPlacement::Screen;
	CHECK(eng::hud::region_valid(screen));

	HudDefault hud;
	CHECK(!hud.add_region(bad).valid());
	CHECK(hud.add_region(band).valid());
	CHECK(hud.region_count() == 1u);
	CHECK(hud.region(eng::hud::RegionHandle {0u}).band.height == 48u);
}

/// **Conjunto de iconos** (H6): mapeo tipo->frame y recoloreado del atlas, lado juego; el
/// modelo solo lleva el frame resuelto (`HudSlotView::icon_frame`).
void test_icon_set() {
	// Las tablas del conjunto necesitan direccion estable (el `IconSet` guarda punteros):
	// `static` a nivel de funcion (patron del lado juego, que las declara en su .rodata).
	static constexpr eng::u16 frames[3] = {1u, 3u, 0u};
	static constexpr eng::u8 colors[4] = {0u, 2u, 3u, 1u};
	static constexpr eng::hud::IconSet set {frames, colors, 3u};

	CHECK(set.frame_for(0u) == 1u);
	CHECK(set.frame_for(1u) == 3u);
	CHECK(set.frame_for(2u) == 0u);
	CHECK(set.frame_for(3u) == 0u); // tipo no declarado -> sin icono
	CHECK(set.frame_for(200u) == 0u); // fuera de rango -> sin icono

	CHECK(set.color_for(0u) == 0u); // el 0 sigue transparente (un skin no lo vuelve opaco)
	CHECK(set.color_for(1u) == 2u);
	CHECK(set.color_for(2u) == 3u);
	CHECK(set.color_for(3u) == 1u);

	constexpr eng::hud::IconSet identity {}; // vacío: todo resuelve a "sin icono"/identidad
	CHECK(identity.frame_for(0u) == 0u);
	CHECK(identity.color_for(2u) == 2u);
	CHECK(identity.color_for(0u) == 0u);

	constexpr eng::hud::IconSet no_colors {frames, nullptr, 3u};
	CHECK(no_colors.color_for(2u) == 2u); // sin tabla de colores: identidad
	CHECK(no_colors.frame_for(1u) == 3u);
}

} // namespace

int main() {
	test_capacity();
	test_dirty();
	test_counter();
	test_grid();
	test_drag();
	test_region();
	test_icon_set();
	if (g_fail == 0) {
		std::printf("HOST-467 hud_model: OK\n");
		return 0;
	}
	std::printf("HOST-467 hud_model: %d fallos\n", g_fail);
	return 1;
}
