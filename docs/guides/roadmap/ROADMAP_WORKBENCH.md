# Roadmap — portabilidad a Workbench (backend de UI + host de mensajes)

Objetivo: que **el mismo código de aplicación** (widgets `eng::ui`, `UiEvent`, despacho) funcione
**sobre el engine a pelo** (juego/HUD, sin SO) y **sobre Workbench/Intuition** (herramienta con
ventanas del OS). No se trata de que Intuition «sea» el compositor del engine, sino de introducir un
**seam** (destino de dibujo + origen de eventos + espera) con dos implementaciones: `EngineHost` y
`IntuitionHost`.

```text
        App / widgets (eng::ui: UiContext, Widget, UiEvent)   <- neutral, sin SO
                          │
              seam:  UiHost (eventos + present + idle)  ·  PaintTarget (Surface | RastPort)
                        /            \
             EngineHost                 IntuitionHost
   (Surface + Compositor + VBlank)   (Window + RastPort + IDCMP)

        eng::os:   system_port() + Msg  <- host tipo-OS (Wait/IDCMP/IO)  o  host juego (ISR/CIA)
```

## Principios

- **Widgets y eventos neutrales primero.** `eng::ui` ya no depende del SO: `UiEvent`, `UiContext`,
  widgets y tema no cambian entre backends.
- **Sin `virtual` en el camino caliente** (regla del engine, `CODING_STYLE.md`): el seam se resuelve
  por **plantilla/policy** (`template <class Host> app_ui_main(Host&)`) o `switch` sobre un tag —
  no por `IUiBackend` con métodos virtuales. (En Workbench un `virtual` sería tolerable, pero no se
  usa para no partir el contrato en dos.)
- **Sin heap en el camino del engine**: pools y árbol intrusivo (ya es así en `eng::ui`); en el
  backend Intuition sí se admite el heap del OS (`OpenWindowTags`), y solo ahí.
- **Reutilizar antes de duplicar** (`AGENTS.md` §1.6): el puente IDCMP termina en `UiEvent`
  reusando `to_ui_event`/`dispatch_msg`; el `Compositor` existente cubre el backend engine.
- **HOST primero**: el seam de dibujo y la traducción de eventos son puros y host-testables; cada
  backend se valida con su demo (build → run → analyze).

## Estado de partida (contrastado con el código)

**Existe y se reutiliza tal cual:**

| Pieza | Dónde |
|---|---|
| Widgets por `switch` sobre `WidgetType` (sin `virtual`) | `engine/include/eng/ui/widget.hpp:16`, `widgets.hpp:9` |
| `UiEvent`/`UiEventKind` (ratón, teclado+mods, joystick, `Tick`) | `engine/include/eng/ui/event.hpp:13` |
| `UiContext` (foco, hit-test, `dispatch`) | `engine/include/eng/ui/context.hpp:26` |
| Tema/rect | `engine/include/eng/ui/theme.hpp:29` (`Rect = eng::Box`, `kThemeWb13`) |
| `draw_tree` / `draw_widget` (pintado del árbol) | `engine/include/eng/ui/widgets.hpp:343`, `:191` |
| Puente `Msg → UiEvent` y adaptador con keymap | `eng/ui/ui_bridge.hpp:15`, `eng/ui/msg_adapter.hpp:20` |
| `Msg`/`MsgType`/`Signal`, `MsgPort`, `system_port()` (singleton) | `eng/os/message.hpp:20`, `port.hpp:218`, `os.hpp:15`, `amiga_minimal_os.cpp:38` |
| `HandlerTable`/`dispatch_all`, `pump_messages`/`MessagePumpGame` | `eng/os/dispatch.hpp:21`, `message_pump.hpp:17` |
| E/S asíncrona sobre `dos.library` (`FileDone`/`FileError`) | `eng/os/file.hpp:66`, `amiga_minimal_file.cpp:131` |
| `Compositor` + `WindowBacking` (pool fijo, backing por ventana) | `eng/ui/compositor.hpp:30`, `backing.hpp:17` |
| `TimerService` puro (el backend aporta `frame_now`/`ticks_now`) | `eng/os/timer.hpp:35` |

**Falta (esto es lo que añade el roadmap):**

