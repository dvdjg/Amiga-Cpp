# Roadmap del HUD de juego (`eng::hud`)

Plan de implementación del diseño canónico [`HUD_SYSTEM.md`](../../engine/architecture/HUD_SYSTEM.md): modelo portable de componentes de HUD, regiones con presupuesto, *hit-test* y arrastre, y presentación sobre `CanvasPlayfield`, sprites, Blitter y `copper::Plan` sin duplicar el compositor de escena ni el *toolkit* de `eng::ui`.

## Principios

- **Modelo primero, HOST primero.** Los componentes, el *dirty*, el *hit-test* y la degradación son puros y se prueban en host antes de tocar hardware.
- **La región compila a lo que ya existe.** `HudRegionSpec` se materializa con `scene::LayerPlan`, `CanvasPlayfield`, `ModeSwitchZone` y `copper::Plan`; no se crea un compositor paralelo.
- **El juego manda.** El HUD refleja vida, puntuación y slots; no posee objetos ni reglas. Los slots se enlazan como vistas (`HudSlotView`).
- **Entrada única.** El HUD consume `UiEvent`/`UiAction` (G9); no abre un segundo sistema de eventos ni interpreta *rawkeys*.
- **Presupuesto medido.** Cada región declara sus límites; la degradación es determinista y observable. Se mide fps, cadencia, *flicker* y se valida una secuencia con Ollama.
- **Reutilización obligatoria.** Antes de añadir una pieza se comprueba si ya existe (`Surface`, `DirtyList`, `GlyphCache`, `HardwareCursor`, `SpriteLineLayer`, `RectFillSink`, `CanvasPlayfield`); ver [`AGENTS.md` §1.7](../../../AGENTS.md).

## Fases

### H0 — Modelo, *handles* y contrato de región

