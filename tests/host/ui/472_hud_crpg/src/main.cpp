// ============================================================================
// Test HOST-472: HUD de aventura/CRPG (`eng/hud` H5 + puente `eng/rpg`)
// ============================================================================
//
// Fase H5 del roadmap (`docs/guides/roadmap/ROADMAP_HUD.md`): componentes que consumen las
// aventuras graficas (`ADVENTURE_LAYER.md` §3: barra de verbos, inventario, subtitulos) y
// los CRPG (inventario/equipo):
//
//   - `VerbBar`: alta desde etiquetas (crea un `Button` por verbo), verbo activo, foco de
//     navegacion con recorte y dirty al cambiar.
//   - `Tooltip`/`Toast`: mostrar con TTL, avance por `tooltip_tick`, visibilidad y dirty al
//     ocultarse.
//   - `Panel` + `panel_cell`: layout por columnas (posicion por formula y rechazo si no cabe).
//   - Puente `eng::rpg::hud_slots_from_bag`: inventario (catalogo/store/bag) + equipo ->
//     `HudSlotView` (id logico, icono por tipo, cantidad, vacio/equipado).
//
//   bash tools/run-host-tests.sh tests/host/ui/472_hud_crpg

#include <cstdio>

#include <eng/hud/model.hpp>
#include <eng/rpg/hud_bridge.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

using eng::u8;
using eng::u16;

} // namespace