1. **Destino de dibujo neutral**: `UiPainter` asume `field::Surface&` — `eng/ui/painter.hpp:25`.
2. **Ciclo de vida de ventana**: no hay `WindowDesc`/`WindowHandle`/`open_window`; lo más cercano es
   `Compositor::add()/raise()/move_window()` (`compositor.hpp:41,52,69`), sin `open/close` nominal.
3. **Contrato de host de UI** (event source + present + idle): no existe.
4. **`MsgPort::wait(mask)` real**: citado en `MINI_OS_MESSAGE_LOOP.md:243` pero **no implementado**
   (`port.hpp` solo tiene `take_signals`/`pop`/`peek`).
5. **Adaptador Intuition/IDCMP** (`IntuiMessage* → os::Msg`): no existe.
6. **`eng/os` en la fachada pública** (`eng/api/api.hpp` hoy solo incluye `eng/ui/ui.hpp`, `api.hpp:35`).
7. **Aspecto del píxel**: los widgets asumen píxel cuadrado; un destino *hi-res* de Workbench
   necesita `aspect_x` + estirado horizontal de texto (ver §Aspecto y fase W8).

## Arquitectura objetivo

**Seam de dibujo — `PaintTarget`.** `UiPainter` deja de fijar `field::Surface&` y opera sobre un
destino etiquetado; las funciones de `painter.hpp` despachan por `kind` (`switch`, estilo widgets).
El `RastPort` solo se compila en el backend Intuition.

```cpp
struct PaintTarget {
    enum class Kind : eng::u8 { Surface, RastPort };
    Kind kind;
    eng::field::Surface* surface;   // engine
    // RastPort* rp;                // Intuition (tras -DENG_UI_INTUITION)
    eng::Box clip;
    eng::u16 aspect_x = 256u;       // 8.8: px de dispositivo por px lógico a lo ancho (256 = cuadrado)
};
```

**Aspecto del píxel (`aspect_x`).** Nuestros widgets asumen **píxel cuadrado** (el playfield del
engine suele ser low-res 320, con píxel casi cuadrado). Un `Window`/`Screen` de Workbench suele ser
**hi-res 640**, con píxeles de ~mitad de ancho: dibujar 1:1 los **estrecha a la mitad**. El destino
lleva `aspect_x` (8.8): `x_device = x_logico * aspect_x / 256`. Con `256` la geometría no cambia
(engine de hoy); con `512` (hi-res) cada píxel lógico ocupa 2 de dispositivo. El **host** es quien lo
fija y, en sentido inverso, convierte la `x` de los eventos de ratón al espacio lógico, para que
**hit-test y pintado usen la misma métrica**. El **texto** (font 8×8) necesita además un **estirado
horizontal** o una fuente hi-res — es la parte cara (fase W8).

**Seam de host — `UiHost` (policy, no interfaz virtual).** La app es una plantilla sobre el host:

```cpp
template <class Host>
void app_ui_main(Host& host) {
    ui::WindowDesc desc{ .title = "Tools", .frame = {20, 20, 200, 120} };
    ui::WindowHandle w = host.open_window(desc);
    ui::UiContext ctx {};
    // ... construir widgets en coords cliente, ctx.set_root(...)
    for (ui::UiEvent ev; host.poll_event(ev); ) {
        if (ev.kind == ui::UiEventKind::None) break;
        ctx.dispatch(ev);
    }
    host.begin_paint(w);            // Engine: Surface del backing · Intuition: RastPort
    ui::draw_tree(*ctx.root(), host.painter(w));
    host.end_paint(w);              // Engine: compositor · Intuition: EndRefresh
    host.idle();                    // Engine: VBlank · Intuition: Wait(señales)
}
```

- **`EngineHost`**: `open_window` → `Compositor::add()`; `poll_event` → `system_port().pop()` +
  `ui_bridge`; `begin/end_paint` → `PaintTarget{Surface}` + `Compositor::present*`; `idle` →
  `wait_vblank`.
- **`IntuitionHost`**: `open_window` → `OpenWindowTags`; `poll_event` → drena `Window->UserPort`
  (IDCMP) y traduce; `begin/end_paint` → `PaintTarget{RastPort}` + `BeginRefresh/EndRefresh`;
  `idle` → `Wait(signals)`.

## Decisiones de diseño (adaptación al engine)