- **Entregable**: `engine/include/eng/hud/` con `model.hpp` (`Meter`, `Counter`, `IconGrid`, `Slot` como `HudSlotView`, `DragSession`, `Label`, `Button`, `Panel`), `region.hpp` (`HudPlacement`, `HudBand`, `HudBudget`, `HudRegionSpec` + validadores), `theme.hpp` (estilos, `IconAtlas`, `HudTheme`), `hud.hpp` (fachada) y `README.md`. *Handles* tipados para cada componente y errores de capacidad en *setup*.
- **Detalle**: [`HUD_SYSTEM.md`](../../engine/architecture/HUD_SYSTEM.md) §4–§5. Los identificadores son índices/*handles*, no cadenas; el contador formatea con potencias por sumas repetidas y sustracción repetida (sin multiplicación ni división; compilación m68k auditada sin helpers caros); el modelo no incluye cabeceras de `eng::ui` ni de `eng::rpg`.
- **Verificación**: **HOST-467** — alta de componentes hasta capacidad, error al desbordar, `set()` marca *dirty* solo al cambiar el valor visible, contador base 10/16/2 con ceros a la izquierda y negativos, rejilla con selección/foco, `DragSession` con transiciones válidas e inválidas, y `HudRegionSpec` inválida rechazada. El test instancia el modelo con dos configuraciones de capacidad distintas.
- **Estado**: **HECHO (2026-10)** — `HOST-467` (`tests/host/ui/467_hud_model`, catálogo `ui`) en verde; gates `engine-tree`/`platform-boundaries` ampliados con el módulo `hud`; cabeceras marcadas **NO VERIFICADAS por demo** hasta H1.

### H1 — Franja propia, `Meter`, `Counter` y *pips*

- **Entregable**: `HudStrip` sobre `CanvasPlayfield` con `Surface`, `DirtyList` y operaciones de dibujo; `Meter` lineal y segmentado; `Counter` con `Font8`/`Font5x7`; *pips* de vidas por *tiles* (la ruta sprite llega en H4); tema por defecto con paleta de banda; compilación a `LayerPlan` con `LayerRole::Overlay` y `ModeSwitchZone` cuando los planos difieren.
- **Detalle**: franja canónica inferior `top = 208`, `height = 48` en 320×256; la franja no resta altura al anillo de scroll; el split y la paleta se validan con `eng::setup`. **Decisión de doble buffer medida aquí**: comparar la reemisión de la zona de conmutación con la base del lienzo trasero (`Scheduler::emit_mode_switch_zone` en la copperlist trasera) frente a la actualización in-place en VBlank, con coste por frame (movimientos de Copper), ausencia de *tearing* y fps; evidencia previa del repo: DT-001 (FG de la demo 110) documenta *tearing* con buffer único actualizado durante el barrido y prescribe doble buffer con flip en la copperlist. La opción elegida se documenta en `HUD_SYSTEM.md` §6.3. El HUD de depuración de la demo se mantiene en una línea de 1 bpp o en la ventana de WinUAE.
- **Verificación**: **HOST-468** — equivalencia de operaciones contra un `Surface` de referencia, *dirty* que no repinta lo estático, medidor con `value`/`max` en los extremos y paleta de banda correcta. **Demo `302_hud_band`** con `INTENT.md`, `VALIDATION.md` y visión por secuencia; fps/cadencia y *flicker* dentro de presupuesto.
- **Estado**: **HECHO (2026-10)** — `eng/hud/strip.hpp` (`HudStrip` + `apply_hud_ops`, con ruta de texto por palabras para celdas alineadas) verificado por **HOST-468**; demo **`302_hud_band`** entregada y verificada (franja 2 planos con paleta propia en la línea 208, contador/medidor/pips por deltas, actor por el sistema de objetos con `SaveUnder`): **49,9 fps / 1 campo** (orden de pintado por banda + rellenos del HUD por CPU), *flicker* con gate OK y candidatos del movimiento del actor declarados en `flicker-baseline.json`, visión A/B/C y canónica en `VALIDATION.md`/`src/VISION.md`. **Decisión de doble buffer medida**: la franja *in-place* basta para una franja inferior (sin *tearing*) si se respeta el orden de pintado por banda y los rellenos van por CPU; la vía de reemisión de la zona con la base del lienzo trasero queda documentada en `HUD_SYSTEM.md` §6.3 para contenido que la requiera.

### H2 — *Hit-test*, selección y foco de joystick

- **Entregable**: `HudInput` sobre el contrato de eventos; *hit map* por región (rectángulos ordenados + fórmula de rejilla); `IconGrid` con selección y foco independientes; `Button`; navegación por joystick/pad; captura de entrada por región (`capture_input`).
- **Dependencia**: G9 de [`ROADMAP_GUI.md`](ROADMAP_GUI.md) (captura de puntero, `PointerMove`/`PointerCancel`, propagación y `UiAction`). Hasta que G9 cierre, H2 consume el subconjunto vigente de `UiEventKind` (`MouseMove`/`MouseDown`/`MouseUp`/`JoyButton`/`Tick`, más `KeyDown`/`KeyUp` con `eng::ui::keymap`) y converge con el contrato final sin duplicar la máquina de eventos.
- **Detalle**: el *slot* se calcula al recibir el evento, no por frame; el *hit map* y el pintado comparten transformación; un *click* es *down* + *up* en el mismo objetivo; el joystick mueve el foco por filas/columnas.
- **Verificación**: **HOST-469** — prioridad de regiones, rejilla con pitch no múltiplo de 16, *click* dentro/fuera, foco con bordes de rejilla, consumo y paso al juego según `capture_input`. **Demo `303_hud_icons`** (inventario en franja o panel) con selección por ratón y joystick; visión y medidas.
- **Estado**: **núcleo y demo entregados (2026-10-19)** — `engine/include/eng/hud/input.hpp` (`hud_hit_test` por capa/fórmula, `HudInput`, selección/foco y política `capture_input`) verificado por **HOST-469**; la demo **`303_hud_icons`** está entregada y su pérdida de latidos DT-036 se cerró con la ruta de sondeo (`frame/vbl` = 0,998–0,999; ver su `VALIDATION.md`). Queda la convergencia con la captura/propagación de puntero del contrato G9; las herramientas MCP de ratón ahora admiten trayectorias curvas con easing, click y drag para la verificación end-to-end.

### H3 — *Drag & drop*

- **Entregable**: `DragSession` global con captura, umbral, *ghost* (sprite si hay canal; BOB/overlay si no), política `accepts(item_id, qty)`, *drop* entre slots y cancelación; apilado de cantidades; realce del destino.
- **Detalle**: [`HUD_SYSTEM.md`](../../engine/architecture/HUD_SYSTEM.md) §5.6 y §7.2. La captura se libera en `PointerUp`, `PointerCancel`, cierre de panel o cambio de escena; la política de destino se consulta al entrar en la celda y al soltar, no por píxel.
- **Verificación**: **HOST-470** — *click* vs *drag* por umbral, destino válido/inválido, cancelación sin efecto, cantidad apilada y liberación de captura. **Demo `304_hud_drag`** con inventario arrastrable y validación de visión de la secuencia de arrastre.
- **Estado**: **COMPLETO (2026-10-18)** — `DragSession` entregado en el modelo (fases, umbral, posición, terminales `Dropped`/`Cancelled` + `drag_reset`; HOST-467/470) y **demo `304_hud_drag` entregada**: inventario RPG arrastrable con fantasma del atlas (H6), **política `accepts(item_id, qty)`** consultada en cada movimiento (`set_drag_target` + realce por veredicto), **apilado con `merge_stack`** del kernel RPG y cancelación; gate de memoria (`drag_ok`/`drag_bad`) + visión (`VALIDATION.md`, 49,9 fps).

### H4 — Overlay, sprites, Copper y presupuesto

- **Entregable**: `HudOverlay` con modos `GameRedraws` y `Backing`; reserva de canales de sprite e integración con `SpriteChannelLedger`; *pips* y *ghost* por sprite; paleta/split por `copper::Plan`; doble buffer de banda por punteros; comprobador de `HudBudget` y escalera de degradación con telemetría (`HudStats`).
- **Detalle**: el *backing* se declara en bytes y se mide; el overlay no escribe fuera de su `Box`; las barras raster solo usan un `MOVE` por frontera y se validan con `check_copper_list`; la degradación nunca altera valores ni *hit-test*.
- **Verificación**: **HOST-471** — restauración de *backing* byte a byte contra referencia, coste declarado, solape de canales rechazado, degradación determinista bajo presupuesto simulado y emisión de intenciones Copper. **Demo `305_hud_overlay`** (barra de vida y *pips* sobre un juego en movimiento) con perfil `.amigaprofile`, fps/cadencia, *flicker* y visión por secuencia.
- **Estado**: **parcial** — overlay con las **dos políticas de restauración** entregado y verificado en la **demo `305_hud_overlay`**: panel A `GameRedraws` (el blit del juego es la restauración) y panel B `Backing` con buffer Chip propio restaurado por blit antes de cada repintado (`budget.max_backing_bytes`), pintados con las operaciones del modelo (`apply_hud_ops`) sobre superficies de la escena en coordenadas absolutas, **skins** de medidor/contador en caliente (H6) y **escalera de degradación** medida sobre la sección HUD (gate de memoria `analyze-sequence.sh`: `ov_bad`/`bk_bad`/`blit_bad` = 0, `deg_changes` > 0). **`eng/hud/budget.hpp` entregado** (H4): coste de *backing* declarado (`hud_backing_bytes`/`hud_backing_fits`), conflicto de canales de sprite (`hud_channels_conflict`), **comprobador de presupuesto** (`hud_budget_check` con el primer campo excedido en el orden de la escalera) y **telemetría** (`HudRegionStats`/`HudStats` por región: ops, blits, Copper, sprites, BOBs, *backing* y nivel), más **`HudDegradeLadder`** (niveles 0..6 de §6.7 con histéresis 7/8 y `changes()`), verificado en **HOST-471** y ejercitado por la `305` (dogfooding de la escalera). Pendientes: reserva de canales de sprite y *pips*/*ghost* por sprite, paleta/split por `copper::Plan`, doble buffer de banda por punteros y el tramo de HOST-471 ligado a ellos (emisión de intenciones Copper; el solape de canales ya tiene validador puro).

