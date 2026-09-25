# Tests HOST — ui

Categoría `ui` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-223 | [ui_painter](223_ui_painter/README.md) | GUI G0: `eng::ui::UiPainter`/`UiTheme` (chrome: `fill`/`frame`/`bevel_out`/`bevel_in`/`panel`/`button_face`/glifo), medida y recorte de texto (`text_width`/`draw_text_clipped`) y `eng::ui::Rect` = `eng::Box`. |
| HOST-224 | [ui_widgets](224_ui_widgets/README.md) | GUI G1: árbol de widgets (`eng/ui/widget.hpp`), `DirtyList<Max>` (fusión/desborde) y `Panel`/`Label` con `draw_widget` (`switch` exhaustivo) y `measure`. |
| HOST-225 | [ui_context](225_ui_context/README.md) | GUI G2: `eng::ui::UiContext` (hit-test de delante hacia atrás, foco, despacho) y `Button` (`on_click` al soltar dentro, una vez). |
| HOST-226 | [ui_toggle](226_ui_toggle/README.md) | GUI G3: `CheckBox` (alterna `*value` al soltar dentro) y `RadioButton` (activa uno y desactiva el grupo por `group_id`). |
| HOST-227 | [ui_edit](227_ui_edit/README.md) | GUI G4: foco de teclado (`Tab`/`Shift+Tab`) y `EditBox` (buffer externo, insertar/borrar, caret y vista horizontal). |
| HOST-228 | [ui_layout](228_ui_layout/README.md) | GUI G5: `layout_stack_v`/`layout_stack_h`, `anchor`, `measure` de botón y cambio de tema (recolorea). |
| HOST-229 | [ui_windows](229_ui_windows/README.md) | GUI G6: ventanas (`Window`/`Popup`/`Toast`/`Dialog`), Z/`raise`, modalidad, `Esc`, TTL y popup que cierra al pulsar fuera. |
| HOST-230 | [ui_compositor](230_ui_compositor/README.md) | GUI G7: compositor con `WindowBacking` (componer de atrás hacia delante, mover/redimensionar sin repintar vecinas, pool con cota). |
| HOST-234 | [app_screen](234_app_screen/README.md) | Fachada de juego `eng::App` + `eng::Screen` (`eng/api/game.hpp`): bucle + contexto de dibujo sin exponer backend/`FramePlan`. |
| HOST-261 | [ui_msg_input](261_ui_msg_input/README.md) | GUI: entrada por **mensajes** (`os::Msg` → `ui_bridge` → `UiContext`) con `keymap` rawkey Amiga → tecla lógica. |
| HOST-262 | [ui_slider](262_ui_slider/README.md) | GUI: `Slider` (click/arrastre → valor, flechas con foco, pista + pomo). |
| HOST-263 | [ui_keymap_layouts](263_ui_keymap_layouts/README.md) | GUI: keymaps nacionales (ES/FR/IT/DE/RU) — `rawkey_to_key(raw, shift, layout)`. |
| HOST-264 | [font_cyrillic](264_font_cyrillic/README.md) | Fuente: glifos cirílicos (U+0410–U+044F, Ё/ё) en `Font8` + UTF-8. |
| HOST-265 | [ui_deadkeys](265_ui_deadkeys/README.md) | GUI: teclas muertas (Alt+acento compone con la letra siguiente). |
| HOST-268 | [ui_compositor_blit](268_ui_compositor_blit/README.md) | Compositor GUI por `Surface::blit` (`Compositor::present_blit`): copia backings con el `Rasterizer` (CPU/Blitter) y cae a CPU si el rect no está alineado a palabra; equivalencia píxel a píxel con `present()`. |
| HOST-301 | [ui_hardware_cursor](301_ui_hardware_cursor/README.md) | Cursor por sprite de hardware (`HardwareCursor`): estructura DMA + emisión SPR0PT/SPREN. |
| HOST-302 | [ui_keymap_specials](302_ui_keymap_specials/README.md) | Teclas comunes del keymap (0x40–0x5F) validadas contra la AHRM 3.ª. |
| HOST-303 | [ui_editbox_utf8](303_ui_editbox_utf8/README.md) | `EditBox` UTF-8: inserción/borrado/movimiento por code point (incluye cirílico). |
| HOST-310 | [ui_double_buffer](310_ui_double_buffer/README.md) | Pantalla de doble buffer (`DoubleBufferScreen`): componer en el trasero sin cambiar el delantero; `flip` publica (flicker-free). |
| HOST-311 | [ui_scrollbar](311_ui_scrollbar/README.md) | `ScrollBar` (vertical/horizontal): pomo proporcional a la página, click→valor y teclado (flechas/página/`Home`/`End`). |
| HOST-312 | [ui_listview](312_ui_listview/README.md) | `ListView`: selección + desplazamiento lógico (solo filas visibles), click/flechas/`Home`/`End` y `ensure_visible`. |