1. **Policy por plantilla, no `IUiBackend` virtual.** El engine es agnóstico del backend por
   plantilla (§1.9/§1.10). Si alguna app debe compilar sin plantillas, se añade después una fachada
   *type-erased* (struct de punteros a función) **encima** de la policy, no al revés.
2. **Eventos: un solo contrato y una sola traducción.** IDCMP → `os::Msg` → `UiEvent`
   (`intuition_bridge` reusa `to_ui_event`/`dispatch_msg`); no se duplica la lógica de keymap.
3. **Soft controls primero** (mismo árbol y mismo tema `kThemeWb13`): el `Window` de Intuition va
   sin gadgets y se pinta con `RastPort`. **Gadgets nativos** (string gadget, file requester) solo
   donde Intuition aporta — con el **estado lógico** en el `Widget` y el backend **sincronizando**.
4. **El `Compositor` es del backend engine.** La app nunca lo nombra (abre `WindowHandle`); en
   Intuition, `end_paint` refresca las regiones *dirty* del `Window` del OS.
5. **`MsgPort::wait` por host.** El mini-SO deja de ser dueño del hardware en Workbench: `wait` =
   `Wait(señales Exec)` + volcado `Exec → Msg`; el bucle/dispatch de la app **no cambia**.
6. **Compilación opt-in** (como `-DENG_AUDIO_OCTAMED`): `-DENG_UI_INTUITION` / `-DENG_OS_WORKBENCH`.
   La demo de juego enlaza el backend engine; la herramienta WB enlaza `intuition`/`graphics`/`layers`.

## Fases

### W0 — `PaintTarget` (destino de dibujo neutral)

- **Entregable**: `eng/ui/painter.hpp` opera sobre `PaintTarget` (variante `Surface`; `RastPort`
  tras `-DENG_UI_INTUITION`), con `aspect_x` (8.8; defecto `256` = cuadrado, sin cambio de
  geometría). `EngineHost`/demos existentes pasan `PaintTarget{Surface}`.
- **Reutiliza**: `field::Surface` (`fill_rect`/`draw_line`/`draw_text`), `draw_text_clipped`.
- **Verificación**: **HOST** — el painter pinta igual que hoy sobre una `Surface` de prueba
  (reusar los casos de HOST-223/224); los tests GUI existentes siguen verdes sin cambios de
  expectativas.
- **Estado**: **entregado**. `eng/ui/paint_target.hpp` (`PaintTarget` + `from_surface`) y `UiPainter`
  sobre el destino; el ctor de `Surface` se conserva como atajo. HOST-223…230 verdes sin cambios y
  demos `215_gui_widgets`/`300_gui_compositor` compilan. `aspect_x` se transporta pero **no** se
  aplica todavía (fase W8).

### W1 — Ciclo de vida de ventana (`WindowDesc`/`WindowHandle`)

- **Entregable**: `WindowDesc` (título, frame, flags) + `WindowHandle` opaco; en el engine,
  `open_window`/`close_window` sobre `Compositor` (pool con liberación de slot); `window_set_title`/
  `window_set_frame` como operaciones del host.
- **Verificación**: **HOST** — abrir/cerrar varias ventanas reutiliza slots; `move/resize` no
  invalida vecinas (reusar HOST-230); el `Window` widget y el handle conviven sin duplicar estado.
- **Estado**: pendiente.

### W2 — Contrato de host + `EngineHost`

- **Entregable**: `eng/ui/host.hpp` (concepto `UiHost`: `open_window`/`close_window`/`poll_event`/
  `painter`/`begin_paint`/`end_paint`/`idle`) y `eng/ui/engine/host_engine.hpp` (sobre
  `system_port()` + `Compositor` + `wait_vblank`). `app_ui_main` de ejemplo como plantilla.
- **Verificación**: **HOST** con un *fake host* (cola de `UiEvent` + `Surface`): `app_ui_main`
  construye, despacha y pinta; demo de juego (**reusar `215_gui_widgets`/`300_gui_compositor`**) que
  pase a usar el host sin cambiar su lógica de widgets.
- **Estado**: pendiente.

### W3 — Puente IDCMP → `os::Msg` → `UiEvent`