### H5 — Aventura y CRPG: paneles, verbos, *tooltips* y puente RPG

- **Entregable**: `HudPanel` (fila/columna/rejilla), `HudVerbBar` (botones + verbo activo), `Tooltip`/`Toast` con TTL por `Tick`, varias regiones simultáneas y `HudPlacement::Screen` para inventario modal; adaptador opcional `eng/rpg/hud_bridge.hpp` que convierte `Inventory`/`ItemInstance` en `HudSlotView` sin invertir dependencias.
- **Detalle**: el panel modal decide pausa en el juego, no en el HUD; los paneles complejos pueden apoyarse en `eng::ui` para ventanas de aplicación, pero los componentes de juego no dependen de `UiContext`. La barra de verbos no incorpora *scripting*: solo presentación y selección.
- **Verificación**: **HOST-472** — layout de panel, TTL de *tooltip*, múltiples regiones con *hit-test* correcto y adaptador RPG (icono, cantidad, equipo, vacío) contra un inventario de juguete. **Demo `306_hud_crpg`** con barra de verbos, inventario y *tooltip*; integración con la demo RPG existente como referencia de datos.
- **Estado**: **COMPLETO (2026-10-18)** — núcleo entregado y verificado por **HOST-472** (`VerbBar` con activo y foco, `Tooltip`/`Toast` con TTL, `Panel` + `panel_cell`, puente `eng/rpg/hud_bridge.hpp`) y **demo `306_hud_crpg` entregada**: barra de verbos (LOOK/TAKE/USE/OPEN/TALK), inventario alimentado por el puente, tooltip con TTL, entrada por `HudInput` **y** `HudFocus`, **dos regiones simultáneas** (banda + overlay con botones EQUIP/DROP en pantalla) con **despacho por región** (`HudHitRect::region` + `hud_hit_test_region`, HOST-469), guion determinista, gate de memoria + visión (`VALIDATION.md`; 49,9 fps).