int main() {
	// --- Barra de verbos (aventura) --------------------------------------------------------
	eng::hud::Hud<> hud {};
	const char* const verbs[] = {"OPEN", "CLOSE", "LOOK", "TALK", "USE", "PICK"};
	const eng::hud::VerbBarHandle bar =
		hud.add_verb_bar(eng::Span<const char* const> {verbs, 6u});
	CHECK(bar.valid());
	CHECK(hud.verb_bar(bar).count == 6u);
	CHECK(hud.verb_active(bar) == 0u);
	// Etiquetas copiadas por valor en los botones.
	{
		const eng::hud::ButtonHandle b2 = hud.verb_button(bar, 2u);
		CHECK(b2.valid());
		const eng::Span<const char> t = hud.button(b2).text_view();
		CHECK(t.size() == 4u && t[0] == 'L' && t[3] == 'K');
	}
	// Seleccion: solo cambia (y marca dirty) si el indice es valido y distinto.
	hud.clear_dirty();
	CHECK(hud.verb_select(bar, 3u));
	CHECK(hud.any_dirty());
	CHECK(hud.verb_active(bar) == 3u && hud.verb_bar(bar).focus == 3u);
	CHECK(!hud.verb_select(bar, 3u)); // mismo verbo: sin cambio
	CHECK(!hud.verb_select(bar, 9u)); // fuera de rango
	// Foco con recorte a los extremos.
	CHECK(!hud.verb_move_focus(bar, 5)); // 3+5 fuera
	CHECK(hud.verb_move_focus(bar, 1) && hud.verb_bar(bar).focus == 4u);
	CHECK(hud.verb_move_focus(bar, -1) && hud.verb_bar(bar).focus == 3u);
	CHECK(!hud.verb_move_focus(bar, -5));
	CHECK(hud.verb_active(bar) == 3u); // el foco no cambia el activo

	// --- Tooltip/toast con TTL -------------------------------------------------------------
	const eng::hud::TooltipHandle tip = hud.add_tooltip();
	CHECK(tip.valid());
	CHECK(!hud.tooltip_visible(tip));
	CHECK(hud.tooltip_show(tip, eng::Span<const char> {"Usar llave", 10u},
			       eng::Box {64, 8, 80u, 8u}, 1u, 3u));
	CHECK(hud.tooltip_visible(tip) && hud.tooltip(tip).ttl_frames == 3u);
	CHECK(hud.tooltip_tick() == 1u); // 3 -> 2
	CHECK(hud.tooltip_visible(tip));
	hud.clear_dirty();
	(void)hud.tooltip_tick(); // 2 -> 1
	CHECK(!hud.any_dirty()); // aun visible: sin cambio de visibilidad
	(void)hud.tooltip_tick(); // 1 -> 0: se oculta
	CHECK(!hud.tooltip_visible(tip) && hud.any_dirty()); // ocultarse marca dirty

	// --- Panel + layout --------------------------------------------------------------------
	const eng::hud::PanelHandle panel = hud.add_panel(eng::Box {100, 8, 64u, 32u});
	CHECK(panel.valid());
	CHECK(hud.panel(panel).columns == 1u);
	{
		eng::Box cell {};
		const eng::hud::Panel p = eng::hud::Panel {eng::Box {100, 8, 64u, 32u}, 4u, false};
		CHECK(eng::hud::panel_cell(p, 16u, 16u, 0u, cell) && cell.x == 100 && cell.y == 8);
		CHECK(eng::hud::panel_cell(p, 16u, 16u, 5u, cell) && cell.x == 116 && cell.y == 24);
		CHECK(!eng::hud::panel_cell(p, 16u, 16u, 8u, cell)); // 2 filas x 4 columnas: no cabe
	}

	// --- Puente RPG -> HUD (inventario + equipo) -------------------------------------------
	eng::rpg::ItemCatalog<4> catalog {};
	{
		eng::rpg::ItemDef sword {};
		sword.stack_max = 1u;
		sword.tags = 0x0001u;
		eng::rpg::ItemDef potion {};
		potion.stack_max = 5u;
		(void)catalog.add(sword);  // type 0
		(void)catalog.add(potion); // type 1
	}
	eng::rpg::ItemStore<8> store {};
	eng::rpg::ItemBag<4> bag {};
	// La espada equipada (fuera de la bolsa, como en un CRPG) y tres pociones en la bolsa.
	const eng::rpg::ItemHandle sword = store.spawn(eng::rpg::ItemInstance {0u, 1u, 0u, 0u, 0u, 0u});
	const eng::rpg::ItemHandle potions = store.spawn(eng::rpg::ItemInstance {1u, 3u, 0u, 0u, 0u, 0u});
	CHECK(sword.valid() && potions.valid());
	CHECK(eng::rpg::bag_add(bag, store, catalog, potions) == 0u);
	eng::rpg::Equipment<2> equipment {};
	eng::rpg::ItemHandle replaced {};
	(void)eng::rpg::equip_item(equipment, store, catalog, 0u, sword, replaced);
	// La espada no esta en la bolsa; se prueba el flag equipado con la pocion en la ranura 1.
	(void)eng::rpg::equip_item(equipment, store, catalog, 1u, potions, replaced);

	const u16 icons[] = {0u, 7u, 0u, 0u}; // type 0 sin icono; type 1 -> frame 7
	eng::hud::HudSlotView views[6] {};
	const u8 n = eng::rpg::hud_slots_from_bag(bag, store, icons, views, &equipment);
	CHECK(n == 1u); // una entrada en la bolsa
	CHECK(views[0u].item_id == 2u);      // type 1 + 1 (0 = vacio)
	CHECK(views[0u].icon_frame == 7u);   // icono por tipo
	CHECK(views[0u].qty == 3u);          // cantidad de la pila
	CHECK((views[0u].flags & eng::hud::kHudSlotEnabled) != 0u);
	CHECK((views[0u].flags & eng::hud::kHudSlotEquipped) != 0u); // equipada
	for (u8 i = 1u; i < 6u; ++i) {
		CHECK(views[i].flags == eng::hud::kHudSlotEmpty); // celdas extra vacias
	}
	// El icono de un tipo sin tabla queda en 0 (el HUD lo trata como sin arte).
	CHECK(eng::rpg::hud_icon_for(eng::Span<const u16> {}, 1u) == 0u);

	if (g_fail != 0) {
		std::printf("HOST-472 hud_crpg: %d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: barra de verbos, tooltip con TTL, layout de panel y puente RPG -> HUD "
		    "validados.\n");
	return 0;
}