- **Entregable**: `eng/ui/bridge_intuition.hpp` (`IntuiMessage* → os::Msg`), reusando
  `ui_bridge::to_ui_event`; mapeo `MOUSEBUTTONS`/`MOUSEMOVE`/`RAWKEY`/`VANILLAKEY`/`CLOSEWINDOW`/
  `NEWSIZE`/`INTUITICKS` → `MsgType` (`Quit`/`VBlank`…). Latch de `INTUITICKS` como un solo tick
  pendiente (misma política que `VBlankLatch`).
- **Verificación**: **HOST** — un `IntuiMessage` sintético se traduce al `UiEvent` esperado (mismos
  casos que HOST-220 para el puente `Msg`).
- **Estado**: pendiente.

### W4 — `IntuitionHost` + pintado por `RastPort`

- **Entregable**: `eng/ui/intuition/host_intuition.hpp` (`OpenWindowTags`/`CloseWindow`, IDCMP,
  `BeginRefresh`/`EndRefresh`) y `rastport_painter` (el camino `RastPort` de `PaintTarget`: `RectFill`,
  `Move`/`Draw`, `Text`); mapeo `UiTheme` → `DrawInfo`/pens.
- **Verificación**: **demo Workbench** (nueva, ver §Tests) — la misma app de W2 abre su ventana,
  responde a ratón/teclado por IDCMP y repinta; build → run → analyze.
- **Estado**: pendiente.

### W5 — Host de mensajes de Workbench (`eng::os`)

- **Entregable**: `eng/os/amiga_workbench.hpp` — `WorkbenchHost` con `wait_mask(mask)` sobre
  *señales Exec* (`window_port`/`io_reply`/`sig_user`) y `pump_exec_to_logical()` (IDCMP → `Msg` +
  replies de IO → `FileDone`/`Timer`); `MsgPort::wait(mask)` real por host (juego: IRQ/`Wait` de
  exec; WB: `Wait`). El `system_port()` y el bucle `while (pop) dispatch` **no cambian**.
- **Verificación**: **HOST** — `wait`/`pump` con puertos simulados; **demo WB** alimentada por
  `system_port()` con ventana + DOS en el mismo bucle.
- **Estado**: pendiente. El `wait` del host **engine** ya existe (`os::wait` + `MsgPort::pending`,
  HOST-250); falta el de Workbench (`Wait` de Exec + volcado `Exec → Msg`).

### W6 — Timers y E/S bajo Workbench

- **Entregable**: `timer.device` (`TR_ADDREQUEST` al mismo `io_reply`) o `INTUITICKS` para UI a
  ~10 Hz; `file_read_async` de `dos.library` sobre el mismo puerto (ya genérica,
  `amiga_minimal_file.cpp`). **No** usar el timer de CIA del juego si el OS posee las CIA.
- **Verificación**: **HOST** con IO simulado; demo WB que lea un archivo sin bloquear el task de
  Intuition.
- **Estado**: pendiente.

### W7 — Gadgets nativos + `EditBox` a nivel Workbench

- **Decisión (fijada aquí)**: W7 **entra en el alcance** e incluye **gadgets nativos** donde
  Intuition aporta de verdad: *file requester*, *string gadget* y **nuestro `EditBox`**.
- **Entregable**: `EditBox` → *string gadget*/BOOPSI con el **estado lógico** en el `Widget` y el
  backend **sincronizando**; el resto (Button/Check/Radio/Slider) sigue en soft controls. Requiere
  **mejorar antes nuestro `EditBox`** al nivel de un *string gadget* de Workbench: selección por
  arrastre, borrado con `Del`/`Backspace` y por selección, caret parpadeante, *scrolling* horizontal
  con la vista, cortar/pegar (portapapeles propio), y campos con límite y *tab-stops*.
- **Verificación**: **HOST** de las mejoras del `EditBox` (selección, corte/pegado, límites) sobre el
  modelo lógico; demo WB con el `EditBox` nativo cuyo contrato (`text()`/`set_text()`, `UiEvent`)
  **no cambia**.
- **Estado**: pendiente.

### W8 — Aspecto del píxel (destino no cuadrado)

- **Entregable**: aplicar de verdad el `PaintTarget::aspect_x` en el painter (rectángulos/líneas:
  `x_device = x_logico * aspect_x / 256`) + **estirado horizontal del texto** (font 8×8 → 2× en
  hi-res) o una fuente hi-res; y en el host, la conversión inversa de la `x` de los eventos de ratón
  para que **hit-test y pintado compartan métrica**.