### H6 — Temas, atlas, fachada y cierre de rendimiento

- **Entregable**: `HudTheme` con presets y cambio en caliente; `IconAtlas` tipado en Chip y pipeline `tools/assets/hud-atlas.mjs` (o extensión del generador de manifiestos) con salida en `out/assets/hud/`; *skins* (`MeterStyle`/`CounterStyle`/`IconSet`); fachada `eng/hud/hud.hpp` incluida desde `eng/api/api.hpp`; tabla de umbrales CPU/Blitter/Copper medida en A500 y páginas de manual/referencia de lo implementado.
- **Detalle**: el atlas se genera en host (cuantización → planar → frames), sin decodificación en runtime; las dimensiones potencia de dos son preferentes; el cambio de tema marca todo sucio y recarga la paleta en el siguiente split. Los umbrales se fijan con medidas, no por intuición.
- **Verificación**: **HOST-473** — atlas y tabla de frames (geometría, *stride*, máscara), cambio de tema, *skins* equivalentes sobre el mismo modelo y fachada. **Demo de cierre** (extensión de `306_hud_crpg` o `307_hud_skins` si se reserva número) con dos temas, medición completa, *flicker*, regresión y visión.
- **Estado**: **casi completo** — pipeline de atlas entregado (`tools/assets/hud-atlas.mjs` con `--selftest` en la suite, `--check` determinista y asset de prueba `assets/amiga/hud/test_icons.png`); **presets de tema entregados** (`eng/hud/theme.hpp`: `hud_theme_classic()`/`hud_theme_amber()` + `hud_theme_fits`, verificados en HOST-472), **tema como contrato de región** (`region_theme`/`set_region_theme` por valor, HOST-472) y **cambio en caliente + atlas integrados en la demo `306_hud_crpg`** (ciclo clásico↔ámbar; prebuild + incbin del atlas y pasada de iconos con las formas visibles por visión). **Umbrales medidos** (`HUD_SYSTEM.md` §6.6, perfil por frame de la 306: régimen ~1-2k ciclos del HUD con 81% del campo en `Wait`; repintado completo ≈ 6 campos al 100% con DMA ×5). **Skins entregados**: `MeterStyle` (`Linear`/`Segmented`/`Pips`/`Raster`) y `CounterStyle` (`Numeric`/`Pips`/`Bar`) conmutables en el modelo (`set_meter_style`/`set_counter_style`, `Counter::present_max`), verificados en HOST-467 y ejercitados en la `305_hud_overlay` (mismo valor, tres materializaciones del contador); **`IconSet` entregado** (`eng/hud/theme.hpp`: mapeo tipo→frame del atlas + remapeo de color del skin, lado juego; la `306` cicla conjunto A/B con el tema y reconstruye las vistas con el puente) con test en HOST-467 (`test_icon_set`). **Ciclo de tema/skin verificado en vivo** (2026-10-19): la app quedaba estancada a los ~262 frames por la carrera de despertar del VBlank bajo SO vivo (familia 212/DT-011) — resuelto con la **espera activa** (`VblankWait::Active` de `run_demo`, también adoptada por la `305`); con ella la `306` corre 825 frames, el ciclo se dispara (`iconset`=2) y la secuencia lo captura (iconos rojo→blanco, verbos LOOK→TAKE→USE; `VISION.md` regenerado). Pendiente: paleta del atlas por skin (el remapeo de color ya está en el `IconSet`) y el cierre de HOST-473.

## Tests y demos previstos

Los identificadores reservan el siguiente hueco global de tests (**HOST-468** en adelante) y el siguiente libre de `demos/features/ui/amiga` (**302**), según [`NUMBERING.md`](../../ai-dev-environment/NUMBERING.md). Si otra rama los ocupa antes, se renumeran en la misma pasada y se actualiza este roadmap.

| ID | Tipo | Contenido |
|---|---|---|
| HOST-467 | test | Modelo, *handles*, capacidad, *dirty*, contadores, `DragSession` y contrato de región. **HECHO** (H0). |
| HOST-468 | test | Franja, operaciones de dibujo, `Meter`/`Counter`/`pips` y equivalencia contra `Surface`. **HECHO** (H1). |
| HOST-469 | test | *Hit map*, prioridad de regiones, rejilla, selección, foco y consumo de entrada. |
| HOST-470 | test | *Drag & drop*: umbral, captura, destino, cancelación y cantidades. |
| HOST-471 | test | Overlay con *backing*, canales de sprite, Copper y degradación por presupuesto. |
| HOST-472 | test | Paneles, barra de verbos, *tooltips*, múltiples regiones y puente RPG. |
| HOST-473 | test | Tema, atlas de iconos, *skins*, cambio en caliente y fachada. |
| `302_hud_band` | demo | HUD de acción en franja propia: contador, medidor y vidas. **Entregada y verificada** (H1). |
| `303_hud_icons` | demo | Rejilla de iconos, selección por ratón y foco de joystick. **Entregada** (H2; DT-036 cerrada con `frame/vbl` = 0,998–0,999 mediante sondeo, ver `VALIDATION.md`). |
| `304_hud_drag` | demo | Inventario con arrastre, *ghost*, *drop* válido/inválido y cancelación. **Entregada** (H3; `VALIDATION.md`). |
| `305_hud_overlay` | demo | Overlay sobre juego en movimiento, sprites y presupuesto. |
| `306_hud_crpg` | demo | Panel/verbos/inventario y puente con `eng::rpg`. **Entregada y verificada** (H5; `VALIDATION.md`). |