- **Verificación**: **HOST** — con `aspect_x = 512`, un `Rect` lógico de ancho `w` ocupa `2w` de
  dispositivo y el hit-test de un punto de dispositivo devuelve el widget correcto; **demo WB** en un
  `Screen` hi-res con los widgets a proporción correcta.
- **Alternativa (más barata)**: abrir la app en un **`Screen`/`Window` low-res** (píxel casi
  cuadrado) para no escalar; se documenta como opción, pero el defecto es **adaptarse**.
- **Estado**: pendiente.

## Tests y demos previstos

| ID | Tipo | Contenido |
|---|---|---|
| HOST-W0 | test | `PaintTarget` sobre `Surface`: mismo resultado que el painter actual (reusa casos GUI). |
| HOST-W1 | test | `open/close` de ventanas reutiliza slots; move/resize sin invalidar vecinas. |
| HOST-W2 | test | `app_ui_main` con un *fake host* (cola `UiEvent` + `Surface`). |
| HOST-W3 | test | `IntuiMessage` sintético → `os::Msg` → `UiEvent` (mismos casos que HOST-220). |
| HOST-W5 | test | `WorkbenchHost::wait_mask`/`pump_exec_to_logical` con puertos simulados. |
| demo | demo | `273_ui_workbench` — la app de widgets en una ventana Intuition real (W4/W5). |
| HOST-W5b | test | `MsgPort::wait` real (juego y WB) con IRQ/puertos simulados. |

**Número de la demo reservado**: `273_ui_workbench` (bloque D, `node tools/check/next-number.mjs`).

## No-objetivos

No se unifican (son engine, no UI portátil): Copper/Blitter/playfield, el compositor de backing
full-screen, los message ports del mini-SO frente a `UserPort` (solo el **puente a `UiEvent`**),
audio/assets/trackdisk, ni los productores de entrada hardware (en WB manda IDCMP; **no** se
ejecutan `JoyProducer`/`MouseProducer` sobre registros a la vez que Intuition).

## Arreglos previos (deuda que este roadmap hereda)

1. **`wait` del mini-SO**: ya **implementado para el host engine** (`os::wait` coopera con `tick()`
   + `MsgPort::pending`/`take_signals`, HOST-250); el de **Workbench** (`Wait` de Exec + volcado
   `Exec → Msg`) es la fase W5.
2. **`ROADMAP_MINI_OS.md`**: el "Estado" final dice que **todas** las fases están pendientes,
   contradiciendo el resto; y cita la demo `208_message_loop` cuando es `212_message_loop`.
3. **`ROADMAP_GUI.md`** cita demos `207`/`208` que hoy son `215`/`300`.
4. **`GUI_LIBRARY.md` §1/§18** declara Intuition *fuera de alcance*: añadir el puntero a este
   roadmap para que el contrato vigente no contradiga el plan.

## Riesgos

- **Dos modelos de refresco** (dirty propio vs *damage* de Intuition): mantener el estado *dirty* en
  los widgets y dejar que el host decida (engine: compositor; WB: `BeginRefresh`).
- **Entrada duplicada**: en WB, IDCMP es la única fuente de ratón/teclado; el joystick, por
  `input.device` o no usarse.
- **Un solo task** drena `system_port`; otros tasks solo `post` + `Signal` (`sig_user`).
- **Tipos de dominio en el seam**: la app no nombra `RastPort`/`Compositor`/`OpenWindow` — solo
  `UiHost`/`PaintTarget`/`WindowHandle` (§1.9).

## Referencias

- [`GUI_LIBRARY.md`](../../engine/architecture/GUI_LIBRARY.md) (contrato de `eng::ui`),
  [`MINI_OS_MESSAGE_LOOP.md`](../../engine/architecture/MINI_OS_MESSAGE_LOOP.md) +
  [`MINI_OS_IO.md`](../../engine/architecture/MINI_OS_IO.md) (mini-SO y E/S).
- [`ROADMAP_GUI.md`](ROADMAP_GUI.md) (G0–G8, entregado), [`ROADMAP_MINI_OS.md`](ROADMAP_MINI_OS.md)
  (M0–M11).
- AHRM 3.ª: Intuition (`docs/reference/ahrm/`) y `docs/reference/amiga/hardware/input-device-rkm.md`.