## Riesgos y decisiones abiertas

- **Dependencia de G9.** El contrato de eventos y la captura de puntero de `eng::ui` deben converger; el HUD no puede mantener una máquina de eventos divergente. Mitigación: H2 define el subconjunto mínimo y se alinea con G9 antes de cerrar la fase.
- **Doble buffer de banda.** Se prefiere intercambio de punteros en el split; si el compositor no lo permite en todas las configuraciones, la alternativa es actualizar en la ventana de VBlank con un solo lienzo. Hay que medir ambas.
- **Coste del *backing* de overlay en A500.** Cada byte de *backing* compite con el juego; el valor por defecto es `GameRedraws` y `Backing` solo con presupuesto declarado. Hay que medir el coste de copia por rectángulo.
- **Canales de sprite.** El HUD compite con jugador, enemigos y puntero; el *ledger* debe rechazar solapes en *setup*. Si no hay canal para el *ghost*, la degradación es un slot resaltado.
- **Formato del atlas.** 1–3 planos con máscara cubren la mayoría; iconos de más planos y tamaños no potencia de dos encarecen el direccionamiento. Decidir el subconjunto canónico antes de construir el pipeline.
- **División en *hit-test* de rejilla.** Un pitch arbitrario exige división al recibir el evento (no por frame). Si la medida muestra un pico en entrada, restringir a pitch potencia de dos o resolver por comparaciones de fila/columna.
- **Convivencia con el HUD de depuración.** La franja inferior interactiva y la franja de versión no pueden solaparse; la demo debe reservar una línea de 1 bpp o usar la ventana de debug. Es un requisito de [`AGENTS.md` §0](../../../AGENTS.md), no una opción.
- **Alcance de aventura/CRPG.** El HUD no interpreta verbos ni scripting; si el juego necesita un motor de diálogo, es otro frente. Mantener el límite evita convertir `eng::hud` en un motor de juego.
- **Paleta compartida en overlay.** Sin paleta propia, los índices del HUD deben existir en la paleta del juego y no pueden reescribirla. El tema debe declarar el mapa de índices y validarlo.

## Estado

**H0–H3 y H5 entregados**; el núcleo de H2 cuenta con **HOST-469** y la demo **`303_hud_icons`** (DT-036 cerrada mediante sondeo); H4 queda parcial en reserva/materialización de canales de sprite, cues de Copper y doble buffer de banda; H6 queda casi completo, con el mapa de paleta del atlas por skin y el cierre de **HOST-473** pendientes. H1 está respaldado por la `302` (49,9 fps, 1 campo, secuencia visual y gate de *flicker*); H3 por la `304`; H5 por la `306`.

## Prioridad actual

1. **Converger la entrada del HUD con el tramo de puntero de G9** (`capture → target → bubble`, `PointerCancel`, *hover* y `UiAction`), sin bloquearlo por el trabajo de edición de texto de la GUI; elimina el subconjunto paralelo de eventos que todavía describe H2.
2. **Calibrar hit-test y arrastre reales en 303/304 o 002 de ADV** con `winuae_input_mouse` en modo `path`, capturando además selección/drop; una prueba en `002` ya confirmó que la trayectoria y el click llegan al HUD, pero el destino pedido MIRA se resolvió como CIERRA, así que la correspondencia de coordenadas sigue abierta.
3. **Cerrar de H4 solo las piezas que consume el compositor de ADV**: reserva de canales de sprite y emisión de cues/split Copper con su test HOST-471; medir después el doble buffer de banda por punteros si un consumidor lo necesita.
4. **Cerrar H6** con el remapeo de paleta por skin y HOST-473; su valor es fijar el contrato de atlas/tema, pero no debe adelantarse a la captura de entrada ni al primer consumidor Stage Composer.
